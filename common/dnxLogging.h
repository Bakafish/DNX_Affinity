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

/** Types and definitions for DNX Server Logging functionality.
 * 
 * @file dnxLogging.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXLOGGING_H_
#define _DNXLOGGING_H_

#include <syslog.h>

/** Log a parameterized message to the dnx system log file.
 * 
 * @param[in] priority - a priority value for the log message.
 * @param[in] fmt - a format specifier string similar to that of printf.
 */
void dnxSyslog(int priority, char * fmt, ... );

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
void dnxDebug(int level, char * fmt, ... );

/** Initialize logging functionality.
 * 
 * @param[in] debug - a pointer to the global debug level.
 * @param[in] logFacility - a pointer to the global log facility.
 */
void initLogging(int * pDebug, int * pLogFacility);

#endif   /* _DNXLOGGING_H_ */

