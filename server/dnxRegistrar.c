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
#include "dnxSleep.h"
#include "dnxTransport.h"
#include "dnxProtocol.h"
#include "dnxLogging.h"
#include "dnxNode.h"

#include <assert.h>
#include <pthread.h>
#include <arpa/inet.h>

/** Registrar dispatch channel timeout in seconds. */
#define DNX_REGISTRAR_REQUEST_TIMEOUT  5

/** The internal registrar structure. */
typedef struct iDnxRegistrar_
{
   DnxChannel * dispchan;  /*!< The dispatch communications channel. */
   DnxQueue * rqueue;      /*!< The registered worker node requests queue. */
   pthread_t tid;          /*!< The registrar thread id. */
} iDnxRegistrar;

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** Compare two node "request for work" requests for equality.
 * 
 * In the message exchange between the Registrar and client worker threads
 * the XID.TYPE field will ALWAYS be DNX_OBJ_WORKER, so there is no need to 
 * compare this field because it will always be the same value. However, the 
 * XID.SERIAL field is configured as the worker's thread identifier, and the
 * XID.SLOT field is configured as the worker's IP node address. Thus, the 
 * XID.SERIAL and XID.SLOT fields uniquely identify a given worker thread.
 * 
 * @param[in] pLeft - the left node to be compared.
 * @param[in] pRight - the right node to be compared.
 * 
 * @return DNX_QRES_FOUND on match; DNX_QRES_CONTINUE if no match.
 */
static DnxQueueResult dnxCompareNodeReq(void * pLeft, void * pRight)
{
   DnxXID * pxl = &((DnxNodeRequest *)pLeft)->xid;
   DnxXID * pxr = &((DnxNodeRequest *)pRight)->xid;

   assert(pLeft && pRight);

   dnxDebug(5, "dnxCompareNodeReq: dnxClient request IP [%lu]", pxl->objSlot);

   return pxl->objSerial == pxr->objSerial && pxl->objSlot == pxr->objSlot
         ? DNX_QRES_FOUND : DNX_QRES_CONTINUE;
}

/** Compare two node "request for work" requests for affinity.
 * 
 * @param[in] pLeft - the left node to be compared.
 * @param[in] pRight - the right node to be compared.
 * 
 * @return DNX_QRES_FOUND on match; DNX_QRES_CONTINUE if no match.
 */
static DnxQueueResult dnxCompareAffinityNodeReq(void * pLeft, void * pRight)
{
   time_t now = time(0);
   
   assert(pLeft && pRight);

   // verify that this request's Time-To-Live (TTL) has not expired
   if((((DnxNodeRequest *)pLeft)->expires - 1) > now) {
      return DNX_QRES_CONTINUE;
   }

   unsigned long long pxl = ((DnxNodeRequest *)pLeft)->flags;
   unsigned long long pxr = ((DnxNodeRequest *)pRight)->flags;

   dnxDebug(6, "dnxCompareAffinityNodeReq: dnxClient flags [%lu], Host [%lu]",
      pxr, pxl);

   return pxl & pxr ? DNX_QRES_FOUND : DNX_QRES_CONTINUE;
}

//----------------------------------------------------------------------------

/** Register a new client node "request for work" request.
 * 
 * The message is either stored or used to find an existing node request
 * that should be updated. If stored, @p pDnxClientReq is returned as zero so that
 * it will be reallocated by the caller. In all other cases, the same 
 * message block can be reused by the caller for the next request.
 * 
 * @param[in] ireg - the registrar on which to register a new client request.
 * @param[in] ppDnxClientReq - the address of the dnx client request node pointer.
 * 
 * @return Zero on success, or a non-zero error value.
 */

/** Register a new client node "request for work" request.
 *
 * The message is either stored or used to find an existing node request
 * that should be updated. If stored, @p ppMsg is returned as zero so that
 * it will be reallocated by the caller. In all other cases, the same
 * message block can be reused by the caller for the next request.
 *
 * @param[in] ireg - the registrar on which to register a new client request.
 * @param[in] ppDnxClientReq - the address of the dnx client request node pointer.
 *
 * @return Zero on success, or a non-zero error value.
 */
