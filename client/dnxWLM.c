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

/** Implements the Work Load Manager functionality.
 *
 * Functions:
 *
 *    1. Started by the DNX Client main
 *    2. Creates the initial thread pool
 *    3. Monitors the thread pool for the need to increase worker thread count
 *    4. Cleans-up worker threads upon shutdown
 *
 * @file dnxWLM.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_CLIENT_IMPL
 */

#include "dnxWLM.h"

#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxLogging.h"
#include "dnxTransport.h"
#include "dnxSleep.h"
#include "dnxProtocol.h"
#include "dnxPlugin.h"

#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>     // MUST be included before ifaddrs.h!
#include <ifaddrs.h>

/** @todo Dynamically allocate based on config file maxResultBuffer setting. */
#define MAX_RESULT_DATA 1024

#define MAX_IP_ADDRSZ   64
#define MAX_HOSTNAME    253

struct iDnxWlm;               // forward declaration: circular reference

/** A value that indicates that current state of a pool thread. */
typedef enum DnxThreadState
{
   DNX_THREAD_DEAD = 0,
   DNX_THREAD_RUNNING,
   DNX_THREAD_ZOMBIE
} DnxThreadState;

/** A structure that describes the attribues of a pool thread. */
typedef struct DnxWorkerStatus
{
   DnxThreadState state;      //!< The current thread state.
   pthread_t tid;             //!< The thread Identifier.
   DnxChannel * dispatch;     //!< The thread job request channel.
   DnxChannel * collect;      //!< The thread job reply channel.
   time_t tstart;             //!< The thread start time.
   unsigned serial;           //!< The current job tracking serial number.
   struct iDnxWlm * iwlm;     //!< A reference to the owning WLM.
} DnxWorkerStatus;

/** The implementation of a work load manager object. */
typedef struct iDnxWlm
{
   DnxWlmCfgData cfg;         //!< WLM configuration parameters.
   DnxWorkerStatus ** pool;   //!< The thread pool context list.
   pthread_mutex_t mutex;     //!< The thread pool sync mutex.
   unsigned jobtm;            //!< Total amount of thread time processing jobs.
   unsigned threadtm;         //!< Total amount of thread life time.
   unsigned jobsok;           //!< The number of successful jobs, so far.
   unsigned jobsfail;         //!< The number of failed jobs so far.
   unsigned active;           //!< The current number of active threads.
   unsigned tcreated;         //!< The number of threads created.
   unsigned tdestroyed;       //!< The number of threads destroyed.
   unsigned threads;          //!< The current number of thread status objects.
   unsigned reqsent;          //!< The number of requests sent.
   unsigned jobsrcvd;         //!< The number of jobs received.
   unsigned minexectm;        //!< The minimum execution time.
   unsigned avgexectm;        //!< The avg execution time.
   unsigned maxexectm;        //!< The maximum execution time.
   unsigned avgthreads;       //!< The avg number of threads in existence.
   unsigned avgactive;        //!< The avg number of active threads.
   unsigned poolsz;           //!< The allocated size of the @em pool array.
   unsigned packets_in;       //!< The total number of packets recieved
   unsigned packets_out;      //!< The total number of packets send
   time_t lastclean;          //!< The last time the pool was cleaned.
   int terminate;             //!< The pool termination flag.
   unsigned long myipaddr;    //!< Binary local address for identification.
   char myipaddrstr[MAX_IP_ADDRSZ];//!< String local address for presentation.
   char myhostname[MAX_HOSTNAME];//!< String local Hostname for presentation.
} iDnxWlm;

// forward declaration required by source code organization
static void * dnxWorker(void * data);


/*--------------------------------------------------------------------------
                     WORK LOAD MANAGER IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** Log changes between old and new configuration data sets.
 * 
 * Dynamic reconfiguration of dispatcher and collector URL's is not allowed
 * so we don't need to check differences in those string values.
 * 
 * @param[in] ocp - a reference to the old configuration data set.
 * @param[in] ncp - a reference to the new configuration data set.
 */
