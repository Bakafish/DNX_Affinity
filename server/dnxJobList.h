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

/** Definitions and prototypes for the DNX Job List mechanism.
 *
 * @file dnxJobList.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IFC
 */

#ifndef _DNXJOBLIST_H_
#define _DNXJOBLIST_H_

#include "dnxProtocol.h"

#include "nagios.h"

typedef struct _DnxNewJob_ 
{
   DnxJobState state;      // Job state
   DnxGuid guid;           // Service Request Serial No.
   char * cmd;             // Processed check command
   time_t start_time;      // Service check start time
   int timeout;            // Service check timeout in seconds
   time_t expires;         // Expiration time
   service * svc;          // Service check structure
   DnxNodeRequest * pNode; // Worker Request that will handle this Job
} DnxNewJob;

typedef struct _DnxJobList_ 
{
   DnxNewJob * pList;      // Array of Job Structures
   unsigned long size;     // Number of elements
   unsigned long head;     // List head
   unsigned long tail;     // List tail
   unsigned long dhead;    // Head of waiting jobs

   // pthread mutex and condition-variable for Job List access
   pthread_mutex_t mut;
   pthread_cond_t cond;
} DnxJobList;

int dnxJobListInit(DnxJobList ** ppJobList, unsigned long size);
int dnxJobListWhack(DnxJobList ** ppJobList);
int dnxJobListAdd(DnxJobList * pJobList, DnxNewJob * pJob);
int dnxJobListExpire(DnxJobList * pJobList, DnxNewJob * pExpiredJobs, int * totalJobs);
int dnxJobListDispatch(DnxJobList * pJobList, DnxNewJob * pJob);
int dnxJobListCollect(DnxJobList * pJobList, DnxGuid * pGuid, DnxNewJob * pJob);

#endif   /* _DNXJOBLIST_H_ */

