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
   int ret = DNX_OK;

   assert(ireg && ppMsg && *ppMsg);

   // compute expiration time of this request
   pReq = *ppMsg;
   pReq->expires = now + pReq->ttl;
   // Store threads affinity flags in struct
   pReq->affinity = getDnxAffinity(pReq->hostname);
   // locate existing node: update expiration time, or add to the queue
   if (dnxQueueFind(ireg->rqueue, (void **)&pReq, dnxCompareNodeReq) == DNX_QRES_FOUND)
   {
      pReq->expires = (*ppMsg)->expires;
      dnxDebug(2, 
            "dnxRegistrar[%lx]: Updated req [%lu,%lu] at %u; expires at %u.", 
            tid, pReq->xid.objSerial, pReq->xid.objSlot, 
            (unsigned)(now % 1000), (unsigned)(pReq->expires % 1000));
   }
   else if ((ret = dnxQueuePut(ireg->rqueue, *ppMsg)) == DNX_OK)
   {
      
      *ppMsg = 0;    // we're keeping this message; return NULL
      dnxDebug(2, 
            "dnxRegistrar[%lx]: Added req for [%s] [%lu,%lu] at %u; expires at %u.", 
            tid, pReq->hostname, pReq->xid.objSerial, pReq->xid.objSlot, 
            (unsigned)(now % 1000), (unsigned)(pReq->expires % 1000));
   }
   else
      dnxLog("DNX Registrar: Unable to enqueue node request: %s.", 
            dnxErrorString(ret));

   return ret;
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
         dnxCompareNodeReq) == DNX_QRES_FOUND)
      xfree(pReq);      // free the dequeued DnxNodeRequest message

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

   dnxLog("DNX Registrar: Awaiting worker node requests...");

   while (1)
   {
      int ret;

      // (re)allocate message block if not consumed in last pass
      if (pMsg == 0 && (pMsg = (DnxNodeRequest *)xmalloc(sizeof *pMsg)) == 0)
      {
         dnxCancelableSleep(10);    // sleep for a while and try again...
         continue;
      }

      pthread_cleanup_push(xfree, pMsg);

      pthread_testcancel();

      // wait on the dispatch socket for a request
      if ((ret = dnxWaitForNodeRequest(ireg->dispchan, pMsg, pMsg->address, 
            DNX_REGISTRAR_REQUEST_TIMEOUT)) == DNX_OK)
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

      pthread_cleanup_pop(0);

      if (ret != DNX_OK && ret != DNX_ERR_TIMEOUT)
         dnxLog("DNX Registrar: Process node request failed: %s.", 
               dnxErrorString(ret));
   }
   return 0;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

