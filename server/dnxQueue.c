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
#include <pthread.h>

/** Queue entry wrapper structure - wraps user payload. */
typedef struct iDnxQueueEntry_ 
{
   struct iDnxQueueEntry_ * next;   /*!< Pointer to next entry, 0 if none. */
   void * pPayload;                 /*!< User payload data. */
} iDnxQueueEntry;

/** Queue implementation structure. */
typedef struct iDnxQueue_ 
{
   iDnxQueueEntry * head;           /*!< Head of linked list of items. */
   iDnxQueueEntry * tail;           /*!< Pointer to last list item. */
   iDnxQueueEntry * current;        /*!< Circular buffer pointer. */
   void (*freepayload)(void *);     /*!< Payload destructor (optional). */
   unsigned size;                   /*!< Number of items in queue. */
   unsigned maxsz;                  /*!< Maximum number of requests allowed in queue (zero = unlimited). */
   pthread_mutex_t mutex;           /*!< Queue's mutex. */
   pthread_cond_t cv;               /*!< Queue's condition variable. */
} iDnxQueue;

/*--------------------------------------------------------------------------
                     NON-EXPORTED INTERFACE (not used yet)
  --------------------------------------------------------------------------*/

/** Waits and returns the first pending item payload from a queue.
 * 
 * Suspends the calling thread if the queue is empty. The returned payload 
 * and its resources becomes the property of the caller.
 * 
 * @param[in] queue - the queue to be waited on.
 * @param[out] ppPayload - the address of storage in which to return the
 *    payload of the first queue item.
 * 
 * @return Zero on success, or DNX_ERR_NOTFOUND if not found.
 * 
 * @note Not currently used (or exported by the dnxQueue.h header file).
 * 
 * @note Cancellation safe.
 */
int dnxQueueGetWait(DnxQueue * queue, void ** ppPayload)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item = 0;
   
   assert(queue && ppPayload);
   
   DNX_PT_MUTEX_LOCK(&iqueue->mutex);
   
   // block this thread until it can dequeue a request
   while (item == 0) 
   {
      // see if we have any queue items already waiting
      if (iqueue->size > 0) 
      {
         item = iqueue->head;
         iqueue->head = item->next;
         if (iqueue->current == item)
            iqueue->current = item->next;

         // adjust the tail pointer if the queue is now empty
         if (iqueue->head == 0)
            iqueue->tail = 0;
         
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

/** Return the next item payload without removing it from the queue.
 * 
 * Ownership of the queue item payload does NOT transfer to the caller.
 * 
 * @param[in] queue - the queue from which the next item payload should 
 *    be returned.
 * @param[out] ppPayload - the address of storage in which to return a 
 *    reference to the next item payload.
 * 
 * @return Zero on success, or DNX_ERR_NOTFOUND if there is no next node.
 * 
 * @note Not currently used (or exported by the dnxQueue.h header file).
 * 
 * @note Cancellation safe.
 */
int dnxQueueNext(DnxQueue * queue, void ** ppPayload)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   
   assert(queue && ppPayload);
   
   *ppPayload = 0;
   
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

/** Return the number of items in the queue.
 * 
 * @param[in] queue - the queue to be queried for item count.
 * 
 * @return The count of items in the queue.
 * 
 * @note Not currently used (or exported by the dnxQueue.h header file).
 * 
 * @note Cancellation safe.
 */
int dnxQueueSize(DnxQueue * queue)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   int count;

   assert(queue);
   
   DNX_PT_MUTEX_LOCK(&iqueue->mutex);
   
   count = (int)iqueue->size;
   
   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);
   
   return count;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

int dnxQueuePut(DnxQueue * queue, void * pPayload)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item;
   
   assert(queue);
   
   // create structure with new request
   if ((item = (iDnxQueueEntry *)xmalloc(sizeof *item)) == 0)
      return DNX_ERR_MEMORY;

   item->pPayload = pPayload;
   item->next = 0;
   
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
   if (iqueue->maxsz > 0 && iqueue->size > iqueue->maxsz)
   {
      // remove the oldest entry at the queue head
      item = iqueue->head;
      iqueue->head = item->next;
      if (iqueue->current == item)
         iqueue->current = item->next;

      // adjust tail if queue is now empty
      if (iqueue->head == 0)
         iqueue->tail = 0;
      
      iqueue->size--;

      // call item payload destructor, if one was supplied
      if (iqueue->freepayload)
         iqueue->freepayload(item->pPayload);

      xfree(item);
   }
   
   // signal any waiters - there's a new item in the queue
   pthread_cond_signal(&iqueue->cv);
   
   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);
   
   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxQueueGet(DnxQueue * queue, void ** ppPayload)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item = 0;
   
   assert(queue && ppPayload);
   
   DNX_PT_MUTEX_LOCK(&iqueue->mutex);
   
   if (iqueue->size > 0) 
   {
      // remove the 'head' item from the queue
      item = iqueue->head;
      iqueue->head = item->next;
      if (iqueue->current == item)
         iqueue->current = item->next;

      // adjust tail pointer if queue is now empty
      if (iqueue->head == 0)
         iqueue->tail = 0;
   
      iqueue->size--;
   }
   
   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);

   // return the payload to the caller, free queue item
   if (item) 
   {
      *ppPayload = item->pPayload;
      xfree(item);
      return DNX_OK;
   }

   return DNX_ERR_NOTFOUND;
}

