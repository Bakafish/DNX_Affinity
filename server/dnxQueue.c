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
#include <assert.h>
#include <syslog.h>

//----------------------------------------------------------------------------

/** Create a requests queue.
 * 
 * Creates a request queue structure, initialize with given parameters.
 * 
 * @param[out] ppQueue - the address of storage in which to return the new
 *    queue object.
 * @param[in] p_mutex - the queue's mutex.
 * @param[in] p_cond_var - the queue's condition variable.
 * @param[in] max_size - the maximum size of the queue.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxQueueInit(DnxQueue ** ppQueue, pthread_mutex_t * p_mutex, 
      pthread_cond_t * p_cond_var, int max_size)
{
   if (!ppQueue || !p_mutex || !p_cond_var || max_size < 0)
      return DNX_ERR_INVALID;
   
   if ((*ppQueue = (DnxQueue *)malloc(sizeof(DnxQueue))) == NULL)
      return DNX_ERR_MEMORY;
   
   dnxDebug(10, "dnxQueueInit: Malloc(*ppQueue=%p)", *ppQueue);
   
   // Initialize queue
   (*ppQueue)->head = NULL;
   (*ppQueue)->tail = NULL;
   (*ppQueue)->current = NULL;
   (*ppQueue)->size = 0;
   (*ppQueue)->max_size = max_size;
   (*ppQueue)->p_mutex = p_mutex;
   (*ppQueue)->p_cond_var = p_cond_var;
   
   return DNX_OK;
}

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
   int rc;                     /* return code of pthreads functions.  */
   DnxQueueEntry * qItem;      /* pointer to newly added request.     */
   
   assert(queue);
   
   /* create structure with new request */
   if ((qItem = (DnxQueueEntry *)malloc(sizeof(DnxQueueEntry ))) == NULL)
      return DNX_ERR_MEMORY;
   
   dnxDebug(10, "dnxQueuePut: Malloc(qItem=%p, pPayload=%p)", qItem, pPayload);
   
   qItem->pPayload = pPayload;
   qItem->next = NULL;
   
   /* lock the mutex, to assure exclusive access to the list */
   DNX_PT_MUTEX_LOCK(queue->p_mutex);
   
   /* add new request to the end of the list, updating list */
   /* pointers as required */
   if (queue->size == 0) /* special case - list is empty */
   {
      queue->head = qItem;
      queue->tail = qItem;
      queue->current = qItem;
   }
   else 
   {
      queue->tail->next = qItem;
      queue->tail = qItem;
   }
   
   /* increase total number of pending requests by one. */
   queue->size++;
   
   /* check for queue overflow if this queue was created with a maximum size */
   if (queue->max_size > 0 && queue->size > queue->max_size)
   {
      /* remove the oldest entry at the queue head */
      qItem = queue->head;
      queue->head = qItem->next;
      if (queue->current == qItem)
         queue->current = qItem->next;
      if (queue->head == NULL)   /* this was last request on the list */
         queue->tail = NULL;
      
      /* decrease the total number of pending requests */
      queue->size--;
      
      /* release the qItem payload */
      if (qItem->pPayload)
      {
         dnxDebug(10, "dnxQueuePut: Free(qItem->pPayload=%p)", qItem->pPayload);
         free(qItem->pPayload);   /* NB: Assumes that payload came from the heap! */
      }
      //dnxDebug(10, "dnxQueuePut: Free(qItem=%p)", qItem);
      free(qItem); /* release the qItem wrapper */
   }
   
   /* signal the condition variable - there's a new request to handle */
   rc = pthread_cond_signal(queue->p_cond_var);
   
   /* unlock mutex */
   DNX_PT_MUTEX_UNLOCK(queue->p_mutex);
   
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
   int rc;                     /* return code of pthreads functions.  */
   DnxQueueEntry * qItem;      /* pointer to request.                 */
   
   /* sanity check - make sure queue is not NULL */
   assert(queue && ppPayload);
   
   /* lock the mutex, to assure exclusive access to the list */
   DNX_PT_MUTEX_LOCK(queue->p_mutex);
   
   if (queue->size > 0) 
   {
      qItem = queue->head;
      queue->head = qItem->next;
      if (queue->current == qItem)
         queue->current = qItem->next;
      if (queue->head == NULL)   /* this was last request on the list */
         queue->tail = NULL;
   
      /* decrease the total number of pending requests */
      queue->size--;
   }
   else  /* requests list is empty */
      qItem = NULL;
   
   /* unlock mutex */
   DNX_PT_MUTEX_UNLOCK(queue->p_mutex);
   
   /* return the payload to the caller. */
   if (qItem) 
   {
      *ppPayload = qItem->pPayload;
      dnxDebug(10, "dnxQueueGet: Free(qItem=%p, *ppPayload=%p)", qItem, *ppPayload);
      free(qItem);    // Release the qItem wrapper
      rc = DNX_OK;
   }
   else 
   {
      *ppPayload = NULL;
      rc = DNX_ERR_NOTFOUND;
   }
   return rc;
}

