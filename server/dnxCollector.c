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
   long * debug;           /*!< A pointer to the global debug level. */
   char * chname;          /*!< The collector channel name. */
   char * url;             /*!< The collector channel URL. */
   DnxJobList * joblist;   /*!< The job list we're collecting for. */
   dnxChannel * channel;   /*!< Collector communications channel. */
   pthread_t tid;          /*!< The collector thread id. */
   int running;            /*!< Running flag - as opposed to cancellation. */
} iDnxCollector;

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

   while (icoll->running)
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
            ret = nagiosPostResult(Job.svc, Job.start_time, FALSE, 
                  sResult.resCode, sResult.resData);

            /** @todo Wrapper release DnxResult structure. */

            // free result output memory (now that it's been copied into new_message)
            xfree(sResult.resData);

            dnxDebug(1, "dnxCollector[%lx]: Posted result [%lu,%lu]: %d: %s", 
                  pthread_self(), sResult.guid.objSerial, 
                  sResult.guid.objSlot, ret, dnxErrorString(ret));

            dnxAuditJob(&Job, "COLLECT");

            dnxJobCleanup(&Job);
         }
         else
            dnxSyslog(LOG_WARNING, 
                  "dnxCollector[%lx]: Unable to dequeue completed job; failed "
                  "with %d: %s", pthread_self(), ret, dnxErrorString(ret));
      }
      else if (ret != DNX_ERR_TIMEOUT)
         dnxSyslog(LOG_ERR, 
               "dnxCollector[%lx]: Failure to read result message "
               "from Collector channel; failed with %d: %s", 
               pthread_self(), ret, dnxErrorString(ret));
   }

   pthread_cleanup_pop(1);
   return 0;
}

//----------------------------------------------------------------------------

/** Create a new collector object.
 * 
 * @param[in] debug - a pointer to the global debug level.
 * @param[in] chname - the name of the collect channel.
 * @param[in] collurl - the collect channel URL.
 * @param[in] joblist - a pointer to the global job list object.
 * @param[out] pcoll - the address of storage for the return of the new
 *    collector object.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxCollectorCreate(long * debug, char * chname, char * collurl,
      DnxJobList * joblist, DnxCollector ** pcoll)
{
   iDnxCollector * icoll;
   int ret;

   if ((icoll = (iDnxCollector *)xmalloc(sizeof *icoll)) == 0)
      return DNX_ERR_MEMORY;

   icoll->chname = xstrdup(chname);
   icoll->url = xstrdup(collurl);
   icoll->joblist = joblist;
   icoll->debug = debug;
   icoll->running = 1;

   if (!icoll->url || !icoll->chname)
   {
      xfree(icoll);
      return DNX_ERR_MEMORY;
   }

   if ((ret = dnxChanMapAdd(chname, collurl)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, 
            "dnxCollectorCreate: dnxChanMapAdd(%s) failed with %d: %s", 
            chname, ret, dnxErrorString(ret));
      goto e1;
   }

   if ((ret = dnxConnect(chname, &icoll->channel, DNX_CHAN_PASSIVE)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, 
            "dnxCollectorCreate: dnxConnect(%s) failed with %d: %s", 
            chname, ret, dnxErrorString(ret));
      goto e2;
   }

   if (*debug)
      dnxChannelDebug(icoll->channel, *debug);

   // create the dispatcher thread
   if ((ret = pthread_create(&icoll->tid, NULL, dnxCollector, icoll)) != 0)
   {
      dnxSyslog(LOG_ERR, 
            "dnxCollectorCreate: thread creation failed with %d: %s", 
            ret, dnxErrorString(ret));
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

/** Destroy an existing collector object.
 * 
 * @param[in] coll - a pointer to the collector object to be destroyed.
 */
void dnxCollectorDestroy(DnxCollector  * coll)
{
   iDnxCollector * icoll = (iDnxCollector *)coll;

   icoll->running = 0;
// pthread_cancel(icoll->tid);
   pthread_join(icoll->tid, NULL);

   dnxDisconnect(icoll->channel);
   dnxChanMapDelete(icoll->chname);

   xfree(icoll->url);
   xfree(icoll->chname);
   xfree(icoll);
}

/*--------------------------------------------------------------------------*/

