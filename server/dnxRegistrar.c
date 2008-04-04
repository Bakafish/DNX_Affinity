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
#include <pthread.h>

/** Registrar dispatch channel timeout in seconds. */
#define DNX_REGISTRAR_REQUEST_TIMEOUT  5

/** The internal registrar structure. */
typedef struct iDnxRegistrar_
{
   DnxChannel * dispchan;  /*!< The dispatch communications channel. */
   DnxQueue * rqueue;      /*!< The registered worker node requests queue. */
   pthread_t tid;          /*!< The registrar thread id. */
   long * debug;           /*!< A pointer to the global debug level. */
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

/** Register a new client node "request for work" request.
 * 
 * @param[in] ireg - the registrar on which to register a new client request.
 * @param[in] pMsg - the dnx client request node to be registered.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxRegisterNode(iDnxRegistrar * ireg, DnxNodeRequest * pMsg)
{
   DnxNodeRequest * pReq = pMsg;
   time_t now;
   int ret;

   assert(ireg && pMsg);

   // compute expiration time of this request
   pMsg->expires = (now = time(0)) + pMsg->ttl;

   dnxDebug(1, "dnxRegisterNode: Received request %lu at %lu, expires at %lu", 
         pMsg->guid.objSerial, (unsigned long)now, (unsigned long)pMsg->expires);

   // locate existing node: update expiration time, or add to the queue
   if (dnxQueueFind(ireg->rqueue, (void **)&pReq, dnxCompareNodeReq) == DNX_QRES_FOUND)
      pReq->expires = pMsg->expires;
   else if ((ret = dnxQueuePut(ireg->rqueue, pMsg)) != DNX_OK)
      dnxSyslog(LOG_ERR, 
            "dnxRegisterNode: dnxQueuePut failed; failed with %d: %s", 
            ret, dnxErrorString(ret));

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
      xfree(pReq);       // free the dequeued DnxNodeRequest message

   xfree(pMsg);          // free the Deregister resquest message

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Process a "request for work" request from a dnx client node.
 * 
 * @param[in] ireg - the registrar for which a node request should be 
 *    processed.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @note Cancellation safe.
 */
static int dnxProcessNodeRequest(iDnxRegistrar * ireg)
{
   DnxNodeRequest * pMsg;
   int ret;

   assert(ireg);

   if ((pMsg = (DnxNodeRequest *)xmalloc(sizeof *pMsg)) == NULL)
      return DNX_ERR_MEMORY;

   pthread_cleanup_push(xfree, pMsg);

   // wait on the dispatch socket for a request
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
            dnxSyslog(LOG_ERR, 
                  "dnxProcessNodeRequest: Received unsupported request "
                  "type: %d", pMsg->reqType);
      }
   }
   if (ret != DNX_OK)
   {
      if (ret == DNX_ERR_TIMEOUT)
         ret = DNX_OK;     // Timeout is OK in this instance
      xfree(pMsg);
   }

   pthread_cleanup_pop(0);

   return ret;
}

//----------------------------------------------------------------------------

/** A comparison operator that always returns "found".
 * 
 * This routine may be used by a DnxQueue user to return "found" for all
 * nodes in the queue, allowing it to walk the queue, performing some 
 * operation on each "found" node.
 * 
 * @param[in] pLeft - the left comparand.
 * @param[in] pRight - the right comparand.
 * 
 * @return Always returns DNX_QRES_FOUND.
 */
static DnxQueueResult dnxRemoveNode(void * pLeft, void * pRight)
      { assert(pLeft && pRight); return DNX_QRES_FOUND; }

//----------------------------------------------------------------------------

/** Deregister all nodes in the registration queue.
 * 
 * @param[in] queue - the queue to be cleaned.
 */
static void dnxDeregisterAllNodes(DnxQueue * queue)
{
   DnxNodeRequest unused, * p = &unused;
   while (dnxQueueRemove(queue, (void **)&p, dnxRemoveNode) == DNX_QRES_FOUND)
      xfree(p);      // free the dequeued DnxNodeRequest message
}

//----------------------------------------------------------------------------

/** Registrar thread cleanup routine - removes all outstanding queue nodes.
 * 
 * @param[in] data - an opaque pointer to the registrar node queue.
 */
static void dnxRegistrarCleanup(void * data)
      { assert(data); dnxDeregisterAllNodes((DnxQueue *)data); }

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

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
   pthread_cleanup_push(dnxRegistrarCleanup, ireg->rqueue);

   if (ireg->debug)
      dnxChannelDebug(ireg->dispchan, 1);

   dnxSyslog(LOG_INFO, "dnxRegistrar[%lx]: Awaiting worker node requests", 
         pthread_self());

   while (1)
   {
      int ret;

      pthread_testcancel();

      // wait for worker node requests
      if ((ret = dnxProcessNodeRequest(ireg)) != DNX_OK)
         dnxSyslog(LOG_ERR, 
               "dnxRegistrar[%lx]: dnxProcessNodeRequest returned %d: %s", 
               pthread_self(), ret, dnxErrorString(ret));
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

   // dequeue a Node Request from the Node Request queue
   while ((ret = dnxQueueGet(ireg->rqueue, (void **)ppNode)) == DNX_OK)
   {
      // verify that this request's Time-To-Live (TTL) has not expired
      if ((*ppNode)->expires > (now = time(NULL)))
         break;

      dnxDebug(1, "dnxRegisterNode: Expired request %lu at %lu, expires at %lu", 
            (*ppNode)->guid.objSerial, (unsigned long)now, 
            (unsigned long)(*ppNode)->expires);

      discard_count++;

      // discard this expired request
      xfree(*ppNode);
      *ppNode = NULL;
   }

   // report discarded node requests
   if (discard_count > 0)
      dnxDebug(1, "dnxGetNodeRequest: Discarded %d expired node requests", 
            discard_count);

   if (ret != DNX_OK)
      dnxDebug(1, "dnxGetNodeRequest: Unable to fulfill node request; "
                  "failed with %d: %s", ret, dnxErrorString(ret));

   return ret;
}

//----------------------------------------------------------------------------

/** Create a new registrar object.
 * 
 * @param[in] debug - a pointer to the global debug level.
 * @param[in] queuesz - the size of the queue to create in this registrar.
 * @param[in] dispchan - a pointer to the dispatcher channel.
 * @param[out] preg - the address of storage in which to return the newly
 *    created registrar.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxRegistrarCreate(long * debug, int queuesz, 
      DnxChannel * dispchan, DnxRegistrar ** preg)
{
   iDnxRegistrar * ireg;
   int ret;

   assert(debug && dispchan && preg);

   if ((ireg = (iDnxRegistrar *)xmalloc(sizeof *ireg)) == 0)
      return DNX_ERR_MEMORY;

   memset(ireg, 0, sizeof *ireg);
   ireg->debug = debug;
   ireg->dispchan = dispchan;

   if ((ret = dnxQueueCreate(queuesz, xfree, &ireg->rqueue)) != 0)
   {
      dnxSyslog(LOG_ERR, "Registrar: queue creation failed with %d: %s", 
            ret, strerror(ret));
      xfree(ireg);
      return ret;
   }
   if ((ret = pthread_create(&ireg->tid, 0, dnxRegistrar, ireg)) != 0)
   {
      dnxSyslog(LOG_ERR, "Registrar: thread creation failed with %d: %s", 
            ret, strerror(ret));
      xfree(ireg);
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
   pthread_join(ireg->tid, 0);

   dnxQueueDestroy(ireg->rqueue);

   xfree(ireg);
}

/*--------------------------------------------------------------------------*/

