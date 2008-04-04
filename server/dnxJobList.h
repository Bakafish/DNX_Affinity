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
 * @file dnxJobList.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#ifndef _DNXJOBLIST_H_
#define _DNXJOBLIST_H_

#include "dnxNebMain.h"

int dnxJobListInit (DnxJobList **ppJobList, unsigned long size);
int dnxJobListWhack (DnxJobList **ppJobList);
int dnxJobListAdd (DnxJobList *pJobList, DnxNewJob *pJob);
int dnxJobListExpire (DnxJobList *pJobList, DnxNewJob *pExpiredJobs, int *totalJobs);
int dnxJobListDispatch (DnxJobList *pJobList, DnxNewJob *pJob);
int dnxJobListCollect (DnxJobList *pJobList, DnxGuid *pGuid, DnxNewJob *pJob);

#endif   /* _DNXJOBLIST_H_ */