static void logConfigChanges(DnxWlmCfgData * ocp, DnxWlmCfgData * ncp)
{
   if (strcmp(ocp->dispatcher, ncp->dispatcher) != 0)
      dnxLog("Config parameter 'channelDispatcher' changed from %s to %s. "
            "NOTE: Changing the dispatcher URL requires a restart.", 
            ocp->dispatcher, ncp->dispatcher);

   if (strcmp(ocp->collector, ncp->collector) != 0)
      dnxLog("Config parameter 'channelCollector' changed from %s to %s. "
            "NOTE: Changing the collector URL requires a restart.", 
            ocp->collector, ncp->collector);

   if (ocp->reqTimeout != ncp->reqTimeout)
      dnxLog("Config parameter 'threadRequestTimeout' changed from %u to %u.", 
            ocp->reqTimeout, ncp->reqTimeout);

   if (ocp->ttlBackoff != ncp->ttlBackoff)
      dnxLog("Config parameter 'threadTtlBackoff' changed from %u to %u.", 
            ocp->ttlBackoff, ncp->ttlBackoff);

   if (ocp->maxRetries != ncp->maxRetries)
      dnxLog("Config parameter 'threadMaxTimeouts' changed from %u to %u.", 
            ocp->maxRetries, ncp->maxRetries);

   if (ocp->poolMin != ncp->poolMin)
      dnxLog("Config parameter 'poolMin' changed from %u to %u.", 
            ocp->poolMin, ncp->poolMin);

   if (ocp->poolInitial != ncp->poolInitial)
      dnxLog("Config parameter 'poolInitial' changed from %u to %u.", 
            ocp->poolInitial, ncp->poolInitial);

   if (ocp->poolMax != ncp->poolMax)
      dnxLog("Config parameter 'poolMax' changed from %u to %u.", 
            ocp->poolMax, ncp->poolMax);

   if (ocp->poolGrow != ncp->poolGrow)
      dnxLog("Config parameter 'poolGrow' changed from %u to %u.", 
            ocp->poolGrow, ncp->poolGrow);

   if (ocp->pollInterval != ncp->pollInterval)
      dnxLog("Config parameter 'wlmPollInterval' changed from %u to %u.", 
            ocp->pollInterval, ncp->pollInterval);

   if (ocp->shutdownGrace != ncp->shutdownGrace)
      dnxLog("Config parameter 'wlmShutdownGracePeriod' changed from %u to %u.", 
            ocp->shutdownGrace, ncp->shutdownGrace);

   if (ocp->maxResults != ncp->maxResults)
      dnxLog("Config parameter 'maxResultBuffer' changed from %u to %u.", 
            ocp->maxResults, ncp->maxResults);

   if (ocp->showNodeAddr != ncp->showNodeAddr)
      dnxLog("Config parameter 'showNodeAddr' changed from %s to %s.", 
            ocp->showNodeAddr? "TRUE" : "FALSE", 
            ncp->showNodeAddr? "TRUE" : "FALSE");
            
   if (ocp->hostname != ncp->hostname)
      dnxLog("Config parameter 'hostname' changed from %s to %s.",
            ocp->hostname, ncp->hostname);
}

//----------------------------------------------------------------------------