static int dnxRegisterNode(iDnxRegistrar * ireg, DnxNodeRequest ** ppDnxClientReq) {
   pthread_t tid = pthread_self();
   DnxNodeRequest * pReq;
   time_t now = time(0);
   int ret = DNX_OK;

   assert(ireg && ppDnxClientReq && *ppDnxClientReq);

   // assign the actual object to a new pointer
   pReq = *ppDnxClientReq;

   // compute expiration time of this request
   pReq->expires = now + pReq->ttl;
   pReq->retry = 0; 
   dnxNodeListIncrementNodeMember(pReq->addr, JOBS_REQ_RECV);


   /* Locate existing dnxClient work request. The DNX client will send a request 
      and we look it up to see if it's in the queue. If it is already registered
      the dnxQueueFind will set the pointer to that object, that's a problem since
      we are passing a real object and we might leak. 
      If we find one, we update the expiration time, if it's already expired or 
      we've never seen that client before we need to create a new node and add 
      it to the queue 
   */
   if (dnxQueueFind(ireg->rqueue, (void **)&pReq, dnxCompareNodeReq) == DNX_QRES_FOUND) {
      // We just assigned the pReq to the pointer in our queue. The old object is
      // still found at *ppDnxClientReq, and since we updated the expiration
      // on that object, we use it to update the pReq object we got from the queue
      pReq->expires = (*ppDnxClientReq)->expires;
      dnxDebug(2,
            "dnxRegistrar[%lx]: Updated req [%lu,%lu] at %u; expires at %u.",
            tid, pReq->xid.objSerial, pReq->xid.objSlot,
            (unsigned)(now % 1000), (unsigned)(pReq->expires % 1000));
      
      // Unless we correctly reuse the ppDnxClientReq object or reap it, it will
      // leak badly
   } else {
      // There was no prior object found, so we will try to store it in the queue
      // Make sure this host is registered to the global node list and set
      // the correct flags in the queued object prior to queueing, so we don't race
      pReq->flags = dnxNodeListSetNodeAffinity(pReq->addr, pReq->hn);
      
      if ((ret = dnxQueuePut(ireg->rqueue, *ppDnxClientReq)) == DNX_OK) {
         // the pointer to the object pointer is set to null to indicate that we 
         // need to allocate a new messaging object, pReq should still be pointing
         // at the object in the queue


         // we're keeping this message object, so we set the pointer to the pointer
         // to null in order to indicate to the caller function that it needs to 
         // create a new object
         *ppDnxClientReq = 0;    
         dnxDebug(2, 
            "dnxRegisterNode[%lx]: Added new req for [%s] [%lu,%lu] at %u; expires at %u.", 
            tid, pReq->hn, pReq->xid.objSerial, pReq->xid.objSlot, 
            (unsigned)(now % 1000), (unsigned)(pReq->expires % 1000));
      } else {
         dnxDebug(1, "dnxRegisterNode: Unable to enqueue node request: %s.", 
               dnxErrorString(ret));
         dnxLog("dnxRegisterNode: Unable to enqueue node request: %s.", 
            dnxErrorString(ret));
      }
   }
   return ret;
}

void dnxDeleteNodeReq(void * pMsg) {
//    assert(pMsg);
   DnxNodeRequest * pNode = (DnxNodeRequest *)pMsg;
   if(pNode != 0) {
      if(pNode->xid.objSlot == -1) {
         dnxDebug(4, "dnxDeleteNodeReq: Deleting node message for job [%lu].", 
            pNode->xid.objSerial);
      } else {
         dnxDebug(4, "dnxDeleteNodeReq: Deleting node request [%lu,%lu].", 
            pNode->xid.objSerial, pNode->xid.objSlot);
      }
      xfree(pNode->addr);
      xfree(pNode->hn);
      xfree(pNode);
   }
}

DnxNodeRequest * dnxNodeCleanup(DnxNodeRequest * pNode) {
//    assert(pMsg);
   if(pNode != 0) {
      xfree(pNode->addr);
      xfree(pNode->hn);
   }
   pNode->flags = 0;
   pNode->hn = NULL;
   pNode->addr = NULL;
   pNode->xid.objSerial = -1;
   pNode->xid.objSlot = -1;
   return pNode;
}

DnxNodeRequest * dnxCreateNodeReq(void)
{

   DnxNodeRequest * pMsg = (DnxNodeRequest *)xmalloc(sizeof *pMsg);
   if(pMsg == 0) {
      dnxDebug(1, "dnxCreateNodeReq: Memory Allocation Failure.");      
      return NULL;
   } else {
      memset(pMsg, 0, sizeof *pMsg);
   }
   return pMsg;
}


//----------------------------------------------------------------------------

