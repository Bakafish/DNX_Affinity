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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <error.h>
#include <assert.h>

#define DNX_DEF_TIMER_SLEEP   5  /*!< Default timer sleep interval. */
#define MAX_EXPIRED           10 /*!< Maximum expired jobs during interval. */

/** DNX job expiration timer implementation structure. */
typedef struct iDnxTimer_
{
   DnxJobList * joblist;   /*!< Job list to be expired. */
   pthread_t tid;          /*!< Timer thread ID. */
   int sleeptime;          /*!< Seconds to sleep between passes. */
   int running;            /*!< Running flag - as opposed to cancellation. */
} iDnxTimer;

//----------------------------------------------------------------------------

/** A sleep routine that can be cancelled.
 * 
 * The pthreads specification indicates clearly that the sleep() system call
 * MUST be a cancellation point. However, it appears that sleep() on Linux 
 * calls a routine named _nanosleep_nocancel, which clearly is not a 
 * cancellation point. Oversight? Not even Google appears to know. It seems
 * that most Unix/Linux distros implement sleep in terms of SIGALRM. This
 * is the problem point for creating a cancelable form of sleep().
 *
 * @param[in] sleep - the number of seconds to sleep.
 */
static void dnxCancelableSleep(int seconds)
{
#if HAVE_NANOSLEEP
   struct timespec rqt;
   rqt.tv_sec = seconds;
   rqt.tv_nsec = 0L;
   while (nanosleep(&rqt, &rqt) == -1 && errno == EINTR)
      ;
#else
   pthread_mutex_t timerMutex = PTHREAD_MUTEX_INITIALIZER;
   pthread_cond_t timerCond  = PTHREAD_COND_INITIALIZER;
   struct timeval now;              // time when we started waiting
   struct timespec timeout;         // timeout value for the wait function

   pthread_mutex_lock(&timerMutex);

   gettimeofday(&now, 0);

   // timeval uses micro-seconds; timespec uses nano-seconds; 1us == 1000ns.
   timeout.tv_sec = now.tv_sec + seconds;
   timeout.tv_nsec = now.tv_usec * 1000;

   pthread_cond_timedwait(&timerCond, &timerMutex, &timeout);

   pthread_mutex_unlock(&timerMutex);
#endif
}

//----------------------------------------------------------------------------

/** Timer thread clean-up routine.
 * 
 * @param[in] data - an opaque pointer to thread data for the dying thread.
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

   dnxSyslog(LOG_INFO, "dnxTimer[%lx]: Watching for expired jobs...", 
         pthread_self());

   while (itimer->running)
   {
      dnxCancelableSleep(itimer->sleeptime);

      // search for expired jobs in the pending queue
      totalExpired = MAX_EXPIRED;
      if ((ret = dnxJobListExpire(itimer->joblist, ExpiredList, 
            &totalExpired)) == DNX_OK && totalExpired > 0)
      {
         for (i = 0; i < totalExpired; i++)
         {
            char msg[128];
            DnxNewJob * job = &ExpiredList[i];

            dnxDebug(1, "dnxTimer[%lx]: Expiring Job: %s", 
                  pthread_self(), job->cmd);

            dnxAuditJob(job, "EXPIRE");

            sprintf(msg, "(DNX Service Check Timed Out - Node: %s)", 
                  job->pNode->address);

            // report the expired job to Nagios
            ret = nagiosPostResult((service *)job->payload, job->start_time, 
                  TRUE, STATE_UNKNOWN, msg);

            dnxJobCleanup(job);
         }
      }

      if (totalExpired > 0 || ret != DNX_OK)
         dnxDebug(1, "dnxTimer[%lx]: Expired job count: %d  Retcode=%d: %s", 
               pthread_self(), totalExpired, ret, dnxErrorString(ret));
   }

   dnxSyslog(LOG_INFO, "dnxTimer[%lx]: Exiting with ret code %d: %s", 
         pthread_self(), ret, dnxErrorString(ret));

   pthread_cleanup_pop(1);
   return 0;
}

//----------------------------------------------------------------------------

/** Create a new job list expiration timer object.
 * 
 * @param[in] joblist - the job list that should be expired by the timer.
 * @param[in] sleeptime - time between expiration checks, in seconds.
 * @param[out] ptimer - the address of storage for returning the new object
 *    reference.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxTimerCreate(DnxJobList * joblist, int sleeptime, DnxTimer ** ptimer)
{
   iDnxTimer * itimer;
   int ret;

   assert(joblist && ptimer);

   // don't allow sleep times outside the range 1 sec to 5 minutes
   if (sleeptime < 1 || sleeptime > 300)
      sleeptime = DNX_DEF_TIMER_SLEEP;

   if ((itimer = (iDnxTimer *)xmalloc(sizeof *itimer)) == 0)
      return DNX_ERR_MEMORY;

   itimer->joblist = joblist;
   itimer->sleeptime = sleeptime;
   itimer->running = 1;

   if ((ret = pthread_create(&itimer->tid, 0, dnxTimer, itimer)) != 0)
   {
      dnxSyslog(LOG_ERR, "Timer: thread creation failed with %d: %s", 
            ret, dnxErrorString(ret));
      xfree(itimer);
      return DNX_ERR_THREAD;
   }

   *ptimer = (DnxTimer *)itimer;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Destroy an existing job list expiration timer object.
 * 
 * @param[in] timer - the timer object to be destroyed.
 */