//----------------------------------------------------------------------------

DnxQueueResult dnxQueueRemove(DnxQueue * queue, void ** ppPayload, 
      DnxQueueResult (*Compare)(void * pLeft, void * pRight))
{
   DnxQueueResult bFound = DNX_QRES_CONTINUE;
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item, * prev;

   assert(queue && ppPayload && Compare);

   DNX_PT_MUTEX_LOCK(&iqueue->mutex);

   prev = 0;
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

            if (item->next == 0)          // removing the tail item
               iqueue->tail = prev;

            if (iqueue->current == item)  // advance circular pointer
               if ((iqueue->current = item->next) == 0)
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

int dnxQueueCreate(unsigned maxsz, void (*pldtor)(void *), DnxQueue ** pqueue)
{
   iDnxQueue * iqueue;

   assert(pqueue);
   
   if ((iqueue = (iDnxQueue *)xmalloc(sizeof *iqueue)) == 0)
      return DNX_ERR_MEMORY;
   
   // initialize queue
   memset(iqueue, 0, sizeof *iqueue);
   iqueue->freepayload = pldtor;
   iqueue->maxsz = maxsz;

   // initialize thread sync
   DNX_PT_MUTEX_INIT(&iqueue->mutex);
   pthread_cond_init(&iqueue->cv, 0);

   *pqueue = (DnxQueue *)iqueue;

   return DNX_OK;
}

//----------------------------------------------------------------------------

void dnxQueueDestroy(DnxQueue * queue)
{
   iDnxQueue * iqueue = (iDnxQueue *)queue;
   iDnxQueueEntry * item;
   
   assert(queue);
   
   DNX_PT_MUTEX_LOCK(&iqueue->mutex);

   // first free any requests that might be on the queue
   item = iqueue->head;
   while (item != 0) 
   {
      iDnxQueueEntry * next = item->next;
      iqueue->freepayload(item->pPayload);
      xfree(item);
      item = next;
   }
   
   DNX_PT_MUTEX_UNLOCK(&iqueue->mutex);
   
   DNX_PT_MUTEX_DESTROY(&iqueue->mutex);
   pthread_cond_destroy(&iqueue->cv);

   xfree(iqueue);
}

/*--------------------------------------------------------------------------
                                 UNIT TEST

   From within dnx/server, compile with GNU tools using this command line:
    
      gcc -DDEBUG -DDNX_QUEUE_TEST -g -O0 -I../common dnxQueue.c \
         ../common/dnxError.c -lpthread -lgcc_s -lrt -o dnxQueueTest

   Alternatively, a heap check may be done with the following command line:

      gcc -DDEBUG -DDEBUG_HEAP -DDNX_QUEUE_TEST -g -O0 -I../common \
         dnxQueue.c ../common/dnxError.c ../common/dnxHeap.c \
         -lpthread -lgcc_s -lrt -o dnxCfgParserTest 

  --------------------------------------------------------------------------*/

#ifdef DNX_QUEUE_TEST

#include "utesthelp.h"
#include <time.h>

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

static int free_count;
static int verbose;

IMPLEMENT_DNX_DEBUG(verbose);

// functional stubs
static DnxQueueResult qtcmp(void * left, void * right)
      { return strcmp((char *)left, (char *)right) == 0? 
            DNX_QRES_FOUND: DNX_QRES_CONTINUE; }

static void qtfree(void * p)
{
   free_count++;
   free(p);
}

int main(int argc, char ** argv)
{
   DnxQueue * queue;
   iDnxQueue * iqueue;
   DnxQueueResult qres;
   char * msg100_static = "message 100";
   char * msg25_static = "message 25";
   char * msg250_static = "message 250";
   char * msgs[101];
   char * msg2;
   int i;

   verbose = argc > 1? 1: 0;

   // create a new queue and get a concrete reference to it for testing
   CHECK_ZERO(dnxQueueCreate(100, qtfree, &queue));
   iqueue = (iDnxQueue *)queue;

   // check internal data structure state
   CHECK_TRUE(iqueue->head == 0);
   CHECK_TRUE(iqueue->tail == 0);
   CHECK_TRUE(iqueue->current == 0);
   CHECK_TRUE(iqueue->freepayload == qtfree);
   CHECK_TRUE(iqueue->size == 0);
   CHECK_TRUE(iqueue->maxsz == 100);

   // enqueue the messages
   free_count = 0;
   for (i = 0; i < elemcount(msgs); i++)
   {
      char buf[32];
      sprintf(buf, "message %d", i);
      CHECK_NONZERO(msgs[i] = strdup(buf));
      CHECK_ZERO(dnxQueuePut(queue, msgs[i]));
   }

   // we pushed one more than the size of the queue
   // item 0 should have been freed by the destructor
   CHECK_TRUE(free_count == 1);

   // get item 1 from the queue - we'll own it after this call
   CHECK_ZERO(dnxQueueGet(queue, (void **)&msg2));
   CHECK_TRUE(strcmp(msg2, "message 1") == 0);
   free(msg2);

   // find and remove item 100 from the queue - we'll own it on success
   msg2 = msg100_static;
   CHECK_TRUE(dnxQueueRemove(queue, (void **)&msg2, qtcmp) == DNX_QRES_FOUND);
   CHECK_NONZERO(msg2);
   CHECK_TRUE(msg2 != msg100_static);
   free(msg2);

   // attempt to find an existing item
   msg2 = msg25_static;
   CHECK_TRUE(dnxQueueFind(queue, (void **)&msg2, qtcmp) == DNX_QRES_FOUND);
   CHECK_TRUE(msg2 != msg25_static);
   CHECK_TRUE(strcmp(msg2, msgs[25]) == 0);

   // attempt to find a non-existent item
   msg2 = msg250_static;
   CHECK_TRUE(dnxQueueFind(queue, (void **)&msg2, qtcmp) == DNX_QRES_CONTINUE);

   // remove remaining entries
   for (i = 3; i < elemcount(msgs); i++)
   {
      CHECK_ZERO(dnxQueueGet(queue, (void **)&msg2));
      free(msg2);
   }

   // attempt to remove one more entry
   CHECK_NONZERO(dnxQueueGet(queue, (void **)&msg2));

   // ensure queue is now empty
   CHECK_TRUE(dnxQueueSize(queue) == 0);

   dnxQueueDestroy(queue);

   // we should have called the destructor only once
   CHECK_TRUE(free_count == 1);

#ifdef DEBUG_HEAP
   CHECK_ZERO(dnxCheckHeap());
#endif

   return 0;
}

#endif   /* DNX_QUEUE_TEST */

/*--------------------------------------------------------------------------*/