/** Initialize worker thread communication resources.
 * 
 * @param[in] ws - a pointer to a worker thread's status data structure.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int initWorkerComm(DnxWorkerStatus * ws)
{
   char szChanDisp[64];
   char szChanColl[64];
   int ret;

   // create a channel for sending job requests (named after its memory address)
   sprintf(szChanDisp, "Dispatch:%lx", ws);
   if ((ret = dnxChanMapAdd(szChanDisp, ws->iwlm->cfg.dispatcher)) != DNX_OK)
   {
      dnxLog("WLM: Failed to initialize dispatcher channel: %s.", dnxErrorString(ret));
      return ret;
   }
   if ((ret = dnxConnect(szChanDisp, 1, &ws->dispatch)) != DNX_OK)
   {
      dnxLog("WLM: Failed to open dispatcher channel: %s.", dnxErrorString(ret));
      dnxChanMapDelete(szChanDisp);
      return ret;
   }

   // create a channel for sending job results (named after its memory address)
   sprintf(szChanColl, "Collect:%lx", ws);
   if ((ret = dnxChanMapAdd(szChanColl, ws->iwlm->cfg.collector)) != DNX_OK)
   {
      dnxLog("WLM: Failed to initialize collector channel: %s.", dnxErrorString(ret));
      dnxDisconnect(ws->dispatch);
      dnxChanMapDelete(szChanDisp);
      return ret;
   }
   if ((ret = dnxConnect(szChanColl, 1, &ws->collect)) != DNX_OK)
   {
      dnxLog("WLM: Failed to open collector channel: %s.", dnxErrorString(ret));
      dnxChanMapDelete(szChanColl);
      dnxDisconnect(ws->dispatch);
      dnxChanMapDelete(szChanDisp);
      return ret;
   }
   return 0;
}

//----------------------------------------------------------------------------

/** Clean up worker thread communications resources.
 * 
 * @param[in] ws - a pointer to a worker thread's status data structure.
 */
static void releaseWorkerComm(DnxWorkerStatus * ws)
{
   char szChan[64];

   // close and delete the dispatch channel
   dnxDisconnect(ws->dispatch);
   sprintf(szChan, "Dispatch:%lx", ws);
   dnxChanMapDelete(szChan);

   // close and delete the collector channel
   dnxDisconnect(ws->collect);
   sprintf(szChan, "Collect:%lx", ws);
   dnxChanMapDelete(szChan);
}

//----------------------------------------------------------------------------

/** Create a new worker thread.
 *
 * @param[in] iwlm - the WLM object whose thread pool is being updated.
 * @param[in] pws - the address of storage for the address of the newly 
 *    allocated and configured worker status structure.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int workerCreate(iDnxWlm * iwlm, DnxWorkerStatus ** pws)
{
   DnxWorkerStatus * ws = NULL;
   int ret;
   
   // allocate and clear a new worker status structure
   if ((ws = (DnxWorkerStatus *)xmalloc(sizeof *ws)) == 0)
      return DNX_ERR_MEMORY;
   memset(ws, 0, sizeof *ws);
   ws->iwlm = iwlm;

   // initialize our communications channels
   if ((ret = initWorkerComm(ws)) != 0)
   {
      dnxLog("WLM: Failed to initialize worker comm channels: %s.",dnxErrorString(ret));
      xfree(ws);
      return ret;
   }

   // create a worker thread
   ws->state = DNX_THREAD_RUNNING; // set thread state to active
   if ((ret = pthread_create(&ws->tid, 0, dnxWorker, ws)) != 0)
   {
      dnxLog("WLM: Failed to create worker thread: %s.", strerror(ret));
      releaseWorkerComm(ws);
      xfree(ws);
      return DNX_ERR_THREAD;
   }
   *pws = ws;
   return 0;
}

/** Clean up zombie threads and compact the thread pool.
 * 
 * @param[in] iwlm - a pointer to the work load manager data structure.
 */
static void cleanThreadPool(iDnxWlm * iwlm)
{
   unsigned i = 0;
   time_t now = time(0);

   iwlm->lastclean = now;  // keep track of when we last cleaned

   // look for zombie threads to join
   while (i < iwlm->threads)
   {
      if (iwlm->pool[i]->state == DNX_THREAD_ZOMBIE)
      {
         DnxWorkerStatus * ws = iwlm->pool[i];
         int ret;

         dnxDebug(1, "WLM: Joining worker[%lx]...", ws->tid);
         pthread_join(ws->tid, 0);

         // reduce thread count; update stats
         iwlm->threads--;
         iwlm->tdestroyed++;
         iwlm->threadtm += (unsigned)(now - ws->tstart);

         // release thread resources; delete thread; compact ptr array
         releaseWorkerComm(ws);
         xfree(iwlm->pool[i]);
         memmove(&iwlm->pool[i], &iwlm->pool[i + 1], 
               (iwlm->threads - i) * sizeof iwlm->pool[i]);
         continue;
      }
      i++;
   }
}

