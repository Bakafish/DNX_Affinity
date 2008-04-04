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

#include <assert.h>

#define DNX_TIMER_SLEEP    5  /*!< Timer sleep interval. */
#define MAX_EXPIRED        10 /*!< Maximum expired jobs during interval. */

/** DNX job expiration timer implementation structure. */
typedef struct iDnxTimer_
{
   DnxJobList * joblist;      /*!< Job list to be expired. */
   pthread_t tid;             /*!< Timer thread ID. */
} iDnxTimer;

//----------------------------------------------------------------------------

/** Post an expired service request to the Nagios service result buffer.
 * 
 * @param[in] svc - the service to be expired.
 * @param[in] start_time - the time the service was originally started.
 * @param[in] msg - the message string to associate with the expired reqeust.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @todo This code should be inside of Nagios. Move it there, export it, and 
 *    add it to the 2.7 and 2.9 dnx patches for Nagios.
 */
static int nagiosExpireJob(service * svc, time_t start_time, char * msg)
{
   extern circular_buffer service_result_buffer;   // type from nagios.h
   extern int check_result_buffer_slots;           // global from nagios

   service_message * new_message;

   if ((new_message = (service_message *)xmalloc(sizeof *new_message)) == NULL)
      return DNX_ERR_MEMORY;

   // Copy the expired job's data to the message buffer
   gettimeofday(&(new_message->finish_time), NULL);
   strncpy(new_message->host_name, svc->host_name, 
         sizeof(new_message->host_name)-1);
   new_message->host_name[sizeof(new_message->host_name)-1] = '\0';
   strncpy(new_message->description, svc->description, 
         sizeof(new_message->description)-1);
   new_message->description[sizeof(new_message->description)-1] = '\0';
   new_message->return_code = STATE_UNKNOWN;
   new_message->exited_ok = TRUE;
   new_message->check_type = SERVICE_CHECK_ACTIVE;
   new_message-> parallelized = svc->parallelize;
   new_message->start_time.tv_sec = start_time;
   new_message->start_time.tv_usec = 0L;
   new_message->early_timeout = TRUE;
   strcpy(new_message->output, msg);

   pthread_mutex_lock(&service_result_buffer.buffer_lock);

   // Handle overflow conditions
   if (service_result_buffer.items == check_result_buffer_slots)
   {
      // Record overflow
      service_result_buffer.overflow++;

      // Update tail pointer
      service_result_buffer.tail = (service_result_buffer.tail + 1) 
            % check_result_buffer_slots;

      dnxSyslog(LOG_ERR, "nagiosExpireJob: Service result buffer overflow = %lu", 
            service_result_buffer.overflow);
   }

   // Save the data to the buffer
   ((service_message **)service_result_buffer.buffer)
         [service_result_buffer.head] = new_message;

   // Increment the head counter and items
   service_result_buffer.head = (service_result_buffer.head + 1) 
         % check_result_buffer_slots;
   if (service_result_buffer.items < check_result_buffer_slots)
      service_result_buffer.items++;
   if(service_result_buffer.items>service_result_buffer.high)
      service_result_buffer.high=service_result_buffer.items;

   pthread_mutex_unlock(&service_result_buffer.buffer_lock);

   return DNX_OK;
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

   while (1)
   {
      sleep(DNX_TIMER_SLEEP);

      // Search for expired jobs in the Pending Queue
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
            ret = nagiosExpireJob(job->svc, job->start_time, msg);

            dnxJobCleanup(job);
         }
      }

      if (totalExpired > 0 || ret != DNX_OK)
         dnxDebug(1, "dnxTimer[%lx]: Expired job count: %d  Retcode=%d", 
               pthread_self(), totalExpired, ret);
   }

   dnxSyslog(LOG_INFO, "dnxTimer[%lx]: Exiting with ret code = %d", 
         pthread_self(), ret);

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
   itimer->tid = 0;

   if ((ret = pthread_create(&itimer->tid, NULL, dnxTimer, itimer)) != 0)
   {
      dnxSyslog(LOG_ERR, "Timer: thread creation failed: %d", ret);
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

   pthread_cancel(itimer->tid);
   pthread_join(itimer->tid, NULL);

   xfree(itimer);
}

/*--------------------------------------------------------------------------*/

