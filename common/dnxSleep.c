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

/** Implements the DNX cancelable sleep routines.
 *
 * @file dnxSleep.c
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

/*!@defgroup DNX_COMMON_IMPL DNX Common Services Implementation 
 * @defgroup DNX_COMMON_IFC  DNX Common Services Interface
 */

#include "dnxSleep.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#if HAVE_CONFIG_H
# include <config.h>
# ifndef HAVE_NANOSLEEP
#  include <sys/time.h>
#  include <pthread.h>
# endif
#endif

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
void dnxCancelableSleep(int msecs)
{
#if HAVE_NANOSLEEP
   struct timespec rqt;
   rqt.tv_sec = msecs / 1000;
   rqt.tv_nsec = (msecs % 1000) * 1000L * 1000L;
   while (nanosleep(&rqt, &rqt) == -1 && errno == EINTR)
      ;
#else
   pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
   pthread_cond_t cv  = PTHREAD_COND_INITIALIZER;
   struct timeval now;              // time when we started waiting
   struct timespec timeout;         // timeout value for the wait function

   pthread_mutex_lock(&mutex);

   // timeval uses micro-seconds; timespec uses nano-seconds; 1us == 1000ns.
   gettimeofday(&now, 0);
   timeout.tv_sec = now.tv_sec + (msecs / 1000);
   timeout.tv_nsec = (now.tv_usec + (msecs % 1000) * 1000L) * 1000L;
   pthread_cond_timedwait(&cv, &mutex, &timeout);

   pthread_mutex_unlock(&mutex);
#endif
}

/*--------------------------------------------------------------------------*/

