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

/** Implements the DNX Job List mechanism.
 *
 * @file dnxJobList.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IMPL
 */

#include "dnxJobList.h"

#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxLogging.h"
#include "dnxTimer.h"

/** The JobList implementation data structure. */
typedef struct iDnxJobList_ 
{
   DnxNewJob * pList;      /*!< Array of Job Structures. */
   unsigned long size;     /*!< Number of elements. */
   unsigned long head;     /*!< List head. */
   unsigned long tail;     /*!< List tail. */
   unsigned long dhead;    /*!< Head of waiting jobs. */
   pthread_mutex_t mut;    /*!< The job list mutex. */
   pthread_cond_t cond;    /*!< the job list condition variable. */
   DnxTimer * timer;       /*!< The job list expiration timer. */
} iDnxJobList;

//----------------------------------------------------------------------------

/** Add a job to a job list.
 * 
 * This routine is invoked by the DNX NEB module's Service Check handler
 * to add new service check requests (i.e., a "job") to the Job List.
 * 
 * Jobs are marked as Waiting to be dispatched to worker nodes (via the
 * Dispatcher thread.)
 *
 * @param[in] pJobList - the job list to which @p pJob should be added.
 * @param[in] pJob - the job to be added to @p pJobList.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxJobListAdd(DnxJobList * pJobList, DnxNewJob * pJob)
{
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   unsigned long tail;
   int ret = DNX_OK;

   assert(pJobList && pJob);

   DNX_PT_MUTEX_LOCK(&ilist->mut);

   tail = ilist->tail;

   // verify space in the job list
   if (ilist->pList[tail].state 
         && (tail = (tail + 1) % ilist->size) == ilist->head)
   {
      dnxSyslog(LOG_ERR, "dnxJobListAdd: Out of job slots (max=%lu): %s", 
            ilist->size, pJob->cmd);
      ret = DNX_ERR_CAPACITY;
      goto abend;
   }

   // add the slot identifier to the Job's GUID
   pJob->guid.objSlot = tail;
   pJob->state = DNX_JOB_PENDING;

   // add this job to the job list
   memcpy(&ilist->pList[tail], pJob, sizeof(DnxNewJob));

   // update dispatch head index
   if (ilist->pList[ilist->tail].state != DNX_JOB_PENDING)
      ilist->dhead = tail;

   ilist->tail = tail;

   dnxDebug(8, "dnxJobListAdd: Job [%lu,%lu]: Head=%lu, DHead=%lu, Tail=%lu", 
         pJob->guid.objSerial, pJob->guid.objSlot, ilist->head, 
         ilist->dhead, ilist->tail);

   pthread_cond_signal(&ilist->cond);  // signal that a new job is available

abend:

   DNX_PT_MUTEX_UNLOCK(&ilist->mut);

   return ret;
}

//----------------------------------------------------------------------------

/** Expire a set of old jobs from a job list.
 * 
 * This routine is invoked by the Timer thread to dequeue all jobs whose
 * timeout has occurred.
 * 
 * Note that this routine walks the *entire* Job List and can remove jobs
 * that are either InProgress (awaiting a result) or Pending (awaiting dispatch.)
 * 
 * Jobs that are deemed to have expired are passed to the Timer thread
 * via a call back mechanism (for efficiency.)
 *
 * @param[in] pJobList - the job list from which to expire old jobs.
 * @param[out] pExpiredJobs - the address of storage in which to return 
 *    a list of up to @p totalJobs jobs.
 * @param[in,out] totalJobs - on entry, contains the maximum size of the 
 *    array pointed to by pExpiredJobs; on exit, contains the number of jobs
 *    stored in the pExpiredJobs array.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxJobListExpire(DnxJobList * pJobList, DnxNewJob * pExpiredJobs, 
      int * totalJobs)
{
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   unsigned long current;
   DnxNewJob * pJob;
   int jobCount;
   time_t now;

   assert(pJobList && pExpiredJobs && totalJobs && *totalJobs > 0);

   DNX_PT_MUTEX_LOCK(&ilist->mut);

   // get the current time (after we acquire the lock! In case we had to wait)
   now = time(0);

   // walk the entire job list - InProgress and Pending jobs (in that order)
   current = ilist->head;
   jobCount = 0;
   while (jobCount < *totalJobs)
   {
      // only examine jobs that are either awaiting dispatch or results
      if ((pJob = &ilist->pList[current])->state == DNX_JOB_INPROGRESS 
            || pJob->state == DNX_JOB_PENDING)
      {
         // check the job's expiration stamp
         if (pJob->expires > now)
            break;   // Bail-out: this job hasn't expired yet

         // job has expired - add it to the expired job list
         memcpy(&pExpiredJobs[jobCount], pJob, sizeof(DnxNewJob));

         pJob->state = DNX_JOB_NULL;

         jobCount++;
      }

      // bail-out if this was the job list tail
      if (current == ilist->tail)
         break;

      // increment the job list index
      current = (current + 1) % ilist->size;
   }

   ilist->head = current;

   // if this job is awaiting dispatch, then it is the new dispatch head
   if (ilist->pList[current].state != DNX_JOB_INPROGRESS)
      ilist->dhead = current;

   // update the total jobs in the expired job list
   *totalJobs = jobCount;

   DNX_PT_MUTEX_UNLOCK(&ilist->mut);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Select a dispatchable job from a job list.
 * 
 * This routine is invoked by the Dispatcher thread to select the next
 * job waiting to be dispatched to a worker node.
 * 
 * The job is *not* removed from the Job List, but is marked as InProgress;
 * that is, it is waiting for the results from the service check.
 *
 * @param[in] pJobList - the job list from which to select a dispatchable job.
 * @param[out] pJob - the address of storage in which to return data about the
 *    job to be dispatched. Makes a copy of the job in the job list and stores
 *    the copy in the @p pJob parameter.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxJobListDispatch(DnxJobList * pJobList, DnxNewJob * pJob)
{
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   unsigned long current;

   assert(pJobList && pJob);

   DNX_PT_MUTEX_LOCK(&ilist->mut);

   // wait on the condition variable if there are no pending jobs

   /** @todo Need to track total number of Pending jobs in the JobList structure?
    * OR simply just check to see if the dhead index points to a valid
    * Pending job?
    */

   // start at current dispatch head
   current = ilist->dhead;

   // see if we have a Pending job
   while (ilist->pList[current].state != DNX_JOB_PENDING)
   {
      dnxDebug(8, "dnxJobListDispatch: BEFORE: Head=%lu, DHead=%lu, Tail=%lu", 
            ilist->head, ilist->dhead, ilist->tail);
      pthread_cond_wait(&ilist->cond, &ilist->mut);
      current = ilist->dhead;
   }

   // transition this job's state to InProgress
   ilist->pList[current].state = DNX_JOB_INPROGRESS;

   // make a copy for the Dispatcher
   memcpy(pJob, &ilist->pList[current], sizeof(DnxNewJob));
   //pJob->cmd = xstrdup(pJob->cmd); // BUG: This causes a memory leak!

   // update the dispatch head
   if (ilist->dhead != ilist->tail)
      ilist->dhead = (current + 1) % ilist->size;

   dnxDebug(8, "dnxJobListDispatch: AFTER: Job [%lu,%lu]: "
               "Head=%lu, DHead=%lu, Tail=%lu", 
         pJob->guid.objSerial, pJob->guid.objSlot, 
         ilist->head, ilist->dhead, ilist->tail);

   DNX_PT_MUTEX_UNLOCK(&ilist->mut);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Locate a pending job to which collected results should apply.
 * 
 * This routine is invoked by the Collector thread to dequeue a job from
 * the Job List when its service check result has been posted by a worker
 * node.
 * 
 * The job *is* removed from the the Job List.
 * 
 * @param[in] pJobList - the job list from which to obtain the pending job.
 * @param[in] pGuid - the unique identifier for this pending job.
 * @param[out] pJob - the address of storage in which to return collected 
 *    result information about the job belonging to @p pGuid.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxJobListCollect(DnxJobList * pJobList, DnxGuid * pGuid, DnxNewJob * pJob)
{
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   unsigned long current;
   int ret = DNX_OK;

   assert(pJobList && pGuid && pJob);  // parameter validation

   current = pGuid->objSlot;

   assert(current < ilist->size);
   if (current >= ilist->size)         // runtime validation requires check
      return DNX_ERR_INVALID;

   DNX_PT_MUTEX_LOCK(&ilist->mut);

   dnxDebug(8, "dnxJobListCollect: Compare [%lu,%lu] to "
               "[%lu,%lu]: Head=%lu, DHead=%lu, Tail=%lu", 
      pGuid->objSerial, pGuid->objSlot, ilist->pList[current].guid.objSerial, 
      ilist->pList[current].guid.objSlot, ilist->head, ilist->dhead, ilist->tail);

   // verify that the GUID of this result matches the GUID of the service check
   if (ilist->pList[current].state == DNX_JOB_NULL 
         || memcmp(pGuid, &ilist->pList[current].guid, sizeof(DnxGuid)) != 0)
   {
      // most likely, this job expired and was removed by the Timer thread
      ret = DNX_ERR_NOTFOUND;
      goto abend;
   }

   // make a copy for the Collector
   memcpy(pJob, &ilist->pList[current], sizeof(DnxNewJob));
   pJob->state = DNX_JOB_COMPLETE;

   // dequeue this job
   ilist->pList[current].state = DNX_JOB_NULL;

   // update the job list head
   if (current == ilist->head && current != ilist->tail)
      ilist->head = ((current + 1) % ilist->size);

abend:

   DNX_PT_MUTEX_UNLOCK(&ilist->mut);

   return ret;
}

