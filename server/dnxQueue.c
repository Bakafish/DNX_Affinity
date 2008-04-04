/*--------------------------------------------------------------------------
 
   Copyright (c) 2006-2007, Intellectual Reserve, Inc. All rights reserved.
 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as 
   published by the Free Software Foundation.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 
  --------------------------------------------------------------------------*/

/** Implements thread-safe queues for DNX.
 *
 * @file dnxQueue.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IMPL
 */

#include "dnxQueue.h"

#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxLogging.h"

#include <stdlib.h>
#include <syslog.h>
#include <assert.h>

/** Queue entry wrapper structure - wraps user payload. */
typedef struct iDnxQueueEntry_ 
{
   struct iDnxQueueEntry_ * next;   /*!< Pointer to next entry, NULL if none. */
   void * pPayload;                 /*!< User payload data. */
} iDnxQueueEntry;

/** Queue implementation structure. */
typedef struct iDnxQueue_ 
{
   iDnxQueueEntry * head;           /*!< Head of linked list of requests. */
   iDnxQueueEntry * tail;           /*!< Pointer to last request. */
   iDnxQueueEntry * current;        /*!< Circular buffer pointer. */
   int size;                        /*!< Number of requests in queue. */
   int max_size;                    /*!< Maximum number of requests allowed in queue (zero = unlimited). */
   pthread_mutex_t mutex;           /*!< Queue's mutex. */
   pthread_cond_t cv;               /*!< Queue's condition variable. */
} iDnxQueue;

//----------------------------------------------------------------------------

/** Add a request to a requests list.
 * 
 * Creates a request structure, adds to the list, and increases the number of 
 * pending requests by one.
 * 
 * @param[in] queue - the queue to which @p pPayload should be added.
 * @param[in] pPayload - the data to be added to @p queue.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxQueuePut(DnxQueue * queue, void * pPayload)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item;
   
   assert(queue);
   
   // create structure with new request
   if ((item = (iDnxQueueEntry *)xmalloc(sizeof *item)) == NULL)
      return DNX_ERR_MEMORY;
   
   item->pPayload = pPayload;
   item->next = NULL;
   
   DNX_PT_MUTEX_LOCK(&iqueue->mutex);
   
   // add new request to end of list, updating list pointers as required
   if (iqueue->size == 0) // special case - list is empty
      iqueue->head = iqueue->tail = iqueue->current = item;
   else 
   {
      iqueue->tail->next = item;
      iqueue->tail = item;
   }
   
   iqueue->size++;
   
   // check for queue overflow if this queue was created with a maximum size
   if (iqueue->max_size > 0 && iqueue->size > iqueue->max_size)
   {
      // remove the oldest entry at the queue head
      item = iqueue->head;
      iqueue->head = item->next;
      if (iqueue->current == item)
         iqueue->current = item->next;
      if (iqueue->head == NULL)   // was last request on list
         iqueue->tail = NULL;
      
      iqueue->size--;
      
      /** @todo This assumes the item payload came from the heap!
       * We don't own it! The thing to do here is to provide a destructor
       * with the payload. Then we can call the payload destructor.
       */
      if (item->pPayload)
         xfree(item->pPayload);

      xfree(item);
   }
   
   // signal the condition variable - there's a new request to handle
   pthread_cond_signal(&iqueue->cv);
   
   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);
   
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Returns the first pending request from the requests list. 
 * 
 * The returned request needs to be freed by the caller.
 * 
 * @param[in] queue - the queue from which to get the next pending request.
 * @param[out] ppPayload - the address of storage in which to return the 
 *    first pending request, if found.
 * 
 * @return Zero if found, or DNX_ERR_NOTFOUND if not found.
 */