//----------------------------------------------------------------------------

/** Grow the thread pool to the configured number of threads.
 * 
 * This routine calculates an appropriate growth factor. If the current
 * number of threads is less than the requested initial pool size, then the 
 * pool is grown to the initial pool size. If the current number of threads
 * is near the maximum pool size, then only grow to the maximum. Otherwise it 
 * is grown by the configured pool growth value.
 * 
 * @param[in] iwlm - a reference to the work load manager whose thread 
 *    pool size is to be increased.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int growThreadPool(iDnxWlm * iwlm)
{
   unsigned i, add, growsz;
   int ret;

   // set additional thread count - keep us between the min and the max
   if (iwlm->threads < iwlm->cfg.poolInitial)
      growsz = iwlm->cfg.poolInitial - iwlm->threads;
   else if (iwlm->threads + iwlm->cfg.poolGrow > iwlm->cfg.poolMax)
      growsz = iwlm->cfg.poolMax - iwlm->threads;
   else
      growsz = iwlm->cfg.poolGrow;

   // fill as many empty slots as we can or need to
   for (i = iwlm->threads, add = growsz; i < iwlm->poolsz && add > 0; i++, add--)
   {
      if ((ret = workerCreate(iwlm, &iwlm->pool[i])) != 0)
         break;
      iwlm->threads++;
      iwlm->tcreated++;
   }
   dnxLog("WLM: Increased thread pool by %d.", growsz - add);
   return ret;
}

//----------------------------------------------------------------------------

/** Dispatch thread clean-up routine
 * 
 * @param[in] data - an opaque pointer to a worker's status data structure.
 */
static void dnxWorkerCleanup(void * data)
{
   DnxWorkerStatus * ws = (DnxWorkerStatus *)data;
   assert(data);
   dnxDebug(2, "Worker[%lx]: Terminating.", pthread_self());
   ws->state = DNX_THREAD_ZOMBIE;
}

//----------------------------------------------------------------------------

/** The main thread routine for a worker thread.
 * 
 * @param[in] data - an opaque pointer to a DnxWorkerStatus structure for this
 *    thread.
 * 
 * @return Always returns 0.
 */
