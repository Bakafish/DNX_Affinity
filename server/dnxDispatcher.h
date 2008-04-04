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

/** Definitions and prototypes for the DNX Dispatcher thread.
 *
 * The purpose of this thread is to dispatch service check jobs to the
 * registered worker nodes for execution.  It accomplishes this by
 * accepting work node registrations and then dispatching service check
 * jobs to registered worker nodes using a weighted-round-robin algorithm.
 * The algorithm's weighting is based upon the number of jobs-per-second
 * throughput rating of each worker node.
 * 
 * @file dnxDispatcher.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IFC
 */

#ifndef _DNXDISPATCHER_H_
#define _DNXDISPATCHER_H_

#include "dnxJobList.h"

/** Abstract data type for the DNX job dispatcher. */
typedef struct { int unused; } DnxDispatcher;

DnxChannel * dnxDispatcherGetChannel(DnxDispatcher * disp);

int dnxDispatcherCreate(long * debug, char * chname, char * dispurl,
      DnxJobList * joblist, DnxDispatcher ** pdisp);
void dnxDispatcherDestroy(DnxDispatcher * disp);

#endif   /* _DNXDISPATCHER_H_ */