int dnxQueueGet(DnxQueue * queue, void ** ppPayload)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item;
   
   assert(queue && ppPayload);
   
   DNX_PT_MUTEX_LOCK(&iqueue->mutex);
   
   if (iqueue->size > 0) 
   {
      item = iqueue->head;
      iqueue->head = item->next;
      if (iqueue->current == item)
         iqueue->current = item->next;
      if (iqueue->head == NULL)     // was last request on the list
         iqueue->tail = NULL;
   
      iqueue->size--;
   }
   else                             // queue is empty
      item = NULL;
   
   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);
   
   // return the payload to the caller.
   if (item) 
   {
      *ppPayload = item->pPayload;
      xfree(item);
      return DNX_OK;
   }

   return DNX_ERR_NOTFOUND;
}

//----------------------------------------------------------------------------

/** Remove a matching element from a requests queue.
 * 
 * @param[in] queue - the queue to be queried for a matching element.
 * @param[in,out] ppPayload - on entry contains the node to be located; on 
 *    exit returns the located node's payload.
 * @param[in] Compare - a comparison function used to location an element
 *    that matches @p ppPayload.
 * 
 * @return Boolean - true if found, false if not found.
 */
DnxQueueResult dnxQueueRemove(DnxQueue * queue, void ** ppPayload, 
      DnxQueueResult (*Compare)(void * pLeft, void * pRight))
{
   DnxQueueResult bFound = DNX_QRES_CONTINUE;
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item, * prev;

   assert(queue && ppPayload && Compare);

   DNX_PT_MUTEX_LOCK(&iqueue->mutex);

   prev = NULL;
   for (item = iqueue->head; item; item = item->next)
   {
      if ((bFound = Compare(*ppPayload, item->pPayload)) != DNX_QRES_CONTINUE)
      {
         if (bFound == DNX_QRES_FOUND)
         {
            *ppPayload = item->pPayload;

            // cross-link previous to next and free current
            if (prev)
               prev->next = item->next;
            else                          // removing the head item
               iqueue->head = item->next;

            if (item->next == NULL)       // removing the tail item
               iqueue->tail = prev;

            if (iqueue->current == item)  // advance circular pointer
               if ((iqueue->current = item->next) == NULL)
                  iqueue->current = iqueue->head;

            iqueue->size--;
         }
         break;
      }
      prev = item;
   }

   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);

   if (bFound == DNX_QRES_FOUND)
      xfree(item);       // free the queue entry wrapper object

   return bFound;
}

//----------------------------------------------------------------------------

/** Waits and returns the first pending request from the requests list.
 * 
 * Removes it from the list. Suspends the calling thread if the queue is empty.
 * The returned request need to be freed by the caller.
 * 
 * @param[in] queue - the queue to be waited on.
 * @param[out] ppPayload - the address of storage in which to return the
 *    payload of the first pending request.
 * 
 * @return Zero on success, or DNX_ERR_NOTFOUND if not found.
 * 
 * @note Not currently used.
 */
int dnxQueueGetWait(DnxQueue * queue, void ** ppPayload)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item = NULL;
   
   assert(queue && ppPayload);
   
   DNX_PT_MUTEX_LOCK(&iqueue->mutex);
   
   // block this thread until it can dequeue a request
   while (item == NULL) 
   {
      // see if we have any queue items already waiting
      if (iqueue->size > 0) 
      {
         item = iqueue->head;
         iqueue->head = item->next;
         if (iqueue->current == item)
            iqueue->current = item->next;
         if (iqueue->head == NULL)   /* this was last request on the list */
            iqueue->tail = NULL;
         
         iqueue->size--;
      }
      else     // queue is empty
         pthread_cond_wait(&iqueue->cv, &iqueue->mutex);
   }
   
   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);
   
   // return the payload to the caller.
   if (item) 
   {
      *ppPayload = item->pPayload;
      xfree(item);
      return DNX_OK;
   }

   return DNX_ERR_NOTFOUND;
}

//----------------------------------------------------------------------------

/** Return the next node's payload in a queue.
 * 
 * @param[in] queue - the queue to be queried for next payload.
 * @param[in,out] ppPayload - on entry, contains the payload whose next node
 *    should be returned; on exit, returns the next node's payload.
 * 
 * @return Zero on success, or DNX_ERR_NOTFOUND if there is no next node.
 * 
 * @note Not currently used.
 */
