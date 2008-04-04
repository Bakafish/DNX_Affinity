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
#include "dnxClientMain.h"
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

//----------------------------------------------------------------------------

/** Delete all threads and thread resources in the thread pool.
 * 
 * @param[in] gData - the global data structure containing information about 
 *    the thread pool to be deleted.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int deleteThreadPool(DnxGlobalData * gData)
{
   int i;
   int ret = DNX_OK;

   // Cancel all threads
   for (i=0; i < gData->poolMax; i++)
   {
      if (gData->tPool[i].state == DNX_THREAD_RUNNING)
      {
         // Cancel each thread
         if (gData->debug)
            syslog(LOG_DEBUG, "deleteThreadPool: Canceling thread %lx", gData->tPool[i].tid);
         if (pthread_cancel(gData->tPool[i].tid) != 0)
         {
            syslog(LOG_ERR, "deleteThreadPool: Failed to cancel thread %lx: %d", gData->tPool[i].tid, errno);
            gData->tPool[i].state = DNX_THREAD_DEAD;
            gData->tPool[i].tid = 0;
         }
      }
   }

   // Join all threads
   for (i=0; i < gData->poolMax; i++)
   {
      if (gData->tPool[i].state != DNX_THREAD_DEAD)
      {
         // Join each thread
         if (gData->debug)
            syslog(LOG_DEBUG, "deleteThreadPool: Waiting to join thread %lx", gData->tPool[i].tid);
         if (pthread_join(gData->tPool[i].tid, NULL) != 0)
         {
            syslog(LOG_ERR, "deleteThreadPool: Failed to join thread %lx: %d", gData->tPool[i].tid, errno);
         }

         // Free-up this thread-pool slot
         gData->tPool[i].state = DNX_THREAD_DEAD;
         gData->tPool[i].tid   = (pthread_t)0;
      }
   }

   return ret;
}

//----------------------------------------------------------------------------

/** Dispatch thread clean-up routine.
 * 
 * @param[in] data - an opaque pointer to the the global data structure 
 *    containing information about the thread to be cleaned up.
 */
static void dnxWLMCleanup(void * data)
{
   DnxGlobalData * gData = (DnxGlobalData *)data;

   assert(data);

   syslog(LOG_INFO, "dnxWLMCleanup[%lx]: WLM thread beginning "
                    "termination sequence", pthread_self());

   // Set the termination flag for the worker threads
   gData->terminate = 1;

   // Unlock the mutex
   DNX_PT_MUTEX_UNLOCK(&gData->wlmMutex);

   // Destroy the mutex
   DNX_PT_MUTEX_DESTROY(&gData->wlmMutex);

   // Destroy the condition variable
   DNX_PT_COND_DESTROY(&gData->wlmCond);

   // If the worker thread pool exists...
   if (gData->tPool)
   {
      // Wait for worker threads to exit
      deleteThreadPool(gData);

      // Release thread pool tracking array
      xfree(gData->tPool);
   }
   syslog(LOG_INFO, "dnxWLMCleanup[%lx]: WLM thread "
                    "termination complete", pthread_self());
}

//----------------------------------------------------------------------------

