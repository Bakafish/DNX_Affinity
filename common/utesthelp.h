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

/** Unit test helper macros.
 *
 * @file utesthelp.h
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#include <stdio.h>      // for fprintf
#include <stdlib.h>     // for exit
#include <stdarg.h>     // for va_list, etc.

#include "dnxError.h"

/* test-bed helper macros */
#define CHECK_ZERO(expr)                                                      \
do {                                                                          \
   int ret;                                                                   \
   if ((ret = (expr)) != 0)                                                   \
   {                                                                          \
      fprintf(stderr, "FAILED: '%s'\n  at %s(%d).\n  error %d: %s\n",         \
            #expr, __FILE__, __LINE__, ret, dnxErrorString(ret));             \
      exit(1);                                                                \
   }                                                                          \
} while (0)
#define CHECK_TRUE(expr)                                                      \
do {                                                                          \
   if (!(expr))                                                               \
   {                                                                          \
      fprintf(stderr, "FAILED: Boolean(%s)\n  at %s(%d).\n",                  \
            #expr, __FILE__, __LINE__);                                       \
      exit(1);                                                                \
   }                                                                          \
} while (0)
#define CHECK_NONZERO(expr)    CHECK_ZERO(!(expr))
#define CHECK_FALSE(expr)      CHECK_TRUE(!(expr))

#define IMPLEMENT_DNX_LOGGER(v,name) \
void dnx##name(int l, char * f, ... ) \
{ if (v) { va_list a; va_start(a,f); vprintf(f,a); va_end(a); puts(""); } }

#define IMPLEMENT_DNX_DEBUG(v)  IMPLEMENT_DNX_LOGGER(v,Debug)
#define IMPLEMENT_DNX_SYSLOG(v) IMPLEMENT_DNX_LOGGER(v,Syslog)
     
/*--------------------------------------------------------------------------*/