int dnxQueueNext(DnxQueue * queue, void ** ppPayload)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   
   assert(queue && ppPayload);
   
   *ppPayload = NULL;
   
   DNX_PT_MUTEX_LOCK(&iqueue->mutex);
   
   // save pointer to current payload
   if (iqueue->current) 
   {
      *ppPayload = iqueue->current->pPayload;
      
      // advance circular buffer pointer
      if (iqueue->current->next)
         iqueue->current = iqueue->current->next;
      else
         iqueue->current = iqueue->head;
   }
   
   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);
   
   return *ppPayload ? DNX_OK : DNX_ERR_NOTFOUND;
}

//----------------------------------------------------------------------------

/** Find a node matching a specified payload in a queue.
 * 
 * @param[in] queue - the queue to be queried for a matching payload.
 * @param[in,out] ppPayload - on entry contains the payload to be matched;
 *    on exit, returns the located matching payload.
 * @param[in] Compare - a node comparison routine.
 * 
 * @return Always returns zero.
 * 
 * @note Not currently used.
 */
DnxQueueResult dnxQueueFind(DnxQueue * queue, void ** ppPayload, 
      DnxQueueResult (*Compare)(void * pLeft, void * pRight))
{
   DnxQueueResult bFound = DNX_QRES_CONTINUE;
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item;

   assert(queue && ppPayload && Compare);

   DNX_PT_MUTEX_LOCK(&iqueue->mutex);

   for (item = iqueue->head; item; item = item->next)
   {
      if ((bFound = Compare(*ppPayload, item->pPayload)) != DNX_QRES_CONTINUE)
      {
         if (bFound == DNX_QRES_FOUND)
            *ppPayload = item->pPayload;
         break;
      }
   }

   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);

   return bFound;
}

//----------------------------------------------------------------------------

/** Return the number of requests in the list.
 * 
 * @param[in] queue - the queue to be queried for request count.
 * @param[out] pSize - the address of storage in which to return the count.
 * 
 * @return Always returns zero.
 * 
 * @note Not currently used.
 */
int dnxQueueSize(DnxQueue * queue, int * pSize)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;

   assert(queue && pSize);
   
   DNX_PT_MUTEX_LOCK(&iqueue->mutex);
   
   *pSize = iqueue->size;
   
   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);
   
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Create a requests queue.
 * 
 * Creates a request queue structure, initialize with given parameters.
 * 
 * @param[in] max_size - the maximum size of the queue.
 * @param[out] pqueue - the address of storage in which to return the new
 *    queue object.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxQueueCreate(int max_size, DnxQueue ** pqueue)
{
   iDnxQueue * iqueue;

   assert(max_size > 0 && pqueue);
   
   if ((iqueue = (iDnxQueue *)xmalloc(sizeof *iqueue)) == NULL)
      return DNX_ERR_MEMORY;
   
   // initialize queue
   iqueue->head = NULL;
   iqueue->tail = NULL;
   iqueue->current = NULL;
   iqueue->size = 0;
   iqueue->max_size = max_size;

   // initialize thread sync
   DNX_PT_MUTEX_INIT(&iqueue->mutex);
   pthread_cond_init(&iqueue->cv, NULL);

   *pqueue = (DnxQueue*)iqueue;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Delete a requests queue.
 * 
 * Delete a request queue structure, and free all memory it uses.
 * 
 * @param[in] queue - the requests queue to be deleted.
 */
void dnxQueueDestroy(DnxQueue * queue)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item;
   
   assert(queue);
   
   DNX_PT_MUTEX_LOCK(&iqueue->mutex);
   
   /* first free any requests that might be on the queue */
   item = iqueue->head;
   while (item != NULL) 
   {
      iDnxQueueEntry * next = item->next;
      xfree(item);
      item = next;
   }
   
   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);
   
   DNX_PT_MUTEX_DESTROY(&iqueue->mutex);
   pthread_cond_destroy(&iqueue->cv);

   xfree(iqueue);
}

/*--------------------------------------------------------------------------*/

