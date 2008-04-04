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

/** Maximum log line length. */
#define MAX_LOG_LINE       1023

static int defDebugLevel = DEF_DEBUG_LEVEL;  //!< The default debug level.

static int * s_debugLevel = &defDebugLevel;  //!< Debug level pointer.
static char s_LogFileName[FILENAME_MAX + 1] = DEF_LOG_FILE;
static char s_DbgFileName[FILENAME_MAX + 1] = DEF_DEBUG_FILE;
static char s_AudFileName[FILENAME_MAX + 1] = "";

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
   FILE * fp_fopened = 0;
   FILE * fp = stdout;
   va_list ap;

   assert(fmt);
   
   // check first for standard file handle references
   if (*s_LogFileName && strcmp(s_LogFileName, "STDOUT") != 0)
   {
      if (strcmp(s_LogFileName, "STDERR") == 0)
         fp = stderr;
      else 
         fp_fopened = fopen(s_LogFileName, "a+");
   }

   va_start(ap, fmt);
   (void)vlogger(fp_fopened? fp_fopened: fp, fmt, ap);
   va_end(ap);

   if (fp_fopened)
      fclose(fp_fopened);
}

//----------------------------------------------------------------------------

void dnxDebug(int level, char * fmt, ... )
{
   assert(fmt);
   assert(s_debugLevel);

   if (level <= *s_debugLevel)
   {
      FILE * fp_fopened = 0;
      FILE * fp = stdout;
      va_list ap;

      // check first for standard file handle references
      if (*s_DbgFileName && strcmp(s_DbgFileName, "STDOUT") != 0)
      {
         if (strcmp(s_DbgFileName, "STDERR") == 0)
            fp = stderr;
         else
            fp_fopened = fopen(s_DbgFileName, "a+");
      }
   
      va_start(ap, fmt);
      (void)vlogger(fp_fopened? fp_fopened: fp, fmt, ap);
      va_end(ap);

      if (fp_fopened)
         fclose(fp_fopened);
   }
}

//----------------------------------------------------------------------------

int dnxAudit(char * fmt, ... )
{
   int ret = 0;

   assert(fmt);

   if (*s_AudFileName)
   {
      FILE * fp_fopened = 0;
      FILE * fp = 0;
      va_list ap;

      // check first for standard file handle references
      if (strcmp(s_AudFileName, "STDOUT") == 0)
         fp = stdout;
      else if (strcmp(s_AudFileName, "STDERR") == 0)
         fp = stderr;
      else if ((fp = fp_fopened = fopen(s_AudFileName, "a+")) == 0)
         return errno;

      va_start(ap, fmt);
      ret = vlogger(fp, fmt, ap);
      va_end(ap);

      if (fp_fopened)
         fclose(fp_fopened);
   }
   return ret;
}

//----------------------------------------------------------------------------

void dnxLogInit(char * logFile, char * debugFile, char * auditFile, 
      int * debugLevel)
{
   if (logFile)
   {
      strncpy(s_LogFileName, logFile, sizeof(s_LogFileName) - 1);
      s_LogFileName[sizeof(s_LogFileName) - 1] = 0;
   }
   if (debugFile)
   {
      strncpy(s_DbgFileName, debugFile, sizeof(s_DbgFileName) - 1);
      s_DbgFileName[sizeof(s_DbgFileName) - 1] = 0;
   }
   if (auditFile)
   {
      strncpy(s_AudFileName, auditFile, sizeof(s_AudFileName) - 1);
      s_AudFileName[sizeof(s_AudFileName) - 1] = 0;
   }
   s_debugLevel = debugLevel;
}

/*--------------------------------------------------------------------------*/

