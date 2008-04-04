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

#include "dnxChannel.h"
#include "dnxNebMain.h"
#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxQueue.h"
#include "dnxProtocol.h"
#include "dnxLogging.h"

#include <assert.h>

/** Registrar dispatch channel timeout in seconds. */
#define DNX_REGISTRAR_REQUEST_TIMEOUT  30

/** The internal registrar structure. */
typedef struct iDnxRegistrar_
{
   long * debug;           /*!< A pointer to the global debug level. */
   DnxChannel * dispchan;  /*!< The dispatch communications channel. */
   DnxQueue * rqueue;      /*!< The registered worker node requests queue. */
   pthread_t tid;          /*!< The registrar thread id. */
} iDnxRegistrar;

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
         ((DnxNodeRequest *)pLeft)->guid.objType   == ((DnxNodeRequest *)pRight)->guid.objType 
      && ((DnxNodeRequest *)pLeft)->guid.objSerial == ((DnxNodeRequest *)pRight)->guid.objSerial
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

/** Register a new client node "request for work" request.
 * 
 * @param[in] ireg - the registrar on which to register a new client request.
 * @param[in] pMsg - the dnx client request node to be registered.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxRegisterNode(iDnxRegistrar * ireg, DnxNodeRequest * pMsg)
{
   time_t now;
   int ret;

   assert(ireg && pMsg);

   // Compute expiration time of this request
   pMsg->expires = (now = time(NULL)) + pMsg->ttl;

   dnxDebug(1, "dnxRegisterNode: Received request %lu at %lu, expires at %lu", 
         pMsg->guid.objSerial, (unsigned long)now, (unsigned long)pMsg->expires);

   /** @todo Ensure that it doesn't already exist in the List... */

   // Add this node to the Worker Node List
   if ((ret = dnxQueuePut(ireg->rqueue, pMsg)) != DNX_OK)
      dnxSyslog(LOG_ERR, "dnxRegisterNode: dnxQueuePut failed: %d", ret);

   return ret;
}

//----------------------------------------------------------------------------

/** Deregister a node "request for work" request.
 * 
 * @param[in] ireg - the registrar from which to deregister a client request.
 * @param[in] pMsg - the dnx client request node to be deregistered.
 * 
 * @return Always returns zero.
 */