void dnxTimerDestroy(DnxTimer * timer)
{
   iDnxTimer * itimer = (iDnxTimer *)timer;

   itimer->running = 0;
// pthread_cancel(itimer->tid);
   pthread_join(itimer->tid, 0);

   xfree(itimer);
}

/*--------------------------------------------------------------------------
                                 UNIT TEST

   From within dnx/server, compile with GNU tools using this command line:
    
      gcc -DDEBUG -DDNX_TIMER_TEST -DHAVE_NANOSLEEP -g -O0 -I../common \
         -I../nagios/nagios-2.7/include dnxTimer.c \
         ../common/dnxError.c -lpthread -lgcc_s -lrt -o dnxTimerTest

   Note: Leave out -DHAVE_NANOSLEEP if your system doesn't have nanosleep.

  --------------------------------------------------------------------------*/

#ifdef DNX_TIMER_TEST

#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

/* test-bed helper macros */
#define CHECK_ZERO(expr)                                                      \
do {                                                                          \
   int ret;                                                                   \
   if ((ret = (expr)) != 0)                                                   \
   {                                                                          \
      fprintf(stderr, "FAILED: '%s'\n  at %s(%d).\n  error %d: %s\n",         \
            #expr, __FILE__, __LINE__, ret, dnxErrorString(ret));             \
      exit(1);                                                                \
   }                                                                          \
} while (0)
#define CHECK_TRUE(expr)                                                      \
do {                                                                          \
   if (!(expr))                                                               \
   {                                                                          \
      fprintf(stderr, "FAILED: Boolean(%s)\n  at %s(%d).\n",                  \
            #expr, __FILE__, __LINE__);                                       \
      exit(1);                                                                \
   }                                                                          \
} while (0)
#define CHECK_NONZERO(expr)   CHECK_ZERO(!(expr))
#define CHECK_FALSE(expr)     CHECK_TRUE(expr)

/* test static globals */
static int verbose;
static DnxNewJob fakejob;
static DnxJobList fakejoblist;
static int fakepayload;
static DnxNodeRequest fakenode;
static int entered_dnxJobListExpire;

/* functional stubs */
int dnxJobListExpire(DnxJobList * pJobList, DnxNewJob * pExpiredJobs, int * totalJobs)
{
   CHECK_TRUE(pJobList == &fakejoblist);
   CHECK_TRUE(*totalJobs > 0);
   memcpy(&pExpiredJobs[0], &fakejob, sizeof *pExpiredJobs);
   *totalJobs = 1;
   entered_dnxJobListExpire = 1;
   return 0;
}
int nagiosPostResult(service * svc, time_t start_time, int early_timeout, 
      int res_code, char * res_data)
{
   CHECK_TRUE(svc == (service *)&fakepayload);
   CHECK_TRUE(start_time == 100);
   CHECK_TRUE(early_timeout != 0);
   CHECK_TRUE(res_code == STATE_UNKNOWN);
   CHECK_ZERO(memcmp(res_data, "(DNX Service", 12));
   return 0;
}
int dnxSyslog(int p, char * f, ... )
{
   if (verbose) { va_list a; va_start(a,f); vprintf(f,a); va_end(a); puts(""); }
   return 0;
}
int dnxDebug(int l, char * f, ... )
{
   if (verbose) { va_list a; va_start(a,f); vprintf(f,a); va_end(a); puts(""); }
   return 0;
}
int dnxAuditJob(DnxNewJob * pJob, char * action) { return 0; }
int dnxJobCleanup(DnxNewJob * pJob) { return 0; }

/* test main */
int main(int argc, char ** argv)
{
   DnxTimer * timer;
   iDnxTimer * itimer;

   verbose = argc > 1? 1: 0;

   // setup test harness
   fakenode.guid.objType   = DNX_OBJ_JOB;
   fakenode.guid.objSerial = 1;
   fakenode.guid.objSlot   = 2;
   fakenode.reqType        = DNX_REQ_DEREGISTER;
   fakenode.jobCap         = 1;
   fakenode.ttl            = 2;
   fakenode.expires        = 3;
   strcpy(fakenode.address, "fake address");

   fakejob.state           = DNX_JOB_INPROGRESS;
   fakejob.guid.objType    = DNX_OBJ_JOB;
   fakejob.guid.objSerial  = 1;
   fakejob.guid.objSlot    = 2;
   fakejob.cmd             = "fake command line";
   fakejob.start_time      = 100;
   fakejob.timeout         = 10;
   fakejob.expires         = fakejob.start_time + fakejob.timeout;
   fakejob.payload         = &fakepayload;
   fakejob.pNode           = &fakenode;

   entered_dnxJobListExpire = 0;

   // create a short timer and reference it as a concrete object for testing
   CHECK_ZERO(dnxTimerCreate(&fakejoblist, 1, &timer));
   itimer = (iDnxTimer *)timer;

   // check internal state
   CHECK_TRUE(itimer->joblist == &fakejoblist);
   CHECK_TRUE(itimer->tid != 0);
   CHECK_TRUE(itimer->sleeptime == 1);
   CHECK_TRUE(itimer->running != 0);

   // wait for timer to have made one pass though timer thread loop
   while (!entered_dnxJobListExpire)
      dnxCancelableSleep(1);

   // shut down
   dnxTimerDestroy(timer);
}

#endif   /* DNX_TIMER_TEST */

/*--------------------------------------------------------------------------*/

