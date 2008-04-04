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
#include "dnxChannel.h"
#include "dnxSleep.h"
#include "dnxProtocol.h"
#include "dnxPlugin.h"

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>

/** @todo Dynamically allocate based on config file maxResultBuffer setting. */
#define MAX_RESULT_DATA 1024

struct iDnxWlm;               // forward declaration: circular reference

/** A value that indicates that current state of a pool thread. */
typedef enum DnxThreadState_ 
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
   time_t jobstart;           /*!< The current job start time. */
   time_t jobtime;            /*!< The total amount of time spent in job processing. */
   unsigned jobsok;           /*!< The total jobs completed. */
   unsigned jobsfail;         /*!< The total jobs not completed. */
   unsigned reqserial;        /*!< The current request tracking serial number. */
   struct iDnxWlm * iwlm;     /*!< A reference to the owning WLM. */
} DnxWorkerStatus;

/** The implementation of a work load manager object. */
typedef struct iDnxWlm
{
   DnxWlmCfgData cfg;         /*!< WLM configuration parameters. */
   unsigned jobsok;           /*!< The number of successful jobs, so far. */
   unsigned jobsfail;         /*!< The number of failed jobs so far. */
   unsigned active;           /*!< The current number of active threads. */
   unsigned tcreated;         /*!< The number of threads created. */
   unsigned tdestroyed;       /*!< The number of threads destroyed. */
   unsigned threads;          /*!< The current number of thread status objects. */
   unsigned poolsz;           /*!< The allocated size of the @em pool array. */
   DnxWorkerStatus ** pool;   /*!< The thread pool context list. */
   time_t termexpires;        /*!< The termination time limit. */
   int terminate;             /*!< The WLM termination flag. */
   pthread_t tid;             /*!< The Work Load Manager thread id. */
   pthread_mutex_t mutex;     /*!< The WLM thread sync mutex. */
   pthread_cond_t cond;       /*!< The WLM thread sync condition variable. */
} iDnxWlm;

/*--------------------------------------------------------------------------
                           WORKER IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** Increment or decrement the active job count on the specified WLM. 
 * 
 * @param[in] iwlm - the Work Load Manager whose active job count should be
 *    returned.
 * @param[in] value - an increment/decrement indicator. If @p value is 
 *    positive, then the job count will be incremented; if it's negative
 *    then the job count will be decremented. Zero is not allowed.
 */
static void wlmSetActiveJobs(iDnxWlm * iwlm, int value)
{
   assert(iwlm && value != 0);
   value = value > 0 ? 1 : -1;
   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   iwlm->active += value;
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
}

//----------------------------------------------------------------------------

