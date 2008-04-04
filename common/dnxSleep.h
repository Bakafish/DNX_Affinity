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

/** Types and definitions for DNX cancelable sleep routines.
 *
 * @file dnxSleep.h
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXSLEEP_H_
#define _DNXSLEEP_H_

/** A millisecond-resolution sleep routine that can be cancelled.
 * 
 * The pthreads specification indicates clearly that the sleep() system call
 * MUST be a cancellation point. However, it appears that sleep() on Linux 
 * calls a routine named _nanosleep_nocancel, which clearly is not a 
 * cancellation point. Oversight? Not even Google appears to know. It seems
 * that most Unix/Linux distros implement sleep in terms of SIGALRM. This
 * is the problem point for creating a cancelable form of sleep().
 *
 * @param[in] msecs - the number of milli-seconds to sleep.
 */
void dnxCancelableSleep(int msecs);

#endif   /* _DNXSLEEP_H_ */

