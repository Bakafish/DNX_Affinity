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
 * @file dstimer.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#include "dstimer.h"

#include "dsjoblist.h"
#include "dsaudit.h"

#include "dnxError.h"
#include "dnxProtocol.h"
#include "dnxLogging.h"

#include <assert.h>

#include "nagios.h"     /* for circular_buffer */

#define DNX_TIMER_SLEEP    5
#define MAX_EXPIRED        10

static int timer_thread_running;
static pthread_t timer_thread_id;

/** Post an expired service rquest to the Nagios service result buffer.
 * @param[in] pExpire - a new job object to be posted.
 * @return Zero on success, or a non-zero error code.
 */
static int dnxExpireJob (DnxNewJob *pExpire)
{
   extern circular_buffer service_result_buffer;
   extern int check_result_buffer_slots;
   service_message *new_message;

   // Allocate memory for the message
   if ((new_message = (service_message *)malloc(sizeof(service_message))) == NULL)
   {
      dnxSyslog(LOG_ERR, "dnxExpireJob: Memory allocation failure");
      return DNX_ERR_MEMORY;
   }

   // Copy the expired job's data to the message buffer
   gettimeofday(&(new_message->finish_time), NULL);
   strncpy(new_message->host_name, pExpire->svc->host_name, sizeof(new_message->host_name)-1);
   new_message->host_name[sizeof(new_message->host_name)-1] = '\0';
   strncpy(new_message->description, pExpire->svc->description, sizeof(new_message->description)-1);
   new_message->description[sizeof(new_message->description)-1] = '\0';
#ifdef SERVICE_CHECK_TIMEOUTS_RETURN_UNKNOWN
   new_message->return_code = STATE_UNKNOWN;
#else
   new_message->return_code = STATE_CRITICAL;
#endif
   new_message->exited_ok = TRUE;
   new_message->check_type = SERVICE_CHECK_ACTIVE;
   new_message-> parallelized = pExpire->svc->parallelize;
   new_message->start_time.tv_sec = pExpire->start_time;
   new_message->start_time.tv_usec = 0L;
   new_message->early_timeout = TRUE;
   strcpy(new_message->output, "(DNX Service Check Timed Out)");

   // Obtain a lock for writing to the buffer
   pthread_mutex_lock(&service_result_buffer.buffer_lock);

   // Handle overflow conditions
   if (service_result_buffer.items == check_result_buffer_slots)
   {
      // Record overflow
      service_result_buffer.overflow++;

      // Update tail pointer
      service_result_buffer.tail = (service_result_buffer.tail + 1) % check_result_buffer_slots;

      dnxSyslog(LOG_ERR, "dnxExpireJob: Service result buffer overflow = %lu", service_result_buffer.overflow);
   }

   // Save the data to the buffer
   ((service_message **)service_result_buffer.buffer)[service_result_buffer.head] = new_message;

   // Increment the head counter and items
   service_result_buffer.head = (service_result_buffer.head + 1) % check_result_buffer_slots;
   if (service_result_buffer.items < check_result_buffer_slots)
      service_result_buffer.items++;
   if(service_result_buffer.items>service_result_buffer.high)
      service_result_buffer.high=service_result_buffer.items;

   // Release lock on buffer
   pthread_mutex_unlock(&service_result_buffer.buffer_lock);

   return DNX_OK;
}


/** The main entry point for the pending job expiration timeout thread.
 * @param[in/out] data - the job list to be expired over time.
 * @return Zero on success.
 */
static void * dnxTimer(void * data)
{
   DnxJobList * jobList = (DnxJobList *)data;

   assert(data);

   // initialize thread cancellation properties
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);

   dnxSyslog(LOG_INFO, "dnxTimer[%lx]: Watching for expired jobs...", pthread_self());

   // wait for wakeup or cancellation
   while (timer_thread_running)
   {
      DnxNewJob ExpiredList[MAX_EXPIRED];
      int i, totalExpired, ret = 0;

      pthread_testcancel();

      sleep(DNX_TIMER_SLEEP); /* wake up every 5 seconds... */

      // Search for expired jobs in the Pending queue
      totalExpired = MAX_EXPIRED;
      if ((ret = dnxJobListExpire(jobList, ExpiredList, &totalExpired)) == DNX_OK && totalExpired > 0)
      {
         for (i = 0; i < totalExpired; i++)
         {
            dnxDebug(1, "dnxTimer[%lx]: Expiring Job: %s", pthread_self(), ExpiredList[i].cmd);

            dsAuditJob(&ExpiredList[i], "EXPIRE");

            ret |= dnxExpireJob(&ExpiredList[i]);

            dnxJobCleanup(&ExpiredList[i]);
         }
      }

      if (totalExpired > 0 || ret != DNX_OK)
         dnxDebug(1, "dnxTimer[%lx]: Expired job count: %d  Retcode=%d", pthread_self(), totalExpired, ret);
   }

   dnxSyslog(LOG_INFO, "dnxTimer[%lx]: Exiting", pthread_self());

   return 0;
}


/** Initialize the pending job expiration timer thread module.
 * Create the pending job expiration timer thread.
 * @return Zero on success, or a non-zero error code.
 */
int dnxTimerInit(DnxJobList * jobList)
{
   int err;
   timer_thread_running = 1;
   if ((err = pthread_create(&timer_thread_id, 0, dnxTimer, jobList)) != 0)
      dnxSyslog(LOG_ERR, "dnxTimerInit: Failed to create Timer thread: %d", err);
   return err;
}


/** Cleanup the pending job expiration timer thread module.
 */
void dnxTimerExit(void)
{
   if (timer_thread_id)
   {
      timer_thread_running = 0;
      pthread_cancel(timer_thread_id);
      pthread_join(timer_thread_id, 0);
      timer_thread_id = 0;
   }
}

/*-------------------------------------------------------------------------*/