/** Deregister a node "request for work" request.
 * 
 * Note that the found node is freed, but the search node remains valid on
 * return from this routine.
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

   if (dnxQueueRemove(ireg->rqueue, (void **)&pReq, dnxCompareNodeReq) == DNX_QRES_FOUND) {
      dnxDeleteNodeReq(pReq);      // free the dequeued DnxNodeRequest message
   }

   // We probably shouldn't delete the request object by default since the thread
   // destructor seems to handle that
   //dnxDeleteNodeReq(pMsg); 
   
   return DNX_OK;
}


//----------------------------------------------------------------------------

/** The main thread entry point procedure for the registrar thread.
 * 
 * @param[in] data - an opaque pointer to registrar thread data. This is 
 *    actually a pointer to the dnx server global data structure.
 * 
 * @return Always returns NULL.
 */
static void * dnxRegistrar(void * data) {
   iDnxRegistrar * ireg = (iDnxRegistrar *)data;
   DnxNodeRequest * pMsg = 0;//dnxCreateNodeReq();

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);

   dnxLog("dnxRegistrar: Awaiting worker node requests...");

   while (1)
   {
      int ret = DNX_ERR_UNSUPPORTED;
      // (re)allocate message block if consumed in last pass
      if (pMsg == 0 && (pMsg = dnxCreateNodeReq()) == 0)
      {
         dnxCancelableSleep(10);    // sleep for a while and try again...
         continue;
      } 

      pthread_cleanup_push(dnxDeleteNodeReq, pMsg); // the thread cleanup handler
      
      pthread_testcancel();

      // wait on the dispatch socket for a request
      if ((ret = dnxWaitForNodeRequest(ireg->dispchan, pMsg, pMsg->address, 
            DNX_REGISTRAR_REQUEST_TIMEOUT)) == DNX_OK) {
         switch (pMsg->reqType)
         {
            case DNX_REQ_REGISTER:
               ret = dnxRegisterNode(ireg, &pMsg);
               break;

            case DNX_REQ_DEREGISTER:
               ret = dnxDeregisterNode(ireg, pMsg);
               break;

            default:
               ret = DNX_ERR_UNSUPPORTED;
         }
      }

      pthread_cleanup_pop(1); // clean up the thread

      if (ret != DNX_OK && ret != DNX_ERR_TIMEOUT)
      {
         dnxDebug(1, "dnxRegistrar: Process node request failed: %s.", 
               dnxErrorString(ret));
         dnxLog("dnxRegistrar: Process node request failed: %s.", 
               dnxErrorString(ret));
      }
   }
   return NULL;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
                                 
   We look in the registrar for a dnxClient that has affinity to the ppNode
   that we pass in. If a dnxClient is found, we update the ppNode to include
   the data required to dispatch the job and delete the node it previously had
  --------------------------------------------------------------------------*/

int dnxGetNodeRequest(DnxRegistrar * reg, DnxNodeRequest ** ppNode) {
   iDnxRegistrar * ireg = (iDnxRegistrar *)reg;
   int ret = DNX_ERR_NOTFOUND;
   int discard_count = 0;
   int client_queue_len = dnxQueueSize(ireg->rqueue);
   DnxNodeRequest * pNode = *(DnxNodeRequest **)ppNode;
   
   assert(reg && ppNode);

   if(! client_queue_len) {
      dnxDebug(1, "dnxGetNodeRequest: There are no DNX client threads regestered.");
      // We probably just started up and no threads are registered yet.
      // It's also possable that all our Clients are down or a previous run 
      // has expired all our threads and we haven't registered any new workers
      // Just return the original request node and let the caller loop try again
      return ret;
   }

   if((ret = dnxQueueRemove(ireg->rqueue, (void **)ppNode, dnxCompareAffinityNodeReq)) == DNX_QRES_FOUND) {
      // make sure we return that we found a match...
      ret = DNX_OK;
      dnxDebug(1, "dnxGetNodeRequest: Found job [%lu] from Hostnode [%s]:(%qu) with Affinity to dnxClient [%s]:(%qu) Returning(%i).",
         pNode->xid.objSerial, pNode->hn, pNode->flags, (*(DnxNodeRequest **)ppNode)->hn, (*(DnxNodeRequest **)ppNode)->flags, ret);   
      // ppNode now points at the dnxClient node , so we need to delete the 
      // job request at pNode to prevent leaks
      dnxDeleteNodeReq(pNode);
   } else {
      ret = DNX_ERR_NOTFOUND;
      dnxDebug(8, "dnxGetNodeRequest: didn't find a match. Returning (%i)", ret);
   }

   return ret;
}

