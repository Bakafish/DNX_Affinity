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

/** Log a parameterized message to the dnx system log file.
 * 
 * @param[in] fmt - a format specifier string similar to that of printf.
 */
void dnxLog(char * fmt, ... );

/** Log a parameterized message to the dnx DEBUG log.
 * 
 * This routine logs a debug message if the current global (configured) 
 * debug level is greater than or equal the value of @p level.
 * 
 * @param[in] level - the debug level at which to log the message.
 * @param[in] fmt - a format specifier string similar to that of printf.
 */
void dnxDebug(int level, char * fmt, ... );

/** Log a parameterized message to the global audit log file.
 * 
 * Returns quickly if auditing is disabled because a null or empty log file
 * was specified on startup.
 * 
 * @param[in] fmt - a format specifier string similar to that of printf.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxAudit(char * fmt, ... );

/** Initialize the logging sub-system with global references.
 * 
 * System and debug logging defaults to STDOUT. Both "STDOUT" and "STDERR"
 * may be specified as log file strings for the log, debug and audit file 
 * paths. The audit log is optional, and is disabled if @p auditFile is null 
 * or empty.
 * 
 * The address of the debug flag is passed so it can change the behavior of
 * the logging system dynamically.
 * 
 * @param[in] logFile - the global log file path.
 * @param[in] debugFile - the global debug file path.
 * @param[in] auditFile - the global audit file path (optional).
 * @param[in] debugLevel - the address of the global debug level indicator.
 */
void dnxLogInit(char * logFile, char * debugFile, char * auditFile, 
      int * debugLevel);

#endif   /* _DNXLOGGING_H_ */

