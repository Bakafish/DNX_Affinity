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
#include <ifaddrs.h>

/** @todo Dynamically allocate based on config file maxResultBuffer setting. */
#define MAX_RESULT_DATA 1024

#define MAX_IP_ADDRSZ   64

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
   DnxThreadState state;      /*!< The current thread state. */
   pthread_t tid;             /*!< The thread Identifier. */
   DnxChannel * dispatch;     /*!< The thread job request channel. */
   DnxChannel * collect;      /*!< The thread job reply channel. */
   time_t tstart;             /*!< The thread start time. */
   time_t jobtime;            /*!< The total amount of time spent in job processing. */
   unsigned jobsok;           /*!< The total jobs completed. */
   unsigned jobsfail;         /*!< The total jobs not completed. */
   unsigned serial;           /*!< The current job tracking serial number. */
   struct iDnxWlm * iwlm;     /*!< A reference to the owning WLM. */
} DnxWorkerStatus;

/** The implementation of a work load manager object. */
typedef struct iDnxWlm
{
   DnxWlmCfgData cfg;         /*!< WLM configuration parameters. */
   DnxWorkerStatus ** pool;   /*!< The thread pool context list. */
   pthread_mutex_t mutex;     /*!< The thread pool sync mutex. */
   unsigned jobsok;           /*!< The number of successful jobs, so far. */
   unsigned jobsfail;         /*!< The number of failed jobs so far. */
   unsigned active;           /*!< The current number of active threads. */
   unsigned tcreated;         /*!< The number of threads created. */
   unsigned tdestroyed;       /*!< The number of threads destroyed. */
   unsigned threads;          /*!< The current number of thread status objects. */
   unsigned poolsz;           /*!< The allocated size of the @em pool array. */
   time_t lastclean;          /*!< The last time the pool was cleaned. */
   int terminate;             /*!< The pool termination flag. */
   unsigned long myipaddr;    /*!< Binary local address for identification. */
   char myipaddrstr[MAX_IP_ADDRSZ];/*!< String local address for presentation. */
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
   if (ocp->reqTimeout != ncp->reqTimeout)
      dnxSyslog(LOG_INFO, "WLM: threadRequestTimeout changed from %u to %u", 
            ocp->reqTimeout, ncp->reqTimeout);

   if (ocp->ttlBackoff != ncp->ttlBackoff)
      dnxSyslog(LOG_INFO, "WLM: threadTtlBackoff changed from %u to %u", 
            ocp->ttlBackoff, ncp->ttlBackoff);

   if (ocp->maxRetries != ncp->maxRetries)
      dnxSyslog(LOG_INFO, "WLM: threadMaxTimeouts changed from %u to %u", 
            ocp->maxRetries, ncp->maxRetries);

   if (ocp->poolMin != ncp->poolMin)
      dnxSyslog(LOG_INFO, "WLM: poolMin changed from %u to %u", 
            ocp->poolMin, ncp->poolMin);

   if (ocp->poolInitial != ncp->poolInitial)
      dnxSyslog(LOG_INFO, "WLM: poolInitial changed from %u to %u", 
            ocp->poolInitial, ncp->poolInitial);

   if (ocp->poolMax != ncp->poolMax)
      dnxSyslog(LOG_INFO, "WLM: poolMax changed from %u to %u", 
            ocp->poolMax, ncp->poolMax);

   if (ocp->poolGrow != ncp->poolGrow)
      dnxSyslog(LOG_INFO, "WLM: poolGrow changed from %u to %u", 
            ocp->poolGrow, ncp->poolGrow);

   if (ocp->pollInterval != ncp->pollInterval)
      dnxSyslog(LOG_INFO, "WLM: wlmPollInterval changed from %u to %u", 
            ocp->pollInterval, ncp->pollInterval);

   if (ocp->shutdownGrace != ncp->shutdownGrace)
      dnxSyslog(LOG_INFO, "WLM: wlmShutdownGracePeriod changed from %u to %u", 
            ocp->shutdownGrace, ncp->shutdownGrace);

   if (ocp->maxResults != ncp->maxResults)
      dnxSyslog(LOG_INFO, "WLM: maxResultBuffer changed from %u to %u", 
            ocp->maxResults, ncp->maxResults);
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
   char szChan[64];
   int ret;

   // create a channel for sending job requests (named after its memory address)
   sprintf(szChan, "Dispatch:%lx", ws);
   if ((ret = dnxChanMapAdd(szChan, ws->iwlm->cfg.dispatcher)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "WLM: dnxChanMapAdd(Dispatch:%lx) failed, %d: %s", 
            ws, ret, dnxErrorString(ret));
      return ret;
   }
   if ((ret = dnxConnect(szChan, 1, &ws->dispatch)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "WLM: dnxConnect(Dispatch:%lx) failed, %d: %s", 
            ws, ret, dnxErrorString(ret));
      return ret;
   }

   // create a channel for sending job results (named after its memory address)
   sprintf(szChan, "Collect:%lx", ws);
   if ((ret = dnxChanMapAdd(szChan, ws->iwlm->cfg.collector)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "WLM: dnxChanMapAdd(Collect:%lx) failed, %d: %s", 
            ws, ret, dnxErrorString(ret));
      return ret;
   }
   if ((ret = dnxConnect(szChan, 1, &ws->collect)) != DNX_OK)
   {
      dnxSyslog(LOG_ERR, "WLM: dnxConnect(Collect:%lx) failed, %d: %s", 
            ws, ret, dnxErrorString(ret));
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
   DnxWorkerStatus * ws;
   int ret;
   
   // allocate and clear a new worker status structure
   if ((ws = (DnxWorkerStatus *)xmalloc(sizeof *ws)) == 0)
      return DNX_ERR_MEMORY;
   memset(ws, 0, sizeof *ws);
   ws->iwlm = iwlm;

   // initialize our communications channels
   if ((ret = initWorkerComm(ws)) != 0)
   {
      dnxSyslog(LOG_ERR, "WLM: Failed to initialize worker comm channels, %d: %s", 
            ret, dnxErrorString(ret));
      xfree(ws);
      return ret;
   }

   // create a worker thread
   ws->state = DNX_THREAD_RUNNING; // set thread state to active
   if ((ret = pthread_create(&ws->tid, 0, dnxWorker, ws)) != 0)
   {
      dnxSyslog(LOG_ERR, "WLM: Failed to create worker thread, %d: %s", 
            ret, strerror(ret));
      releaseWorkerComm(ws);
      xfree(ws);
      return DNX_ERR_THREAD;
   }
   *pws = ws;
   return 0;
}

//----------------------------------------------------------------------------

/** Grow the thread pool to the configured number of threads.
 * 
 * This routine calculates an appropriate growth factor. If the current
 * number of threads is less than the requestd initial pool size, then the 
 * pool is grown to the initial pool size. If the current number of threads
 * is near the maximum pool size, then only grow to the maximum. Otherwise it 
 * is grown by the configured pool growth number.
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
   dnxSyslog(LOG_INFO, "WLM: Increased thread pool by %d", growsz - add);
   return ret;
}

/** Clean up zombie threads and compact the thread pool.
 * 
 * @param[in] iwlm - a pointer to the work load manager data structure.
 */
static void cleanThreadPool(iDnxWlm * iwlm)
{
   unsigned i = 0;

   // look for zombie threads to join
   while (i < iwlm->threads)
   {
      if (iwlm->pool[i]->state == DNX_THREAD_ZOMBIE)
      {
         DnxWorkerStatus * ws = iwlm->pool[i];
         int ret;

         dnxDebug(1, "WLM: Joining worker[%lx]...", ws->tid);
         pthread_join(ws->tid, 0);

         // save stats to global counters
         iwlm->jobsok += ws->jobsok;
         iwlm->jobsfail += ws->jobsfail;
         iwlm->tdestroyed++;

         releaseWorkerComm(ws);

         // reduce threads count, delete thread object, compact ptr array
         iwlm->threads--;
         xfree(iwlm->pool[i]);
         memmove(&iwlm->pool[i], &iwlm->pool[i + 1], 
               (iwlm->threads - i) * sizeof iwlm->pool[i]);
         continue;
      }
      i++;
   }
   dnxDebug(1, "WLM: Threads: %d; Busy: %d", iwlm->threads, iwlm->active);
}

//----------------------------------------------------------------------------

/** Dispatch thread clean-up routine
 * 
 * @param[in] data - an opaque pointer to a worker 's status data structure.
 */
static void dnxWorkerCleanup(void * data)
{
   assert(data);
   ((DnxWorkerStatus *)data)->state = DNX_THREAD_ZOMBIE;
   dnxDebug(2, "Worker[%lx]: Terminating", pthread_self());
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

   assert(data);

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);
   pthread_cleanup_push(dnxWorkerCleanup, data);

   time(&ws->tstart);   // set thread start time (for stats)

   while (!ws->iwlm->terminate)
   {
      iDnxWlm * iwlm = ws->iwlm;
      DnxNodeRequest msg;
      DnxJob job;
      int ret;

      pthread_testcancel();
      
      // setup job request message - use thread id and node address in XID
      dnxMakeXID(&msg.xid, DNX_OBJ_WORKER, tid, iwlm->myipaddr);
      msg.reqType = DNX_REQ_REGISTER;
      msg.jobCap = 1;
      msg.ttl = iwlm->cfg.reqTimeout - iwlm->cfg.ttlBackoff;

      // request a job, and then wait for a job to come in...
      if ((ret = dnxSendNodeRequest(ws->dispatch, &msg, 0)) != DNX_OK)
      {
         switch (ret)
         {
            case DNX_ERR_SEND:
            case DNX_ERR_TIMEOUT:
               dnxSyslog(LOG_ERR, "Worker[%lx]: Unable to contact server: %s", 
                     tid, dnxErrorString(ret));
               break;
            default:
               dnxSyslog(LOG_ERR, "Worker[%lx]: dnxWantJob failed: %s", 
                     tid, dnxErrorString(ret));
         }
      }
      else if ((ret = dnxWaitForJob(ws->dispatch, &job, job.address, 
            iwlm->cfg.reqTimeout)) != DNX_OK)
      {
         switch (ret)
         {
            case DNX_ERR_TIMEOUT:  
               break;                  // Timeout is OK here
            case DNX_ERR_RECEIVE:
               dnxSyslog(LOG_ERR, "Worker[%lx]: Unable to contact server: %s", 
                     tid, dnxErrorString(ret));
               break;
            default:
               dnxSyslog(LOG_ERR, "Worker[%lx]: dnxGetJob failed: %s", 
                     tid, dnxErrorString(ret));
         }
      }

      // check for transmission error, or execute the job and reset retry count
      if (ret != DNX_OK)
      {
         // if exceeded max retries and above pool minimum...
         if (++retries > iwlm->cfg.maxRetries
               && iwlm->threads > iwlm->cfg.poolMin)
         {
            dnxSyslog(LOG_INFO, "Worker[%lx]: Exiting - max retries exceeded", tid);
            break;
         }
      }
      else
      {
         char resData[MAX_RESULT_DATA + 1];
         DnxResult result;
         time_t jobstart;

         // check pool size before we get too busy -
         // if we're not shutting down, and this is the last thread out
         //    and we haven't reached our configured limit, then increase
         DNX_PT_MUTEX_LOCK(&iwlm->mutex);
         {
            iwlm->active++;
            if (!iwlm->terminate 
                  && iwlm->active == iwlm->threads 
                  && iwlm->threads < iwlm->cfg.poolMax)
               growThreadPool(iwlm);
         }
         DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);

         dnxDebug(2, "Worker[%lx]: Received job [%lu,%lu] (T/O %d): %s", 
               tid, job.xid.objSerial, job.xid.objSlot, job.timeout, job.cmd);

         // prepare result structure
         result.xid = job.xid;               // result xid must match job xid
         result.state = DNX_JOB_COMPLETE;    // complete or expired
         result.delta = 0;
         result.resCode = DNX_PLUGIN_RESULT_OK;
         result.resData = 0;

         /** @todo Allocate result data buffer based on configured buffer size. */

         *resData = 0;
         jobstart = time(0);
         dnxPluginExecute(job.cmd, &result.resCode, resData, 
               sizeof resData - 1, job.timeout, iwlm->myipaddrstr);
         result.delta = time(0) - jobstart;

         // store allocated copy of the result string
         if (*resData) result.resData = xstrdup(resData);

         // update per-thread statistics
         ws->jobtime += result.delta;
         if (result.resCode == DNX_PLUGIN_RESULT_OK) 
            ws->jobsok++;
         else 
            ws->jobsfail++;

         dnxDebug(2, "Worker[%lx]: Job [%lu,%lu] completed in %lu seconds: %d, %s", 
               tid, job.xid.objSerial, job.xid.objSlot, result.delta, 
               result.resCode, result.resData);

         if ((ret = dnxSendResult(ws->collect, &result, 0)) != DNX_OK)
            dnxSyslog(LOG_ERR, "Worker[%lx]: Post job [%lu,%lu] results failed: %s", 
                  tid, job.xid.objSerial, job.xid.objSlot, dnxErrorString(ret));

         xfree(result.resData);

         // if we haven't cleaned up zombies in a while, then do it now
         DNX_PT_MUTEX_LOCK(&iwlm->mutex);
         {
            time_t now = time(0);
            if (iwlm->lastclean + iwlm->cfg.pollInterval < now)
            {
               cleanThreadPool(iwlm);
               iwlm->lastclean = now;
            }
            iwlm->active--;
         }
         DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);

         ws->serial++;  // increment job serial number for next job
         retries = 0;
      }
   }
   pthread_cleanup_pop(1);
   return 0;
}

