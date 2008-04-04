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

// dnxDispatcher.h
//
// This file implements the DNX Dispatcher thread.
//
// The purpose of this thread is to dispatch service check jobs to the
// registered worker nodes for execution.  It accomplishes this by
// accepting work node registrations and then dispatching service check
// jobs to registered worker nodes using a weighted-round-robin algorithm.
// The algorithm's weighting is based upon the number of jobs-per-second
// throughput rating of each worker node.
//
// Author: Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
// First Written: 2006-07-11  R.W.Ingraham
// Last Modified: 2007-02-08


#ifndef _DNXDISPATCHER_H_
#define _DNXDISPATCHER_H_


//
// Constants
//


//
// Structures
//


//
// Globals
//


//
// Prototypes
//

void *dnxDispatcher (void *data);

#endif   /* _DNXDISPATCHER_H_ */