/** Execute a job.
 * 
 * @param[in] ws - a pointer to a worker thread's status data structure.
 * @param[in] job - a job to be executed by this thread.
 * @param[out] result - the address of storage for returning a job's result
 *    code.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxExecuteJob(DnxWorkerStatus * ws, DnxJob * job, DnxResult * result)
{
   char resData[MAX_RESULT_DATA + 1];
   pthread_t tid = pthread_self();
   int ret;

   dnxDebug(1, "Worker[%lx]: Received job [%lu,%lu] (T/O %d): %s", 
         tid, job->xid.objSerial, job->xid.objSlot, 
         job->timeout, job->cmd);

   // prepare result structure
   result->xid = job->xid;             // result xid must match job xid
   result->state = DNX_JOB_COMPLETE;   // complete or expired
   result->delta = 0;
   result->resCode = DNX_PLUGIN_RESULT_OK;
   result->resData = 0;

   /** @todo Allocate result data buffer based on configured buffer size. */
   *resData = 0;

   wlmSetActiveJobs(ws->iwlm, 1);

   time(&ws->jobstart);

   ret = dnxPluginExecute(job->cmd, &result->resCode, resData, 
         sizeof resData - 1, job->timeout);

   result->delta = time(0) - ws->jobstart;
   ws->jobstart = 0;

   wlmSetActiveJobs(ws->iwlm, -1);

   // store allocated copy of the result string
   if (*resData && (result->resData = xstrdup(resData)) == 0)
   {
      dnxSyslog(LOG_ERR, 
            "Worker[%lx]: Results allocation failure for job [%lu,%lu]: %s", 
            tid, job->xid.objSerial, job->xid.objSlot, job->cmd);
      ret = DNX_ERR_MEMORY;
   }

   // update per-thread statistics
   ws->jobtime += result->delta;
   if (result->resCode == DNX_PLUGIN_RESULT_OK) ws->jobsok++;
   else                                         ws->jobsfail++;

   dnxDebug(1, "Worker[%lx]: Job [%lu,%lu] completed in %lu seconds: %d, %s", 
         tid, job->xid.objSerial, job->xid.objSlot, 
         result->delta, result->resCode, result->resData);

   return ret;
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
   dnxDebug(1, "Worker[%lx]: Terminating", pthread_self());
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
      DnxNodeRequest msg;
      DnxJob job;
      int ret;

      pthread_testcancel();
      
      ws->reqserial++;  // increment request serial number

      // setup job request message
      dnxMakeXID(&msg.xid, DNX_OBJ_WORKER, ws->reqserial, tid);
      msg.reqType = DNX_REQ_REGISTER;
      msg.jobCap = 1;
      msg.ttl = ws->iwlm->cfg.reqTimeout - ws->iwlm->cfg.ttlBackoff;

      // request a job
      if ((ret = dnxWantJob(ws->dispatch, &msg, 0)) != DNX_OK)
      {
         switch (ret)
         {
            case DNX_ERR_SEND:
            case DNX_ERR_TIMEOUT:
               dnxSyslog(LOG_ERR, "Worker[%lx]: Unable to contact server: %d", 
                     tid, ret, dnxErrorString(ret));
               break;
            default:
               dnxSyslog(LOG_ERR, "Worker[%lx]: dnxWantJob failure, %d: %s", 
                     tid, ret, dnxErrorString(ret));
         }
      }
      else if ((ret = dnxGetJob(ws->dispatch, &job, job.address, 
            ws->iwlm->cfg.reqTimeout)) != DNX_OK)
      {
         switch (ret)
         {
            case DNX_ERR_TIMEOUT:  
               break;                  // Timeout is OK here
            case DNX_ERR_RECEIVE:
               dnxSyslog(LOG_ERR, "Worker[%lx]: Unable to contact server, %d: %s", 
                     tid, ret, dnxErrorString(ret));
               break;
            default:
               dnxSyslog(LOG_ERR, "Worker[%lx]: dnxGetJob failed, %d: %s", 
                     tid, ret, dnxErrorString(ret));
         }
      }
      else
      {
         DnxResult result;
         if ((ret = dnxExecuteJob(ws, &job, &result)) != DNX_OK)
            dnxSyslog(LOG_ERR, "Worker[%lx]: Job execution failed, %d: %s", 
                  tid, ret, dnxErrorString(ret));
         else if ((ret = dnxPutResult(ws->collect, &result, 0)) != DNX_OK)
            dnxSyslog(LOG_ERR, "Worker[%lx]: Result posting failed, %d: %s", 
                  tid, ret, dnxErrorString(ret));
         xfree(result.resData);
      }
      if (ret != DNX_OK)
      {
         // if exceeded max retries and above pool minimum...
         if (retries++ >= ws->iwlm->cfg.maxRetries
               && ws->iwlm->threads > ws->iwlm->cfg.poolMin)
         {
            dnxSyslog(LOG_INFO, "Worker[%lx]: Exiting - max retries exceeded", tid);
            break;
         }
      }
      else
         retries = 0;
   }
   pthread_cleanup_pop(1);
   return 0;
}