static void * dnxWorker(void * data)
{
   DnxWorkerStatus * ws = (DnxWorkerStatus *)data;
   pthread_t tid = pthread_self();
   int retries = 0;
   iDnxWlm * iwlm;

   assert(data);
   
   iwlm = ws->iwlm;

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);
   pthread_cleanup_push(dnxWorkerCleanup, data);

   time(&ws->tstart);   // set thread start time (for stats)

   while (!iwlm->terminate)
   {
      DnxNodeRequest msg;
      DnxJob job;
      DnxAck ack; //= (DnxAck *)xmalloc(sizeof *ack);
      int ret;
      
      // setup job request message - use thread id and node address in XID
      dnxMakeXID(&msg.xid, DNX_OBJ_WORKER, tid, iwlm->myipaddr);
      msg.reqType = DNX_REQ_REGISTER;
      msg.jobCap = 1;
      msg.ttl = iwlm->cfg.reqTimeout - iwlm->cfg.ttlBackoff;
      msg.hn = iwlm->myhostname;
      // request a job, and then wait for a job to come in...
      if ((ret = dnxSendNodeRequest(ws->dispatch, &msg, 0)) != DNX_OK) {
         dnxLog("Worker[%lx]: Error sending node request: %s.", 
               tid, dnxErrorString(ret));
      } else {
         DNX_PT_MUTEX_LOCK(&iwlm->mutex);
         iwlm->reqsent++;
         DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
      }

      // wait for job, even if request was never sent
      if ((ret = dnxWaitForJob(ws->dispatch, &job, job.address,iwlm->cfg.reqTimeout)) != DNX_OK && ret != DNX_ERR_TIMEOUT) {
         dnxLog("Worker[%lx]: Error receiving job: %s.",
               tid, dnxErrorString(ret));
      }
      
      pthread_testcancel();

      DNX_PT_MUTEX_LOCK(&iwlm->mutex);
      cleanThreadPool(iwlm); // ensure counts are accurate before using them
      if (ret != DNX_OK)
      {
         // if above pool minimum and exceeded max retries...
         if (iwlm->threads > iwlm->cfg.poolMin 
               && ++retries > iwlm->cfg.maxRetries)
         {
            dnxLog("Worker[%lx]: Exiting - max retries exceeded.", tid);
            DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
            break;
         }
      }
      else
      {
         iwlm->jobsrcvd++;
         iwlm->active++;
//          dnxSendJobAck(ws->collect, &job, &job.address);
//          dnxDebug(3, "Worker[%lx]: Acknowledged job [%lu,%lu] (T/O %d): %s.", 
//                tid, job.xid.objSerial, job.xid.objSlot, job.timeout, job.cmd);
         
         // check pool size before we get too busy -
         // if we're not shutting down and we haven't reached the configured
         // maximum and this is the last thread out, then increase the pool
         if (!iwlm->terminate 
               && iwlm->threads < iwlm->cfg.poolMax
               && iwlm->active == iwlm->threads) // Maybe more aggressive here
            growThreadPool(iwlm);
      }
      DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);

      // if we have a job, execute it and reset retry count
      if (ret == DNX_OK)
      {
         char resData[MAX_RESULT_DATA + 1];
         DnxResult result;
         time_t jobstart;


         dnxDebug(3, "Worker[%lx]: Received job [%lu,%lu] (T/O %d): %s.", 
               tid, job.xid.objSerial, job.xid.objSlot, job.timeout, job.cmd);
               
//          memset(ack, 0, sizeof *ack);
         ack.xid = job.xid;
         ack.timestamp = job.timestamp;

         dnxSendJobAck(ws->collect, &ack, 0);
         dnxDebug(3, "Worker[%lx]: Acknowledged job [%lu,%lu] to channel (%lx) (T/S %lu).", 
               tid, ack.xid.objSerial, ack.xid.objSlot, ws->collect, ack.timestamp);
         
         // prepare result structure
         result.xid = job.xid;               // result xid must match job xid
         result.state = DNX_JOB_COMPLETE;    // complete or expired
         result.delta = 0;
         result.resCode = DNX_PLUGIN_RESULT_OK;
         result.resData = 0;

         /** @todo Allocate result data buffer based on configured buffer size. */

         // we want to be able to cancel threads while they're out on a task
         // in order to obtain timely shutdown for long jobs - move into
         // async cancel mode, but only for the duration of the check
         pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0);

         *resData = 0;
         jobstart = time(0);
         dnxPluginExecute(job.cmd, &result.resCode, resData, sizeof resData - 1, job.timeout,iwlm->cfg.showNodeAddr? iwlm->myipaddrstr: 0);
         result.delta = time(0) - jobstart;

         pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);

         // store allocated copy of the result string
         if (*resData) result.resData = xstrdup(resData);

         dnxDebug(3, "Worker[%lx]: Job [%lu,%lu] completed in %lu seconds: %d, %s.",
               tid, job.xid.objSerial, job.xid.objSlot, result.delta, 
               result.resCode, result.resData);

         if ((ret = dnxSendResult(ws->collect, &result, 0)) != DNX_OK)
            dnxDebug(3, "Worker[%lx]: Post job [%lu,%lu] results failed: %s.",
                  tid, job.xid.objSerial, job.xid.objSlot, dnxErrorString(ret));

         xfree(result.resData);
         xfree(&ack);
 
         // update all statistics
         DNX_PT_MUTEX_LOCK(&iwlm->mutex);
         {
            // track status
            if (result.resCode == DNX_PLUGIN_RESULT_OK) 
               iwlm->jobsok++;
            else 
               iwlm->jobsfail++;

            // track min/max/avg execution time
            if (result.delta > iwlm->maxexectm)
               iwlm->maxexectm = result.delta;
            if (result.delta < iwlm->minexectm)
               iwlm->minexectm = result.delta;
            iwlm->avgexectm = (iwlm->avgexectm + result.delta) / 2;

            // total job processing time
            iwlm->jobtm += (unsigned)result.delta;
            iwlm->active--;   // reduce active count
         }
         DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);

         ws->serial++;     // increment job serial number for next job
         retries = 0;
      }
   }
   pthread_cleanup_pop(1);
   return 0;
}

