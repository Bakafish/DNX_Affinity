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

/** Implements the DNX Collector thread.
 *
 * The purpose of this thread is to collect service check
 * completion results from the worker nodes.  When a service
 * check result is collected, this thread dequeues the service
 * check from the Jobs queue and posts the result to the existing
 * Nagios service_result_buffer.
 * 
 * @file dnxCollector.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IMPL
 */

#include "dnxCollector.h"

#include "dnxNebMain.h"
#include "dnxDebug.h"
#include "dnxError.h"
#include "dnxQueue.h"
#include "dnxProtocol.h"
#include "dnxJobList.h"
#include "dnxLogging.h"

#include <stdlib.h>
#include <assert.h>

#define DNX_COLLECTOR_TIMEOUT 30

/** The implementation data structure for a collector object. */
typedef struct iDnxCollector_
{
   char * chname;          /*!< The collector channel name. */
   char * url;             /*!< The collector channel URL. */
   DnxJobList * joblist;   /*!< The job list we're collecting for. */
   DnxChannel * channel;   /*!< Collector communications channel. */
   pthread_t tid;          /*!< The collector thread id. */
} iDnxCollector;

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** The collector thread main entry point procedure.
 * 
 * @param[in] data - an opaque pointer to the collector thread data structure,
 *    which is actually a DnxGlobalData object (the dnxServer global data 
 *    structure).
 * 
 * @return Always returns NULL.
 */
static void * dnxCollector(void * data)
{
   iDnxCollector * icoll = (iDnxCollector *)data;
   pthread_t tid = pthread_self();
   DnxResult sResult;
   DnxNewJob Job;
   int ret;

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);

   dnxSyslog(LOG_INFO, "dnxCollector[%lx]: Awaiting service check results", tid);

   while (1)
   {
      pthread_testcancel();

      if ((ret = dnxWaitForResult(icoll->channel, 
            &sResult, sResult.address, DNX_COLLECTOR_TIMEOUT)) == DNX_OK)
      {
         dnxDebug(1, "dnxCollector[%lx]: Received result for [%lu,%lu]: %s", 
               tid, sResult.xid.objSerial, sResult.xid.objSlot, sResult.resData);

         // dequeue the matching service request from the pending job queue
         if ((ret = dnxJobListCollect(icoll->joblist, &sResult.xid, &Job)) == DNX_OK)
         {
            ret = nagiosPostResult((service *)Job.payload, Job.start_time, 
                  FALSE, sResult.resCode, sResult.resData);

            /** @todo Wrapper release DnxResult structure. */
            xfree(sResult.resData);

            dnxDebug(1, "dnxCollector[%lx]: Post result [%lu-%lu]: %s", 
                  tid, sResult.xid.objSerial, sResult.xid.objSlot, 
                  dnxErrorString(ret));

            dnxAuditJob(&Job, "COLLECT");

            dnxJobCleanup(&Job);
         }
         else
            dnxSyslog(LOG_WARNING, "dnxCollector[%lx]: Dequeue job failed: %s",
                  tid, dnxErrorString(ret));
      }
      else if (ret != DNX_ERR_TIMEOUT)
         dnxSyslog(LOG_ERR, "dnxCollector[%lx]: Receive failed: %s", 
               tid, dnxErrorString(ret));
   }
   return 0;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

DnxChannel * dnxCollectorGetChannel(DnxCollector * coll)
      { return ((iDnxCollector *)coll)->channel; }

//----------------------------------------------------------------------------

int dnxCollectorCreate(char * chname, 
      char * collurl, DnxJobList * joblist, DnxCollector ** pcoll)
{
   iDnxCollector * icoll;
   int ret;

   if ((icoll = (iDnxCollector *)xmalloc(sizeof *icoll)) == 0)
      return DNX_ERR_MEMORY;

   memset(icoll, 0, sizeof *icoll);
   icoll->chname = xstrdup(chname);
   icoll->url = xstrdup(collurl);
   icoll->joblist = joblist;

   if (!icoll->url || !icoll->chname)
   {
      xfree(icoll);
      return DNX_ERR_MEMORY;
   }
   if ((ret = dnxChanMapAdd(chname, collurl)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxCollectorCreate: dnxChanMapAdd(%s) failed: %s", 
            chname, dnxErrorString(ret));
      goto e1;
   }
   if ((ret = dnxConnect(chname, &icoll->channel, DNX_CHAN_PASSIVE)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxCollectorCreate: dnxConnect(%s) failed: %s", 
            chname, dnxErrorString(ret));
      goto e2;
   }

   // create the collector thread
   if ((ret = pthread_create(&icoll->tid, 0, dnxCollector, icoll)) != 0)
   {
      dnxSyslog(LOG_ERR, "dnxCollectorCreate: thread creation failed: %s", 
            dnxErrorString(ret));
      ret = DNX_ERR_THREAD;
      goto e3;
   }

   *pcoll = (DnxCollector *)icoll;

   return DNX_OK;

// error paths

e3:dnxDisconnect(icoll->channel);
e2:dnxChanMapDelete(icoll->chname);
e1:xfree(icoll->url);
   xfree(icoll->chname);
   xfree(icoll);

   return ret;
}

//----------------------------------------------------------------------------

void dnxCollectorDestroy(DnxCollector  * coll)
{
   iDnxCollector * icoll = (iDnxCollector *)coll;

   pthread_cancel(icoll->tid);
   pthread_join(icoll->tid, 0);

   dnxDisconnect(icoll->channel);
   dnxChanMapDelete(icoll->chname);

   xfree(icoll->url);
   xfree(icoll->chname);
   xfree(icoll);
}

