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

#include <assert.h>

#define DNX_COLLECTOR_TIMEOUT 30

/** The implementation data structure for a collector object. */
typedef struct iDnxCollector_
{
   long * debug;           /*!< A pointer to the global debug level. */
   char * chname;          /*!< The collector channel name. */
   char * url;             /*!< The collector channel URL. */
   DnxJobList * joblist;   /*!< The job list we're collecting for. */
   dnxChannel * channel;   /*!< Collector communications channel. */
   pthread_t tid;          /*!< The collector thread id. */
} iDnxCollector;

//----------------------------------------------------------------------------

/** Post a completed service request to the Nagios service result buffer.
 * 
 * @param[in] icoll - the collector object.
 * @param[in] svc - the nagios service object.
 * @param[in] start_time - the nagios start time.
 * @param[in] res_code - the result code of this job.
 * @param[in] res_data - the resulting STDOUT output text of this job.
 * 
 * @return Zero on success, or a non-zero error code.
 * 
 * @todo This routine should be in nagios code. Add it to the dnx patch files
 * for nagios 2.7 and 2.9, and export it from nagios so we can call it.
 */
static int nagiosPostResult(iDnxCollector * icoll, service * svc, 
      time_t start_time, int res_code, char * res_data)
{
   extern circular_buffer service_result_buffer;
   extern int check_result_buffer_slots;

   service_message * new_message;

   if ((new_message = (service_message *)malloc(sizeof(service_message))) == NULL)
   {
      dnxSyslog(LOG_ERR, "dnxCollector[%lx]: nagiosPostResult: "
                         "Memory allocation failure", pthread_self());
      return DNX_ERR_MEMORY;
   }

   gettimeofday(&new_message->finish_time, NULL);
   strncpy(new_message->host_name, svc->host_name, 
         sizeof(new_message->host_name) - 1);
   new_message->host_name[sizeof(new_message->host_name) - 1] = 0;
   strncpy(new_message->description, svc->description, 
         sizeof(new_message->description) - 1);
   new_message->description[sizeof(new_message->description) - 1] = 0;
   new_message->return_code = res_code;
   new_message->exited_ok = TRUE;
   new_message->check_type = SERVICE_CHECK_ACTIVE;
   new_message-> parallelized = svc->parallelize;
   new_message->start_time.tv_sec = start_time;
   new_message->start_time.tv_usec = 0L;
   new_message->early_timeout = FALSE;
   strncpy(new_message->output, res_data, sizeof(new_message->output) - 1);
   new_message->output[sizeof(new_message->output) - 1] = 0;

   pthread_mutex_lock(&service_result_buffer.buffer_lock);

   // handle overflow conditions
   if (service_result_buffer.items == check_result_buffer_slots)
   {
      service_result_buffer.overflow++;

      // update tail pointer
      service_result_buffer.tail = (service_result_buffer.tail + 1) 
            % check_result_buffer_slots;

      dnxSyslog(LOG_ERR, "dnxCollector[%lx]: nagiosPostResult: "
                         "Service result buffer overflow = %lu", 
            pthread_self(), service_result_buffer.overflow);
   }

   // Save the data to the buffer
   ((service_message **)service_result_buffer.buffer)
         [service_result_buffer.head] = new_message;

   // Increment the head counter and items
   service_result_buffer.head = (service_result_buffer.head + 1) 
         % check_result_buffer_slots;
   if (service_result_buffer.items < check_result_buffer_slots)
      service_result_buffer.items++;
   if (service_result_buffer.items > service_result_buffer.high)
      service_result_buffer.high = service_result_buffer.items;

   pthread_mutex_unlock(&service_result_buffer.buffer_lock);

   return 0;
}

//----------------------------------------------------------------------------

/** Collector thread clean-up routine.
 * 
 * @param[in] data - an opaque pointer to the thread data to be cleaned up.
 */
static void dnxCollectorCleanup(void * data)
{
   iDnxCollector * icoll = (iDnxCollector *)data;

   assert(data);
}