/*--------------------------------------------------------------------------
                     WORK LOAD MANAGER IMPLEMENTATION
  --------------------------------------------------------------------------*/

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
   if ((ret = dnxConnect(szChan, &ws->dispatch, DNX_CHAN_ACTIVE)) != DNX_OK)
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
   if ((ret = dnxConnect(szChan, &ws->collect, DNX_CHAN_ACTIVE)) != DNX_OK)
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

/** Grow the thread pool by the specified number of threads.
 * 
 * This routine calculates an appropriate increment number. If the current
 * number of threads is less than the requestd initial pool size, then the 
 * pool is grown to the initial pool size, otherwise it tried to grow by the 
 * configure pool growth number.
 * 
 * @param[in] iwlm - a reference to the work load manager whose thread 
 *    pool size is to be increased.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int growThreadPool(iDnxWlm * iwlm)
{
   int i, add, growsz;

   // set additional thread count
   growsz = add = (int)(iwlm->threads < iwlm->cfg.poolInitial ? 
         iwlm->cfg.poolInitial - iwlm->threads : iwlm->cfg.poolGrow);

   // fill as many empty slots as we can or need to
   for (i = iwlm->threads; i < iwlm->poolsz && add > 0; i++, add--)
   {
      int ret;
      if ((ret = workerCreate(iwlm, &iwlm->pool[i])) != 0)
         return ret;
      iwlm->threads++;
      iwlm->tcreated++;
   }
   dnxSyslog(LOG_INFO, "WLM: Increased thread pool by %d", growsz - add);
   return 0;
}

//----------------------------------------------------------------------------

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
}

//----------------------------------------------------------------------------

/** Stop all worker threads and delete all worker thread resources.
 * 
 * @param[in] iwlm - the WLM data structure containing information about 
 *    the thread pool to be deleted.
 */
static void shutdownThreadPool(iDnxWlm * iwlm)
{
   unsigned i;

   assert(iwlm);

   // cancel all workers
   for (i = 0; i < iwlm->threads; i++)
      if (iwlm->pool[i]->state == DNX_THREAD_RUNNING)
      {
         dnxDebug(1, "WLM: Cancelling worker[%lx]", iwlm->pool[i]->tid);
         pthread_cancel(iwlm->pool[i]->tid);
      }

   // join all workers, free the ptr array
   cleanThreadPool(iwlm);
   xfree(iwlm->pool);
}

//----------------------------------------------------------------------------

/** WLM thread clean-up routine.
 * 
 * @param[in] data - an opaque pointer to the the WLM data structure.
 */
static void dnxWlmCleanup(void * data)
{
   assert(data);
   dnxSyslog(LOG_INFO, "WLM: Beginning termination sequence");
   shutdownThreadPool((iDnxWlm *)data);
   dnxSyslog(LOG_INFO, "WLM: Termination sequence complete");
}

//----------------------------------------------------------------------------

/** The main thread routine for the work load manager.
 * 
 * @param[in] data - an opaque pointer to the global data structure containing 
 *    information about the work load manager thread.
 * 
 * @return Always returns 0.
 */