/** Create the global thread pool.
 * 
 * @param[in] gData - a pointer to the global data structure containing
 *    configuration information about the thread pool to be created.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int createThreadPool(DnxGlobalData * gData)
{
   int i;
   int ret = DNX_OK;

   // Clear the termination flag for the worker threads
   gData->terminate = 0;

   for (i=0; i < gData->poolInitial; i++)
   {
      gData->tPool[i].state = DNX_THREAD_RUNNING;  // Set this thread's state to active
      gData->tPool[i].data  = gData;   // Allow thread access to global data

      // Create a worker thread
      if ((ret = pthread_create(&(gData->tPool[i].tid), NULL, dnxWorker, &(gData->tPool[i]))) != 0)
      {
         syslog(LOG_ERR, "createThreadPool: Failed to create thread %d: %d", i, errno);
         gData->tPool[i].state = DNX_THREAD_DEAD;
         gData->tPool[i].tid   = (pthread_t)0;
         ret = DNX_ERR_THREAD;
         break;
      }
   }

   return ret;
}

//----------------------------------------------------------------------------

/** Grow the thread pool by the specified number of threads.
 * 
 * @param[in] gData - the global data structure containing information about
 *    the thread pool to be increased.
 * @param[in] gThreads - the number of threads by which to increase the pool.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int growThreadPool(DnxGlobalData * gData, int gThreads)
{
   int i, addThreads, growSize;
   int ret = DNX_OK;

   // Set additional thread count
   growSize = addThreads = (int)((gThreads < gData->poolInitial) ? 
         (gData->poolInitial - gThreads) : (gData->poolGrow));

   // Scan for empty pool slots
   for (i = 0; i < gData->poolMax && addThreads > 0; i++)
   {
      // Find an empty slot
      if (gData->tPool[i].state == DNX_THREAD_DEAD)
      {
         // Clear this thread's local data structure
         memset(&(gData->tPool[i]), 0, sizeof(DnxWorkerStatus));

         gData->tPool[i].state = DNX_THREAD_RUNNING;  // Set this thread's state to active
         gData->tPool[i].data = gData; // Allow thread access to global data

         // Create a worker thread
         if ((ret = pthread_create(&(gData->tPool[i].tid), NULL, 
               dnxWorker, &(gData->tPool[i]))) != 0)
         {
            syslog(LOG_ERR, "growThreadPool: Failed to create thread %d: %d", i, ret);
            gData->tPool[i].state = DNX_THREAD_DEAD;
            gData->tPool[i].tid = (pthread_t)0;
            ret = DNX_ERR_THREAD;
            break;
         }

         addThreads--;  // Decrement the threads-to-add counter
      }
   }
   syslog(LOG_INFO, "growThreadPool: Increased thread pool by %d", 
         (int)(growSize - addThreads));
   return ret;
}

//----------------------------------------------------------------------------

/** Scan the global work load manager thread pool for dead threads.
 * 
 * @param[in] gData - the global data structure containing information about
 *    the pool to be scanned.
 * @param[out] activeThreads - the address of storage for returning the 
 *    new number of active threads in the pool.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int scanThreadPool(DnxGlobalData * gData, int * activeThreads)
{
   int i;
   int ret = DNX_OK;

   // Clear the active thread counter
   *activeThreads = 0;

   // Look for zombie threads to join
   for (i=0; i < gData->poolMax; i++)
   {
      if (gData->tPool[i].state == DNX_THREAD_ZOMBIE)
      {
         // Join each zombie thread
         if (gData->debug)
            syslog(LOG_DEBUG, "scanThreadPool: Waiting to join thread %lx", 
                  gData->tPool[i].tid);

         if (pthread_join(gData->tPool[i].tid, NULL) != 0)
            syslog(LOG_ERR, "scanThreadPool: Failed to join thread %lx: %d", 
                  gData->tPool[i].tid, errno);

         // Free-up this thread-pool slot
         gData->tPool[i].state = DNX_THREAD_DEAD;
         gData->tPool[i].tid   = (pthread_t)0;
      }
      else if (gData->tPool[i].state == DNX_THREAD_RUNNING)
         (*activeThreads)++;     // Increment active thread count
   }
   return ret;
}

//----------------------------------------------------------------------------

/** The main thread routine for the work load manager.
 * 
 * @param[in] data - an opaque pointer to the global data structure containing 
 *    information about the work load manager thread.
 * 
 * @return Always returns NULL.
 */
