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

/** Implements the DNX Client logging functions.
 *
 * @file client/dnxLogging.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_CLIENT_IMPL
 */

#include "dnxLogging.h"

#include "dnxClientMain.h"
#include "dnxError.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#define MAX_LOG_LINE 1023

extern DnxGlobalData dnxGlobalData;

/*--------------------------------------------------------------------------*/

/** Log a parameterized message to the dnx system log file.
 * 
 * @param[in] priority - a priority value for the log message.
 * @param[in] fmt - a format specifier string similar to that of printf.
 * 
 * @return Zero on success, or a non-zero error code.
 */
int dnxSyslog(int priority, char * fmt, ...)
{
   va_list ap;
   char sbuf[MAX_LOG_LINE + 1];

   // Validate input parameters
   if (!fmt)
      return DNX_ERR_INVALID;

   // See if we need formatting
   if (strchr(fmt, '%'))
   {
      // Format the string
      va_start(ap, fmt);
      vsnprintf(sbuf, MAX_LOG_LINE, fmt, ap);
      va_end(ap);
   }
   else
      strncpy(sbuf, fmt, MAX_LOG_LINE);
   sbuf[MAX_LOG_LINE] = '\0';

   // Publish the results
   syslog((dnxGlobalData.dnxLogFacility | priority), "%s", sbuf);

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

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
int dnxDebug(int level, char * fmt, ...)
{
   va_list ap;
   char sbuf[MAX_LOG_LINE + 1];

   // Validate input parameters
   if (!fmt)
      return DNX_ERR_INVALID;

   // See if this message meets or exceeds our debugging level
   if (level <= dnxGlobalData.debug)
   {
      // See if we need formatting
      if (strchr(fmt, '%'))
      {
         // Format the string
         va_start(ap, fmt);
         vsnprintf(sbuf, MAX_LOG_LINE, fmt, ap);
         va_end(ap);
      }
      else
         strncpy(sbuf, fmt, MAX_LOG_LINE);
      sbuf[MAX_LOG_LINE] = '\0';

      // Publish the results
      syslog((dnxGlobalData.dnxLogFacility | LOG_DEBUG), "%s", sbuf);
   }

   return DNX_OK;
}

/*--------------------------------------------------------------------------*/

