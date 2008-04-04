//	dnxQueue.c
//
//	Implements thread-safe queues for DNX.
//
//	Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
//	First Written: 2006-07-11	R.W.Ingraham
//	Last Modified: 2007-08-22
//
//	License:
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License version 2 as
//	published by the Free Software Foundation.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//


#include <stdlib.h>     // malloc() and free()
#include <assert.h>     // assert()
#include <syslog.h>

#include "dnxError.h"
#include "dnxQueue.h"   // dnxQueue definitions and prototypes
#include "dnxLogging.h"


//----------------------------------------------------------------------------

/*
 * function dnxQueueInit(): create a requests queue.
 * algorithm: creates a request queue structure, initialize with given
 *            parameters.
 * input:     queue's mutex, queue's condition variable.
 * output:    none.
 */
int dnxQueueInit (DnxQueue **ppQueue, pthread_mutex_t *p_mutex, pthread_cond_t *p_cond_var, int max_size)
{
    // Validate parameters
    if (!ppQueue || !p_mutex || !p_cond_var || max_size < 0)
        return DNX_ERR_INVALID;

    if ((*ppQueue = (DnxQueue *)malloc(sizeof(DnxQueue))) == NULL)
        return DNX_ERR_MEMORY;

	//dnxDebug(10, "dnxQueueInit: Malloc(*ppQueue=%p)", *ppQueue);

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

/*
 * function dnxQueuePut(): add a request to the requests list
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     pointer to queue, request number.
 * output:    none.
 */
int dnxQueuePut (DnxQueue *queue, void *pPayload)
{
    int rc;                     /* return code of pthreads functions.  */
    DnxQueueEntry * qItem;      /* pointer to newly added request.     */

    /* sanity check - make sure queue is not NULL */
    assert(queue);

    /* create structure with new request */
    if ((qItem = (DnxQueueEntry *)malloc(sizeof(DnxQueueEntry ))) == NULL)
        return DNX_ERR_MEMORY;

	//dnxDebug(10, "dnxQueuePut: Malloc(qItem=%p, pPayload=%p)", qItem, pPayload);

    qItem->pPayload = pPayload;
    qItem->next = NULL;

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(queue->p_mutex);

    /* add new request to the end of the list, updating list */
    /* pointers as required */
    if (queue->size == 0) { /* special case - list is empty */
        queue->head = qItem;
        queue->tail = qItem;
        queue->current = qItem;
    }
    else {
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
        if (queue->head == NULL) { /* this was last request on the list */
            queue->tail = NULL;
        }

        /* decrease the total number of pending requests */
        queue->size--;

        /* release the qItem payload */
		if (qItem->pPayload)
		{
			//dnxDebug(10, "dnxQueuePut: Free(qItem->pPayload=%p)", qItem->pPayload);
	        free(qItem->pPayload);	/* NB: Assumes that payload came from the heap! */
		}
		//dnxDebug(10, "dnxQueuePut: Free(qItem=%p)", qItem);
        free(qItem);	/* release the qItem wrapper */
	}

    /* signal the condition variable - there's a new request to handle */
    rc = pthread_cond_signal(queue->p_cond_var);

    /* unlock mutex */
    rc = pthread_mutex_unlock(queue->p_mutex);

    return DNX_OK;
}

//----------------------------------------------------------------------------

/*
 * function dnxQueueGet(): gets the first pending request from the requests list
 *                         removing it from the list.
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     pointer to requests queue.
 * output:    pointer to the removed request, or NULL if none.
 * memory:    the returned request needs to be freed by the caller.
 */
int dnxQueueGet (DnxQueue *queue, void **ppPayload)
{
    int rc;                     /* return code of pthreads functions.  */
    DnxQueueEntry * qItem;      /* pointer to request.                 */

    /* sanity check - make sure queue is not NULL */
    assert(queue && ppPayload);

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(queue->p_mutex);

    if (queue->size > 0) {
        qItem = queue->head;
        queue->head = qItem->next;
        if (queue->current == qItem)
	        queue->current = qItem->next;
        if (queue->head == NULL) { /* this was last request on the list */
            queue->tail = NULL;
        }

        /* decrease the total number of pending requests */
        queue->size--;
    }
    else { /* requests list is empty */
        qItem = NULL;
    }

    /* unlock mutex */
    rc = pthread_mutex_unlock(queue->p_mutex);

    /* return the payload to the caller. */
    if (qItem) {
        *ppPayload = qItem->pPayload;
		//dnxDebug(10, "dnxQueueGet: Free(qItem=%p, *ppPayload=%p)", qItem, *ppPayload);
        free(qItem);    // Release the qItem wrapper
        rc = DNX_OK;
    }
    else {
        *ppPayload = NULL;
        rc = DNX_ERR_NOTFOUND;
    }

    return rc;
}

//----------------------------------------------------------------------------

/*
 * function dnxQueueGetWait(): gets the first pending request from the requests list
 *                         removing it from the list.  Suspends the calling thread
 *                         if the queue is empty.
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     pointer to requests queue.
 * output:    pointer to the removed request, or NULL if none.
 * memory:    the returned request need to be freed by the caller.
 */
int dnxQueueGetWait (DnxQueue *queue, void **ppPayload)
{
    int rc;                      /* return code of pthreads functions.  */
    DnxQueueEntry *qItem = NULL; /* pointer to request.                 */

    /* sanity check - make sure queue is not NULL */
    assert(queue && ppPayload);

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(queue->p_mutex);

    /* Block this thread until it can dequeue a request */
    while (rc == 0 && qItem == NULL) {

        /* See if we have any queue items already waiting */
        if (queue->size > 0) {
            qItem = queue->head;
            queue->head = qItem->next;
            if (queue->current == qItem)
    	        queue->current = qItem->next;
            if (queue->head == NULL) { /* this was last request on the list */
                queue->tail = NULL;
            }

            /* decrease the total number of pending requests */
            queue->size--;
        }
        else { /* requests list is empty */
            /* Wait until something is posted to this queue */
            rc = pthread_cond_wait(queue->p_cond_var, queue->p_mutex);
        }
    }

    /* unlock mutex */
    rc = pthread_mutex_unlock(queue->p_mutex);

    /* return the payload to the caller. */
    if (qItem) {
        *ppPayload = qItem->pPayload;
		//dnxDebug(10, "dnxQueueGetWait: Free(qItem=%p, *ppPayload=%p)", qItem, *ppPayload);
        free(qItem);    // Release the qItem wrapper
        rc = DNX_OK;
    }
    else {
        *ppPayload = NULL;
        rc = DNX_ERR_NOTFOUND;
    }

    return rc;
}

//----------------------------------------------------------------------------

int dnxQueueNext (DnxQueue *queue, void **ppPayload)
{
    int rc;

    /* sanity check - make sure queue is not NULL */
    assert(queue && ppPayload);

    *ppPayload = NULL;

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(queue->p_mutex);

    /* Save pointer to current payload */
    if (queue->current) {
        *ppPayload = queue->current->pPayload;

        /* Advance circular buffer pointer */
        if (queue->current->next)
            queue->current = queue->current->next;
        else
            queue->current = queue->head;
    }

    /* unlock mutex */
    rc = pthread_mutex_unlock(queue->p_mutex);

    return (*ppPayload ? DNX_OK : DNX_ERR_NOTFOUND);
}

//----------------------------------------------------------------------------

DnxQueueResult dnxQueueFind (DnxQueue *queue, void **ppPayload, DnxQueueResult (*Compare)(void *pLeft, void *pRight))
{
    DnxQueueEntry *qItem;
	DnxQueueResult bFound = DNX_QRES_CONTINUE;
	int rc;

	// Validate input parameters
	if (!queue || !ppPayload || !Compare)
		return DNX_QRES_ERROR;

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(queue->p_mutex);

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

    /* unlock mutex */
    rc = pthread_mutex_unlock(queue->p_mutex);

    return bFound;
}

//----------------------------------------------------------------------------

DnxQueueResult dnxQueueRemove (DnxQueue *queue, void **ppPayload, DnxQueueResult (*Compare)(void *pLeft, void *pRight))
{
    DnxQueueEntry *qItem, *qPrev;
	DnxQueueResult bFound = DNX_QRES_CONTINUE;
	int rc;

	// Validate input parameters
	if (!queue || !ppPayload || !Compare)
		return DNX_QRES_ERROR;

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(queue->p_mutex);

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
				else	// We are removing the head item from the queue
					queue->head = qItem->next;

				if (qItem->next == NULL)	// We are removing the tail item
					queue->tail = qPrev;
	
				if (queue->current == qItem)	// Advance circular pointer
				{
					if ((queue->current = qItem->next) == NULL)
						queue->current = queue->head;
				}

				queue->size--;		// Decrement queue size
			}
			break;
		}

		qPrev = qItem;
	}

    /* unlock mutex */
    rc = pthread_mutex_unlock(queue->p_mutex);

	if (bFound == DNX_QRES_FOUND)
	{
		//dnxDebug(10, "dnxQueueRemove: Free(qItem=%p)", qItem);
		free(qItem);		// Free the DnxQueueEntry wrapper
	}

    return bFound;
}

