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

#include <sys/time.h>

#define DNX_JOBLIST_TIMEOUT   5     /*!< Wake up to see if we're shutting down. */
#define DNX_TIMER_SLEEP       5000  /*!< Timer sleep interval, in milliseconds */

DnxJobList * joblist; // Fwd declaration

/** The JobList implementation data structure. */
typedef struct iDnxJobList_ 
{
   DnxNewJob * list;       /*!< Array of Job Structures. */
   unsigned long size;     /*!< Number of elements. */
   unsigned long head;     /*!< List head. */
   unsigned long tail;     /*!< List tail. */
   unsigned long dhead;    /*!< Head of waiting jobs. */
   pthread_mutex_t mut;    /*!< The job list mutex. */
   pthread_cond_t cond;    /*!< The job list condition variable. */
   DnxTimer * timer;       /*!< The job list expiration timer. */
} iDnxJobList;


/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

int dnxJobListAdd(DnxJobList * pJobList, DnxNewJob * pJob)
{
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   unsigned long tail;
   int ret = DNX_OK;

   assert(pJobList && pJob);

   DNX_PT_MUTEX_LOCK(&ilist->mut);

   tail = ilist->tail;

   // verify space in the job list
   if (ilist->list[tail].state && (tail = (tail + 1) % ilist->size) == ilist->head)
   {
      dnxLog("dnxJobListAdd: Out of job slots (max=%lu): %s.", 
            ilist->size, pJob->cmd);
      ret = DNX_ERR_CAPACITY;
   }
   else
   {
      // add the slot index to the Job's XID - this allows us to index 
      //    the job list using the returned result's XID.objSlot field
      pJob->xid.objSlot = tail;
      pJob->state = DNX_JOB_PENDING;
   
      // add this job to the job list
      memcpy(&ilist->list[tail], pJob, sizeof *pJob);
   
      // update dispatch head index
      if (ilist->list[ilist->tail].state != DNX_JOB_PENDING)
         ilist->dhead = tail;
   
      ilist->tail = tail;
   
      dnxDebug(8, "dnxJobListAdd: Job [%lu,%lu]: Head=%lu, DHead=%lu, Tail=%lu.", 
            pJob->xid.objSerial, pJob->xid.objSlot, ilist->head, ilist->dhead, 
            ilist->tail);
   
      pthread_cond_signal(&ilist->cond);  // signal that a new job is available
   }

   DNX_PT_MUTEX_UNLOCK(&ilist->mut);

   return ret;
}

