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

/** Implements the DNX Server logging functions.
 *
 * @file dnxLogging.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxLogging.h"
#include "dnxError.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <assert.h>

#define MAX_LOG_LINE 1023                    //!< Maximum log line length

static int defDebug = 0;                     //!< The default debug level
static int defLogFacility = LOG_LOCAL7;      //!< The default log facility

static int * pDebug = &defDebug;             //!< A pointer to the debug level
static int * pLogFacility = &defLogFacility; //!< A pointer to the log facility

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

void dnxSyslog(int priority, char * fmt, ...)
{
   char sbuf[MAX_LOG_LINE + 1];

   assert(fmt);

   // see if we need formatting
   if (strchr(fmt, '%'))
   {
      va_list ap;
      va_start(ap, fmt);
      vsnprintf(sbuf, MAX_LOG_LINE, fmt, ap);
      va_end(ap);
   }
   else
      strncpy(sbuf, fmt, MAX_LOG_LINE);

   sbuf[MAX_LOG_LINE] = 0;

   syslog(*pLogFacility | priority, "%s", sbuf);
}

//----------------------------------------------------------------------------

void dnxDebug(int level, char * fmt, ...)
{
   char sbuf[MAX_LOG_LINE + 1];

   assert(fmt);

   if (level <= *pDebug)
   {
      // see if we need formatting
      if (strchr(fmt, '%'))
      {
         va_list ap;
         va_start(ap, fmt);
         vsnprintf(sbuf, MAX_LOG_LINE, fmt, ap);
         va_end(ap);
      }
      else
         strncpy(sbuf, fmt, MAX_LOG_LINE);
      sbuf[MAX_LOG_LINE] = 0;

      syslog(*pLogFacility | LOG_DEBUG, "%s", sbuf);
   }
}

//----------------------------------------------------------------------------

void initLogging(int * debug, int * logFacility)
{
   pDebug = debug;
   pLogFacility = logFacility;
}

/*--------------------------------------------------------------------------*/