//----------------------------------------------------------------------------

/** Returns the first pending request from the requests list.
 * 
 * Removes it from the list. Suspends the calling thread if the queue is empty.
 * The returned request need to be freed by the caller.
 * 
 * @param[in] queue - the queue to be waited on.
 * @param[out] ppPayload - the address of storage in which to return the
 *    payload of the first pending request.
 * 
 * @return Zero on success, or DNX_ERR_NOTFOUND if not found.
 */
int dnxQueueGetWait(DnxQueue * queue, void ** ppPayload)
{
   int rc;                       /* return code of pthreads functions.  */
   DnxQueueEntry * qItem = NULL; /* pointer to request.                 */
   
   assert(queue && ppPayload);
   
   DNX_PT_MUTEX_LOCK(queue->p_mutex);
   
   /* Block this thread until it can dequeue a request */
   while (rc == 0 && qItem == NULL) 
   {
      /* See if we have any queue items already waiting */
      if (queue->size > 0) 
      {
         qItem = queue->head;
         queue->head = qItem->next;
         if (queue->current == qItem)
            queue->current = qItem->next;
         if (queue->head == NULL)   /* this was last request on the list */
            queue->tail = NULL;
         
         /* decrease the total number of pending requests */
         queue->size--;
      }
      else   /* requests list is empty */
         rc = pthread_cond_wait(queue->p_cond_var, queue->p_mutex);
   }
   
   DNX_PT_MUTEX_UNLOCK(queue->p_mutex);
   
   /* return the payload to the caller. */
   if (qItem) 
   {
      *ppPayload = qItem->pPayload;
      dnxDebug(10, "dnxQueueGetWait: Free(qItem=%p, *ppPayload=%p)", qItem, *ppPayload);
      free(qItem);    // Release the qItem wrapper
      rc = DNX_OK;
   }
   else 
   {
      *ppPayload = NULL;
      rc = DNX_ERR_NOTFOUND;
   }
   
   return rc;
}

//----------------------------------------------------------------------------

/** Return the next node's payload in a queue.
 * 
 * @param[in] queue - the queue to be queried for next payload.
 * @param[in,out] ppPayload - on entry, contains the payload whose next node
 *    should be returned; on exit, returns the next node's payload.
 * 
 * @return Zero on success, or DNX_ERR_NOTFOUND if there is no next node.
 */
