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
#include "dnxNebMain.h"

#include <sys/time.h>

#define DNX_JOBLIST_TIMEOUT   5     /*!< Wake up to see if we're shutting down. */
#define DNX_TIMER_SLEEP       2500  /*!< Timer sleep interval, in milliseconds */

DnxJobList * joblist; // Fwd declaration

/** The JobList implementation data structure. */
typedef struct iDnxJobList_ 
{
   DnxNewJob * list;       /*!< Array of Job Structures. */
   unsigned long size;     /*!< Number of elements. */
   unsigned long head;     /*!< List head. */
   unsigned long tail;     /*!< List tail. */
   pthread_mutex_t mut;    /*!< The job list mutex. */
   pthread_cond_t cond;    /*!< The job list condition variable. */
   DnxTimer * timer;       /*!< The job list expiration timer. */
} iDnxJobList;

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

int dnxJobListAdd(DnxJobList * pJobList, DnxNewJob * pJob) {
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   unsigned long tail;
   int ret = DNX_OK;

   assert(pJobList && pJob);

   DNX_PT_MUTEX_LOCK(&ilist->mut);

   tail = ilist->tail;

   // verify space in the job list, this keeps a single empty buffer element to 
   // protect us from not knowing a full ring from an empty one
   if (ilist->list[tail].state && (tail = (tail + 1) % ilist->size) == ilist->head) {
      dnxLog("dnxJobListAdd: Out of job slots (max=%lu): %s.", 
            ilist->size, pJob->cmd);
      dnxDebug(1, "dnxJobListAdd: Out of job slots (max=%lu): %s.", 
            ilist->size, pJob->cmd);
     ret = DNX_ERR_CAPACITY;
   } else {
      // add the slot index to the Job's XID - this allows us to index 
      //    the job list using the returned result's XID.objSlot field
      pJob->xid.objSlot = tail;
      // We were unable to get an available dnxClient job request so we
      // put the job into the queue anyway and have the timer thread try 
      // and find a dnxClient for it later
      if (pJob->pNode->xid.objSlot == -1) {
         pJob->state = DNX_JOB_UNBOUND;
      } else {
         pJob->state = DNX_JOB_PENDING;
      }
      
      dnxAuditJob(pJob, "ASSIGN");
      
      // add this job to the job list
      memcpy(&ilist->list[tail], pJob, sizeof *pJob);
      
      ilist->tail = tail;
   
      dnxDebug(1, "dnxJobListAdd: Job [%lu:%lu]: Head=%lu, Tail=%lu.", 
            pJob->xid.objSerial, pJob->xid.objSlot, ilist->head, ilist->tail);
      
      if(pJob->state == DNX_JOB_PENDING) {
         pthread_cond_signal(&ilist->cond);  // signal that a new job is available
      }         
   }

   DNX_PT_MUTEX_UNLOCK(&ilist->mut);

   return ret;
}

int dnxJobListMarkAck(DnxJobList * pJobList, DnxResult * pRes) {
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   assert(pJobList && pRes);   // parameter validation
   time_t now = time(0);
   int ret = DNX_ERR_NOTFOUND;
   dnxDebug(4, "dnxJobListMarkAck: Job [%lu:%lu] serial (%lu) slot (%lu) latency (%lu) sec.", 
        pRes->xid.objSerial, pRes->xid.objSlot, pRes->xid.objSerial, pRes->xid.objSlot, (now - pRes->timestamp));
   unsigned long current = pRes->xid.objSlot;

   DNX_PT_MUTEX_LOCK(&ilist->mut);
   if (dnxEqualXIDs(&(pRes->xid), &ilist->list[current].xid)) {
      if(ilist->list[current].state == DNX_JOB_PENDING || ilist->list[current].state == DNX_JOB_UNBOUND) {
         ilist->list[current].state = DNX_JOB_INPROGRESS;
         dnxAuditJob(&(ilist->list[current]), "ACK");
         ret = DNX_OK;
      }
   }
   DNX_PT_MUTEX_UNLOCK(&ilist->mut);
   return ret;
}