//----------------------------------------------------------------------------

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
   DnxResult sResult;
   DnxNewJob Job;
   int ret;

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
   pthread_cleanup_push(dnxCollectorCleanup, data);

   dnxSyslog(LOG_INFO, "dnxCollector[%lx]: Awaiting service check results", 
         pthread_self());

   while (1)
   {
      pthread_testcancel();

      // wait for worker node results to show up...
      if ((ret = dnxGetResult(icoll->channel, &sResult, sResult.address, 
            DNX_COLLECTOR_TIMEOUT)) == DNX_OK)
      {
         dnxDebug(1, "dnxCollector[%lx]: Received result for [%lu,%lu]: %s", 
               pthread_self(), sResult.guid.objSerial, sResult.guid.objSlot, 
               sResult.resData);

         // dequeue the matching service request from the pending job queue
         if (dnxJobListCollect(icoll->joblist, &sResult.guid, &Job) == DNX_OK)
         {
            // post the results to the Nagios service request buffer
            ret = nagiosPostResult(icoll, Job.svc, Job.start_time, 
                  sResult.resCode, sResult.resData);

            // free result output memory (now that it's been copied into new_message)
            /** @todo Wrapper release DnxResult structure. */
            free(sResult.resData);

            dnxDebug(1, "dnxCollector[%lx]: Posted result [%lu,%lu]: %d", 
                  pthread_self(), sResult.guid.objSerial, 
                  sResult.guid.objSlot, ret);

            dnxAuditJob(&Job, "COLLECT");

            dnxJobCleanup(&Job);
         }
         else
            dnxSyslog(LOG_WARNING, "dnxCollector[%lx]: Unable to dequeue "
                                   "completed job: %d", pthread_self(), ret);
      }
      else if (ret != DNX_ERR_TIMEOUT)
         dnxSyslog(LOG_ERR, "dnxCollector[%lx]: Failure to read result "
                            "message from Collector channel: %d", 
               pthread_self(), ret);
   }

   pthread_cleanup_pop(1);
   return 0;
}

//----------------------------------------------------------------------------

/** Create a new collector object.
 * 
 * @param[in] debug - a pointer to the global debug level.
 * @param[in] chname - the name of the collect channel.
 * @param[in] dispurl - the collect channel URL.
 * @param[in] joblist - a pointer to the global job list object.
 * @param[out] pdisp - the address of storage for the return of the new
 *    collector object.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxCollectorCreate(long * debug, char * chname, char * dispurl,
      DnxJobList * joblist, DnxCollector ** pdisp)
{
   iDnxCollector * icoll;
   int ret;

   if ((icoll = (iDnxCollector *)malloc(sizeof *icoll)) == 0)
      return DNX_ERR_MEMORY;

   icoll->chname = strdup(chname);
   icoll->url = strdup(dispurl);
   icoll->joblist = joblist;
   icoll->debug = debug;
   icoll->channel = 0;
   icoll->tid = 0;

   if (!icoll->url || icoll->chname)
   {
      free(icoll);
      return DNX_ERR_MEMORY;
   }

   if ((ret = dnxChanMapAdd(chname, dispurl)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxCollectorCreate: "
                         "dnxChanMapAdd(%s) failed: %d", chname, ret);
      goto e1;
   }

   if ((ret = dnxConnect(chname, &icoll->channel, DNX_CHAN_PASSIVE)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "dnxCollectorCreate: "
                         "dnxConnect(%s) failed: %d", chname, ret);
      goto e2;
   }

   if (*debug)
      dnxChannelDebug(icoll->channel, *debug);

   // create the dispatcher thread
   if ((ret = pthread_create(&icoll->tid, NULL, dnxCollector, icoll)) != 0)
   {
      dnxSyslog(LOG_ERR, 
            "dnxCollectorCreate: thread creation failed: (%d) %s", 
            ret, strerror(ret));
      ret = DNX_ERR_THREAD;
      goto e3;
   }

   return DNX_OK;

// error paths

e3:dnxDisconnect(icoll->channel);
e2:dnxChanMapDelete(icoll->chname);
e1:free(icoll->url);
   free(icoll->chname);
   free(icoll);

   return ret;
}

//----------------------------------------------------------------------------

/** Destroy an existing collector object.
 * 
 * @param[in] coll - a pointer to the collector object to be destroyed.
 */
void dnxCollectorDestroy(DnxCollector  * coll)
{
   iDnxCollector * icoll = (iDnxCollector *)coll;

   pthread_cancel(icoll->tid);
   pthread_join(icoll->tid, NULL);

   dnxDisconnect(icoll->channel);
   dnxChanMapDelete(icoll->chname);

   free(icoll->url);
   free(icoll->chname);
   free(icoll);
}

/*--------------------------------------------------------------------------*/

