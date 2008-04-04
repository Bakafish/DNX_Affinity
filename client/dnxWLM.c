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
#include "dnxWorker.h"

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <syslog.h>
#include <errno.h>

struct iDnxWLM_;                 // forward declaration: circular reference

/** A value that indicates that current state of a pool thread. */
typedef enum DnxThreadState_ 
{
   DNX_THREAD_DEAD = 0,
   DNX_THREAD_RUNNING,
   DNX_THREAD_ZOMBIE
} DnxThreadState;

/** A structure that describes the attribues of a pool thread. */
typedef struct DnxWorkerStatus_ 
{
   DnxThreadState state;         /*!< Current thread state. */
   pthread_t tid;                /*!< Thread ID. */
   DnxChannel * dispatch;        /*!< Thread job request channel. */
   DnxChannel * collect;         /*!< Thread job reply channel. */
   time_t thdstart;              /*!< Thread start time. */
   time_t jobstart;              /*!< Current job start time. */
   time_t jobtime;               /*!< Total amount of time spent in job processing. */
   unsigned jobsok;              /*!< Total jobs completed. */
   unsigned jobsfail;            /*!< Total jobs not completed. */
   unsigned retries;             /*!< Total communications retries. */
   unsigned long req_serial;     /*!< Request tracking serial number. */
   struct iDnxWLM_ * iwlm;       /*!< A reference to the owning WLM. */
} DnxWorkerStatus;

/** The implementation of a work load manager object. */
typedef struct iDnxWLM_
{
   int terminate;             /*!< WLM termination flag. */
   time_t term_expires;       /*!< WLM termination expiration time. */

   unsigned pool_init;        /*!< The initial thread pool size. */
   unsigned pool_min;         /*!< The minimum size of the thread pool. */
   unsigned pool_max;         /*!< The maximum size of the thread pool. */
   unsigned pool_incr;        /*!< The number of threads by which to grow. */
   unsigned poll_int;         /*!< The WLM poll interval. */

   unsigned active_jobs;      /*!< The current number of active jobs. */
   unsigned active_threads;   /*!< The current number of active threads. */
   unsigned th_created;       /*!< The number of threads created. */
   unsigned th_destroyed;     /*!< The number of threads destroyed. */

   unsigned * pdebug;         /*!< A reference to the global debug level. */

   pthread_t tid;             /*!< Work load manager thread id. */
   pthread_mutex_t mutex;     /*!< WLM thread sync mutex. */
   pthread_cond_t cond;       /*!< WLM thread sync condition variable. */

   DnxWorkerStatus * pool;    /*!< The thread pool context list. */
} iDnxWLM;

//----------------------------------------------------------------------------

/** Delete all threads and thread resources in the thread pool.
 * 
 * @param[in] wlm - the global data structure containing information about 
 *    the thread pool to be deleted.
 */
static void deleteThreadPool(iDnxWLM * iwlm)
{
   int i;

   assert(iwlm);

   if (iwlm->pool == 0) return;

   // cancel all worker threads
   for (i = 0; i < iwlm->pool_max; i++)
   {
      if (iwlm->pool[i].state == DNX_THREAD_RUNNING)
      {
         if (*iwlm->pdebug)
            syslog(LOG_DEBUG, "WLM: Cancelling worker thread %lx", 
                  iwlm->pool[i].tid);

         if (pthread_cancel(iwlm->pool[i].tid) != 0)
         {
            // no such thread, for some reason - just clear it
            iwlm->pool[i].state = DNX_THREAD_DEAD;
            iwlm->pool[i].tid = 0;
         }
      }
   }

   // join all cancelled worker threads
   for (i = 0; i < iwlm->pool_max; i++)
   {
      if (iwlm->pool[i].state != DNX_THREAD_DEAD)
      {
         if (*iwlm->pdebug)
            syslog(LOG_DEBUG, "WLM: Waiting to join worker thread %lx", 
                  iwlm->pool[i].tid);
         if (pthread_join(gData->tPool[i].tid, NULL) != 0)
            syslog(LOG_ERR, "WLM: Failed to join worker thread %lx: %d", 
                  iwlm->pool[i].tid, errno);

         iwlm->pool[i].state = DNX_THREAD_DEAD;
         iwlm->pool[i].tid = 0;
      }
   }
   xfree(iwlm->pool);
}

//----------------------------------------------------------------------------