/*--------------------------------------------------------------------------
                        WORK LOAD MANAGER INTERFACE
  --------------------------------------------------------------------------*/

void dnxWlmResetStats(DnxWlm * wlm)
{
   iDnxWlm * iwlm = (iDnxWlm *)wlm;

   assert(wlm);

   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   iwlm->jobtm = iwlm->threadtm = 0;
   iwlm->jobsok = iwlm->jobsfail = iwlm->tcreated = iwlm->tdestroyed = 0;
   iwlm->reqsent = iwlm->jobsrcvd = iwlm->avgexectm = 0;
   iwlm->maxexectm = iwlm->avgthreads = iwlm->avgactive = 0;
   iwlm->minexectm = (unsigned)(-1);   // the largest possible value
   iwlm->packets_out = 0;
   iwlm->packets_in = 0;
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
}

//----------------------------------------------------------------------------

void dnxWlmGetStats(DnxWlm * wlm, DnxWlmStats * wsp)
{
   iDnxWlm * iwlm = (iDnxWlm *)wlm;

   assert(wlm && wsp);

   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   wsp->jobs_succeeded = iwlm->jobsok;
   wsp->jobs_failed = iwlm->jobsfail;
   wsp->threads_created = iwlm->tcreated;
   wsp->threads_destroyed = iwlm->tdestroyed;
   wsp->total_threads = iwlm->threads;
   wsp->active_threads = iwlm->active;
   wsp->requests_sent = iwlm->reqsent;
   wsp->jobs_received = iwlm->jobsrcvd;
   wsp->min_exec_time = iwlm->minexectm;
   wsp->avg_exec_time = iwlm->avgexectm;
   wsp->max_exec_time = iwlm->maxexectm;
   wsp->avg_total_threads = iwlm->avgthreads;
   wsp->avg_active_threads = iwlm->avgactive;
   wsp->thread_time = iwlm->threadtm;
   wsp->job_time = iwlm->jobtm;
   wsp->packets_out = iwlm->packets_out;
   wsp->packets_in = iwlm->packets_in;
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
}

//----------------------------------------------------------------------------

