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

/** Implements the DNX Registrar thread.
 *
 * The purpose of this thread is to manage Worker Node registrations.
 * When a Worker Node wants to receive service check jobs from the
 * Scheduler Node, it must first register itself with the Scheduler
 * Node by sending a UDP-based registration message to it.
 * 
 * The Registrar thread manages this registration process on behalf
 * of the Scheduler.
 * 
 * @file dnxRegistrar.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IMPL
 */

#include "dnxRegistrar.h"

#include "dnxNebMain.h"
#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxQueue.h"
#include "dnxProtocol.h"
#include "dnxLogging.h"

#include <assert.h>

#define DNX_REGISTRAR_REQUEST_TIMEOUT  30

//----------------------------------------------------------------------------

/** Registrar thread clean-up routine.
 * 
 * @param[in] data - an opaque pointer to registrar thread data. This is 
 *    actually a pointer to the dnx server global data structure.
 */
static void dnxRegistrarCleanup(void * data)
{
   DnxGlobalData * gData = (DnxGlobalData *)data;

   assert(data);

   // Release all remaining Worker Node channels
   dnxDeregisterAllNodes(gData);

   // Unlock the Worker Node Request Queue mutex
   /** @todo Fix this - we should always know the state of our mutexes. */
   if (&(gData->tmReq))
      pthread_mutex_unlock(&(gData->tmReq));

   // Unlock the Go signal mutex
   if (&(gData->tmGo))
      pthread_mutex_unlock(&(gData->tmGo));
}

//----------------------------------------------------------------------------

/** Compare two node "request for work" requests for equality.
 * 
 * @param[in] pLeft - the left node to be compared.
 * @param[in] pRight - the right node to be compared.
 * 
 * @return DNX_QRES_FOUND on match; DNX_QRES_CONTINUE if no match.
 */
static DnxQueueResult dnxCompareNodeReq(void * pLeft, void * pRight)
{
   assert(pLeft && pRight);

   return
      (
         (((DnxNodeRequest *)pLeft)->guid.objType   == ((DnxNodeRequest *)pRight)->guid.objType) 
      && (((DnxNodeRequest *)pLeft)->guid.objSerial == ((DnxNodeRequest *)pRight)->guid.objSerial)
      ) ? DNX_QRES_FOUND : DNX_QRES_CONTINUE;
}

//----------------------------------------------------------------------------

/** A node compare routine that always returns DNX_QRES_FOUND.
 * 
 * This no-op comparison routine allows the list walker to return success
 * for every node in the list - that way every node may be processed by 
 * simply continuing after each match.
 * 
 * @param[in] pLeft - the left node to be compared - not used.
 * @param[in] pRight - the right node to be compared - not used.
 * 
 * @return Always returns DNX_QRES_FOUND.
 */
static DnxQueueResult dnxRemoveNode(void * pLeft, void * pRight)
{
   assert(pLeft && pRight);

   return DNX_QRES_FOUND;
}

//----------------------------------------------------------------------------

/** The main thread entry point procedure for the registrar thread.
 * 
 * @param[in] data - an opaque pointer to registrar thread data. This is 
 *    actually a pointer to the dnx server global data structure.
 * 
 * @return Always returns NULL.
 */
void * dnxRegistrar(void * data)
{
   DnxGlobalData * gData = (DnxGlobalData *)data;
   int ret = 0;

   assert(data);

   // Set my cancel state to 'enabled', and cancel type to 'deferred'
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

   // Set thread cleanup handler
   pthread_cleanup_push(dnxRegistrarCleanup, data);

   // Wait for Go signal from dnxNebMain
   DNX_PT_MUTEX_LOCK(&gData->tmGo);

   // See if the go signal has already been broadcast
   if (gData->isGo == 0)
   {
      // Nope.  Wait for the synchronization signal
      if (pthread_cond_wait(&(gData->tcGo), &(gData->tmGo)) != 0)
      {
         // pthread_mutex_unlock(&(gData->tmGo));
         pthread_exit(NULL);
      }
   }

   // Release the lock
   DNX_PT_MUTEX_UNLOCK(&gData->tmGo);

   // Check for debug flag
   if (gData->debug)
      dnxChannelDebug(gData->pDispatch, 1);

   dnxSyslog(LOG_INFO, "dnxRegistrar[%lx]: Awaiting worker node requests", pthread_self());

   // Wait for new service checks or cancellation
   while (1)
   {
      // Test for thread cancellation
      pthread_testcancel();

      // Wait for Worker Node Requests
      if ((ret = dnxProcessNodeRequest(gData)) != DNX_OK)
      {
         dnxSyslog(LOG_ERR, "dnxRegistrar[%lx]: dnxProcessNodeRequest returned %d", pthread_self(), ret);
      }
   }

   // Remove thread cleanup handler
   pthread_cleanup_pop(1);

   return 0;
}

//----------------------------------------------------------------------------