/** Grow the thread pool by the specified number of threads.
 * 
 * @param[in] iwlm - a reference to the work load manager whose thread 
 *    pool size is to be increased.
 * @param[in] active - the current number of active pool threads. This value
 *    is used to calculate an appropriate increment number. If the current
 *    number of active threads is less than the requestd initial pool size, 
 *    then the pool is grown to the initial pool size.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int growThreadPool(iDnxWLM * iwlm, int active)
{
   int i, addThreads, growSize;

   // set additional thread count
   growSize = addThreads = (int)(active < iwlm->pool_init ? 
         iwlm->pool_init - active : iwlm->pool_incr);

   // scan for empty pool slots
   for (i = 0; i < iwlm->pool_max && addThreads > 0; i++, addThreads--)
   {
      // find an empty slot
      if (iwlm->pool[i].state == DNX_THREAD_DEAD)
      {
         int ret;

         // clear this thread's local data structure
         memset(&iwlm->pool[i], 0, sizeof *iwlm->pool);

         iwlm->pool[i].state = DNX_THREAD_RUNNING; // set thread state to active
         iwlm->pool[i].data = iwlm;                // allow thread access to wlm

         // create a worker thread
         if ((ret = pthread_create(
               &iwlm->pool[i].tid, 0, dnxWorker, &iwlm->pool[i])) != 0)
         {
            syslog(LOG_ERR, "WLM: Failed to create thread %d; %d: %s", 
                  i, ret, strerror(ret));
            iwlm->pool[i].state = DNX_THREAD_DEAD;
            iwlm->pool[i].tid = 0;
            return DNX_ERR_THREAD;
         }
      }
   }
   syslog(LOG_INFO, "WLM: Increased thread pool by %d", 
         (int)(growSize - addThreads));
   return 0;
}

//----------------------------------------------------------------------------

/** Create the global thread pool.
 * 
 * @param[in] gData - a pointer to the global data structure containing
 *    configuration information about the thread pool to be created.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int createThreadPool(iDnxWLM * iwlm)
{
   int i, ret;

   assert(iwlm);

   // create worker thread pool tracking array
   if ((iwlm->pool = (DnxWorkerStatus *)
         xcalloc(iwlm->pool_max, sizeof *iwlm->pool)) == 0)
      return DNX_ERR_MEMORY;

   // grow the pool to the requested initial pool size
   if ((ret = growThreadPool(iwlm, 0)) != 0)
      deleteThreadPool(iwlm);

   return ret;
}

//----------------------------------------------------------------------------

/** Scan the global work load manager thread pool for dead threads.
 * 
 * Also recalculates the current active thread count.
 * 
 * @param[in] gData - the global data structure containing information about
 *    the pool to be scanned.
 * @param[out] activeThreads - the address of storage for returning the 
 *    new number of active threads in the pool.
 */
static void scanThreadPool(iDnxWLM * iwlm, int * activeThreads)
{
   int i, active = 0;

   // look for zombie threads to join
   for (i = 0; i < iwlm->pool_max; i++)
   {
      if (iwlm->pool[i].state == DNX_THREAD_ZOMBIE)
      {
         int ret;

         if (*iwlm->pdebug)
            syslog(LOG_DEBUG, "WLM: Waiting to join thread %lx", 
                  iwlm->pool[i].tid);

         if ((ret = pthread_join(iwlm->pool[i].tid, 0)) != 0)
            syslog(LOG_ERR, "WLM: Failed to join thread %lx, %d: %s", 
                  iwlm->pool[i].tid, ret, strerror(ret));

         iwlm->pool[i].state = DNX_THREAD_DEAD;
         iwlm->pool[i].tid = 0;
      }
      else if (iwlm->pool[i].state == DNX_THREAD_RUNNING)
         active++;
   }
   *activeThreads = active;
}

//----------------------------------------------------------------------------

/** WLM thread clean-up routine.
 * 
 * @param[in] data - an opaque pointer to the the WLM data structure.
 */
static void dnxWLMCleanup(void * data)
{
   assert(data);

   syslog(LOG_INFO, "WLM: Beginning termination sequence");

   deleteThreadPool((iDnxWLM *)data);

   syslog(LOG_INFO, "WLM: Termination sequence complete");
}

//----------------------------------------------------------------------------

/** The main thread routine for the work load manager.
 * 
 * @param[in] data - an opaque pointer to the global data structure containing 
 *    information about the work load manager thread.
 * 
 * @return Always returns NULL.
 */