int dnxWlmReconfigure(DnxWlm * wlm, DnxWlmCfgData * cfg)
{
   iDnxWlm * iwlm = (iDnxWlm *)wlm;
   DnxWorkerStatus ** pool;
   int ret = 0;

   assert(wlm && cfg);
   assert(cfg->poolMin > 0);
   assert(cfg->poolMax >= cfg->poolMin);
   assert(cfg->poolInitial >= cfg->poolMin);
   assert(cfg->poolInitial <= cfg->poolMax);

   DNX_PT_MUTEX_LOCK(&iwlm->mutex);

   // dynamic reconfiguration of dispatcher/collector URL's is not allowed

   logConfigChanges(&iwlm->cfg, cfg);
 
   iwlm->cfg.reqTimeout = cfg->reqTimeout;
   iwlm->cfg.ttlBackoff = cfg->ttlBackoff;
   iwlm->cfg.maxRetries = cfg->maxRetries;
   iwlm->cfg.poolMin = cfg->poolMin;
   iwlm->cfg.poolInitial = cfg->poolInitial;
   iwlm->cfg.poolMax = cfg->poolMax;
   iwlm->cfg.poolGrow = cfg->poolGrow;
   iwlm->cfg.pollInterval = cfg->pollInterval;
   iwlm->cfg.shutdownGrace = cfg->shutdownGrace;
   iwlm->cfg.maxResults = cfg->maxResults;
   iwlm->cfg.showNodeAddr = cfg->showNodeAddr;
   strcpy(iwlm->cfg.hostname, cfg->hostname);

   // we can't reduce the poolsz until the number of threads
   //    drops below the new maximum
   while (iwlm->threads > iwlm->cfg.poolMax)
   {
      DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
      dnxCancelableSleep(3 * 1000);
      DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   }

   // reallocate the pool to the new size
   if ((pool = (DnxWorkerStatus **)xrealloc(iwlm->pool, 
         iwlm->cfg.poolMax * sizeof *pool)) == 0)
      ret = DNX_ERR_MEMORY;
   else
   {
      iwlm->poolsz = iwlm->cfg.poolMax;
      iwlm->pool = pool;
   }
    
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);

   return ret;
}

//----------------------------------------------------------------------------

int dnxWlmCreate(DnxWlmCfgData * cfg, DnxWlm ** pwlm)
{
   iDnxWlm * iwlm;
   struct ifaddrs * ifa = NULL;

   assert(cfg && pwlm);
   assert(cfg->poolMin > 0);
   assert(cfg->poolMax >= cfg->poolMin);
   assert(cfg->poolInitial >= cfg->poolMin);
   assert(cfg->poolInitial <= cfg->poolMax);

   // allocate and configure the master thread pool data structure
   if ((iwlm = (iDnxWlm *)xmalloc(sizeof *iwlm)) == 0)
      return DNX_ERR_MEMORY;

   memset(iwlm, 0, sizeof *iwlm);
   iwlm->cfg = *cfg;
   iwlm->cfg.dispatcher = xstrdup(iwlm->cfg.dispatcher);
   iwlm->cfg.collector = xstrdup(iwlm->cfg.collector);
   iwlm->poolsz = iwlm->cfg.poolMax;
   iwlm->pool = (DnxWorkerStatus **)xmalloc(iwlm->poolsz * sizeof *iwlm->pool);
   iwlm->minexectm = (unsigned)(-1);   // the largest possible value
   memset(iwlm->pool, 0, iwlm->poolsz * sizeof *iwlm->pool);

   // cache our (primary?) ip address in binary and string format
   if (getifaddrs(&ifa) == 0)
   {
      u_int setflags = IFF_UP | IFF_RUNNING;
      u_int clrflags = IFF_LOOPBACK;
      struct ifaddrs * ifcur = ifa;

      // locate the first proper AF_NET address in our interface list
      while (ifcur && (ifcur->ifa_addr == 0 
            || ifcur->ifa_addr->sa_family != AF_INET 
            || (ifcur->ifa_flags & setflags) != setflags
            || (ifcur->ifa_flags & clrflags) != 0))
         ifcur = ifcur->ifa_next;

      if (ifcur)
      {
         // cache binary and presentation (string) versions of the ip address
         iwlm->myipaddr = (unsigned long)
               ((struct sockaddr_in *)ifcur->ifa_addr)->sin_addr.s_addr;
         inet_ntop(ifcur->ifa_addr->sa_family,
                &((struct sockaddr_in *)ifcur->ifa_addr)->sin_addr,
                iwlm->myipaddrstr, sizeof iwlm->myipaddrstr);
      }
      freeifaddrs(ifa);
   }
   
   char unset[] = "NULL";
   if(!strnlen(iwlm->myhostname, 1)) //See if the global hostname has been set
   {
      dnxDebug(3, "dnxWlmCreate: Hostname not set in parent thread.");
      char machineName [MAX_HOSTNAME];
      if(strcmp(cfg->hostname, unset)==0)
      {
         dnxDebug(3, "dnxWlmCreate: Hostname undefined in config.");
         // Get our hostname
         if(gethostname(machineName, MAX_HOSTNAME)==0)
         {
            dnxDebug(3, "dnxWlmCreate: Hostname is [%s].", machineName);
            // cache hostname
            strcpy(iwlm->myhostname, machineName);
         } else {
            dnxLog("dnxWlmCreate: Unable to obtain Hostname [%s?],"
               "please set hostname in config.", machineName);
            sprintf( machineName, "localhost");
            strcpy(iwlm->myhostname, machineName);
         }
      } else {
         dnxDebug(3, "dnxWlmCreate: Using hostname in config [%s].", cfg->hostname);
         strcpy(iwlm->myhostname, cfg->hostname);
      }
   } else {
      dnxDebug(3, "dnxWlmCreate: Using cached hostname [%s].", iwlm->myhostname);
      strcpy(iwlm->cfg.hostname, iwlm->myhostname);
   }

   // if any of the above failed, we really can't continue
   if (!iwlm->cfg.dispatcher || !iwlm->cfg.collector || !iwlm->pool)
   {
      xfree(iwlm->cfg.dispatcher);
      xfree(iwlm->cfg.collector);
      xfree(iwlm);
      return DNX_ERR_MEMORY;
   }

   // create initial worker thread pool
   DNX_PT_MUTEX_INIT(&iwlm->mutex);
   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   {
      int ret;
      if ((ret = growThreadPool(iwlm)) != DNX_OK)
      {
         if (iwlm->threads)
            dnxLog("WLM: Error creating SOME worker threads: %s; "
                  "continuing with smaller initial pool.", dnxErrorString(ret));
         else
         {
            dnxLog("WLM: Unable to create ANY worker threads: %s; "
                  "terminating.", dnxErrorString(ret));
            DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
            DNX_PT_MUTEX_DESTROY(&iwlm->mutex);
            xfree(iwlm);
            return ret;
         }
      }
   }
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);

   dnxLog("WLM: Started worker thread pool.");

   *pwlm = (DnxWlm *)iwlm;

   return DNX_OK;
}