/** Return an available node "request for work" object pointer.
 * 
 * @param[in] gData - the dnx server global data structure.
 * @param[out] ppNode - the address of storage in which to return the located
 *    request node.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxGetNodeRequest(DnxGlobalData * gData, DnxNodeRequest ** ppNode)
{
   int discard_count = 0;
   time_t now;
   int ret;

   // Validate input parameters
   if (!gData || !ppNode)
   {
      dnxSyslog(LOG_ERR, "dnxGetNodeRequest: Invalid parameters");
      return DNX_ERR_INVALID;
   }

   *ppNode = NULL;

   // Dequeue a Node Request from the Node Request queue
   while ((ret = dnxQueueGet(gData->qReq, (void **)ppNode)) == DNX_OK)
   {
      // Verify that this request's Time-To-Live (TTL) has not expired
      if ((*ppNode)->expires > (now = time(NULL)))
         break;

      dnxDebug(1, "dnxRegisterNode: Expired request %lu at %lu, expires at %lu", (*ppNode)->guid.objSerial, (unsigned long)now, (unsigned long)((*ppNode)->expires));

      discard_count++;

      // Discard this expired request
      free(*ppNode);
      *ppNode = NULL;

      // Cycle around to grab another request...
   }

   // Report discarded node requests
   if (discard_count > 0)
      dnxDebug(1, "dnxGetNodeRequest: Discarded %d expired node requests", discard_count);
   if (ret != DNX_OK)
      dnxDebug(1, "dnxGetNodeRequest: Unable to fulfill node request: %d", ret);

   return ret;
}

//----------------------------------------------------------------------------

/** Process a "request for work" request from a dnx client node.
 * 
 * @param[in] gData - the dnx server global data structure.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxProcessNodeRequest(DnxGlobalData * gData)
{
   DnxNodeRequest * pMsg;
   int ret;

   assert(gData);

   // Allocate a DnxNodeRequest structure
   if ((pMsg = (DnxNodeRequest *)malloc(sizeof(DnxNodeRequest))) == NULL)
      return DNX_ERR_MEMORY;

   //dnxDebug(10, "dnxProcessNodeRequest: Malloc(pMsg=%p)", pMsg);

   // Wait on the Dispatch socket for a request
   if ((ret = dnxWaitForNodeRequest(gData->pDispatch, pMsg, pMsg->address, DNX_REGISTRAR_REQUEST_TIMEOUT)) == DNX_OK)
   {
      // Handle the message
      switch (pMsg->reqType)
      {
      case DNX_REQ_REGISTER:
         ret = dnxRegisterNode(gData, pMsg);
         break;
      case DNX_REQ_DEREGISTER:
         ret = dnxDeregisterNode(gData, pMsg);
         break;
      default:
         ret = DNX_ERR_UNSUPPORTED;
         dnxSyslog(LOG_ERR, "dnxProcessNodeRequest: Received unsupported request type: %d", pMsg->reqType);
      }
   }

   if (ret != DNX_OK)
   {
      if (ret == DNX_ERR_TIMEOUT)
         ret = DNX_OK;  // Timeout is OK in this instance

      // Free message struct if things didn't work out
      //dnxDebug(10, "dnxProcessNodeRequest: Free(pMsg=%p)", pMsg);
      free(pMsg);
   }

   return ret;
}

//----------------------------------------------------------------------------

/** Register a new client node "request for work" request.
 * 
 * @param[in] gData - the dnx server global data structure.
 * @param[in] pMsg - the dnx client request node to be registered.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxRegisterNode(DnxGlobalData * gData, DnxNodeRequest * pMsg)
{
   time_t now;
   int ret;

   assert(gData && pMsg);

   // Compute expiration time of this request
   pMsg->expires = (now = time(NULL)) + pMsg->ttl;

   dnxDebug(1, "dnxRegisterNode: Received request %lu at %lu, expires at %lu", pMsg->guid.objSerial, (unsigned long)now, (unsigned long)(pMsg->expires));

   // Add this node to the Worker Node List
   /** @todo Ensure that it doesn't already exist in the List... */
   if ((ret = dnxQueuePut(gData->qReq, pMsg)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxRegisterNode: dnxQueuePut failed: %d", ret);
   }

   return ret;
}

//----------------------------------------------------------------------------

/** Deregister a node "request for work" request.
 * 
 * @param[in] gData - the dnx server global data structure.
 * @param[in] pMsg - the dnx client request node to be deregistered.
 * 
 * @return Always returns zero.
 */
int dnxDeregisterNode(DnxGlobalData * gData, DnxNodeRequest * pMsg)
{
   DnxNodeRequest * pReq = pMsg;

   // Search for and remove this node from the Node Request List
   if (dnxQueueRemove(gData->qReq, (void **)&pReq, dnxCompareNodeReq) == DNX_QRES_FOUND)
      free(pReq);    // Free the dequeued DnxNodeRequest message

   free(pMsg);    // Free the Deregister resquest message

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Deregister all nodes in a node list.
 * 
 * @param[in] gData - the dnx server global data structure.
 * 
 * @return Always returns zero.
 */
int dnxDeregisterAllNodes(DnxGlobalData * gData)
{
   DnxNodeRequest dummy, * pReq = &dummy;

   // Search for and remove this node from the Node Request List
   while (dnxQueueRemove(gData->qReq, (void **)&pReq, dnxRemoveNode) == DNX_QRES_FOUND)
      free(pReq);    // Free the dequeued DnxNodeRequest message

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