static int dnxDeregisterNode(iDnxRegistrar * ireg, DnxNodeRequest * pMsg)
{
   DnxNodeRequest * pReq = pMsg;

   assert(ireg && pMsg);

   // Search for and remove this node from the Node Request List
   if (dnxQueueRemove(ireg->rqueue, (void **)&pReq, 
         dnxCompareNodeReq) == DNX_QRES_FOUND)
      free(pReq);       // free the dequeued DnxNodeRequest message

   free(pMsg);          // free the Deregister resquest message

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Deregister all nodes in a node list.
 * 
 * @param[in] ireg - the registrar for which all requests should be destroyed.
 * 
 * @return Always returns zero.
 */
static int dnxDeregisterAllNodes(iDnxRegistrar * ireg)
{
   DnxNodeRequest dummy;
   DnxNodeRequest * pReq = &dummy;

   // Search for and remove this node from the Node Request List
   while (dnxQueueRemove(ireg->rqueue, (void **)&pReq, 
         dnxRemoveNode) == DNX_QRES_FOUND)
      free(pReq);    // free the dequeued DnxNodeRequest message

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Process a "request for work" request from a dnx client node.
 * 
 * @param[in] ireg - the registrar for which a node request should be 
 *    processed.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxProcessNodeRequest(iDnxRegistrar * ireg)
{
   DnxNodeRequest * pMsg;
   int ret;

   assert(ireg);

   if ((pMsg = (DnxNodeRequest *)malloc(sizeof *pMsg)) == NULL)
      return DNX_ERR_MEMORY;
   dnxDebug(10, "dnxProcessNodeRequest: Malloc(pMsg=%p)", pMsg);

   // Wait on the dispatch socket for a request
   if ((ret = dnxWaitForNodeRequest(ireg->dispchan, pMsg, pMsg->address, 
         DNX_REGISTRAR_REQUEST_TIMEOUT)) == DNX_OK)
   {
      switch (pMsg->reqType)
      {
         case DNX_REQ_REGISTER:
            ret = dnxRegisterNode(ireg, pMsg);
            break;

         case DNX_REQ_DEREGISTER:
            ret = dnxDeregisterNode(ireg, pMsg);
            break;

         default:
            ret = DNX_ERR_UNSUPPORTED;
            dnxSyslog(LOG_ERR, "dnxProcessNodeRequest: Received "
                               "unsupported request type: %d", pMsg->reqType);
      }
   }

   if (ret != DNX_OK)
   {
      if (ret == DNX_ERR_TIMEOUT)
         ret = DNX_OK;  // Timeout is OK in this instance

      // Free message struct if things didn't work out
      dnxDebug(10, "dnxProcessNodeRequest: Free(pMsg=%p)", pMsg);
      free(pMsg);
   }
   return ret;
}

//----------------------------------------------------------------------------

/** Registrar thread clean-up routine.
 * 
 * @param[in] data - an opaque pointer to registrar thread data. This is 
 *    actually a pointer to the dnx server global data structure.
 */
static void dnxRegistrarCleanup(void * data)
{
   iDnxRegistrar * ireg = (iDnxRegistrar *)data;

   assert(data);

   // Release all remaining Worker Node channels
   dnxDeregisterAllNodes((DnxRegistrar *)ireg);
}

//----------------------------------------------------------------------------

/** The main thread entry point procedure for the registrar thread.
 * 
 * @param[in] data - an opaque pointer to registrar thread data. This is 
 *    actually a pointer to the dnx server global data structure.
 * 
 * @return Always returns NULL.
 */
static void * dnxRegistrar(void * data)
{
   iDnxRegistrar * ireg = (iDnxRegistrar *)data;
   int ret = 0;

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
   pthread_cleanup_push(dnxRegistrarCleanup, data);

   if (ireg->debug)
      dnxChannelDebug(ireg->dispchan, 1);

   dnxSyslog(LOG_INFO, "dnxRegistrar[%lx]: Awaiting worker node requests", 
         pthread_self());

   while (1)
   {
      pthread_testcancel();

      // Wait for Worker Node Requests
      if ((ret = dnxProcessNodeRequest(ireg)) != DNX_OK)
         dnxSyslog(LOG_ERR, "dnxRegistrar[%lx]: dnxProcessNodeRequest "
                            "returned %d", pthread_self(), ret);
   }

   pthread_cleanup_pop(1);

   return 0;
}

//----------------------------------------------------------------------------

/** Return an available node "request for work" object pointer.
 * 
 * @param[in] reg - the registrar from which a node request should be returned.
 * @param[out] ppNode - the address of storage in which to return the located
 *    request node.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxGetNodeRequest(DnxRegistrar * reg, DnxNodeRequest ** ppNode)
{
   iDnxRegistrar * ireg = (iDnxRegistrar *)reg;
   int discard_count = 0;
   time_t now;
   int ret;

   assert(reg && ppNode);

   *ppNode = NULL;

   // Dequeue a Node Request from the Node Request queue
   while ((ret = dnxQueueGet(ireg->rqueue, (void **)ppNode)) == DNX_OK)
   {
      // Verify that this request's Time-To-Live (TTL) has not expired
      if ((*ppNode)->expires > (now = time(NULL)))
         break;

      dnxDebug(1, "dnxRegisterNode: Expired request %lu at %lu, expires at %lu", 
            (*ppNode)->guid.objSerial, (unsigned long)now, 
            (unsigned long)(*ppNode)->expires);

      discard_count++;

      // Discard this expired request
      free(*ppNode);
      *ppNode = NULL;
   }

   // Report discarded node requests
   if (discard_count > 0)
      dnxDebug(1, "dnxGetNodeRequest: Discarded %d expired node requests", 
            discard_count);

   if (ret != DNX_OK)
      dnxDebug(1, "dnxGetNodeRequest: Unable to fulfill node request: %d", ret);

   return ret;
}

/** Create a new registrar object.
 * 
 * @param[in] debug - a pointer to the global debug level.
 * @param[in] dispchan - a pointer to the dispatcher channel.
 * @param[in] rqueue - a pointer to the request queue.
 * @param[out] preg - the address of storage in which to return the newly
 *    created registrar.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxRegistrarCreate(long * debug, DnxChannel * dispchan, 
      DnxQueue * rqueue, DnxRegistrar ** preg)
{
   iDnxRegistrar * ireg;
   int ret;

   assert(debug && dispchan && rqueue && preg);

   if ((ireg = (iDnxRegistrar *)malloc(sizeof *ireg)) == 0)
      return DNX_ERR_MEMORY;

   ireg->debug = debug;
   ireg->dispchan = dispchan;
   ireg->rqueue = rqueue;
   ireg->tid = 0;

   if ((ret = pthread_create(&ireg->tid, NULL, dnxRegistrar, ireg)) != 0)
   {
      dnxSyslog(LOG_ERR, "Registrar: thread creation failed: (%d) %s", 
            ret, strerror(ret));
      free(ireg);
      return DNX_ERR_THREAD;
   }

   *preg = (DnxRegistrar *)ireg;

   return DNX_OK;
}

/** Destroy a previously created registrar object.
 * 
 * Signals the registrar thread, waits for it to stop, and frees allocated 
 * resources.
 * 
 * @param[in] reg - the registrar to be destroyed.
 */
void dnxRegistrarDestroy(DnxRegistrar * reg)
{
   iDnxRegistrar * ireg = (iDnxRegistrar *)reg;

   assert(reg && ireg->tid);

   pthread_cancel(ireg->tid);
   pthread_join(ireg->tid, NULL);
   free(ireg);
}

/*--------------------------------------------------------------------------*/

