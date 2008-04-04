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

#define MAX_LOG_LINE 1023

static int defDebug = 0;
static int defLogFacility = LOG_LOCAL7;

static int * pDebug = &defDebug;
static int * pLogFacility = &defLogFacility;

//----------------------------------------------------------------------------

/** Log a parameterized message to the dnx system log file.
 * 
 * @param[in] priority - a priority value for the log message.
 * @param[in] fmt - a format specifier string similar to that of printf.
 */
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

/** Log a parameterized message to the dnx DEBUG log.
 * 
 * This routine logs a debug message if the current global (configured) 
 * debug level is greater than or equal the value of @p level.
 * 
 * @param[in] level - the debug level at which to log the message.
 * @param[in] fmt - a format specifier string similar to that of printf.
 * 
 * @return Zero on success, or a non-zero error code.
 */
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

/** Initialize logging functionality.
 * 
 * @param[in] debug - a pointer to the global debug level.
 * @param[in] logFacility - a pointer to the global log facility.
 */
void initLogging(int * debug, int * logFacility)
{
   pDebug = debug;
   pLogFacility = logFacility;
}

/*--------------------------------------------------------------------------*/