/*--------------------------------------------------------------------------
                        WORK LOAD MANAGER INTERFACE
  --------------------------------------------------------------------------*/

int dnxWlmGetActiveThreads(DnxWlm * wlm)
{
   int active;
   iDnxWlm * iwlm = (iDnxWlm *)wlm;
   assert(wlm);
   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   active = iwlm->threads;
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
   return active;
}

//----------------------------------------------------------------------------

int dnxWlmGetActiveJobs(DnxWlm * wlm)
{
   int active;
   iDnxWlm * iwlm = (iDnxWlm *)wlm;
   assert(wlm);
   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   active = iwlm->active;
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
   return active;
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
   struct ifaddrs * ifa;

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
   memset(iwlm->pool, 0, iwlm->poolsz * sizeof *iwlm->pool);

   // cache our (primary?) ip address in binary and string format
   if (getifaddrs(&ifa) == 0)
   {
      // locate the first AF_NET address in our interface list
      struct ifaddrs * ifcur = ifa;
      while (ifcur && ifcur->ifa_addr->sa_family != AF_INET)
         ifcur = ifcur->ifa_next;

      if (ifcur)
      {
         // cache binary and presentation (string) versions of the ip address
         iwlm->myipaddr = (unsigned long)
               ((struct sockaddr_in *)ifcur->ifa_addr)->sin_addr.s_addr;
         inet_ntop(ifcur->ifa_addr->sa_family, ifcur->ifa_addr->sa_data, 
               iwlm->myipaddrstr, sizeof iwlm->myipaddrstr);
      }
      freeifaddrs(ifa);
   }

   // if any of the above failed, we really can't continue
   if (!iwlm->cfg.dispatcher || !iwlm->cfg.collector || !iwlm->pool || !iwlm->myipaddr)
   {
      xfree(iwlm->cfg.dispatcher);
      xfree(iwlm->cfg.collector);
      xfree(iwlm);
      if (!iwlm->myipaddr)
      {
         dnxSyslog(LOG_ERR, "WLM: Unable to access network interfaces.");
         return DNX_ERR_ADDRESS;
      }
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
            dnxSyslog(LOG_ERR, 
                  "WLM: Error creating SOME worker threads: %s; "
                  "continuing with smaller initial pool.", dnxErrorString(ret));
         else
         {
            dnxSyslog(LOG_ERR, 
                  "WLM: Error creating ANY worker threads: %s; "
                  "terminating.", dnxErrorString(ret));
            DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
            DNX_PT_MUTEX_DESTROY(&iwlm->mutex);
            xfree(iwlm);
            return ret;
         }
      }
   }
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);

   dnxSyslog(LOG_INFO, "WLM: Started worker thread pool.");

   *pwlm = (DnxWlm *)iwlm;

   return DNX_OK;
}

