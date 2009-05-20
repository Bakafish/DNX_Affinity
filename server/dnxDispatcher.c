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

/** Implements the DNX Dispatcher thread.
 *
 * The purpose of this thread is to dispatch service check jobs to the
 * registered worker nodes for execution.  It accomplishes this by
 * accepting work node registrations and then dispatching service check
 * jobs to registered worker nodes using a weighted-round-robin algorithm.
 * The algorithm's weighting is based upon the number of jobs-per-second
 * throughput rating of each worker node.
 * 
 * @file dnxDispatcher.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IMPL
 */

#include "dnxDispatcher.h"

#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxProtocol.h"
#include "dnxXml.h"
#include "dnxNebMain.h"
#include "dnxRegistrar.h"
#include "dnxJobList.h"
#include "dnxLogging.h"
#include "dnxNode.h"

#include <netinet/in.h>
#include <assert.h>

/** The implementation data structure for a dispatcher object. */
typedef struct iDnxDispatcher_
{
   char * chname;          /*!< The dispatcher channel name. */
   char * url;             /*!< The dispatcher channel URL. */
   DnxJobList * joblist;   /*!< The job list we're dispatching from. */
   DnxChannel * channel;   /*!< Dispatcher communications channel. */
   pthread_t tid;          /*!< The dispatcher thread id. */
} iDnxDispatcher;

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** Send a job to a designated client node.
 * 
 * @param[in] idisp - the dispatcher object.
 * @param[in] pSvcReq - the service request block belonging to the client 
 *    node we're targeting.
 * @param[in] pNode - the dnx request node to be sent.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxSendJobMsg(iDnxDispatcher * idisp, DnxNewJob * pSvcReq, DnxNodeRequest * pNode)
{
   struct sockaddr * sin; 
   pthread_t tid = pthread_self();
   DnxJob job;
   int ret;

//   sin = (struct sockaddr *)pNode->address;
   dnxDebug(2, 
         "dnxDispatcher[%lx]: Dispatching job [%lu,%lu] (%s) to dnxClient [%s]"
         " at node %s host flags = (%qu)",
         tid, pSvcReq->xid.objSerial, pSvcReq->xid.objSlot, pSvcReq->cmd, 
         pNode->hn, pNode->addr, pNode->flags);

   memset(&job, 0, sizeof job);
   job.xid      = pSvcReq->xid;
   job.state    = DNX_JOB_PENDING;
   job.priority = 1;
   job.timeout  = pSvcReq->timeout;
   job.cmd      = pSvcReq->cmd;

   if ((ret = dnxSendJob(idisp->channel, &job, pNode->address)) != DNX_OK)
   {
            dnxDebug(1,"Unable to send job [%lu,%lu] (%s) to worker node %s: %s.",
            tid, pSvcReq->xid.objSerial, pSvcReq->xid.objSlot, pSvcReq->cmd, 
            pNode->addr, dnxErrorString(ret));

            dnxLog("Unable to send job [%lu,%lu] (%s) to worker node %s: %s.",
            tid, pSvcReq->xid.objSerial, pSvcReq->xid.objSlot, pSvcReq->cmd, 
            pNode->addr, dnxErrorString(ret));
   }else{
//        char * addr = ntop((struct sockaddr *)sin);
        dnxNodeListIncrementNodeMember(pNode->addr,JOBS_DISPATCHED);        
//        xfree(addr);
   }
   // Clean up the pNode
   dnxDeleteNodeReq(pNode);
   return ret;
}

//----------------------------------------------------------------------------

/** Send a service request to the appropriate worker node.
 * 
 * @param[in] idisp - the dispatcher object.
 * @param[in] pSvcReq - the service request to be dispatched.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxDispatchJob(iDnxDispatcher * idisp, DnxNewJob * pSvcReq)
{
   DnxNodeRequest * pNode = pSvcReq->pNode;
   int ret;

   ret = dnxSendJobMsg(idisp, pSvcReq, pNode);

   /** @todo Implement the fork-error re-scheduling logic as 
    * found in run_service_check() in checks.c. 
    */

   return ret;
}

//----------------------------------------------------------------------------

/** The dispatcher thread entry point.
 * 
 * @param[in] data - an opaque pointer to the dispatcher object.
 * 
 * @return Always returns NULL.
 */
