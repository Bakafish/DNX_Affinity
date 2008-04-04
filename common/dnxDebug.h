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

/** Implements various debugging macros.
 *
 * @file dnxDebug.h
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DXDEBUG_H_
#define _DXDEBUG_H_

#include <errno.h>
#include <string.h>
#include <pthread.h>

#if defined(DEBUG) && defined(PTHREAD_MUTEX_ERRORCHECK_NP)
extern int pthread_mutexattr_settype(pthread_mutexattr_t * attr, int kind);
# define DNX_PT_MUTEX_INIT(mp) { \
      pthread_mutexattr_t attr; \
      pthread_mutexattr_init(&attr); \
      pthread_mutex_init((mp),&attr); \
      pthread_mutexattr_settype((mp), PTHREAD_MUTEX_ERRORCHECK_NP); \
      pthread_mutexattr_destroy(&attr); \
   }
# define DNX_PT_MUTEX_DESTROY(mp) \
   if (pthread_mutex_destroy(mp) != 0) { \
      dnxSyslog(LOG_ERR, "mutex_destroy: mutex is in use!"); \
      assert(0); \
   }
# define DNX_PT_MUTEX_LOCK(mp) { \
   if (pthread_mutex_lock(mp) != 0) { \
      switch (errno) { \
      case EINVAL:   dnxSyslog(LOG_ERR, "mutex_lock: mutex is not initialized!"); break; \
      case EDEADLK:  dnxSyslog(LOG_ERR, "mutex_lock: mutex already locked by this thread!"); break; \
      default:       dnxSyslog(LOG_ERR, "mutex_lock: unknown error %d: %s!", errno, strerror(errno)); break; \
      } assert(0); \
   }}
# define DNX_PT_MUTEX_UNLOCK(mp) { \
   if (pthread_mutex_unlock(mp) != 0) { \
      switch (errno) { \
      case EINVAL:   dnxSyslog(LOG_ERR, "mutex_unlock: mutex is not initialized!"); break; \
      case EPERM:    dnxSyslog(LOG_ERR, "mutex_unlock: mutex not locked by this thread!"); break; \
      default:       dnxSyslog(LOG_ERR, "mutex_unlock: unknown error %d: %s!", errno, strerror(errno)); break; \
      } assert(0); \
   }}
#else    /* !?(DEBUG && PTHREAD_MUTEX_ERRORCHECK_NP) */
# define DNX_PT_MUTEX_INIT(mp)      pthread_mutex_init((mp),0)
# define DNX_PT_MUTEX_DESTROY(mp)   pthread_mutex_destroy(mp)
# define DNX_PT_MUTEX_LOCK(mp)      pthread_mutex_lock(mp)
# define DNX_PT_MUTEX_UNLOCK(mp)    pthread_mutex_unlock(mp)
#endif   /* ?(DEBUG && PTRHEAD_MUTEX_ERRORCHECK_NP) */

#ifdef DEBUG
# define DNX_PT_COND_DESTROY(cvp) \
   if (pthread_cond_destroy(cvp) != 0) { \
      dnxSyslog(LOG_ERR, "cond_destroy: condition-variable is in use!"); \
      assert(0); \
   }
#else    /* !?DEBUG */
# define DNX_PT_COND_DESTROY(cvp)         pthread_cond_destroy(cvp)
#endif   /* ?DEBUG */

#endif   /* !?_DXDEBUG_H_ */

