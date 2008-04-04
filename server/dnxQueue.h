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

typedef enum DnxQueueResult_
{ 
   DNX_QRES_CONTINUE = 0,
   DNX_QRES_FOUND,
   DNX_QRES_EXIT,
   DNX_QRES_ERROR
} DnxQueueResult;

/** An abstract data type for DnxQueue. */
typedef struct { int unused; } DnxQueue;

/** Add an opaque payload to a queue.
 * 
 * @param[in] queue - the queue to which @p pPayload should be added.
 * @param[in] pPayload - the data to be added to @p queue.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @note Cancellation safe.
 */
int dnxQueuePut(DnxQueue * queue, void * pPayload);

/** Returns the first entry from the queue.
 * 
 * The returned payload needs to be destroyed/freed by the caller.
 * 
 * @param[in] queue - the queue from which to get the next pending queue item.
 * @param[out] ppPayload - the address of storage in which to return the 
 *    first pending queue item, if found.
 * 
 * @return Zero if found, or DNX_ERR_NOTFOUND if not found.
 * 
 * @note Cancellation safe.
 */
int dnxQueueGet(DnxQueue * queue, void ** ppPayload);

/** Remove a matching payload from a queue.
 * 
 * @param[in] queue - the queue to be queried for a matching payload.
 * @param[in,out] ppPayload - on entry contains the payload to be located; 
 *    on exit returns the located payload.
 * @param[in] Compare - a comparison function used to location an item whose
 *    payload matches @p ppPayload; accepts two void pointers to user payload 
 *    objects, and returns a DnxQueueResult code.
 * 
 * @return A DnxQueueResult code - DNX_QRES_FOUND (1) if found, or
 * DNX_QRES_CONTINUE (0) if not found. This is essentially a boolean value
 * as the value of DNX_QRES_CONTINUE is zero.
 * 
 * @note Cancellation safe.
 */
DnxQueueResult dnxQueueRemove(DnxQueue * queue, void ** ppPayload, 
      DnxQueueResult (*Compare)(void * pLeft, void * pRight));

/** Find a node matching a specified payload in a queue.
 * 
 * @param[in] queue - the queue to be queried for a matching payload.
 * @param[in,out] ppPayload - on entry contains the payload to be matched;
 *    on exit, returns the located matching payload.
 * @param[in] Compare - a node comparison routine; accepts two void pointers
 *    to payload objects, and returns a DnxQueueResult code.
 * 
 * @return A DnxQueueResult code; DNX_QRES_FOUND (1) if the requested item
 * payload was found in the queue, or DNX_QRES_CONTINUE of not. This is 
 * essentially a boolean return value, as the value of DNX_QRES_CONTINUE is 
 * zero.
 * 
 * @note Cancellation safe.
 */
DnxQueueResult dnxQueueFind(DnxQueue * queue, void ** ppPayload, 
      DnxQueueResult (*Compare)(void * pLeft, void * pRight));

/** Create a new queue object.
 * 
 * @param[in] maxsz - the maximum size of the queue - zero means unlimited.
 * @param[in] pldtor - a function that is called when the queue
 *    needs to delete a queue item payload. Optional; may be passed as 0.
 * @param[out] pqueue - the address of storage in which to return the new
 *    queue object.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @note Cancellation safe.
 */
int dnxQueueCreate(unsigned maxsz, void (*pldtor)(void *), DnxQueue ** pqueue);

/** Delete a requests queue.
 * 
 * Delete a request queue structure, and free all memory it uses.
 * 
 * @param[in] queue - the requests queue to be deleted.
 */
void dnxQueueDestroy(DnxQueue * queue);

#endif   /* _DNXQUEUE_H_ */

