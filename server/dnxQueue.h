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

/** Definitions and prototypes for thread-safe DNX queues.
 *
 * @file dnxQueue.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IFC
 */

#ifndef _DNXQUEUE_H_
#define _DNXQUEUE_H_

#include <stdio.h>       // standard I/O routines
#include <pthread.h>     // pthread functions and data structures

typedef enum DnxQueueResult_
{ 
   DNX_QRES_CONTINUE = 0,
   DNX_QRES_FOUND,
   DNX_QRES_EXIT,
   DNX_QRES_ERROR
} DnxQueueResult;

typedef void DnxQueue;

/* Add a request to the requests list */
int dnxQueuePut(DnxQueue * queue, void * pPayload);

/* Get the first pending request from the requests list */
int dnxQueueGet(DnxQueue * queue, void ** ppPayload);

/* Atomically find and remove a queue item */
DnxQueueResult dnxQueueRemove(DnxQueue * queue, void ** ppPayload, 
      DnxQueueResult (*Compare)(void * pLeft, void * pRight));

/* Wait for, and then get the first pending request from the requests list */
//int dnxQueueGetWait(DnxQueue * queue, void ** ppPayload);

/* Advance queue's circular buffer pointer */
//int dnxQueueNext(DnxQueue * queue, void ** ppPayload);

/* Find a queue item without removing it */
//DnxQueueResult dnxQueueFind(DnxQueue * queue, void ** ppPayload, 
//      DnxQueueResult (*Compare)(void * pLeft, void * pRight));

/* Get the number of requests in the list */
//int dnxQueueSize(DnxQueue * queue, int * pSize);

/* Create a requests queue. associate it with the given mutex
 * and condition variables.
 */
int dnxQueueCreate(int max_size, DnxQueue ** pqueue);

/* Free the resources taken by the given requests queue */
void dnxQueueDestroy(DnxQueue * queue);

#endif   /* _DNXQUEUE_H_ */

