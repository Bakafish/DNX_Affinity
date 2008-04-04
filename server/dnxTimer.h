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

/** Definitions and prototypes for the DNX Timer thread.
 *
 * The purpose of this thread is to monitor the age of service requests
 * which are being actively executed by the worker nodes.
 * 
 * This requires access to the global Pending queue (which is also
 * manipulated by the Dispatcher and Collector threads.)
 *
 * @file dnxTimer.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_SERVER_IFC
 */

#ifndef _DNXTIMER_H_
#define _DNXTIMER_H_

#include "dnxJobList.h"

/** DNX job expiration timer abstract type. */
typedef struct { int unused; } DnxTimer;

int dnxTimerCreate(DnxJobList * joblist, DnxTimer ** ptimer);
void dnxTimerDestroy(DnxTimer * timer);

#endif   /* _DNXTIMER_H_ */