//----------------------------------------------------------------------------

/*
 * function dnxQueueSize(): get the number of requests in the list.
 * input:     pointer to requests queue.
 * output:    number of pending requests on the queue.
 */
int dnxQueueSize (DnxQueue *queue, int *pSize)
{
    int rc;                     /* return code of pthreads functions.  */

    /* sanity check */ 
    assert(queue && pSize);

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(queue->p_mutex);

    *pSize = queue->size;

    /* unlock mutex */
    rc = pthread_mutex_unlock(queue->p_mutex);

    return DNX_OK;
}

//----------------------------------------------------------------------------

/*
 * function dnxQueueDelete(): delete a requests queue.
 * algorithm: delete a request queue structure, and free all memory it uses.
 * input:     pointer to requests queue.
 * output:    success code
 */
int dnxQueueDelete (DnxQueue *queue)
{
    DnxQueueEntry *qItem, *qNext;      // QueueEntry pointers
    int rc;                     /* return code of pthreads functions.  */

    /* sanity check - make sure queue is not NULL */
    assert(queue);

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(queue->p_mutex);

    /* first free any requests that might be on the queue */
    qItem = queue->head;
    while (qItem != NULL) {
        qNext = qItem->next;
        free(qItem);
        qItem = qNext;
    }

    /* unlock mutex */
    rc = pthread_mutex_unlock(queue->p_mutex);

    /* finally, free the queue's struct itself */
	//dnxDebug(10, "dnxQueueDelete: Free(queue=%p)", queue);
    free(queue);

    return DNX_OK;
}

//----------------------------------------------------------------------------
