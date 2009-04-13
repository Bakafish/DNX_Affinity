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

/** Implements the DNX Timer thread.
 *
 * The purpose of this thread is to monitor the age of service requests
 * which are being actively executed by the worker nodes.
 * 
 * This requires access to the global Pending queue (which is also
 * manipulated by the Dispatcher and Collector threads.)
 * 
 * @file dnxTimer.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IMPL
 */

#include "dnxTimer.h"

#include "dnxNebMain.h"
#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxProtocol.h"
#include "dnxJobList.h"
#include "dnxLogging.h"
#include "dnxSleep.h"

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "nagios.h"
#include "objects.h"    // for nagios service data type
#include "nebmodules.h"
#include "nebstructs.h"
#include "nebcallbacks.h"
#include "neberrors.h"
#include "broker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <error.h>
#include <assert.h>

#define DNX_DEF_TIMER_SLEEP   5000  /*!< Default timer sleep interval. */
#define MAX_EXPIRED           10    /*!< Maximum expired jobs during interval. */

/** DNX job expiration timer implementation structure. */
typedef struct iDnxTimer_
{
   DnxJobList * joblist;   /*!< Job list to be expired. */
   pthread_t tid;          /*!< Timer thread ID. */
   int sleepms;            /*!< Milliseconds to sleep between passes. */
} iDnxTimer;

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** Timer thread clean-up routine.
 * 
 * @param[in] data - an opaque pointer to thread data for the dying thread.
 * 
 * @note Currently, this cleanup routine does nothing. It's here just in case 
 * the timer code is modified to call cancelable kernel or pthread library 
 * routines in the future.
 */
static void dnxTimerCleanup(void * data)
{
   iDnxTimer * itimer = (iDnxTimer *)data;
   assert(data);
}

//----------------------------------------------------------------------------

/** The main timer thread procedure entry point.
 * 
 * @param[in] data - an opaque pointer to thread data for the timer thread.
 *    This is actually the dnx server global data object.
 * 
 * @return Always returns 0.
 */
static void * dnxTimer(void * data)
{
   iDnxTimer * itimer = (iDnxTimer *)data;
   DnxNewJob ExpiredList[MAX_EXPIRED];
   int i, totalExpired;
   int ret = 0;

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);
   pthread_cleanup_push(dnxTimerCleanup, data);

   dnxLog("dnxTimer[%lx]: Watching for expired jobs...", pthread_self());

   while (1)
   {
      pthread_testcancel();

      dnxCancelableSleep(itimer->sleepms);

      // search for expired jobs in the pending queue
      totalExpired = MAX_EXPIRED;
      if ((ret = dnxJobListExpire(itimer->joblist, ExpiredList, &totalExpired)) == DNX_OK && totalExpired > 0)
      {
         dnxDebug(4, "Expired Checks");
         for (i = 0; i < totalExpired; i++)
         {
            char msg[128];
            DnxNewJob * job = &ExpiredList[i];

            dnxDebug(1, "dnxTimer[%lx]: Expiring Job [%lu,%lu]: %s.",pthread_self(), job->xid.objSerial, job->xid.objSlot, job->cmd);

            dnxAuditJob(job, "EXPIRE");
            char * addr = ntop(job->pNode->address);

            if(job->ack)
            {
                sprintf(msg, "(DNX: Service Check [%lu,%lu] Timed Out - Node: %s - Failed to return job response in time allowed)",job->xid.objSerial, job->xid.objSlot, addr);

            }else{
                sprintf(msg, "(DNX: Service Check [%lu,%lu] Timed Out - Node: %s - Failed to acknowledge job reciept)",job->xid.objSerial, job->xid.objSlot, addr);
            }

            dnxDebug(2,msg);

            xfree(addr);

            // report the expired job to Nagios
            char * svc_description;
            char * host_name;
            nebstruct_service_check_data * srv;
            nebstruct_host_check_data * hst;
            int result_code;

            if(job->object_check_type == 0) {
                // It's a Service check
                dnxDebug(4, "Expired Service Check");
                srv = (nebstruct_service_check_data *)job->check_data;
                svc_description = xstrdup(srv->service_description);
                host_name = xstrdup(srv->host_name);
                result_code = 3;
            } else {
                dnxDebug(4, "Expired Host Check");
                hst = (nebstruct_host_check_data *)job->check_data;
                host_name = xstrdup(hst->host_name);
                result_code = 2;
            }

            time_t check_time = job->start_time;
            ret = dnxSubmitCheck(host_name, svc_description, result_code, msg, check_time);

            dnxJobCleanup(job);
         }
      }

      if (totalExpired > 0 || ret != DNX_OK)
         dnxDebug(2, "dnxTimer[%lx]: Expired job count: %d  Retcode=%d: %s.",pthread_self(), totalExpired, ret, dnxErrorString(ret));
   }

   dnxLog("dnxTimer[%lx]: Terminating: %s.", pthread_self(), dnxErrorString(ret));

   pthread_cleanup_pop(1);
   return 0;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