//----------------------------------------------------------------------------

int dnxRegistrarCreate(unsigned queuesz, DnxChannel * dispchan, 
      DnxRegistrar ** preg)
{
   iDnxRegistrar * ireg;
   int ret;

   assert(queuesz && dispchan && preg);

   if ((ireg = (iDnxRegistrar *)xmalloc(sizeof *ireg)) == 0)
      return DNX_ERR_MEMORY;

   memset(ireg, 0, sizeof *ireg);
   ireg->dispchan = dispchan;

   // xfree needs to be replaced with a better destructor
   if ((ret = dnxQueueCreate(queuesz, xfree, &ireg->rqueue)) != 0)
   {
      dnxDebug(1, "dnxRegistrar: Queue creation failed: %s.", dnxErrorString(ret));
      xfree(ireg);
      dnxLog("dnxRegistrar: Queue creation failed: %s.", dnxErrorString(ret));
      xfree(ireg);
      return ret;
   }
   if ((ret = pthread_create(&ireg->tid, 0, dnxRegistrar, ireg)) != 0)
   {
      dnxDebug(1, "dnxRegistrar: Thread creation failed: %s.", strerror(ret));
      xfree(ireg);
      dnxLog("dnxRegistrar: Thread creation failed: %s.", strerror(ret));
      xfree(ireg);
      return DNX_ERR_THREAD;
   }

   *preg = (DnxRegistrar *)ireg;

   return DNX_OK;
}

//----------------------------------------------------------------------------

void dnxRegistrarDestroy(DnxRegistrar * reg)
{
   iDnxRegistrar * ireg = (iDnxRegistrar *)reg;

   assert(reg && ireg->tid);

   pthread_cancel(ireg->tid);
   pthread_join(ireg->tid, 0);

   dnxQueueDestroy(ireg->rqueue);

   xfree(ireg);
}

DnxAffinityList* dnxAddAffinity(DnxAffinityList *p, char * name, unsigned long long flag) 
{
   DnxAffinityList * temp_list = p;
   
   if (p->next == p) 
   {
      p->name = xstrdup(name);
      p->flag = flag;
      p->next = NULL;
      dnxDebug(3, "dnxAddAffinity: Added linked list item [%s]", p->name);    
   } else {
      while (temp_list != NULL) {
         if(strcmp(name, temp_list->name) == 0){
            dnxDebug(3, "dnxAddAffinity: Item [%s] flag was (%d) is now (%d)",
               p->name, (int)temp_list->flag, (int)(temp_list->flag | flag));    
            temp_list->flag = temp_list->flag | flag;
            return p;
         }
         temp_list = temp_list->next;
      }
      DnxAffinityList * new_item = (DnxAffinityList *)malloc(sizeof(DnxAffinityList));
      new_item->name = xstrdup(name);
      new_item->flag = flag;
      new_item->next = p->next;
      p->next = new_item;
      dnxDebug(3, "dnxAddAffinity: Added linked list item [%s] to [%s]", new_item->name, p->name);    
   }
   return p;
}

/*--------------------------------------------------------------------------
                                 TEST MAIN

   From within dnx/server, compile with GNU tools using this command line:
    
      gcc -DDEBUG -DDNX_REGISTRAR_TEST -DHAVE_NANOSLEEP -g -O0 \
         -lpthread -o dnxRegistrarTest -I../nagios/nagios-2.7/include \
         -I../common dnxRegistrar.c ../common/dnxError.c \
         ../common/dnxSleep.c

   Alternatively, a heap check may be done with the following command line:

      gcc -DDEBUG -DDEBUG_HEAP -DDNX_REGISTRAR_TEST -DHAVE_NANOSLEEP -g -O0 \
         -lpthread -o dnxRegistrarTest -I../nagios/nagios-2.7/include \
         -I../common dnxRegistrar.c ../common/dnxError.c \
         ../common/dnxSleep.c ../common/dnxHeap.c

   Note: Leave out -DHAVE_NANOSLEEP if your system doesn't have nanosleep.

  --------------------------------------------------------------------------*/

