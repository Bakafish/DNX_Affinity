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
   unsigned long long pxl = ((DnxNodeRequest *)pLeft)->flags;
   unsigned long long pxr = ((DnxNodeRequest *)pRight)->flags;

   assert(pLeft && pRight);

   dnxDebug(5, "dnxCompareAffinityNodeReq: dnxClient flags [%lu]", pxl);

   return pxl & pxr ? DNX_QRES_FOUND : DNX_QRES_CONTINUE;
}

//----------------------------------------------------------------------------

/** Register a new client node "request for work" request.
 * 
 * The message is either stored or used to find an existing node request
 * that should be updated. If stored, @p ppMsg is returned as zero so that
 * it will be reallocated by the caller. In all other cases, the same 
 * message block can be reused by the caller for the next request.
 * 
 * @param[in] ireg - the registrar on which to register a new client request.
 * @param[in] ppMsg - the address of the dnx client request node pointer.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxRegisterNode(iDnxRegistrar * ireg, DnxNodeRequest ** ppMsg)
{
   pthread_t tid = pthread_self();
   DnxNodeRequest * pReq;
   time_t now = time(0);
   time_t expires = now + (*ppMsg)->ttl;
   int ret = DNX_OK;
   assert(ireg && ppMsg && *ppMsg);
   pReq = *ppMsg;
   
   // locate existing node: update expiration time, or add to the queue
   if (dnxQueueFind(ireg->rqueue, &pReq, dnxCompareNodeReq) == DNX_QRES_FOUND)
   {
      pReq->expires = expires;
            
      dnxDebug(2, 
        "dnxRegisterNode[%lx]: Updated req [%lu,%lu] at %u; expires at %u.", 
        tid, pReq->xid.objSerial, pReq->xid.objSlot, 
        (unsigned)(now % 1000), (unsigned)(pReq->expires % 1000));
      dnxNodeListIncrementNodeMember(pReq->addr, JOBS_REQ_RECV);
   }
   else if ((ret = dnxQueuePut(ireg->rqueue, *ppMsg)) == DNX_OK)
   {
      // This is new, add the affinity flags  
       dnxNodeListSetNodeAffinity(pReq->addr, pReq->hn);
       pReq->flags = dnxGetAffinity(pReq->hn);
       pReq->expires = expires;
      *ppMsg = 0;    // Registered new request node
      dnxDebug(2, 
        "dnxRegisterNode[%lx]: Added new req for [%s] [%lu,%lu] at %u; expires at %u.", 
        tid, pReq->hn, pReq->xid.objSerial, pReq->xid.objSlot, 
        (unsigned)(now % 1000), (unsigned)(pReq->expires % 1000));
      dnxNodeListIncrementNodeMember(pReq->addr, JOBS_REQ_RECV);
   }
   else
   {
      dnxDebug(1, "dnxRegisterNode: Unable to enqueue node request: %s.", 
            dnxErrorString(ret));
      dnxLog("dnxRegisterNode: Unable to enqueue node request: %s.", 
            dnxErrorString(ret));
//      dnxDeleteNodeReq(*ppMsg);
   }
   return ret;
}

int dnxDeleteNodeReq(DnxNodeRequest * pMsg)
{

   assert(pMsg);
   
   xfree(pMsg->addr);
   xfree(pMsg->hn);
   xfree(pMsg);
   
   return DNX_OK;
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

   if (dnxQueueRemove(ireg->rqueue, (void **)&pReq, 
         dnxCompareNodeReq) == DNX_QRES_FOUND) {
      dnxDeleteNodeReq(pReq);      // free the dequeued DnxNodeRequest message
   }
   
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
static void * dnxRegistrar(void * data)
{
   iDnxRegistrar * ireg = (iDnxRegistrar *)data;
   DnxNodeRequest * pMsg = 0;

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);

   dnxLog("dnxRegistrar: Awaiting worker node requests...");

   while (1)
   {
      int ret;

      // (re)allocate message block if not consumed in last pass
      if (pMsg == 0 && (pMsg = (DnxNodeRequest *)xmalloc(sizeof *pMsg)) == 0)
      {
         dnxCancelableSleep(10);    // sleep for a while and try again...
         continue;
      } else {
        // Clean out the old object
        xfree(pMsg->addr);
        xfree(pMsg->hn);
      }

//      pthread_cleanup_push(xfree, pMsg); // the thread cleanup handler

      pthread_cleanup_push(dnxDeleteNodeReq, pMsg); // the thread cleanup handler
      
      pthread_testcancel();

      // wait on the dispatch socket for a request
      if ((ret = dnxWaitForNodeRequest(ireg->dispchan, pMsg, pMsg->address, 
            DNX_REGISTRAR_REQUEST_TIMEOUT)) == DNX_OK) // LEAKING
      {
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

      pthread_cleanup_pop(0); // clean up the thread

      if (ret != DNX_OK && ret != DNX_ERR_TIMEOUT)
      {
         dnxDebug(1, "dnxRegistrar: Process node request failed: %s.", 
               dnxErrorString(ret));
         dnxLog("dnxRegistrar: Process node request failed: %s.", 
               dnxErrorString(ret));
      }
   }
   return 0;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

int dnxGetNodeRequest(DnxRegistrar * reg, DnxNodeRequest ** ppNode)
{
   iDnxRegistrar * ireg = (iDnxRegistrar *)reg;
   int ret = DNX_ERR_NOTFOUND;
   int discard_count = 0;
   int client_queue_len = dnxQueueSize(ireg->rqueue);
   DnxNodeRequest * hostNode = *(DnxNodeRequest **)ppNode;
   // Temperarily assign so we can find a affinity match, dnxQueueRemove should
   // replace it with an appropriate worker node
   DnxNodeRequest * node = hostNode;
   
   assert(reg && ppNode);

    if(! client_queue_len)
    {
        dnxDebug(1, "dnxGetNodeRequest: There are no DNX client threads regestered.");
        // We probably just started up and no threads are registered yet.
        // It's also possable that all our Clients are down or a previous run 
        // has expired all our threads and we haven't registered any new workers
        // Just return the original request node and let the mail loop try again
        return ret;
    }

    dnxDebug(4, "dnxGetNodeRequest: Entering loop (%i) Number of elements [%i]",
        ireg->tid, client_queue_len);

   while ((ret = dnxQueueRemove(ireg->rqueue, (void **)&node, dnxCompareAffinityNodeReq)) == DNX_QRES_FOUND)
   {
      time_t now = time(0);

      dnxDebug(2, "dnxGetNodeRequest: For Host[%s] :: DNX Client (%s) node exp (%u) now (%u)",
      hostNode->hn, node->hn, (unsigned)(node->expires % 1000), (unsigned)(now % 1000));

      // verify that this request's Time-To-Live (TTL) has not expired and
      // that this thread has affinity
      if (node->expires > now)
      {
        dnxDebug(1, "dnxGetNodeRequest: Found Hostnode [%s]:(%qu) with Affinity to dnxClient [%s]:(%qu) .",
                hostNode->hn, hostNode->flags, node->hn, node->flags);
        break;
      } else {  
      
        //SM 09/08 DnxNodeList
//        char * addr = (char *)ntop(node->address,addr);
        dnxNodeListIncrementNodeMember(node->addr,JOBS_REQ_EXP);
//        xfree(addr);
        //SM 09/08 DnxNodeList END

         dnxDebug(1, 
            "dnxGetNodeRequest: Expired req [%lu,%lu] at %u; expired at %u.", 
            node->xid.objSerial, node->xid.objSlot, 
            (unsigned)(now % 1000), (unsigned)(node->expires % 1000));

         discard_count++;
         
         // Delete the expired node
         dnxDeleteNodeReq(node); 

         // Re-initialize with host node so we can try and match affinity again
         node = hostNode;
      }
   }
//   while ((ret = dnxQueueGet(ireg->rqueue, (void **)&node)) == DNX_OK)
//    {
//       time_t now = time(0);
// 
// dnxDebug(4, "dnxGetNodeRequest: For Host[%s] :: DNX Client (%s)",
//     hostNode->hn, node->hostname);
// 
//       // verify that this request's Time-To-Live (TTL) has not expired and
//       // that this thread has affinity
//       if (node->expires > now)
//       {
//       dnxDebug(4, "dnxGetNodeRequest: Affinity Client [%s]:(%qu) Host [%s]:(%qu).",
//                 node->hostname, node->flags, 
//                 hostNode->hn, hostNode->flags);
//       
//       
//          // make sure that this thread has affinity
//          if (node->flags & hostNode->flags)
//          {
//             dnxDebug(4, "dnxGetNodeRequest: dnxClient [%s] has affinity to (%s).",
//                 *(char **)node->hostname, hostNode->hn);
//             break;
//          } else {
// 
//             // DnxNodeList increment may need to be here
// //             char * addr = ntop(node->address,addr);
// //             dnxNodeListIncrementNodeMember(addr,JOBS_REQ_EXP);
// //             xfree(addr);
// 
//             dnxDebug(2, "dnxGetNodeRequest: dnxClient [%s] can not service request for (%s).",
//                *(char **)node->hostname, hostNode->hn);
// //             ret = DNX_ERR_NOTFOUND;
//          }
//       } else {  
//       
//         //SM 09/08 DnxNodeList
//         char * addr = ntop(node->address,addr);
//         dnxNodeListIncrementNodeMember(addr,JOBS_REQ_EXP);
//         xfree(addr);
//         //SM 09/08 DnxNodeList END
// 
//          dnxDebug(3, 
//             "dnxGetNodeRequest: Expired req [%lu,%lu] at %u; expired at %u.", 
//             node->xid.objSerial, node->xid.objSlot, 
//             (unsigned)(now % 1000), (unsigned)(node->expires % 1000));
// 
//          discard_count++;
// 
//          xfree(node); 
//          node = 0;
//       }
//    }

    // If we break out of the loop with affinity then we should have set
    // the node to a correct dnxClient object
    int after_client_queue_len = dnxQueueSize(ireg->rqueue);
    dnxDebug(6, "dnxGetNodeRequest: Exiting loop (%i) Number of elements [%i]",
        ireg->tid, after_client_queue_len);

    
   if (discard_count > 0)
   {
      dnxDebug(4, "dnxGetNodeRequest: Discarded %d expired node requests.", 
            discard_count);
   }

// If no affinity matches or there are no dnxClient requests in the
// queue we send it to the jobs list without a node
   if (ret != DNX_QRES_FOUND)
   {
      if(ret == DNX_QRES_CONTINUE) {
         // The only way we should be hitting this is if we expired 
         // all valid workers.
         ret = DNX_ERR_NOTFOUND;
         // set the pointer back to the original object we passed
         node = hostNode;
      } 
      else 
      {
        // A real error, we shouldn't return any object
        node = 0;
        // Get rid of the struct we used to pass the host data
        dnxDeleteNodeReq(hostNode);
      }
   } else {
      if(hostNode != node) {
          dnxDeleteNodeReq(hostNode); 
      }
      ret = DNX_OK;
   }

   *ppNode = node;   // return a node or NULL

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
    if (p->next == p) 
    {
       p->name = xstrdup(name);
       p->flag = flag;
       p->next = NULL;
       dnxDebug(4, "dnxAddAffinity: Added linked list item [%s]", p->name);    
    } else {
       DnxAffinityList * new_item = (DnxAffinityList *)malloc(sizeof(DnxAffinityList));
       new_item->name = xstrdup(name);
       new_item->flag = flag;
       new_item->next = p->next;
       p->next = new_item;
       dnxDebug(4, "dnxAddAffinity: Added linked list item [%s] to [%s]", new_item->name, p->name);    
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