int dnxTimerCreate(DnxJobList * joblist, int sleeptime, DnxTimer ** ptimer)
{
   iDnxTimer * itimer;
   int ret;

   assert(joblist && ptimer);

   // don't allow sleep times outside the range 1/10th sec to 5 minutes
   if (sleeptime < 100 || sleeptime > 300000)
      sleeptime = DNX_DEF_TIMER_SLEEP;

   if ((itimer = (iDnxTimer *)xmalloc(sizeof *itimer)) == 0)
      return DNX_ERR_MEMORY;

   // initialize the itimer
   memset(itimer, 0, sizeof *itimer);
   itimer->joblist = joblist;
   itimer->sleepms = sleeptime;

   // create the timer thread
   if ((ret = pthread_create(&itimer->tid, 0, dnxTimer, itimer)) != 0)
   {
      dnxLog("Timer thread creation failed: %s.", dnxErrorString(ret));
      xfree(itimer);
      return DNX_ERR_THREAD;
   }

   *ptimer = (DnxTimer *)itimer;

   return DNX_OK;
}

//----------------------------------------------------------------------------

void dnxTimerDestroy(DnxTimer * timer)
{
   iDnxTimer * itimer = (iDnxTimer *)timer;

   pthread_cancel(itimer->tid);
   pthread_join(itimer->tid, 0);

   xfree(itimer);
}

/*--------------------------------------------------------------------------
                                 UNIT TEST

   From within dnx/server, compile with GNU tools using this command line:
    
      gcc -DDEBUG -DDNX_TIMER_TEST -DHAVE_NANOSLEEP -g -O0 -I../common \
         -I../nagios/nagios-2.7/include dnxTimer.c ../common/dnxSleep.c \
         ../common/dnxError.c -lpthread -lgcc_s -lrt -o dnxTimerTest

   Note: Leave out -DHAVE_NANOSLEEP if your system doesn't have nanosleep.

  --------------------------------------------------------------------------*/

#ifdef DNX_TIMER_TEST
// 
// #include "utesthelp.h"
// #include <time.h>
// 
// #define elemcount(x) (sizeof(x)/sizeof(*(x)))
// 
// static int verbose;
// static DnxNewJob fakejob;
// static DnxJobList fakejoblist;
// static int fakepayload;
// static DnxNodeRequest fakenode;
// static int entered_dnxJobListExpire;
// 
// // functional stubs
// IMPLEMENT_DNX_SYSLOG(verbose);
// IMPLEMENT_DNX_DEBUG(verbose);
// 
// int dnxJobListExpire(DnxJobList * pJobList, DnxNewJob * pExpiredJobs, int * totalJobs)
// {
//    CHECK_TRUE(pJobList == &fakejoblist);
//    CHECK_TRUE(*totalJobs > 0);
//    memcpy(&pExpiredJobs[0], &fakejob, sizeof *pExpiredJobs);
//    *totalJobs = 1;
//    entered_dnxJobListExpire = 1;
//    return 0;
// }
// int dnxPostResult(void * payload, time_t start_time, unsigned delta, 
//       int early_timeout, int res_code, char * res_data)
// {
//    CHECK_TRUE(payload == &fakepayload);
//    CHECK_TRUE(start_time == 100);
//    CHECK_TRUE(early_timeout != 0);
//    CHECK_TRUE(res_code == 0);
//    CHECK_ZERO(memcmp(res_data, "(DNX Service", 12));
//    return 0;
// }
// int dnxAuditJob(DnxNewJob * pJob, char * action) { return 0; }
// void dnxJobCleanup(DnxNewJob * pJob) { }
// 
// int main(int argc, char ** argv)
// {
//    DnxTimer * timer;
//    iDnxTimer * itimer;
// 
//    verbose = argc > 1? 1: 0;
// 
//    // setup test harness
//    fakenode.xid.objType    = DNX_OBJ_JOB;
//    fakenode.xid.objSerial  = 1;
//    fakenode.xid.objSlot    = 2;
//    fakenode.reqType        = DNX_REQ_DEREGISTER;
//    fakenode.jobCap         = 1;
//    fakenode.ttl            = 2;
//    fakenode.expires        = 3;
//    strcpy(fakenode.address, "fake address");
// 
//    fakejob.state           = DNX_JOB_INPROGRESS;
//    fakejob.xid.objType     = DNX_OBJ_JOB;
//    fakejob.xid.objSerial   = 1;
//    fakejob.xid.objSlot     = 2;
//    fakejob.cmd             = "fake command line";
//    fakejob.start_time      = 100;
//    fakejob.timeout         = 10;
//    fakejob.expires         = fakejob.start_time + fakejob.timeout;
//    fakejob.payload         = &fakepayload;
//    fakejob.pNode           = &fakenode;
// 
//    entered_dnxJobListExpire = 0;
// 
//    // create a short timer and reference it as a concrete object for testing
//    CHECK_ZERO(dnxTimerCreate(&fakejoblist, 100, &timer));
//    itimer = (iDnxTimer *)timer;
// 
//    // check internal state
//    CHECK_TRUE(itimer->joblist == &fakejoblist);
//    CHECK_TRUE(itimer->tid != 0);
//    CHECK_TRUE(itimer->sleepms == 100);
// 
//    // wait for timer to have made one pass though timer thread loop
//    while (!entered_dnxJobListExpire)
//       dnxCancelableSleep(10);
// 
//    // shut down
//    dnxTimerDestroy(timer);
// 
//    return 0;
// }

#endif   /* DNX_TIMER_TEST */

/*--------------------------------------------------------------------------*/