void * dnxWLM(void * data)
{
   DnxGlobalData *gData = (DnxGlobalData *)data;
   struct timeval  now;            // Time when we started waiting
   struct timespec timeout;        // Timeout value for the wait function
   int activeThreads;            // Number of currently active worker threads
   int gJobs, gThreads;       // Global Thread and Job activity counters
   int ret = 0;

   assert(data);

   // Announce our presence
   syslog(LOG_INFO, "dnxWLM[%lx]: Work Load Manager thread started", pthread_self());

   // Set my cancel state to 'enabled', and cancel type to 'deferred'
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
   pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

   // Set thread cleanup handler
   pthread_cleanup_push(dnxWLMCleanup, data);

   // Initialize the mutex and condition variable
   DNX_PT_MUTEX_INIT(&gData->wlmMutex);
   pthread_cond_init(&(gData->wlmCond), NULL);

   gData->tPool = NULL;

   // Validate worker thread pool parameters
   if (gData->poolInitial < 1)
   {
      syslog(LOG_ERR, "dnxWLM[%lx]: Invalid initial thread pool size: %ld", pthread_self(), gData->poolInitial);
      goto abend;
   }
   if (gData->poolMax < gData->poolInitial)
   {
      syslog(LOG_ERR, "dnxWLM[%lx]: Thread pool max is less than initial: %ld", pthread_self(), gData->poolMax);
      goto abend;
   }
   if (gData->poolMax < gData->poolMin)
   {
      syslog(LOG_ERR, "dnxWLM[%lx]: Thread pool max is less than min: %ld", pthread_self(), gData->poolMax);
      goto abend;
   }

   // Create worker thread pool tracking array
   if ((gData->tPool = (DnxWorkerStatus *)xcalloc((size_t)(gData->poolMax), sizeof(DnxWorkerStatus))) == NULL)
   {
      syslog(LOG_ERR, "dnxWLM[%lx]: Unable to allocate memory for thread pool", pthread_self());
      goto abend;
   }

   // Create initial worker thread pool
   if ((ret = createThreadPool(gData)) != DNX_OK)
   {
      syslog(LOG_ERR, "dnxWLM[%lx]: Unable to create thread pool", pthread_self());
      goto abend;
   }

   // Wait for new service checks or cancellation
   while (1)
   {
      if (gData->debug)
         syslog(LOG_DEBUG, "dnxWLM[%lx]: Waiting on condition variable", pthread_self());

      // Monitor thread pool performance
      DNX_PT_MUTEX_LOCK(&gData->wlmMutex);

      gettimeofday(&now, NULL);

      // timeval uses micro-seconds.
      // timespec uses nano-seconds.
      // 1 micro-second = 1000 nano-seconds.
      timeout.tv_sec = now.tv_sec + gData->wlmPollInterval;
      timeout.tv_nsec = now.tv_usec * 1000;

      // Sleep for the specified time
      ret = pthread_cond_timedwait(&(gData->wlmCond), &(gData->wlmMutex), &timeout);

      if (gData->debug)
      {
         if (ret == ETIMEDOUT)
            syslog(LOG_DEBUG, "dnxWLM[%lx]: Awoke due to timeout", pthread_self());
         else
         {
            if (gData->terminate)
               syslog(LOG_DEBUG, "dnxWLM[%lx]: Awoke due to shutdown initiation: now=%lu, max=%lu", pthread_self(), time(NULL), gData->noLaterThan);
            else
               syslog(LOG_DEBUG, "dnxWLM[%lx]: Awoke due to UNKNOWN condition!", pthread_self());
         }
      }

      // See if we are in shutdown mode
      if (gData->terminate)
      {
         // See if we have reached the max shutdown time
         //
         // NB: This is used to allow the worker threads some time to finish-up whatever checks
         //     they may be currently working on.
         if (time(NULL) >= gData->noLaterThan)
         {
            if (gData->debug)
               syslog(LOG_DEBUG, "dnxWLM[%lx]: Exiting - reached max shutdown wait time", pthread_self());
            break;
         }
      }
      // Otherwise, see if we need to increase the thread pool
      else 
      {
         gThreads = dnxGetThreadsActive();
         gJobs = dnxGetJobsActive();

         if (gJobs == gThreads || gThreads < gData->poolInitial)
            growThreadPool(gData, gThreads);
      }

      // Scan the thread pool for zombie threads to cleanup
      scanThreadPool(gData, &activeThreads);

      // Exit if there are no active worker threads
      if (activeThreads == 0)
      {
         if (gData->debug)
            syslog(LOG_DEBUG, "dnxWLM[%lx]: Exiting - no active worker threads", pthread_self());
         break;
      }

      if (gData->debug)
         syslog(LOG_DEBUG, "dnxWLM[%lx]: Active thread count: %d (%d)   Busy: %d", pthread_self(), activeThreads, gThreads, gJobs);

      DNX_PT_MUTEX_UNLOCK(&gData->wlmMutex);
   }

abend:;

   // Remove thread cleanup handler
   pthread_cleanup_pop(1);

   // Terminate this thread
   pthread_exit(NULL);

   return 0;
}

/*--------------------------------------------------------------------------*/