static void * dnxDispatcher(void * data)
{
   iDnxDispatcher * idisp = (iDnxDispatcher *)data;

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);

   dnxLog("Dispatcher awaiting jobs...");

   while (1)
   {
      DnxNewJob svcReq;
      int ret;

      pthread_testcancel();

      // wait for a new entry to be added to the job queue
      if ((ret = dnxJobListDispatch(idisp->joblist, &svcReq)) == DNX_OK)
      {
         if ((ret = dnxDispatchJob(idisp, &svcReq)) == DNX_OK)
            dnxAuditJob(&svcReq, "DISPATCH");
         else
            dnxAuditJob(&svcReq, "DISPATCH-FAIL");
      }

   }
   return 0;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

DnxChannel * dnxDispatcherGetChannel(DnxDispatcher * disp)
      { return ((iDnxDispatcher *)disp)->channel; }

//----------------------------------------------------------------------------

int dnxDispatcherCreate(char * chname, char * dispurl, DnxJobList * joblist, 
      DnxDispatcher ** pdisp)
{
   iDnxDispatcher * idisp;
   int ret;

   if ((idisp = (iDnxDispatcher *)xmalloc(sizeof *idisp)) == 0)
      return DNX_ERR_MEMORY;

   memset(idisp, 0, sizeof *idisp);
   idisp->chname = xstrdup(chname);
   idisp->url = xstrdup(dispurl);
   idisp->joblist = joblist;

   if (!idisp->url || !idisp->chname)
   {
      xfree(idisp);
      return DNX_ERR_MEMORY;
   }
   if ((ret = dnxChanMapAdd(chname, dispurl)) != DNX_OK)
   {
      dnxDebug(1, "dnxDispatcherCreate: dnxChanMapAdd(%s) failed: %s.",
            chname, dnxErrorString(ret));
      dnxLog("dnxDispatcherCreate: dnxChanMapAdd(%s) failed: %s.",
            chname, dnxErrorString(ret));
      goto e1;
   }
   if ((ret = dnxConnect(chname, 0, &idisp->channel)) != DNX_OK)
   {
      dnxDebug(1, "dnxDispatcherCreate: dnxConnect(%s) failed: %s.",
            chname, dnxErrorString(ret));
      dnxLog("dnxDispatcherCreate: dnxConnect(%s) failed: %s.",
            chname, dnxErrorString(ret));
      goto e2;
   }

   // create the dispatcher thread
   if ((ret = pthread_create(&idisp->tid, 0, dnxDispatcher, idisp)) != 0)
   {
      dnxDebug(1, "dnxDispatcherCreate: thread creation failed: %s.",
            dnxErrorString(ret));
      dnxLog("dnxDispatcherCreate: thread creation failed: %s.",
            dnxErrorString(ret));
      ret = DNX_ERR_THREAD;
      goto e3;
   }

   *pdisp = (DnxDispatcher*)idisp;

   return DNX_OK;

// error paths

e3:dnxDisconnect(idisp->channel);
e2:dnxChanMapDelete(idisp->chname);
e1:xfree(idisp->url);
   xfree(idisp->chname);
   xfree(idisp);

   return ret;
}

//----------------------------------------------------------------------------

void dnxDispatcherDestroy(DnxDispatcher * disp)
{
   iDnxDispatcher * idisp = (iDnxDispatcher *)disp;

   pthread_cancel(idisp->tid);
   pthread_join(idisp->tid, 0);

   dnxDisconnect(idisp->channel);
   dnxChanMapDelete(idisp->chname);

   xfree(idisp->url);
   xfree(idisp->chname);
   xfree(idisp);
}

/*--------------------------------------------------------------------------
                                 TEST MAIN

   From within dnx/server, compile with GNU tools using this command line:
    
      gcc -DDEBUG -DDNX_DISPATCHER_TEST -DHAVE_NANOSLEEP -g -O0 \
         -lpthread -o dnxDispatcherTest -I../nagios/nagios-2.7/include \
         -I../common dnxDispatcher.c ../common/dnxError.c \
         ../common/dnxSleep.c

   Alternatively, a heap check may be done with the following command line:

      gcc -DDEBUG -DDEBUG_HEAP -DDNX_DISPATCHER_TEST -DHAVE_NANOSLEEP -g -O0 \
         -lpthread -o dnxDispatcherTest -I../nagios/nagios-2.7/include \
         -I../common dnxDispatcher.c ../common/dnxError.c \
         ../common/dnxSleep.c ../common/dnxHeap.c

   Note: Leave out -DHAVE_NANOSLEEP if your system doesn't have nanosleep.

  --------------------------------------------------------------------------*/

#ifdef DNX_DISPATCHER_TEST