int dnxJobListMarkAckSent(DnxJobList * pJobList, DnxXID * pXid) {
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   assert(pJobList && pXid);   // parameter validation
   int ret = DNX_ERR_NOTFOUND;
   dnxDebug(4, "dnxJobListMarkAckSent: Job [%lu:%lu]", 
        pXid->objSerial, pXid->objSlot);
   unsigned long current = pXid->objSlot;

   DNX_PT_MUTEX_LOCK(&ilist->mut);
   if (dnxEqualXIDs(pXid, &ilist->list[current].xid)) {
      if(ilist->list[current].state == DNX_JOB_RECEIVED || ilist->list[current].state == DNX_JOB_COMPLETE) {
         ilist->list[current].ack = 1;
         dnxAuditJob(&(ilist->list[current]), "CONFIRMED");
         ret = DNX_OK;
      }
   }
   DNX_PT_MUTEX_UNLOCK(&ilist->mut);
   return ret;
}

int dnxJobListMarkComplete(DnxJobList * pJobList, DnxXID * pXid) {
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   assert(pJobList && pXid);   // parameter validation
   int ret = DNX_ERR_NOTFOUND;
   dnxDebug(4, "dnxJobListMarkComplete: Job [%lu:%lu]", 
        pXid->objSerial, pXid->objSlot);
   unsigned long current = pXid->objSlot;

   DNX_PT_MUTEX_LOCK(&ilist->mut);
   if (dnxEqualXIDs(pXid, &ilist->list[current].xid)) {
      if(ilist->list[current].state == DNX_JOB_RECEIVED) {
         ilist->list[current].state = DNX_JOB_COMPLETE;
         ret = DNX_OK;
      }
   }
   DNX_PT_MUTEX_UNLOCK(&ilist->mut);
   return ret;
}

//----------------------------------------------------------------------------