//----------------------------------------------------------------------------

void dnxWlmDestroy(DnxWlm * wlm)
{
   iDnxWlm * iwlm = (iDnxWlm *)wlm;
   time_t expires;
   unsigned i;

   assert(wlm);

   dnxLog("WLM: Beginning termination sequence...");

   // sleep till we can't stand it anymore, then kill everyone
   iwlm->terminate = 1;
   expires = iwlm->cfg.shutdownGrace + time(0);

   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   while (iwlm->threads > 0 && time(0) < expires)
   {
      cleanThreadPool(iwlm);
      DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
      dnxCancelableSleep(100);
      DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   }

   // check for workers remaining after grace period
   if (iwlm->threads)
      dnxDebug(1, "WLM: Termination - %d workers remaining"
            " after grace period.", iwlm->threads);
      
   // cancel all remaining workers
   for (i = 0; i < iwlm->threads; i++)
      if (iwlm->pool[i]->state == DNX_THREAD_RUNNING)
      {
         dnxDebug(1, "WLMDestroy: Cancelling worker[%lx].", iwlm->pool[i]->tid);
         pthread_cancel(iwlm->pool[i]->tid);
      }

   // give remaining thread some time to quit
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
   dnxCancelableSleep(1000);
   DNX_PT_MUTEX_LOCK(&iwlm->mutex);

   // join all zombies (should be everything left)
   cleanThreadPool(iwlm);
   assert(iwlm->threads == 0);
   xfree(iwlm->pool);
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);

   DNX_PT_MUTEX_DESTROY(&iwlm->mutex);

   xfree(iwlm->cfg.dispatcher);
   xfree(iwlm->cfg.collector);
   xfree(iwlm);

   dnxLog("WLM: Termination sequence complete.");
}

/*--------------------------------------------------------------------------*/