// #include "utesthelp.h"
// 
// static int verbose;
// static int once = 0;
// static char * test_url = "udp://0.0.0.0:12489";
// static char * test_chname = "TestCollector";
// static DnxChannel * test_channel = (DnxChannel *)1;
// static DnxJobList * test_joblist = (DnxJobList *)1;
// static DnxNewJob test_job;
// static int test_payload;
// static DnxNodeRequest test_node;
// 
// 
// // functional stubs
// IMPLEMENT_DNX_DEBUG(verbose);
// IMPLEMENT_DNX_SYSLOG(verbose);
// 
// int dnxEqualXIDs(DnxXID * pxa, DnxXID * pxb)
// {
//    return pxa->objType == pxb->objType 
//          && pxa->objSerial == pxb->objSerial 
//          && pxa->objSlot == pxb->objSlot;
// }
// 
// int dnxChanMapAdd(char * name, char * url) 
// {
//    CHECK_TRUE(name != 0);
//    CHECK_TRUE(strcmp(name, test_chname) == 0);
//    CHECK_TRUE(url != 0);
//    CHECK_TRUE(strcmp(url, test_url) == 0);
//    return 0;
// }
// 
// int dnxConnect(char * name, int active, DnxChannel ** channel) 
// {
//    *channel = test_channel;
//    CHECK_TRUE(name != 0);
//    CHECK_TRUE(strcmp(name, test_chname) == 0);
//    CHECK_TRUE(active == 0);
//    return 0;
// }
// 
// int dnxJobListDispatch(DnxJobList * pJobList, DnxNewJob * pJob)
// {
//    CHECK_TRUE(pJobList == test_joblist);
//    CHECK_TRUE(pJob != 0);
//    memcpy(pJob, &test_job, sizeof *pJob);
// 
//    once++; 
// 
//    return 0;
// }
// 
// int dnxSendJob(DnxChannel * channel, DnxJob * pJob, char * address) 
// {
//    CHECK_TRUE(channel != 0);
//    CHECK_TRUE(pJob != 0);
// 
//    CHECK_TRUE(dnxEqualXIDs(&pJob->xid, &test_job.xid));
//    CHECK_TRUE(pJob->state == DNX_JOB_PENDING);
//    CHECK_TRUE(pJob->priority == 1);
//    CHECK_TRUE(pJob->timeout == test_job.timeout);
//    CHECK_TRUE(pJob->cmd == test_job.cmd);
// 
//    return 0;
// }
// 
// int dnxAuditJob(DnxNewJob * pJob, char * action)
// {
//    CHECK_TRUE(pJob != 0);
//    CHECK_TRUE(strcmp(action, "DISPATCH") == 0);
//    return 0;
// }
// 
// void dnxDisconnect(DnxChannel * channel) 
// {
//    CHECK_TRUE(channel == test_channel);
// }
// 
// void dnxChanMapDelete(char * name) 
// {
//    CHECK_TRUE(name != 0);
//    CHECK_TRUE(strcmp(name, test_chname) == 0);
// }
// 
// int main(int argc, char ** argv)
// {
//    DnxDispatcher * dp;
//    iDnxDispatcher * idp;
// 
//    verbose = argc > 1 ? 1 : 0;
// 
//    memset(&test_node, 0, sizeof test_node);
//    test_job.state = DNX_JOB_PENDING;
//    memset(&test_job.xid, 1, sizeof test_job.xid);
//    test_job.cmd = "test command";
//    test_job.start_time = 1000;
//    test_job.timeout = 5;
//    test_job.expires = 5000;
//    test_job.payload = &test_payload;
//    test_job.pNode = &test_node;
// 
//    CHECK_ZERO(dnxDispatcherCreate(test_chname, test_url, test_joblist, &dp));
// 
//    idp = (iDnxDispatcher *)dp;
// 
//    CHECK_TRUE(strcmp(idp->chname, test_chname) == 0);
//    CHECK_TRUE(idp->joblist == test_joblist);
//    CHECK_TRUE(idp->tid != 0);
//    CHECK_TRUE(strcmp(idp->url, test_url) == 0);
// 
//    CHECK_TRUE(dnxDispatcherGetChannel(dp) == idp->channel);
// 
//    while (!once)
//       dnxCancelableSleep(10);
// 
//    dnxDispatcherDestroy(dp);
// 
// #ifdef DEBUG_HEAP
//    CHECK_ZERO(dnxCheckHeap());
// #endif
// 
//    return 0;
// }
// 
#endif   /* DNX_DISPATCHER_TEST */

/*--------------------------------------------------------------------------*/