/*--------------------------------------------------------------------------
                                 TEST MAIN

   From within dnx/server, compile with GNU tools using this command line:
    
      gcc -DDEBUG -DDNX_COLLECTOR_TEST -DHAVE_NANOSLEEP -g -O0 \
         -lpthread -o dnxCollectorTest -I../nagios/nagios-2.7/include \
         -I../common dnxCollector.c ../common/dnxError.c \
         ../common/dnxSleep.c

   Alternatively, a heap check may be done with the following command line:

      gcc -DDEBUG -DDEBUG_HEAP -DDNX_COLLECTOR_TEST -DHAVE_NANOSLEEP -g -O0 \
         -lpthread -o dnxCollectorTest -I../nagios/nagios-2.7/include \
         -I../common dnxCollector.c ../common/dnxError.c \
         ../common/dnxSleep.c ../common/dnxHeap.c

   Note: Leave out -DHAVE_NANOSLEEP if your system doesn't have nanosleep.

  --------------------------------------------------------------------------*/

#ifdef DNX_COLLECTOR_TEST

#include "utesthelp.h"

static int verbose;
static int once = 0;
static char * test_url = "udp://0.0.0.0:12489";
static char * test_chname = "TestCollector";
static char * test_cmd = "test command";
static DnxChannel * test_channel = (DnxChannel *)1;
static DnxJobList * test_joblist = (DnxJobList *)1;
static DnxResult test_result;
static int test_payload;

// test stubs
IMPLEMENT_DNX_DEBUG(verbose);
IMPLEMENT_DNX_SYSLOG(verbose);

int dnxChanMapAdd(char * name, char * url) 
{
   CHECK_TRUE(name != 0);
   CHECK_TRUE(strcmp(name, test_chname) == 0);
   CHECK_TRUE(url != 0);
   CHECK_TRUE(strcmp(url, test_url) == 0);
   return 0;
}

int dnxConnect(char * name, DnxChannel ** channel, DnxChanMode mode) 
{
   *channel = test_channel;
   CHECK_TRUE(name != 0);
   CHECK_TRUE(strcmp(name, test_chname) == 0);
   CHECK_TRUE(mode == DNX_CHAN_PASSIVE);
   return 0;
}

int dnxWaitForResult(DnxChannel * channel, DnxResult * pResult, char * address, int timeout) 
{
   CHECK_TRUE(pResult != 0);

   memset(pResult, 1, sizeof *pResult);
   pResult->resData = 0;

   CHECK_TRUE(channel == test_channel);
   CHECK_TRUE(timeout == DNX_COLLECTOR_TIMEOUT);

   once++;     // stop the test after first pass

   return 0; 
}

int dnxJobListCollect(DnxJobList * pJobList, DnxXID * pxid, DnxNewJob * pJob)
{
   CHECK_TRUE(pJob != 0);
   CHECK_TRUE(pxid != 0);
   CHECK_TRUE(pJobList == test_joblist);

   memset(pJob, 0, sizeof *pJob);
   memcpy(&pJob->xid, pxid, sizeof pJob->xid);
   pJob->state = DNX_JOB_COMPLETE;
   pJob->cmd = test_cmd;
   pJob->payload = &test_payload;

   return 0; 
}

int nagiosPostResult(service * svc, time_t start_time, int early_timeout, 
      int res_code, char * res_data)
{
   CHECK_TRUE(svc == (service *)&test_payload);
   return 0;
}

int dnxAuditJob(DnxNewJob * pJob, char * action)
{
   CHECK_TRUE(pJob != 0);
   CHECK_TRUE(action != 0);
   return 0;
}

void dnxJobCleanup(DnxNewJob * pJob) { CHECK_TRUE(pJob != 0); }

int dnxDisconnect(DnxChannel * channel) 
{
   CHECK_TRUE(channel == test_channel);
   return 0;
}

int dnxChanMapDelete(char * name) 
{
   CHECK_TRUE(name != 0);
   CHECK_TRUE(strcmp(name, test_chname) == 0);
   return 0; 
}

int main(int argc, char ** argv)
{
   DnxCollector * cp;
   iDnxCollector * icp;

   verbose = argc > 1 ? 1 : 0;

   memset(&test_result, 0, sizeof test_result);

   test_result.xid.objSerial = 1;
   test_result.xid.objSlot = 1;
   test_result.xid.objType = DNX_OBJ_COLLECTOR;
   test_result.state = DNX_JOB_INPROGRESS;
   test_result.delta = 1;
   test_result.resCode = 1;

   CHECK_ZERO(dnxCollectorCreate(test_chname, test_url, test_joblist, &cp));

   icp = (iDnxCollector *)cp;

   CHECK_TRUE(strcmp(icp->chname, test_chname) == 0);
   CHECK_TRUE(icp->joblist == test_joblist);
   CHECK_TRUE(icp->tid != 0);
   CHECK_TRUE(strcmp(icp->url, test_url) == 0);

   CHECK_TRUE(dnxCollectorGetChannel(cp) == icp->channel);

   while (!once)
      dnxCancelableSleep(10);

   dnxCollectorDestroy(cp);

#ifdef DEBUG_HEAP
   CHECK_ZERO(dnxCheckHeap());
#endif

   return 0;
}

#endif   /* DNX_COLLECTOR_TEST */

/*--------------------------------------------------------------------------*/