int dnxJobListExpire(DnxJobList * pJobList, DnxNewJob * pExpiredJobs, int * totalJobs) {
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   unsigned long current;
   DnxNewJob * pJob;
   int jobCount = 0;
   time_t now;

   assert(pJobList && pExpiredJobs && totalJobs && *totalJobs > 0);

   DNX_PT_MUTEX_LOCK(&ilist->mut);

   // get the current time (after we acquire the lock! In case we had to wait)
   now = time(0);

   // walk the entire job list - InProgress and Pending jobs (in that order)
   current = ilist->head;
   int zero_factor = ilist->size - current; // add this value to normalize the index
   dnxDebug(6, "dnxJobListExpire: searching for (%i) expired objects. Head(%lu) Tail(%i)", *totalJobs, ilist->head, ilist->tail);
   int state = 0;
   while(jobCount < *totalJobs) {
      state = (pJob = &ilist->list[current])->state;
      unsigned long dispatch_timeout = now - DNX_DISPATCH_TIMEOUT;

      // only examine jobs that are either awaiting dispatch or results
      switch (state) {
         case DNX_JOB_UNBOUND:
            if(pJob->start_time <= dispatch_timeout) {
               dnxDebug(2, "dnxJobListExpire: Expiring Unbound %s Job [%lu:%lu] count(%i) type(%i) Start Time: (%lu) Now: (%lu) Expire: (%lu)",
                  (pJob->object_check_type ? "Host" : "Service"),  pJob->xid.objSerial, pJob->xid.objSlot, current, state, pJob->start_time, now, dispatch_timeout);               
               // Put the old job in a purgable state   
               pJob->state = DNX_JOB_EXPIRED;
               
               // Add a copy to the expired job list
               memcpy(&pExpiredJobs[jobCount++], pJob, sizeof(DnxNewJob));    
            } else {
               // If there is a client associated with it, xid.objSlot != -1
               // then it means we may be getting a result coming back to us
            
               // This job has not expired, try and get a dnxClient for it
               if (dnxGetNodeRequest(dnxGetRegistrar(), &(pJob->pNode)) == DNX_OK) { 
                  // If OK we have successfully dispatched it so update it's expiration
                  dnxDebug(2, "dnxJobListExpire: Dequeueing DNX_JOB_UNBOUND job [%lu:%lu] Expires in (%i) seconds. Dispatch TO:(%i) Now: (%lu) count(%i) type(%i)", 
                     pJob->xid.objSerial, pJob->xid.objSlot, pJob->start_time - dispatch_timeout, dispatch_timeout, now, current, state);
                  pJob->state = DNX_JOB_PENDING;
                  pthread_cond_signal(&ilist->cond);  // signal that a new job is available
               } else {
                  dnxDebug(6, "dnxJobListExpire: Unable to dequeue DNX_JOB_UNBOUND job [%lu:%lu] Expires in (%i) seconds. Dispatch TO:(%i) Now: (%lu) count(%i) type(%i)", 
                     pJob->xid.objSerial, pJob->xid.objSlot, pJob->start_time - dispatch_timeout, dispatch_timeout, now, current, state);
               }
            }
            break;
         case DNX_JOB_PENDING:
         case DNX_JOB_INPROGRESS:
            // check the job's expiration stamp
            if (pJob->expires <= now) { //  
               // This is an expired job, it was sent out, but never came back
               dnxDebug(1, "dnxJobListExpire: Expiring Job [%lu:%lu] count(%i) type(%i) Exp: (%lu) Now: (%lu)",
                  pJob->xid.objSerial, pJob->xid.objSlot, current, state, pJob->expires, now);               
               // Put the old job in a purgable state   
               pJob->state = DNX_JOB_EXPIRED;
               // Add a copy to the expired job list
               memcpy(&pExpiredJobs[jobCount++], pJob, sizeof(DnxNewJob));
            } 
            break;
         case DNX_JOB_COMPLETE:
            // If the Ack hasn't been sent out yet, give it time to complete
            if(! pJob->ack) {
               dnxDebug(3, "dnxJobListExpire: Waiting to send Ack. count(%i) type(%i)", current, state);
               break;
            }
         case DNX_JOB_EXPIRED:
            dnxJobCleanup(pJob);
            dnxDebug(3, "dnxJobListExpire: Nullified Job. count(%i) type(%i)", current, state);
         case DNX_JOB_NULL:
            if(current == ilist->head && current != ilist->tail) {
               ilist->head = ((current + 1) % ilist->size);
               dnxDebug(2, "dnxJobListExpire: Moving head to (%i). count(%i) type(%i)", ilist->head, current, pJob->state);
               // we have an old item at the head of the list, so we need to
               // increment the head. It should never be larger than the tail.
            } else {
               dnxDebug(5, "dnxJobListExpire: Null Job. count(%i) type(%i)", current, pJob->state);
            }
            break;
         case DNX_JOB_RECEIVED:
            if(! pJob->ack) {
               dnxDebug(3, "dnxJobListExpire: Waiting to send Ack. job [%lu:%lu] count(%i) type(%i)", current, state);
            } else {
               dnxDebug(2, "dnxJobListExpire: Ack sent. job [%lu:%lu] count(%i) type(%i)", current, state);
            }
            // The Collector thread will set this to DNX_JOB_COMPLETE once it has 
            // replied to Nagios, but we don't advance the head until that happens
            break;
      }

      // bail-out if this was the job list tail
      if (current == ilist->tail) {
         break;
      }
      // increment the job list index
      current = ((current + 1) % ilist->size);
   }
      
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
   int ret = DNX_OK; //DNX_ERR_TIMEOUT;
   struct timeval now;
   struct timespec timeout;

   assert(pJobList && pJob);

   DNX_PT_MUTEX_LOCK(&ilist->mut);


   // start at current head
   current = ilist->head;

   dnxDebug(6, "dnxJobListDispatch: BEFORE: Head=%lu, Tail=%lu, Queue=%lu.", 
       ilist->head, ilist->tail, ilist->size);

   while (1) {
 
      switch (ilist->list[current].state) {
         case DNX_JOB_INPROGRESS:
            dnxDebug(8, "dnxJobListDispatch: In Progress Item in slot:(%lu) head:(%lu) tail:(%lu).", 
               current, ilist->head, ilist->tail);
            break;
         case DNX_JOB_NULL:
            dnxDebug(8, "dnxJobListDispatch: Null Item in slot:(%lu) head:(%lu) tail:(%lu).", 
               current, ilist->head, ilist->tail);
            break;
         case DNX_JOB_EXPIRED:
            dnxDebug(8, "dnxJobListDispatch: Expired Item in slot:(%lu) head:(%lu) tail:(%lu).", 
               current, ilist->head, ilist->tail);
            break;
         case DNX_JOB_UNBOUND:
            dnxDebug(8, "dnxJobListDispatch: Unbound Item in slot:(%lu) head:(%lu) tail:(%lu).", 
               current, ilist->head, ilist->tail);
            break;
         case DNX_JOB_PENDING:
            gettimeofday(&now, 0);

            // Check to see if we have recently dispatched this
            if((ilist->list[current].pNode)->retry > now.tv_sec) {
               dnxDebug(5, "dnxJobListDispatch: Pending job [%lu:%lu] waiting for Ack, resend in (%i) sec.",
                  ilist->list[current].xid.objSerial, ilist->list[current].xid.objSlot, ((ilist->list[current].pNode)->retry - now.tv_sec));
               break;
            } else {
                if((ilist->list[current].pNode)->retry) {
                  // Make sure the dnxClient service offer is still fresh
                  if((ilist->list[current].pNode)->expires < now.tv_sec) {
                     dnxDebug(4, "dnxJobListDispatch: Pending job [%lu:%lu] waiting for Ack, client node expired. Resubmitting.",
                     ilist->list[current].xid.objSerial, ilist->list[current].xid.objSlot);
                     ilist->list[current].state = DNX_JOB_UNBOUND;
                     
                     // reset the node?
                     // It's likely that the same client will be servicing us
                     // or that the job might come back in the mean time, so we
                     // should keep this node as long as possible
                     // We just need to make sure that the Affinity is correct and that 
                     // it's only used to find a new node, so if we get as far as 
                     // resubmitting, we will have a valid node anyway
                     
                     // If the original job comes back, the acks will get all messed up
                     // not sure how to deal with that other than to just be graceful
                     // about receiving lots of results...
                   
                     
//                      dnxDeleteNodeReq(ilist->list[current].pNode);
//                      DnxNodeRequest * pNode = dnxCreateNodeReq();
                     ilist->list[current].pNode->flags = *(dnxGetAffinity(ilist->list[current].host_name));
//                      ilist->list[current].pNode->hn = xstrdup(ilist->list[current].host_name);
//                      ilist->list[current].pNode->addr = NULL;

                     // We should leave the address alone so we don't segfault if results come in late
                     // but should we reset these? 
//                      ilist->list[current].pNode->xid.objSlot = -1;
//                      ilist->list[current].pNode->xid.objSerial = ilist->list[current].xid.objSerial;
//                      ilist->list[current].pNode = pNode;
                  }
                  break;                  
               } else {
                  // This is a new job, so dispatch it
                  dnxDebug(4, "dnxJobListDispatch: Dispatching new job [%lu:%lu] waiting for Ack",
                     ilist->list[current].xid.objSerial, ilist->list[current].xid.objSlot);
               }
            }
            
            // set our retry interval
            // This should be fairly forgiving in case we just missed the Ack but it actually
            // got the job and is returning our results.
            (ilist->list[current].pNode)->retry = now.tv_sec + 5; 
            
         
            // make a copy for the Dispatcher to send to client
            memcpy(pJob, &ilist->list[current], sizeof *pJob);
            
            // release the mutex
            DNX_PT_MUTEX_UNLOCK(&ilist->mut);
            return ret;
         case DNX_JOB_COMPLETE:
         case DNX_JOB_RECEIVED:
            // This is a job that we have received the response and we need to send an ack to
            // the client to let it know we got it
            if(ilist->list[current].ack) {
               // Only send a single Ack
               break;
            }
            // make a copy for the Dispatcher to send an Ack to the client
            memcpy(pJob, &ilist->list[current], sizeof *pJob);
            
            dnxDebug(4, "dnxJobListDispatch: Received job [%lu:%lu] sending Ack.",
               ilist->list[current].xid.objSerial, ilist->list[current].xid.objSlot);
            
            // release the mutex
            DNX_PT_MUTEX_UNLOCK(&ilist->mut);
            return ret;
      }

      if (current == ilist->tail) {
         // if we are at the end of the queue
         gettimeofday(&now, 0);
         timeout.tv_sec = now.tv_sec + DNX_JOBLIST_TIMEOUT;
         timeout.tv_nsec = now.tv_usec * 1000;
         if ((ret = pthread_cond_timedwait(&ilist->cond, &ilist->mut, &timeout)) == ETIMEDOUT) {
            // We waited for the time out period and no new jobs arrived. So give control back to caller.
            dnxDebug(5, "dnxJobListDispatch: Reached end of dispatch queue. Thread timer returned.");      
            DNX_PT_MUTEX_UNLOCK(&ilist->mut);
            return ret;
         } else {
            // We were signaled that there is a new job, so lets move back to the head and get it!
            current = ilist->head;
            dnxDebug(5, "dnxJobListDispatch: Reached end of dispatch queue. A new job arrived.");      
         }
      } else {
         // move to next item in queue
         current = ((current + 1) % ilist->size);
      }
   }
}

