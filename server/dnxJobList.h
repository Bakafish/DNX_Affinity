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

#include <time.h>

typedef struct _DnxNewJob_ 
{
   DnxJobState state;      // Job state
   DnxXID xid;             // Service request transaction id.
   char * cmd;             // Processed check command
   time_t start_time;      // Service check start time
   int timeout;            // Service check timeout in seconds
   time_t expires;         // Expiration time
   void * payload;         // job payload (service check structure)
   DnxNodeRequest * pNode; // Worker Request that will handle this Job
} DnxNewJob;

/** An abstract data type for a DNX Job List object. */
typedef struct { int unused; } DnxJobList;

int dnxJobListAdd(DnxJobList * pJobList, DnxNewJob * pJob);
int dnxJobListExpire(DnxJobList * pJobList, DnxNewJob * pExpiredJobs, int * totalJobs);
int dnxJobListDispatch(DnxJobList * pJobList, DnxNewJob * pJob);
int dnxJobListCollect(DnxJobList * pJobList, DnxXID * pxid, DnxNewJob * pJob);

int dnxJobListCreate(unsigned size, DnxJobList ** ppJobList);
void dnxJobListDestroy(DnxJobList * pJobList);

#endif   /* _DNXJOBLIST_H_ */