static void * dnxWLM(void * data)
{
   iDnxWLM * iwlm = (iDnxWLM *)data;
   struct timeval now;        // Time when we started waiting
   struct timespec timeout;   // Timeout value for the wait function
   int activeThreads;         // Number of currently active worker threads
   int gJobs, gThreads;       // Global Thread and Job activity counters
   int ret = 0;

   assert(data);

   syslog(LOG_INFO, "WLM: Work Load Manager thread started");

   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 0);
   pthread_cleanup_push(dnxWLMCleanup, data);

   // create initial worker thread pool
   if ((ret = createThreadPool(iwlm)) != DNX_OK)
   {
      syslog(LOG_ERR, "WLM: Unable to create thread pool, %d: %s", 
            ret, dnxErrorString(ret));
      return 0;
   }

   // Wait for new service checks or cancellation
   while (1)
   {
      if (*iwlm->pdebug)
         syslog(LOG_DEBUG, "WLM: Waiting on condition variable");

      // Monitor thread pool performance
      DNX_PT_MUTEX_LOCK(&iwlm->mutex);

      gettimeofday(&now, 0);

      // timeval uses micro-seconds, timespec uses nano-seconds.
      timeout.tv_sec = now.tv_sec + gData->wlmPollInterval;
      timeout.tv_nsec = now.tv_usec * 1000;

      // sleep for the specified time
      ret = pthread_cond_timedwait(&iwlm->cond, &iwlm->mutex, &timeout);

      // log appropriate debug messages if requested
      if (*iwlm->pdebug)
      {
         if (ret == ETIMEDOUT)
            syslog(LOG_DEBUG, "WLM: Awoke due to timeout");
         else if (iwlm->terminate)
            syslog(LOG_DEBUG, 
                  "WLM: Awoke due to shutdown initiation: now=%lu, max=%lu", 
                  time(0), iwlm->term_expires);
         else
            syslog(LOG_DEBUG, "WLM: Awoke due to UNKNOWN condition!");
      }

      // see if we are in shutdown mode
      if (iwlm->terminate)
      {
         // see if we have reached the max shutdown time
         if (time(0) >= iwlm->term_expires)
         {
            if (*iwlm->pdebug)
               syslog(LOG_DEBUG, "WLM: Exiting - reached max shutdown wait time");
            break;
         }
      }
      else     // otherwise, see if we need to increase the thread pool
      {
         gThreads = dnxGetThreadsActive();
         gJobs = dnxGetJobsActive();
         if (iwlm->active_jobs == iwlm->active_threads || gThreads < iwlm->pool_init)
            growThreadPool(gData, gThreads);
      }

      // scan the thread pool for zombie threads to cleanup
      scanThreadPool(iwlm, &activeThreads);

      // exit if there are no active worker threads
      if (activeThreads == 0)
      {
         if (*iwlm->pdebug)
            syslog(LOG_DEBUG, "WLM: Exiting - no active worker threads");
         break;
      }

      if (*iwlm->pdebug)
         syslog(LOG_DEBUG, "WLM: Active thread count: %d (%d); Busy: %d", 
               activeThreads, gThreads, gJobs);

      DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
   }

   pthread_cleanup_pop(1);

   return 0;
}

//----------------------------------------------------------------------------

/** Return the active thread count on the specified Work Load Manager.
 * 
 * @param[in] wlm - the Work Load Manager whose active thread count should be
 *    returned.
 * 
 * @return The active thread count on @p wlm.
 */
int dnxWLMGetActiveThreads(DnxWLM * wlm)
{
   int active;
   iDnxWLM * iwlm = (iDnxWLM *)wlm;
   assert(wlm);
   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   active = iwlm->active_threads;
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
   return active;
}

//----------------------------------------------------------------------------

/** Increment or decrement the active thread count on the specified WLM. 
 * 
 * @param[in] wlm - the Work Load Manager whose active thread count should be
 *    returned.
 * @param[in] value - an increment/decrement indicator. If @p value is 
 *    positive, then the thread count will be incremented; if it's negative
 *    then the thread count will be decremented. Zero is not allowed.
 */
void dnxWLMSetActiveThreads(DnxWLM * wlm, int value)
{
   iDnxWLM * iwlm = (iDnxWLM *)wlm;
   assert(wlm && value != 0);
   value = value > 0 ? 1 : -1;
   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   iwlm->active_threads += value;
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
}

//----------------------------------------------------------------------------

/** Return the active job count on the specified Work Load Manager.
 * 
 * @param[in] wlm - the Work Load Manager whose active job count should be
 *    returned.
 * 
 * @return The active job count on @p wlm.
 */
int dnxWLMGetActiveJobs(DnxWLM * wlm)
{
   int active;
   iDnxWLM * iwlm = (iDnxWLM *)wlm;
   assert(wlm);
   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   active = iwlm->active_jobs;
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
   return active;
}

//----------------------------------------------------------------------------