int dnxJobListMarkAck(DnxXID * pXid)
{
    int current = 0;
    iDnxJobList * ilist = (iDnxJobList *)joblist;
    DNX_PT_MUTEX_LOCK(&ilist->mut);
    while(current++ <= ilist->size)
    {
        if (dnxEqualXIDs(pXid, &ilist->list[current].xid))
        {
            ilist->list[current].ack = true;
            break;
        }
    }
    DNX_PT_MUTEX_UNLOCK(&ilist->mut);
    return DNX_OK;
}
//----------------------------------------------------------------------------

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
      if ((pJob = &ilist->list[current])->state == DNX_JOB_INPROGRESS 
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
   if (ilist->list[current].state != DNX_JOB_INPROGRESS)
      ilist->dhead = current;

   // update the total jobs in the expired job list
   *totalJobs = jobCount;

   DNX_PT_MUTEX_UNLOCK(&ilist->mut);

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxJobListDispatch(DnxJobList * pJobList, DnxNewJob * pJob)
{
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   unsigned long current;
   int ret = 0;

   assert(pJobList && pJob);

   DNX_PT_MUTEX_LOCK(&ilist->mut);

   // wait on the condition variable if there are no pending jobs

   /** @todo Need to track total number of Pending jobs in the JobList structure?
    * OR simply check to see if the dhead index points to a valid Pending job?
    */

   // start at current dispatch head
   current = ilist->dhead;

   // see if we have a pending job
   while (ilist->list[current].state != DNX_JOB_PENDING)
   {
      struct timeval now;
      struct timespec timeout;

      gettimeofday(&now, 0);
      timeout.tv_sec = now.tv_sec + DNX_JOBLIST_TIMEOUT;
      timeout.tv_nsec = now.tv_usec * 1000;

      dnxDebug(8, "dnxJobListDispatch: BEFORE: Head=%lu, DHead=%lu, Tail=%lu.", 
            ilist->head, ilist->dhead, ilist->tail);

      if ((ret = pthread_cond_timedwait(&ilist->cond, &ilist->mut, 
            &timeout)) == ETIMEDOUT)
         break;

      current = ilist->dhead;
   }

   if (ret == 0)
   {
      // transition this job's state to InProgress
      ilist->list[current].state = DNX_JOB_INPROGRESS;
   
      // make a copy for the Dispatcher
      memcpy(pJob, &ilist->list[current], sizeof *pJob);
   
      // update the dispatch head
      if (ilist->dhead != ilist->tail)
         ilist->dhead = (current + 1) % ilist->size;
   
      dnxDebug(8, "dnxJobListDispatch: AFTER: Job [%lu,%lu]; Head=%lu, DHead=%lu, Tail=%lu.", 
            pJob->xid.objSerial, pJob->xid.objSlot, ilist->head, ilist->dhead, ilist->tail);
   }

   DNX_PT_MUTEX_UNLOCK(&ilist->mut);

   return ret;
}

//----------------------------------------------------------------------------

int dnxJobListCollect(DnxJobList * pJobList, DnxXID * pxid, DnxNewJob * pJob)
{
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   unsigned long current;
   int ret = DNX_OK;
   dnxDebug(4, "dnxJobListCollect: Entering id(%lu) slot(%lu)", 
        pxid->objSerial, pxid->objSlot);

   assert(pJobList && pxid && pJob);   // parameter validation
   dnxDebug(4, "dnxJobListCollect: Good params");

   current = pxid->objSlot;
   dnxDebug(4, "dnxJobListCollect: Job id (%i) list length(%i)", 
        current, ilist->size);

   assert(current < ilist->size);
   dnxDebug(4, "dnxJobListCollect: Job id smaller than list length");


   if (current >= ilist->size)         // runtime validation requires check
      return DNX_ERR_INVALID;          // corrupt client network message

   DNX_PT_MUTEX_LOCK(&ilist->mut);

   dnxDebug(4, 
         "dnxJobListCollect: Compare job (%s) [%lu,%lu] to job [%lu,%lu]: "
         "Head=%lu, DHead=%lu, Tail=%lu.", ilist->list[current].cmd, pxid->objSerial, pxid->objSlot,
         ilist->list[current].xid.objSerial, ilist->list[current].xid.objSlot, 
         ilist->head, ilist->dhead, ilist->tail);

   // verify that the XID of this result matches the XID of the service check
   if (ilist->list[current].state == DNX_JOB_NULL 
         || !dnxEqualXIDs(pxid, &ilist->list[current].xid)) 
   {
      dnxDebug(4, "dnxJobListCollect: Job expired before retrieval");      
      ret = DNX_ERR_NOTFOUND;          // job expired; removed by the timer
   }
   else
   {
      // make a copy for the Collector
      memcpy(pJob, &ilist->list[current], sizeof *pJob);
      pJob->state = DNX_JOB_COMPLETE;
      dnxDebug(4, "dnxJobListCollect: Job complete");      

      // dequeue this job; make slot available for another job
      ilist->list[current].state = DNX_JOB_NULL;
      dnxDebug(4, "dnxJobListCollect: Job freed");      
   
      // update the job list head
      if (current == ilist->head && current != ilist->tail)
      {
         ilist->head = (current + 1) % ilist->size;
         dnxDebug(4, "dnxJobListCollect: Set head to (%i)", ilist->head);
      }
   }

   DNX_PT_MUTEX_UNLOCK(&ilist->mut);

   return ret;
}

//----------------------------------------------------------------------------

int dnxJobListCreate(unsigned size, DnxJobList ** ppJobList)
{
   iDnxJobList * ilist;
   int ret;

   assert(ppJobList && size);

   if ((ilist = (iDnxJobList *)xmalloc(sizeof *ilist)) == 0)
      return DNX_ERR_MEMORY;
   memset(ilist, 0, sizeof *ilist);

   if ((ilist->list = (DnxNewJob *)xmalloc(sizeof *ilist->list * size)) == 0)
   {
      xfree(ilist);
      return DNX_ERR_MEMORY;
   }
   memset(ilist->list, 0, sizeof *ilist->list * size);

   ilist->size = size;

   DNX_PT_MUTEX_INIT(&ilist->mut);
   pthread_cond_init(&ilist->cond, 0);

   if ((ret = dnxTimerCreate((DnxJobList *)ilist, DNX_TIMER_SLEEP, 
         &ilist->timer)) != 0)
   {
      DNX_PT_COND_DESTROY(&ilist->cond);
      DNX_PT_MUTEX_DESTROY(&ilist->mut);
      xfree(ilist->list);
      xfree(ilist);
      return ret;
   }

   *ppJobList = (DnxJobList *)ilist;

   return DNX_OK;
}

//----------------------------------------------------------------------------

void dnxJobListDestroy(DnxJobList * pJobList)
{
   iDnxJobList * ilist = (iDnxJobList *)pJobList;

   assert(pJobList);

   dnxTimerDestroy(ilist->timer);

   DNX_PT_COND_DESTROY(&ilist->cond);
   DNX_PT_MUTEX_DESTROY(&ilist->mut);

   xfree(ilist->list);
   xfree(ilist);
}

/*--------------------------------------------------------------------------
                                 UNIT TEST

   From within dnx/server, compile with GNU tools using this command line:
    
      gcc -DDEBUG -DDNX_JOBLIST_TEST -g -O0 -I../common dnxJobList.c \
         ../common/dnxError.c -lpthread -lgcc_s -lrt -o dnxJobListTest

  --------------------------------------------------------------------------*/

#ifdef DNX_JOBLIST_TEST

// #include "utesthelp.h"
// #include <time.h>
// 
// #define elemcount(x) (sizeof(x)/sizeof(*(x)))
// 
// static int verbose;
// 
// // functional stubs
// IMPLEMENT_DNX_DEBUG(verbose);
// IMPLEMENT_DNX_SYSLOG(verbose);
// 
// int dnxTimerCreate(DnxJobList * jl, int s, DnxTimer ** pt) { *pt = 0; return 0; }
// void dnxTimerDestroy(DnxTimer * t) { }
// 
// int dnxEqualXIDs(DnxXID * pxa, DnxXID * pxb)
//       { return pxa->objType == pxb->objType && pxa->objSerial == pxb->objSerial 
//             && pxa->objSlot == pxb->objSlot; }
// 
// int dnxMakeXID(DnxXID * x, DnxObjType t, unsigned long s, unsigned long l)
//       { x->objType = t; x->objSerial = s; x->objSlot = l; return DNX_OK; }
// 
// int main(int argc, char ** argv)
// {
//    DnxJobList * jobs;
//    DnxNodeRequest n1[101];
//    DnxNewJob j1[101];
//    DnxNewJob jtmp;
//    DnxXID xid;
//    int serial, xlsz, expcount;
//    iDnxJobList * ijobs;
//    time_t now;
// 
//    verbose = argc > 1;
// 
//    // create a new job list and get a concrete reference to it for testing
//    CHECK_ZERO(dnxJobListCreate(100, &jobs));
//    ijobs = (iDnxJobList *)jobs;
// 
//    // force entire array to non-zero values for testing
//    memset(j1, 0xcc, sizeof j1);
//    memset(n1, 0xdd, sizeof n1);
//    
//    // test that we CAN add 100 jobs to the 100-job list
//    now = time(0);
//    for (serial = 0; serial < elemcount(j1); serial++)
//    {
//       // configure request node
//       dnxMakeXID(&n1[serial].xid, DNX_OBJ_WORKER, serial, 0);
//       n1[serial].reqType      = DNX_REQ_REGISTER;
//       n1[serial].jobCap       = 1;     // jobs
//       n1[serial].ttl          = 5;     // seconds
//       n1[serial].expires      = 5;     // seconds
//       strcpy(n1[serial].address, "localhost");
// 
//       // configure job
//       dnxMakeXID(&j1[serial].xid, DNX_OBJ_JOB, serial, 0);
//       j1[serial].cmd          = "some command line";
//       j1[serial].start_time   = now;
//       j1[serial].timeout      = serial < 50? 0: 5;  // 50 expire immediately
//       j1[serial].expires      = j1[serial].start_time + j1[serial].timeout;
//       j1[serial].payload      = 0;     // no payload for tests
//       j1[serial].pNode        = &n1[serial];
// 
//       if (serial < 100)
//          CHECK_ZERO(dnxJobListAdd(jobs, &j1[serial]));
//       else  // test that we CAN'T add 101 jobs
//          CHECK_NONZERO(dnxJobListAdd(jobs, &j1[serial]));
//    }
// 
//    // test job expiration - ensure first 50 jobs have already expired
//    expcount = 0;
//    do
//    {
//       DnxNewJob xl[10];
//       xlsz = (int)elemcount(xl);
//       CHECK_ZERO(dnxJobListExpire(jobs, xl, &xlsz));
//       expcount += xlsz;
//    } while (xlsz != 0);
//    CHECK_TRUE(expcount == 50);
// 
//    // dispatch 49 of the remaining 50 jobs
//    for (serial = 50; serial < elemcount(j1) - 2; serial++)
//       CHECK_ZERO(dnxJobListDispatch(jobs, &jtmp));
// 
//    // ensure the dispatch head is valid and not in progress
//    CHECK_TRUE(ijobs->dhead != 0);
//    CHECK_TRUE(ijobs->list[ijobs->dhead].state != DNX_JOB_INPROGRESS);
//    CHECK_TRUE(ijobs->list[ijobs->head].state == DNX_JOB_INPROGRESS);
// 
//    // ensure dispatch head points to last item in list
//    CHECK_TRUE(ijobs->dhead == 99);
// 
//    // collect all pending jobs
//    for (serial = 50; serial < elemcount(j1) - 2; serial++)
//    {
//       dnxMakeXID(&xid, DNX_OBJ_JOB, serial, serial);
//       CHECK_ZERO(dnxJobListCollect(jobs, &xid, &jtmp));
//    }
// 
//    // ensure there's one left
//    CHECK_TRUE(ijobs->head == ijobs->tail);
//    CHECK_TRUE(ijobs->head != 0);
// 
//    // ensure head, tail and dhead all point to the last element (99)
//    CHECK_TRUE(ijobs->head == 99);
//    CHECK_TRUE(ijobs->tail == 99);
//    CHECK_TRUE(ijobs->dhead == 99);
// 
//    dnxJobListDestroy(jobs);
// 
//    return 0;
// }

#endif   /* DNX_JOBLIST_TEST */

/*--------------------------------------------------------------------------*/