int dnxGetNodeRequest(DnxRegistrar * reg, DnxNodeRequest ** ppNode, unsigned long long flag)
{
   iDnxRegistrar * ireg = (iDnxRegistrar *)reg;
   int ret, discard_count = 0;
   DnxNodeRequest * node = 0;

   assert(reg && ppNode);

   while ((ret = dnxQueueGet(ireg->rqueue, (void **)&node)) == DNX_OK)
   {
      time_t now = time(0);

      // verify that this request's Time-To-Live (TTL) has not expired and
      // that this thread has affinity
      if (node->expires > now)
      {
         // make sure that this thread has affinity
         if (node->affinity & flag)
         {
            dnxDebug(4, "dnxRegisterNode: [%s] has affinity.",
               node->hostname);
            break;
         } else {
            dnxDebug(4, "dnxRegisterNode: [%s] can not service request.",
               node->hostname);
         }
      } else {  
         dnxDebug(3, 
            "dnxRegisterNode: Expired req [%lu,%lu] at %u; expired at %u.", 
            node->xid.objSerial, node->xid.objSlot, 
            (unsigned)(now % 1000), (unsigned)(node->expires % 1000));

         discard_count++;

         xfree(node); 
         node = 0;
      }
   }

   if (discard_count > 0)
      dnxDebug(1, "dnxGetNodeRequest: Discarded %d expired node requests.", 
            discard_count);

   if (ret != DNX_OK)
      dnxDebug(2, "dnxGetNodeRequest: Unable to fulfill node request: %s.",
            dnxErrorString(ret));

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
      dnxLog("DNX Registrar: Queue creation failed: %s.", dnxErrorString(ret));
      xfree(ireg);
      return ret;
   }
   if ((ret = pthread_create(&ireg->tid, 0, dnxRegistrar, ireg)) != 0)
   {
      dnxLog("DNX Registrar: Thread creation failed: %s.", strerror(ret));
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

DnxAffinityList* addDnxAffinity(DnxAffinityList *p, char * name, unsigned long long flag) 
{
    if (p->next == p) 
    {
       p->name = name;
       p->flag = flag;
       p->next = NULL;
       dnxDebug(4, "addDnxAffinity: Added [%s]", p->name);    
    } else {
       DnxAffinityList *new_item = (DnxAffinityList *)malloc(sizeof(DnxAffinityList));
       new_item->name = name;
       new_item->flag = flag;
       new_item->next = p->next;
       p->next = new_item;
       dnxDebug(4, "addDnxAffinity: Added [%s] to [%s]", new_item->name, p->name);    
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

#include "utesthelp.h"

static int verbose;
static int passes = 0;
static DnxNodeRequest * test_req1;
static DnxNodeRequest * test_req2;

// functional stubs
IMPLEMENT_DNX_DEBUG(verbose);
IMPLEMENT_DNX_SYSLOG(verbose);

int dnxWaitForNodeRequest(DnxChannel * channel, DnxNodeRequest * pReg, 
      char * address, int timeout)
{
   CHECK_TRUE(channel == (DnxChannel *)17);
   CHECK_TRUE(pReg != 0);
   CHECK_TRUE(address != 0);
   CHECK_TRUE(timeout == DNX_REGISTRAR_REQUEST_TIMEOUT);

   passes++;      // bump registrar loop pass count

   // complex test harness -
   //   pass 1: add a new registration
   //   pass 2: update an existing registration
   //   pass 3: remove an existing registration

   memset(pReg, 0, sizeof *pReg);
   pReg->ttl = 10;   // seconds - won't timeout during test
   if (passes < 3)
      pReg->reqType = DNX_REQ_REGISTER;
   else
      pReg->reqType = DNX_REQ_DEREGISTER;

   if (passes < 4)
      return 0;

   return DNX_ERR_TIMEOUT;
}

DnxQueueResult dnxQueueFind(DnxQueue * queue, void ** ppPayload, 
      DnxQueueResult (*Compare)(void * pLeft, void * pRight))
{
   CHECK_TRUE(queue = (DnxQueue *)37);
   if (passes == 1)
      return DNX_QRES_CONTINUE;  // pass 1: return not-found
   *ppPayload = test_req1;
   return DNX_QRES_FOUND;        // pass 2: return found
}

int dnxQueuePut(DnxQueue * queue, void * pPayload)
{
   CHECK_TRUE(queue = (DnxQueue *)37);
   CHECK_TRUE(pPayload != 0);
   test_req1 = (DnxNodeRequest *)pPayload;
   return 0;                     // pass 1: add new registration
}

DnxQueueResult dnxQueueRemove(DnxQueue * queue, void ** ppPayload, 
      DnxQueueResult (*Compare)(void * pLeft, void * pRight))
{
   CHECK_TRUE(queue = (DnxQueue *)37);
   CHECK_TRUE(ppPayload != 0);
   CHECK_TRUE(Compare == dnxCompareNodeReq);
   *ppPayload = test_req1;       // pass 3: remove existing registration
   return DNX_QRES_FOUND;
}

int dnxQueueGet(DnxQueue * queue, void ** ppPayload)
{
   CHECK_TRUE(queue = (DnxQueue *)37);
   CHECK_TRUE(ppPayload != 0);
   *ppPayload = test_req1;       // pass 4+: called from dnxGetNodeRequest
   return 0;
}

int dnxQueueCreate(unsigned maxsz, void (*pldtor)(void *), DnxQueue ** pqueue)
{
   CHECK_TRUE(pqueue != 0);
   *pqueue = (DnxQueue *)37;
   return 0;
}

void dnxQueueDestroy(DnxQueue * queue)
{
   CHECK_TRUE(queue == (DnxQueue *)37);
}

int main(int argc, char ** argv)
{
   DnxRegistrar * reg;
   iDnxRegistrar * ireg;
   DnxNodeRequest * node;

   verbose = argc > 1 ? 1 : 0;

   CHECK_ZERO(dnxRegistrarCreate(5, (DnxChannel *)17, &reg));

   ireg = (iDnxRegistrar *)reg;

   CHECK_TRUE(ireg->dispchan == (DnxChannel *)17);
   CHECK_TRUE(ireg->rqueue == (DnxQueue *)37);
   CHECK_TRUE(ireg->tid != 0);

   while (passes < 4)
      dnxCancelableSleep(10);

   CHECK_ZERO(dnxGetNodeRequest(reg, &node));
   CHECK_TRUE(node == test_req1);

   dnxRegistrarDestroy(reg);

#ifdef DEBUG_HEAP
   CHECK_ZERO(dnxCheckHeap());
#endif

   return 0;
}

#endif   /* DNX_REGISTRAR_TEST */

/*--------------------------------------------------------------------------*/

