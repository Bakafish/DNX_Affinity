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

// dnxTimer.h
//
// This file implements the DNX Timer thread.
//
// The purpose of this thread is to monitor the age of service requests
// which are being actively executed by the worker nodes.
//
// This requires access to the global Pending queue (which is also
// manipulated by the Dispatcher and Collector threads.)
//
// Author: Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
// First Written: 2006-07-11  R.W.Ingraham
// Last Modified: 2007-02-08


#ifndef _DNXTIMER_H_
#define _DNXTIMER_H_


//
// Constants
//

#define DNX_TIMER_SLEEP    5


//
// Structures
//


//
// Globals
//


//
// Prototypes
//

void *dnxTimer (void *data);
int dnxThreadSleep (int seconds);

#endif   /* _DNXTIMER_H_ */

