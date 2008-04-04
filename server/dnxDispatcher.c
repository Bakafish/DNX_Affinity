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

#include <assert.h>

/** The implementation data structure for a dispatcher object. */
typedef struct iDnxDispatcher_
{
   long * debug;           /*!< A pointer to the global debug level. */
   char * chname;          /*!< The dispatcher channel name. */
   char * url;             /*!< The dispatcher channel URL. */
   DnxJobList * joblist;   /*!< The job list we're dispatching from. */
   dnxChannel * channel;   /*!< Dispatcher communications channel. */
   pthread_t tid;          /*!< The dispatcher thread id. */
} iDnxDispatcher;

//----------------------------------------------------------------------------

/** Send a job to a designated client node.
 * 
 * @param[in] idisp - the dispatcher object.
 * @param[in] pSvcReq - the service request block belonging to the client 
 *    node we're targeting.
 * @param[in] pNode - the dnx request node to be sent.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxSendJob(iDnxDispatcher * idisp, DnxNewJob * pSvcReq, 
      DnxNodeRequest * pNode)
{
   DnxJob job;
   int ret;

   // Debug tracking
   dnxDebug(1, "dnxDispatcher[%lx]: dnxSendJob: Dispatching "
               "job %lu to worker node: %s",
         pthread_self(), pSvcReq->guid.objSerial, pSvcReq->cmd);

   // Initialize the job structure message
   memset(&job, 0, sizeof(job));
   job.guid     = pSvcReq->guid;
   job.state    = DNX_JOB_PENDING;
   job.priority = 1;
   job.timeout  = pSvcReq->timeout;
   job.cmd      = pSvcReq->cmd;

   // Transmit the job
   if ((ret = dnxPutJob(idisp->channel, &job, pNode->address)) != DNX_OK)
      dnxSyslog(LOG_ERR, "dnxDispatcher[%lx]: dnxSendJob: Unable to "
                         "send job %lu to worker node (%d): %s",
            pthread_self(), pSvcReq->guid.objSerial, ret, pSvcReq->cmd);

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
   DnxNodeRequest * pNode;
   int ret;

   // Get the worker thread request
   pNode = pSvcReq->pNode;

   // Send this job to the selected worker node
   if ((ret = dnxSendJob(idisp, pSvcReq, pNode)) != DNX_OK)
      dnxSyslog(LOG_ERR, "dnxDispatcher[%lx]: dnxDispatchJob: "
                         "dnxSendJob failed with %d: %s", 
            pthread_self(), ret, dnxErrorString(ret));

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
   DnxNewJob SvcReq;
   int ret = 0;

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);

   dnxSyslog(LOG_INFO, "dnxDispatcher[%lx]: Awaiting new jobs...", pthread_self());

   // wait for new service checks or cancellation
   while (1)
   {
      pthread_testcancel();

      // wait for a new entry to be added to the Job Queue
      if ((ret = dnxJobListDispatch(idisp->joblist, &SvcReq)) == DNX_OK)
      {
         // send the Job to the next Worker Node
         if ((ret = dnxDispatchJob(idisp, &SvcReq)) == DNX_OK)
            dnxAuditJob(&SvcReq, "DISPATCH");
         else
         {
            dnxSyslog(LOG_ERR, 
                  "dnxDispatcher[%lx]: dnxDispatchJob failed with %d: %s", 
                  pthread_self(), ret, dnxErrorString(ret));
            dnxAuditJob(&SvcReq, "DISPATCH-FAIL");
         }
      }
   }
   dnxSyslog(LOG_INFO, "dnxDispatcher[%lx]: Exiting with error %d: %s", 
         pthread_self(), ret, dnxErrorString(ret));
   return 0;
}

//----------------------------------------------------------------------------

/** Return a reference to the dispatcher channel object.
 * 
 * @param[in] disp - the dispatcher whose dispatch channel should be returned.
 * 
 * @return A pointer to the dispatcher channel object.
 */
DnxChannel * dnxDispatcherGetChannel(DnxDispatcher * disp)
      { return ((iDnxDispatcher *)disp)->channel; }

//----------------------------------------------------------------------------

/** Create a new dispatcher object.
 * 
 * @param[in] debug - a pointer to the global debug level.
 * @param[in] chname - the name of the dispatch channel.
 * @param[in] dispurl - the dispatcher channel URL.
 * @param[in] joblist - a pointer to the global job list object.
 * @param[out] pdisp - the address of storage for the return of the new
 *    dispatcher object.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxDispatcherCreate(long * debug, char * chname, char * dispurl,
      DnxJobList * joblist, DnxDispatcher ** pdisp)
{
   iDnxDispatcher * idisp;
   int ret;

   if ((idisp = (iDnxDispatcher *)xmalloc(sizeof *idisp)) == 0)
      return DNX_ERR_MEMORY;

   memset(idisp, 0, sizeof *idisp);
   idisp->chname = xstrdup(chname);
   idisp->url = xstrdup(dispurl);
   idisp->joblist = joblist;
   idisp->debug = debug;

   if (!idisp->url || !idisp->chname)
   {
      xfree(idisp);
      return DNX_ERR_MEMORY;
   }
   if ((ret = dnxChanMapAdd(chname, dispurl)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxDispatcherCreate: dnxChanMapAdd(%s) failed "
                         "with %d: %s", chname, ret, dnxErrorString(ret));
      goto e1;
   }
   if ((ret = dnxConnect(chname, &idisp->channel, DNX_CHAN_PASSIVE)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxDispatcherCreate: dnxConnect(%s) failed "
                         "with %d: %s", chname, ret, dnxErrorString(ret));
      goto e2;
   }

   if (*debug)
      dnxChannelDebug(idisp->channel, *debug);

   // create the dispatcher thread
   if ((ret = pthread_create(&idisp->tid, NULL, dnxDispatcher, idisp)) != 0)
   {
      dnxSyslog(LOG_ERR, "dnxDispatcherCreate: thread creation failed "
                         "with %d: %s", ret, dnxErrorString(ret));
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

/** Destroy an existing dispatcher object.
 * 
 * @param[in] disp - a pointer to the dispatcher object to be destroyed.
 */
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

/*--------------------------------------------------------------------------*/