static void * dnxWlm(void * data)
{
   iDnxWlm * iwlm = (iDnxWlm *)data;
   int ret;

   assert(data);

   dnxSyslog(LOG_INFO, "WLM: Work Load Manager thread started");

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);
   pthread_cleanup_push(dnxWlmCleanup, data);

   // create initial worker thread pool
   if ((ret = growThreadPool(iwlm)) != 0)
   {
      dnxSyslog(LOG_ERR, "WLM: Unable to create thread pool, %d: %s", 
            ret, dnxErrorString(ret));
      return 0;
   }

   // while not shutting down or (still have threads 
   //    and not reached shutdown grace period)
   while (!iwlm->terminate || iwlm->threads && time(0) < iwlm->termexpires)
   {
      struct timeval now;
      struct timespec timeout;

      DNX_PT_MUTEX_LOCK(&iwlm->mutex);

      gettimeofday(&now, 0);
      timeout.tv_sec = now.tv_sec + iwlm->cfg.pollInterval;
      timeout.tv_nsec = now.tv_usec * 1000;

      pthread_cond_timedwait(&iwlm->cond, &iwlm->mutex, &timeout);

      cleanThreadPool(iwlm);  // join all zombies

      // if not shutting down and (all threads busy 
      //    or thread count less than initial)
      if (!iwlm->terminate && (iwlm->active == iwlm->threads 
            || iwlm->threads < iwlm->cfg.poolInitial))
         growThreadPool(iwlm);

      dnxDebug(1, "WLM: Active threads: %d; Busy: %d", iwlm->threads, iwlm->active);

      DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
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
   int rc;

   assert(cfg && pwlm);
   assert(cfg->poolMin > 0);
   assert(cfg->poolMax >= cfg->poolMin);
   assert(cfg->poolInitial >= cfg->poolMin);
   assert(cfg->poolInitial <= cfg->poolMax);

   if ((iwlm = (iDnxWlm *)xmalloc(sizeof *iwlm)) == 0)
      return DNX_ERR_MEMORY;

   memset(iwlm, 0, sizeof *iwlm);
   iwlm->cfg = *cfg;
   iwlm->cfg.dispatcher = xstrdup(iwlm->cfg.dispatcher);
   iwlm->cfg.collector = xstrdup(iwlm->cfg.collector);
   iwlm->poolsz = iwlm->cfg.poolMax;
   iwlm->pool = (DnxWorkerStatus **)xmalloc(iwlm->poolsz * sizeof *iwlm->pool);
   memset(iwlm->pool, 0, iwlm->poolsz * sizeof *iwlm->pool);

   if (!iwlm->cfg.dispatcher || !iwlm->cfg.collector || !iwlm->pool)
   {
      xfree(iwlm->cfg.dispatcher);
      xfree(iwlm->cfg.collector);
      xfree(iwlm);
      return DNX_ERR_MEMORY;
   }

   DNX_PT_MUTEX_INIT(&iwlm->mutex);
   pthread_cond_init(&iwlm->cond, 0);

   if ((rc = pthread_create(&iwlm->tid, 0, dnxWlm, iwlm)) != 0)
   {
      dnxSyslog(LOG_ERR, 
            "WLMCreate: Failed to create Work Load Manager thread, %d: %s", 
            rc, dnxErrorString(rc));
      DNX_PT_COND_DESTROY(&iwlm->cond);
      DNX_PT_MUTEX_DESTROY(&iwlm->mutex);
      xfree(iwlm);
      return DNX_ERR_THREAD;
   }

   dnxSyslog(LOG_INFO, "WLMCreate: Created WLM thread %lx", iwlm->tid);

   *pwlm = (DnxWlm *)iwlm;

   return DNX_OK;
}

//----------------------------------------------------------------------------

void dnxWlmDestroy(DnxWlm * wlm)
{
   iDnxWlm * iwlm = (iDnxWlm *)wlm;

   assert(wlm);

   dnxDebug(1, "WLMDestroy: Signaling termination to WLM thread %lx", iwlm->tid);

   DNX_PT_MUTEX_LOCK(&iwlm->mutex);

   // add now to the grace period, set the termination flag, and signal all
   iwlm->termexpires = iwlm->cfg.shutdownGrace + time(0);
   iwlm->terminate = 1;
   pthread_cond_signal(&iwlm->cond);

   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);

   dnxDebug(1, "WLMDestroy: Waiting to join WLM thread %lx", iwlm->tid);

   pthread_join(iwlm->tid, 0);

   assert(iwlm->threads == 0);

   DNX_PT_COND_DESTROY(&iwlm->cond);
   DNX_PT_MUTEX_DESTROY(&iwlm->mutex);

   xfree(iwlm);
}

/*--------------------------------------------------------------------------*/