int dnxQueueNext(DnxQueue * queue, void ** ppPayload)
{
   int rc;
   
   assert(queue && ppPayload);
   
   *ppPayload = NULL;
   
   DNX_PT_MUTEX_LOCK(queue->p_mutex);
   
   /* Save pointer to current payload */
   if (queue->current) 
   {
      *ppPayload = queue->current->pPayload;
      
      /* Advance circular buffer pointer */
      if (queue->current->next)
         queue->current = queue->current->next;
      else
         queue->current = queue->head;
   }
   
   DNX_PT_MUTEX_UNLOCK(queue->p_mutex);
   
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
 */
DnxQueueResult dnxQueueFind(DnxQueue * queue, void ** ppPayload, 
      DnxQueueResult (*Compare)(void * pLeft, void * pRight))
{
   DnxQueueEntry * qItem;
   DnxQueueResult bFound = DNX_QRES_CONTINUE;
   int rc;

   if (!queue || !ppPayload || !Compare)
      return DNX_QRES_ERROR;

   DNX_PT_MUTEX_LOCK(queue->p_mutex);

   // Walk the list
   for (qItem = queue->head; qItem; qItem = qItem->next)
   {
      if ((bFound = (*Compare)(*ppPayload, qItem->pPayload)) != DNX_QRES_CONTINUE)
      {
         if (bFound == DNX_QRES_FOUND)
            *ppPayload = qItem->pPayload;
         break;
      }
   }

   DNX_PT_MUTEX_UNLOCK(queue->p_mutex);

   return bFound;
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
   DnxQueueEntry * qItem, * qPrev;
   DnxQueueResult bFound = DNX_QRES_CONTINUE;
   int rc;

   if (!queue || !ppPayload || !Compare)
      return DNX_QRES_ERROR;

   DNX_PT_MUTEX_LOCK(queue->p_mutex);

   // Walk the list
   qPrev = NULL;
   for (qItem = queue->head; qItem; qItem = qItem->next)
   {
      if ((bFound = (*Compare)(*ppPayload, qItem->pPayload)) != DNX_QRES_CONTINUE)
      {
         if (bFound == DNX_QRES_FOUND)
         {
            *ppPayload = qItem->pPayload;

            // Cross-link previous to next and free current
            if (qPrev)
               qPrev->next = qItem->next;
            else  // We are removing the head item from the queue
               queue->head = qItem->next;

            if (qItem->next == NULL)   // We are removing the tail item
               queue->tail = qPrev;
   
            if (queue->current == qItem)  // Advance circular pointer
               if ((queue->current = qItem->next) == NULL)
                  queue->current = queue->head;

            queue->size--;    // Decrement queue size
         }
         break;
      }

      qPrev = qItem;
   }

   DNX_PT_MUTEX_UNLOCK(queue->p_mutex);

   if (bFound == DNX_QRES_FOUND)
   {
      dnxDebug(10, "dnxQueueRemove: Free(qItem=%p)", qItem);
      free(qItem);      // Free the DnxQueueEntry wrapper
   }
   return bFound;
}

//----------------------------------------------------------------------------

/** Return the number of requests in the list.
 * 
 * @param[in] queue - the queue to be queried for request count.
 * @param[out] pSize - the address of storage in which to return the count.
 * 
 * @return Always returns zero.
 */
int dnxQueueSize(DnxQueue * queue, int * pSize)
{
    int rc;                     /* return code of pthreads functions.  */

    assert(queue && pSize);

    DNX_PT_MUTEX_LOCK(queue->p_mutex);

    *pSize = queue->size;

    DNX_PT_MUTEX_UNLOCK(queue->p_mutex);

    return DNX_OK;
}

//----------------------------------------------------------------------------

/** Delete a requests queue.
 * 
 * Delete a request queue structure, and free all memory it uses.
 * 
 * @param[in] queue - the requests queue to be deleted.
 * 
 * @return Always returns zero.
 */
int dnxQueueDelete(DnxQueue * queue)
{
    DnxQueueEntry *qItem, *qNext;      // QueueEntry pointers
    int rc;                     /* return code of pthreads functions.  */

    assert(queue);

    DNX_PT_MUTEX_LOCK(queue->p_mutex);

    /* first free any requests that might be on the queue */
    qItem = queue->head;
    while (qItem != NULL) {
        qNext = qItem->next;
        free(qItem);
        qItem = qNext;
    }

    DNX_PT_MUTEX_UNLOCK(queue->p_mutex);

    dnxDebug(10, "dnxQueueDelete: Free(queue=%p)", queue);
    free(queue);

    return DNX_OK;
}

/*--------------------------------------------------------------------------*/