//----------------------------------------------------------------------------

int dnxJobListCollect(DnxJobList * pJobList, DnxXID * pxid, DnxNewJob * pJob)
{
   iDnxJobList * ilist = (iDnxJobList *)pJobList;
   unsigned long current;
   int ret = DNX_OK;
   assert(pJobList && pxid && pJob);   // parameter validation

   current = pxid->objSlot;

   dnxDebug(4, "dnxJobListCollect: Job serial (%lu) slot (%lu) list head(%i)", 
        pxid->objSerial, pxid->objSlot, ilist->head);

   if (current >= ilist->size)         // runtime validation requires check
      return DNX_ERR_INVALID;          // corrupt client network message

   DNX_PT_MUTEX_LOCK(&ilist->mut);
   
   // verify that the XID of this result matches the XID of the service check 
   if (ilist->list[current].state == DNX_JOB_NULL 
         || !dnxEqualXIDs(pxid, &ilist->list[current].xid)) {
      dnxDebug(4, "dnxJobListCollect: Job [%lu:%lu] not found.", pxid->objSerial, pxid->objSlot);      
      ret = DNX_ERR_NOTFOUND;          // Very old job or we restarted and lost state
   } else if(ilist->list[current].state == DNX_JOB_EXPIRED) {
      dnxDebug(4, "dnxJobListCollect: Job [%lu:%lu] expired before retrieval.", pxid->objSerial, pxid->objSlot);      
      ret = DNX_ERR_EXPIRED;          // job expired; removed by the timer
   } else {
      if(ilist->list[current].state == DNX_JOB_COMPLETE || ilist->list[current].state == DNX_JOB_RECEIVED) {
         dnxDebug(4, "dnxJobListCollect: Job [%lu:%lu] already retrieved.", pxid->objSerial, pxid->objSlot);      
         ilist->list[current].ack = 0;
         ret = DNX_ERR_ALREADY;           // It needs another Ack
      } else {
         // DNX_JOB_INPROGRESS // DNX_JOB_UNBOUND!!
         ilist->list[current].state = DNX_JOB_RECEIVED;      
         // make a copy to return to the Collector
         memcpy(pJob, &ilist->list[current], sizeof *pJob);
         dnxDebug(4, "dnxJobListCollect: Job [%lu:%lu] completed. Copy of result for (%s) assigned to collector.",
             pxid->objSerial, pxid->objSlot, pJob->cmd);
      }
      
      // Signal to the dispatcher that we need to send an Ack
      pthread_cond_signal(&ilist->cond);
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
   // I'm pretty sure we should initialize these...
   ilist->head = 0;
   ilist->tail = 0;

   DNX_PT_MUTEX_INIT(&ilist->mut);
   pthread_cond_init(&ilist->cond, 0);

   if ((ret = dnxTimerCreate((DnxJobList *)ilist, DNX_TIMER_SLEEP, 
         &ilist->timer)) != 0) {
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

