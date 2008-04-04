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
 * @file dscollector.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */
#include "dscollector.h"

#include "dsnebmain.h"
#include "dsqueue.h"
#include "dsjoblist.h"
#include "dsaudit.h"

#include "dnxError.h"
#include "dnxProtocol.h"
#include "dnxLogging.h"

#include <assert.h>


#define DNX_COLLECTOR_TIMEOUT 30

static void dnxCollectorCleanup (void *data);
static int dnxPostResult (DnxGlobalData *gData, DnxNewJob *pJob, DnxResult *pResult);


//----------------------------------------------------------------------------

void *dnxCollector (void *data)
{
   DnxGlobalData *gData = (DnxGlobalData *)data;
   DnxResult sResult;
   DnxNewJob Job;
   int ret = 0;

   assert(data);

   // Set my cancel state to 'enabled', and cancel type to 'deferred'
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

   // Set thread cleanup handler
   pthread_cleanup_push(dnxCollectorCleanup, data);

   // Wait for Go signal from dnxNebMain
   if (pthread_mutex_lock(&(gData->tmGo)) != 0)
      pthread_exit(NULL);

   // See if the go signal has already been broadcast
   if (gData->isGo == 0)
   {
      // Nope.  Wait for the synchronization signal
      if (pthread_cond_wait(&(gData->tcGo), &(gData->tmGo)) != 0)
      {
         // pthread_mutex_unlock(&(gData->tmGo));
         pthread_exit(NULL);
      }
   }

   // Release the lock
   pthread_mutex_unlock(&(gData->tmGo));

   dnxSyslog(LOG_INFO, "dnxCollector[%lx]: Awaiting service check results", pthread_self());

   // Wait for new service checks or cancellation
   while (1)
   {
      // Test for thread cancellation
      pthread_testcancel();

      // Wait for Worker Node Results
      if ((ret = dnxGetResult(gData->pCollect, &sResult, sResult.address, DNX_COLLECTOR_TIMEOUT)) == DNX_OK)
      {
         dnxDebug(1, "dnxCollector[%lx]: Received result for [%lu,%lu]: %s", pthread_self(), sResult.guid.objSerial, sResult.guid.objSlot, sResult.resData);

         // Dequeue the matching service request from the Pending Job Queue
         if (dnxJobListCollect(gData->JobList, &(sResult.guid), &Job) == DNX_OK)
         {
            // Post the results to the Nagios service request buffer
            ret = dnxPostResult(gData, &Job, &sResult);

            dnxDebug(1, "dnxCollector[%lx]: Posted result [%lu,%lu]: %d", pthread_self(), sResult.guid.objSerial, sResult.guid.objSlot, ret);

            // Worker Audit Logging
            dsAuditJob(&Job, "COLLECT");

            // Release this Job's resources
            dnxJobCleanup(&Job);
         }
         else
            dnxSyslog(LOG_WARNING, "dnxCollector[%lx]: Unable to dequeue completed job: %d", pthread_self(), ret);
      }
      else if (ret != DNX_ERR_TIMEOUT)
         dnxSyslog(LOG_ERR, "dnxCollector[%lx]: Failure to read result message from Collector channel: %d", pthread_self(), ret);
   }

   // Remove thread cleanup handler
   pthread_cleanup_pop(1);

   // Terminate this thread
   pthread_exit(NULL);
}

//----------------------------------------------------------------------------
// Dispatch thread clean-up routine

static void dnxCollectorCleanup (void *data)
{
   DnxGlobalData *gData = (DnxGlobalData *)data;
   assert(data);

   // Unlock the Go signal mutex
   if (&(gData->tmGo))
      pthread_mutex_unlock(&(gData->tmGo));
}

//----------------------------------------------------------------------------
// Posts a completed service request to the Nagios service result buffer

static int dnxPostResult (DnxGlobalData *gData, DnxNewJob *pJob, DnxResult *pResult)
{
   extern circular_buffer service_result_buffer;
   extern int check_result_buffer_slots;
   service_message *new_message;

   // Allocate memory for the message
   if ((new_message = (service_message *)malloc(sizeof(service_message))) == NULL)
   {
      dnxSyslog(LOG_ERR, "dnxCollector[%lx]: dnxPostResult: Memory allocation failure", pthread_self());
      return DNX_ERR_MEMORY;
   }

   // Copy the completed job's data to the message buffer
   gettimeofday(&(new_message->finish_time), NULL);
   strncpy(new_message->host_name, pJob->svc->host_name, sizeof(new_message->host_name)-1);
   new_message->host_name[sizeof(new_message->host_name)-1] = '\0';
   strncpy(new_message->description, pJob->svc->description, sizeof(new_message->description)-1);
   new_message->description[sizeof(new_message->description)-1] = '\0';
   new_message->return_code = pResult->resCode;
   new_message->exited_ok = TRUE;
   new_message->check_type = SERVICE_CHECK_ACTIVE;
   new_message-> parallelized = pJob->svc->parallelize;
   new_message->start_time.tv_sec = pJob->start_time;
   new_message->start_time.tv_usec = 0L;
   new_message->early_timeout = FALSE;
   strncpy(new_message->output, pResult->resData, sizeof(new_message->output)-1);
   new_message->output[sizeof(new_message->output)-1] = '\0';

   // Free result output memory (now that it's been copied into new_message)
   // TODO: Wrapper release DnxResult structure
   free(pResult->resData);

   // Obtain a lock for writing to the buffer
   pthread_mutex_lock(&service_result_buffer.buffer_lock);

   // Handle overflow conditions
   if (service_result_buffer.items == check_result_buffer_slots)
   {
      // Record overflow
      service_result_buffer.overflow++;

      // Update tail pointer
      service_result_buffer.tail = (service_result_buffer.tail + 1) % check_result_buffer_slots;

      dnxSyslog(LOG_ERR, "dnxCollector[%lx]: dnxPostResult: Service result buffer overflow = %lu", pthread_self(), service_result_buffer.overflow);
   }

   // Save the data to the buffer
   ((service_message **)service_result_buffer.buffer)[service_result_buffer.head] = new_message;

   // Increment the head counter and items
   service_result_buffer.head = (service_result_buffer.head + 1) % check_result_buffer_slots;
   if (service_result_buffer.items < check_result_buffer_slots)
      service_result_buffer.items++;
   if (service_result_buffer.items > service_result_buffer.high)
      service_result_buffer.high = service_result_buffer.items;

   // Release lock on buffer
   pthread_mutex_unlock(&service_result_buffer.buffer_lock);

   return DNX_OK;
}

/*-------------------------------------------------------------------------*/

