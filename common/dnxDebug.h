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

#ifndef _DNXDEBUG_H_
#define _DNXDEBUG_H_

#include "dnxLogging.h"

#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if DEBUG_LOCKS && defined(PTHREAD_MUTEX_ERRORCHECK_NP)
extern int pthread_mutexattr_settype(pthread_mutexattr_t * attr, int kind);
# define DNX_PT_MUTEX_INIT(mp) { \
      pthread_mutexattr_t attr; \
      pthread_mutexattr_init(&attr); \
      pthread_mutex_init((mp),&attr); \
      pthread_mutexattr_settype((mp), PTHREAD_MUTEX_ERRORCHECK_NP); \
      pthread_mutexattr_destroy(&attr); \
      dnxDebug(2, "mutex INIT at %s:%d", __FILE__, __LINE__); \
   }
# define DNX_PT_MUTEX_DESTROY(mp) \
   dnxDebug(2, "mutex DESTROY at %s:%d", __FILE__, __LINE__); \
   if (pthread_mutex_destroy(mp) != 0) { \
      dnxDebug(1, "mutex_destroy: mutex is in use at %s:%d!", __FILE__, __LINE__); \
      assert(0); \
   }
# define DNX_PT_MUTEX_LOCK(mp) \
   dnxDebug(10, "mutex LOCK at %s:%d", __FILE__, __LINE__); \
   if (pthread_mutex_lock(mp) != 0) { \
      switch (errno) { \
      case EINVAL:   dnxDebug(1, "mutex_lock: mutex is not initialized at %s:%d!", __FILE__, __LINE__); break; \
      case EDEADLK:  dnxDebug(1, "mutex_lock: mutex already locked by this thread at %s:%d!", __FILE__, __LINE__); break; \
      default:       dnxDebug(1, "mutex_lock: unknown error %d: %s at %s:%d!", errno, strerror(errno), __FILE__, __LINE__); break; \
      } assert(0); \
   }
# define DNX_PT_MUTEX_UNLOCK(mp) \
   dnxDebug(10, "mutex UNLOCK at %s:%d", __FILE__, __LINE__); \
   if (pthread_mutex_unlock(mp) != 0) { \
      switch (errno) { \
      case EINVAL:   dnxDebug(1, "mutex_unlock: mutex is not initialized at %s:%d!", __FILE__, __LINE__); break; \
      case EPERM:    dnxDebug(1, "mutex_unlock: mutex not locked by this thread at %s:%d!", __FILE__, __LINE__); break; \
      default:       dnxDebug(1, "mutex_unlock: unknown error %d: %s at %s:%d!", errno, strerror(errno), __FILE__, __LINE__); break; \
      } assert(0); \
   }
#else    /* !DEBUG_LOCKS || !?PTHREAD_MUTEX_ERRORCHECK_NP */
# define DNX_PT_MUTEX_INIT(mp)      pthread_mutex_init((mp),0)
# define DNX_PT_MUTEX_DESTROY(mp)   pthread_mutex_destroy(mp)
# define DNX_PT_MUTEX_LOCK(mp)      pthread_mutex_lock(mp)
# define DNX_PT_MUTEX_UNLOCK(mp)    pthread_mutex_unlock(mp)
#endif   /* DEBUG && ?PTRHEAD_MUTEX_ERRORCHECK_NP */

#if DEBUG_LOCKS
# define DNX_PT_COND_DESTROY(cvp) \
   if (pthread_cond_destroy(cvp) != 0) { \
      dnxDebug(1, "cond_destroy: condition-variable is in use at %s:%d!", __FILE__, __LINE__); \
      assert(0); \
   }
#else    /* !DEBUG_LOCKS */
# define DNX_PT_COND_DESTROY(cvp)   pthread_cond_destroy(cvp)
#endif   /* ?DEBUG */

#if DEBUG_HEAP
# include "dnxHeap.h"
# define xmalloc(sz)    dnxMalloc((sz), __FILE__, __LINE__)
# define xcalloc(n,sz)  dnxCalloc((n), (sz), __FILE__, __LINE__)
# define xrealloc(p,sz) dnxRealloc((p), (sz), __FILE__, __LINE__)
# define xstrdup(str)   dnxStrdup((str), __FILE__, __LINE__)
# define xfree          dnxFree
# define xheapchk       dnxCheckHeap
#else
# include <stdlib.h>
# include <string.h>
# define xmalloc        malloc
# define xcalloc        calloc
# define xrealloc       realloc
# define xstrdup        strdup
# define xfree          free
# define xheapchk()
#endif

#endif   /* !?_DNXDEBUG_H_ */