//----------------------------------------------------------------------------

/** Create a new job list.
 * 
 * This routine is invoked by the DNX NEB module's initialization routine to 
 * create the DNX Job List.
 * 
 * The Job List contains a time-ordered list of service check requests 
 * (i.e., "jobs") that are either waiting to be dispatched to a worker node 
 * for execution (state = Waiting) or are already executing on a worker node 
 * and are pending the service check result from the worker node (state = 
 * Pending).
 * 
 * @param[out] ppJobList - the address of storage for returning a new job
 *    list object pointer.
 * @param[in] size - the initial size of the job list to be created.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxJobListCreate(DnxJobList ** ppJobList, unsigned long size)
{
   iDnxJobList * ilist;
   int ret;

   assert(ppJobList && size);

   if ((ilist = (iDnxJobList *)xmalloc(sizeof *ilist)) == 0
         || (ilist->pList = (DnxNewJob *)xmalloc(sizeof *ilist->pList * size)) == 0)
   {
      xfree(ilist);
      return DNX_ERR_MEMORY;
   }
   memset(ilist, 0, sizeof *ilist);
   memset(ilist->pList, 0, sizeof *ilist->pList * size);

   ilist->size = size;

   DNX_PT_MUTEX_INIT(&ilist->mut);
   pthread_cond_init(&ilist->cond, 0);

   if ((ret = dnxTimerCreate((DnxJobList *)ilist, &ilist->timer)) != 0)
   {
      DNX_PT_COND_DESTROY(&ilist->cond);
      DNX_PT_MUTEX_DESTROY(&ilist->mut);
      xfree(ilist->pList);
      xfree(ilist);
      return ret;
   }

   *ppJobList = (DnxJobList *)ilist;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Destroy a job list.
 * 
 * This routine is invoked by the DNX NEB module's de-initialization routine
 * to release and remove the DNX Job List.
 * 
 * @param[in] pJobList - refers to the job list to be destroyed.
 */
void dnxJobListDestroy(DnxJobList * pJobList)
{
   iDnxJobList * ilist = (iDnxJobList *)pJobList;

   assert(pJobList);

   dnxTimerDestroy(ilist->timer);

   DNX_PT_COND_DESTROY(&ilist->cond);
   DNX_PT_MUTEX_DESTROY(&ilist->mut);

   xfree(ilist->pList);
   xfree(ilist);
}

/*--------------------------------------------------------------------------*/