#ifdef DNX_REGISTRAR_TEST
// 
// #include "utesthelp.h"
// 
// static int verbose;
// static int passes = 0;
// static DnxNodeRequest * test_req1;
// static DnxNodeRequest * test_req2;
// 
// // functional stubs
// IMPLEMENT_DNX_DEBUG(verbose);
// IMPLEMENT_DNX_SYSLOG(verbose);
// 
// int dnxWaitForNodeRequest(DnxChannel * channel, DnxNodeRequest * pReg, 
//       char * address, int timeout)
// {
//    CHECK_TRUE(channel == (DnxChannel *)17);
//    CHECK_TRUE(pReg != 0);
//    CHECK_TRUE(address != 0);
//    CHECK_TRUE(timeout == DNX_REGISTRAR_REQUEST_TIMEOUT);
// 
//    passes++;      // bump registrar loop pass count
// 
//    // complex test harness -
//    //   pass 1: add a new registration
//    //   pass 2: update an existing registration
//    //   pass 3: remove an existing registration
// 
//    memset(pReg, 0, sizeof *pReg);
//    pReg->ttl = 10;   // seconds - won't timeout during test
//    if (passes < 3)
//       pReg->reqType = DNX_REQ_REGISTER;
//    else
//       pReg->reqType = DNX_REQ_DEREGISTER;
// 
//    if (passes < 4)
//       return 0;
// 
//    return DNX_ERR_TIMEOUT;
// }
// 
// DnxQueueResult dnxQueueFind(DnxQueue * queue, void ** ppPayload, 
//       DnxQueueResult (*Compare)(void * pLeft, void * pRight))
// {
//    CHECK_TRUE(queue = (DnxQueue *)37);
//    if (passes == 1)
//       return DNX_QRES_CONTINUE;  // pass 1: return not-found
//    *ppPayload = test_req1;
//    return DNX_QRES_FOUND;        // pass 2: return found
// }
// 
// int dnxQueuePut(DnxQueue * queue, void * pPayload)
// {
//    CHECK_TRUE(queue = (DnxQueue *)37);
//    CHECK_TRUE(pPayload != 0);
//    test_req1 = (DnxNodeRequest *)pPayload;
//    return 0;                     // pass 1: add new registration
// }
// 
// DnxQueueResult dnxQueueRemove(DnxQueue * queue, void ** ppPayload, 
//       DnxQueueResult (*Compare)(void * pLeft, void * pRight))
// {
//    CHECK_TRUE(queue = (DnxQueue *)37);
//    CHECK_TRUE(ppPayload != 0);
//    CHECK_TRUE(Compare == dnxCompareNodeReq);
//    *ppPayload = test_req1;       // pass 3: remove existing registration
//    return DNX_QRES_FOUND;
// }
// 
// int dnxQueueGet(DnxQueue * queue, void ** ppPayload)
// {
//    CHECK_TRUE(queue = (DnxQueue *)37);
//    CHECK_TRUE(ppPayload != 0);
//    *ppPayload = test_req1;       // pass 4+: called from dnxGetNodeRequest
//    return 0;
// }
// 
// int dnxQueueCreate(unsigned maxsz, void (*pldtor)(void *), DnxQueue ** pqueue)
// {
//    CHECK_TRUE(pqueue != 0);
//    *pqueue = (DnxQueue *)37;
//    return 0;
// }
// 
// void dnxQueueDestroy(DnxQueue * queue)
// {
//    CHECK_TRUE(queue == (DnxQueue *)37);
// }
// 
// int main(int argc, char ** argv)
// {
//    DnxRegistrar * reg;
//    iDnxRegistrar * ireg;
//    DnxNodeRequest * node;
// 
//    verbose = argc > 1 ? 1 : 0;
// 
//    CHECK_ZERO(dnxRegistrarCreate(5, (DnxChannel *)17, &reg));
// 
//    ireg = (iDnxRegistrar *)reg;
// 
//    CHECK_TRUE(ireg->dispchan == (DnxChannel *)17);
//    CHECK_TRUE(ireg->rqueue == (DnxQueue *)37);
//    CHECK_TRUE(ireg->tid != 0);
// 
//    while (passes < 4)
//       dnxCancelableSleep(10);
// 
//    CHECK_ZERO(dnxGetNodeRequest(reg, &node));
//    CHECK_TRUE(node == test_req1);
// 
//    dnxRegistrarDestroy(reg);
// 
// #ifdef DEBUG_HEAP
//    CHECK_ZERO(dnxCheckHeap());
// #endif
// 
//    return 0;
// }

#endif   /* DNX_REGISTRAR_TEST */

/*--------------------------------------------------------------------------*/

