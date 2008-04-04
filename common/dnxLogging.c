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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#if HAVE_CONFIG_H
# include <config.h>
#endif

#ifndef LOCALSTATEDIR
# define LOCALSTATEDIR     "/var"
#endif

#define LOGDIR             LOCALSTATEDIR "/log"

#ifndef DEF_LOG_FILE
# define DEF_LOG_FILE      LOGDIR "/dnx.log"
#endif

#ifndef DEF_DEBUG_FILE
# define DEF_DEBUG_FILE    LOGDIR "/dnx.debug.log"
#endif

#ifndef DEF_DEBUG_LEVEL
# define DEF_DEBUG_LEVEL   0
#endif

#define MAX_LOG_LINE       1023              //!< Maximum log line length.

static int defDebugLevel   = DEF_DEBUG_LEVEL;//!< The default debug level.
static int * s_debugLevel  = &defDebugLevel; //!< A pointer to the debug level.
static FILE * s_logFile    = 0;              //!< The global log file.
static FILE * s_debugFile  = 0;              //!< The global debug file.
static FILE * s_auditFile  = 0;              //!< The global audit file.

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** A variable argument logger function that takes a stream.
 * 
 * @param[in] fp - the stream to write to.
 * @param[in] fmt - the format string to write.
 * @param[in] ap - the argument list to use.
 * 
 * @return zero on success or a non-zero error value if the data could not
 * be written.
 */
static int vlogger(FILE * fp, char * fmt, va_list ap)
{
   if (fp)
   {
      if (!isatty(fileno(fp)))
      {
         time_t tm = time(0);
         if (fprintf(fp, "[%.*s] ", 24, ctime(&tm)) < 0)
            return errno;
      }
      if (vfprintf(fp, fmt, ap) < 0)
         return errno;
      if (fputc('\n', fp) == EOF)
         return errno;
      if (fflush(fp) == EOF)
         return errno;
   }
   return 0;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

void dnxLog(char * fmt, ... )
{
   assert(fmt);

   va_list ap;
   va_start(ap, fmt);
   (void)vlogger(s_logFile? s_logFile: stdout, fmt, ap);
   va_end(ap);
}

//----------------------------------------------------------------------------

void dnxDebug(int level, char * fmt, ... )
{
   assert(fmt);

   if (level <= *s_debugLevel)
   {
      va_list ap;
      va_start(ap, fmt);
      (void)vlogger(s_debugFile? s_debugFile: stdout, fmt, ap);
      va_end(ap);
   }
}

//----------------------------------------------------------------------------

int dnxAudit(char * fmt, ... )
{
   int ret = 0;

   assert(fmt);

   if (s_auditFile)
   {
      va_list ap;
      va_start(ap, fmt);
      ret = vlogger(s_auditFile, fmt, ap);
      va_end(ap);
   }
   return ret;
}

//----------------------------------------------------------------------------

int dnxLogInit(char * logFile, char * debugFile, char * auditFile, 
      int * debugLevel)
{
   // set debug level pointer
   s_debugLevel = debugLevel;

   // open log file - default to stdout (global default)
   s_logFile = s_debugFile = stdout;
   if (logFile && *logFile && strcmp(logFile, "STDOUT") != 0)
   {
      if (strcmp(logFile, "STDERR") == 0)
         s_logFile = stderr;
      else
      {
         int eno;
         mode_t oldmask = umask(0077);    // only owner can read/write
         s_logFile = fopen(logFile, "a");
         eno = errno;
         umask(oldmask);
         if (!s_logFile)
         {
            s_logFile = stdout;           // stdout on failure; return error
            return eno;
         }
      }
   }

   // open debug file
   if (debugFile && *debugFile || strcmp(debugFile, "STDOUT") != 0)
   {
      if (strcmp(debugFile, "STDERR") == 0)
         s_debugFile = stderr;
      else
      {
         int eno;
         mode_t oldmask = umask(0077);
         s_debugFile = fopen(debugFile, "a");
         eno = errno;
         umask(oldmask);
         if (!s_debugFile)
         {
            s_debugFile = stdout;
            return eno;
         }
      }
   }

   // open audit log
   if (auditFile && *auditFile)
   {
      if (strcmp(auditFile, "STDOUT") == 0)
         s_auditFile = stdout;
      else if (strcmp(debugFile, "STDERR") == 0)
         s_auditFile = stderr;
      else
      {
         int eno;
         mode_t oldmask = umask(0077);
         s_auditFile = fopen(auditFile, "a");
         eno = errno;
         umask(oldmask);
         if (!s_auditFile)
            return eno;
      }
   }
   return DNX_OK;
}

//----------------------------------------------------------------------------

void dnxLogExit(void)
{
   if (s_auditFile && s_auditFile != stdout && s_auditFile != stderr) 
      fclose(s_auditFile);
   if (s_debugFile && s_debugFile != stdout && s_auditFile != stderr) 
      fclose(s_debugFile);
   if (s_logFile && s_logFile != stdout && s_logFile != stderr) 
      fclose(s_logFile);
   s_logFile = s_debugFile = s_auditFile = 0;
}

/*--------------------------------------------------------------------------*/