//----------------------------------------------------------------------------

void dnxWlmDestroy(DnxWlm * wlm)
{
   iDnxWlm * iwlm = (iDnxWlm *)wlm;
   time_t expires;

   assert(wlm);

   dnxSyslog(LOG_INFO, "WLM: Beginning termination sequence...");

   // sleep till we can't stand it anymore, then kill everyone
   iwlm->terminate = 1;
   expires = iwlm->cfg.shutdownGrace + time(0);
   while (iwlm->threads > 0 && time(0) < expires)
      dnxCancelableSleep(100);

   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   {
      unsigned i;
   
      // cancel all remaining workers
      for (i = 0; i < iwlm->threads; i++)
         if (iwlm->pool[i]->state == DNX_THREAD_RUNNING)
         {
            dnxDebug(1, "WLMDestroy: Cancelling worker[%lx]", iwlm->pool[i]->tid);
            pthread_cancel(iwlm->pool[i]->tid);
         }

      // join all zombies (should be everything left)
      cleanThreadPool(iwlm);
      assert(iwlm->threads == 0);
      xfree(iwlm->pool);
   }
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
   DNX_PT_MUTEX_DESTROY(&iwlm->mutex);

   dnxSyslog(LOG_INFO, "WLM: Termination sequence complete");

   xfree(iwlm);
}

/*--------------------------------------------------------------------------*/

