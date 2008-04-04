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

#include "nagios.h"

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

#define DNX_TIMER_SLEEP 5  /*!< Timer sleep interval. */
#define MAX_EXPIRED     10 /*!< Maximum expired jobs during interval. */

/** DNX job expiration timer implementation structure. */
typedef struct iDnxTimer_
{
   DnxJobList * joblist;   /*!< Job list to be expired. */
   pthread_t tid;          /*!< Timer thread ID. */
   int running;            /*!< Running flag - as opposed to cancellation. */
} iDnxTimer;

//----------------------------------------------------------------------------

/** A sleep routine that can be cancelled.
 * 
 * The pthreads specification indicates clearly that the sleep() system call
 * MUST be a cancellation point. However, it appears that sleep() on Linux 
 * calls a routine named _nanosleep_nocancel, which clearly is not a 
 * cancellation point. Oversight? Not even Google appears to know...
 *
 * @param[in] sleep - the number of seconds to sleep.
 */
static void dnxCancelableSleep(int seconds)
{
#if HAVE_NANOSLEEP
   struct timespec rqt = {seconds, 0};
   while (nanosleep(&rqt, &rqt) != 0 && errno == EINTR)
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
 * @return Always returns NULL.
 */
static void * dnxTimer(void * data)
{
   iDnxTimer * itimer = (iDnxTimer *)data;
   DnxNewJob ExpiredList[MAX_EXPIRED];
   int i, totalExpired;
   int ret = 0;

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
   pthread_cleanup_push(dnxTimerCleanup, data);

   dnxSyslog(LOG_INFO, "dnxTimer[%lx]: Watching for expired jobs...", 
         pthread_self());

   while (itimer->running)
   {
      dnxCancelableSleep(DNX_TIMER_SLEEP);

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
            ret = nagiosPostResult(job->svc, job->start_time, TRUE, 
                  STATE_UNKNOWN, msg);

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
 * @param[out] ptimer - the address of storage for returning the new object
 *    reference.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxTimerCreate(DnxJobList * joblist, DnxTimer ** ptimer)
{
   iDnxTimer * itimer;
   int ret;

   assert(ptimer);

   if ((itimer = (iDnxTimer *)xmalloc(sizeof *itimer)) == 0)
      return DNX_ERR_MEMORY;

   itimer->joblist = joblist;
   itimer->running = 1;

   if ((ret = pthread_create(&itimer->tid, NULL, dnxTimer, itimer)) != 0)
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
   pthread_join(itimer->tid, NULL);

   xfree(itimer);
}

/*--------------------------------------------------------------------------*/

