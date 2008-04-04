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

/** Provides structure and prototype definitions for thread-safe DNX queues.
 *
 * The purpose of this thread is to monitor the age of service requests
 * which are being actively executed by the worker nodes.
 *
 * This requires access to the global Pending queue (which is also
 * manipulated by the Dispatcher and Collector threads.)
 *
 * @file dnxQueue.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#ifndef _DNXQUEUE_H_
#define _DNXQUEUE_H_

#include <stdio.h>       // standard I/O routines
#include <pthread.h>     // pthread functions and data structures

typedef enum _DnxQueueResult_ { DNX_QRES_CONTINUE = 0, DNX_QRES_FOUND, DNX_QRES_EXIT, DNX_QRES_ERROR } DnxQueueResult;

// Queue Entry Wrapper Structure: This wraps whatever the app wants to store in the queue
typedef struct _DnxQueueEntry_ {
    void *pPayload;					// Payload data stored by app in the queue
    struct _DnxQueueEntry_ *next;	// Pointer to next entry, NULL if none
} DnxQueueEntry;

// Queue Structure
typedef struct _DnxQueue_ {
    DnxQueueEntry *head;			// head of linked list of requests
    DnxQueueEntry *tail;			// pointer to last request
    DnxQueueEntry *current;			// circular buffer pointer
    int size;						// number of requests in queue
    int max_size;					// max number of requests allowed in queue (zero = unlimited)
    pthread_mutex_t* p_mutex;		// queue's mutex
    pthread_cond_t*  p_cond_var;	// queue's condition variable
} DnxQueue;


/*
 * create a requests queue. associate it with the given mutex
 * and condition variables.
 */
extern int dnxQueueInit (DnxQueue **ppQueue, pthread_mutex_t *p_mutex, pthread_cond_t *p_cond_var, int max_size);

/* add a request to the requests list */
extern int dnxQueuePut (DnxQueue *queue, void *pPayload);

/* get the first pending request from the requests list */
extern int dnxQueueGet (DnxQueue *queue, void **ppPayload);
extern int dnxQueueGetWait (DnxQueue *queue, void **ppPayload);

/* Advance queue's circular buffer pointer */
int dnxQueueNext (DnxQueue *queue, void **ppPayload);

/* Find a queue item without removing it */
DnxQueueResult dnxQueueFind (DnxQueue *queue, void **ppPayload, DnxQueueResult (*Compare)(void *pLeft, void *pRight));

/* Atomically find and remove a queue item */
DnxQueueResult dnxQueueRemove (DnxQueue *queue, void **ppPayload, DnxQueueResult (*Compare)(void *pLeft, void *pRight));

/* get the number of requests in the list */
extern int dnxQueueSize (DnxQueue *queue, int *pSize);

/* free the resources taken by the given requests queue */
extern int dnxQueueDelete (DnxQueue *queue);

#endif   /* _DNXQUEUE_H_ */