/** Increment or decrement the active job count on the specified WLM. 
 * 
 * @param[in] wlm - the Work Load Manager whose active job count should be
 *    returned.
 * @param[in] value - an increment/decrement indicator. If @p value is 
 *    positive, then the job count will be incremented; if it's negative
 *    then the job count will be decremented. Zero is not allowed.
 */
void dnxWLMSetActiveJobs(DnxWLM * wlm, int value)
{
   iDnxWLM * iwlm = (iDnxWLM *)wlm;
   assert(wlm && value != 0);
   value = value > 0 ? 1 : -1;
   DNX_PT_MUTEX_LOCK(&iwlm->mutex);
   iwlm->active_jobs += value;
   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);
}

//----------------------------------------------------------------------------

/** Creates a new Work Load Manager object.
 * 
 * @param[in] minsz - the minimum number of threads (must be <= maxsz).
 * @param[in] initsz - the initial thread count (must be >= minsz, <= maxsz).
 * @param[in] maxsz - the maximum number of threads (must be >= minsz).
 * @param[in] incrsz - the number of threads by which to grow, when necessary.
 * @param[in] term_grace - the grace period in seconds we're giving the work
 *    load manager to have all worker threads stopped.
 * @param[in] pdebug - a reference to the global debug flag.
 * @param[out] pwlm - the address of storage for the returned WLM object.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxWLMCreate(unsigned minsz, unsigned initsz, unsigned maxsz, 
      unsigned incrsz, unsigned term_grace, unsigned * pdebug, DnxWLM ** pwlm)
{
   iDnxWLM * iwlm;
   int rc;

   assert(minsz > 0);
   assert(maxsz >= minsz && initsz >= minsz && initsz <= maxsz);
   assert(pdebug);
   assert(pwlm);

   if ((iwlm = (iDnxWLM *)xmalloc(sizeof *iwlm)) == 0)
      return DNX_ERR_MEMORY;

   memset(iwlm, 0, sizeof *iwlm);
   iwlm->term_expires = term_grace;
   iwlm->pool_min = minsz;
   iwlm->pool_init = initsz;
   iwlm->pool_max = maxsz;
   iwlm->pool_incr = incrsz;
   iwlm->pdebug = pdebug;

   DNX_PT_MUTEX_INIT(&iwlm->mutex);
   pthread_cond_init(&iwlm->cond, 0);

   if ((rc = pthread_create(&iwlm->tid, 0, dnxWLM, iwlm)) != 0)
   {
      syslog(LOG_ERR, "dnxWLMCreate: Failed to create "
                      "Work Load Manager thread, %d: %s", 
            rc, dnxErrorString(rc));
      DNX_PT_COND_DESTROY(&iwlm->cond);
      DNX_PT_MUTEX_DESTROY(&iwlm->mut);
      xfree(iwlm);
      return DNX_ERR_THREAD;
   }

   syslog(LOG_INFO, "WLMCreate: Created WLM thread %lx", iwlm->tid);

   *pwlm = (DnxWLM *)iwlm;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** The main thread routine for the work load manager.
 * 
 * @param[in] wlm - the work load manager object to be destroyed.
 */
void dnxWLMDestroy(DnxWLM * wlm)
{
   iDnxWLM * iwlm = (iDnxWLM *)wlm;
   int rc;

   assert(wlm);

   if (*iwlm->pdebug)
      syslog(LOG_DEBUG, "WLMDestroy: Signaling termination "
                        "condition to WLM thread %lx", iwlm->tid);

   DNX_PT_MUTEX_LOCK(&iwlm->mutex);

   // add now to the grace period, set the termination flag, and signal all
   iwlm->term_expires += time(0);
   iwlm->terminate = 1;
   pthread_cond_signal(&iwlm->cond);

   DNX_PT_MUTEX_UNLOCK(&iwlm->mutex);

   // wait for the WLM thread to exit
   if (*iwlm->pdebug)
      syslog(LOG_DEBUG, "WLMDestroy: Waiting to join WLM thread %lx", 
            iwlm->tid);

   if ((rc = pthread_join(iwlm->tid, 0)) != 0)
      syslog(LOG_ERR, "WLMDestroy: pthread_join(Agent) failed, %d: %s", 
            rc, dnxErrorString(rc));

   // wait for all threads to be gone...
   while (dnxGetThreadsActive() > 0)
      sleep(1);   /** @todo Switch to nanosleep to get better granularity. */

   DNX_PT_COND_DESTROY(&iwlm->cond);
   DNX_PT_MUTEX_DESTROY(&iwlm->mutex);

   xfree(iwlm);
}

/*--------------------------------------------------------------------------*/

