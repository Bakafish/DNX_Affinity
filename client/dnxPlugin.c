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

/** Utility routines to support plugin loading and execution.
 *
 * @file dnxPlugin.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_CLIENT_IMPL
 */

#include "dnxPlugin.h"

#include "dnxError.h"
#include "dnxDebug.h"
#include "pfopen.h"

#include <sys/types.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define MAX_PLUGIN_PREFIX     1024  /*!< The maximum plugin prefix. */
#define MAX_PLUGIN_PATH       2048  /*!< The maximum plugin path. */
#define DNX_MAX_ARGV          256   /*!< The maximum number of arguments. */
#define MAX_INPUT_BUFFER      1024  /*!< The maximum input buffer. */
#define DNX_MAX_PLUGIN_NAME   255   /*!< The maximum plugin name length. */

/** The DNX module implementation data structure. */
typedef struct iDnxModule
{
   char * path;               /*!< The module path and file name. */
   void * handle;             /*!< The dlopen module handle. */
   int (* init)(void);        /*!< The module initialization function. */
   int (* deinit)(void);      /*!< The module deinitialization function. */
   struct iDnxModule * next;  /*!< The "next module" pointer. */
   struct iDnxModule * prev;  /*!< The "previous module" pointer. */
} iDnxModule;

/** The DNX plugin implementation data structure. */
typedef struct iDnxPlugin
{
   char * name;               /*!< The DNX plugin name. */
   int (* func)(int argc, char ** argv);  
                              /*!< The plugin execution function. */
   iDnxModule * parent;       /*!< A pointer to the parent module object. */
   struct iDnxPlugin * next;  /*!< The "next plugin" pointer. */
   struct iDnxPlugin * prev;  /*!< The "previous plugin" pointer. */
} iDnxPlugin;

static iDnxModule * gModules; /*!< The loaded module chain. */
static iDnxPlugin * gPlugins; /*!< The loaded plugin chain. */
static char * gPluginPath;    /*!< The configured plugin path. */
static int gInitialized = 0;  /*!< The module initialization flag. */

// HACK: The NRPE module is hardwired for now...
#ifdef USE_NRPE_MODULE
extern int mod_nrpe(int argc, char ** argv, char * resData);
#endif

//----------------------------------------------------------------------------

/** Strip leading and trailing whitespace from a specified string.
 * 
 * Leading white space is removed by moving all text from the first non-
 * white space character and after to the beginning of @p buffer. Trailing
 * white space is removed by simply zero-terminating the string after the 
 * last non-white space character in @p buffer.
 * 
 * @param[in,out] buf - the buffer whose leading and trailing white space
 *    should be removed. 
 */
static void strip(char * buf)
{
   register char * cp, * ep;
   assert(buf);
   ep = buf + strlen(cp);
   while (ep > buf && !isspace(ep[-1])) ep--;
   *ep = 0;                      // terminate at last space (or existing null)
   cp = buf;
   while (isspace(*cp)) cp++;
   memmove(buf, cp, ep - cp);    // move zero or more characters
}

//----------------------------------------------------------------------------

int dnxPluginRegister(char * szPlugin, char * szErrMsg)
{
   assert(gInitialized);
   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxPluginLoad(DnxModule * module)
{
   iDnxModule * imod = (iDnxModule *)module;
   assert(gInitialized);
   assert(module);
   return DNX_OK;
}

//----------------------------------------------------------------------------

void dnxPluginUnload(DnxModule * module)
{
   iDnxPlugin * imod = (iDnxPlugin *)module;
   assert(gInitialized);
   assert(module);
}

//----------------------------------------------------------------------------

int dnxPluginExecute(char * command, int * resCode, char * resData, 
      int maxData, int timeout)
{
   DnxPlugin * plugin;
   int ret;

   assert(gInitialized);
   assert(command && resCode && resData && maxData > 1);

   // see if this is an internal or external plugin
   if ((ret = dnxPluginLocate(command, &plugin)) == DNX_OK)
      ret = dnxPluginInternal(plugin, command, resCode, resData, maxData, timeout);
   else if (ret == DNX_ERR_NOTFOUND)
      ret = dnxPluginExternal(command, resCode, resData, maxData, timeout);
   else
   {
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      sprintf(resData, "Unable to isolate plugin base name %d", ret);
   }
   return ret;
}

//----------------------------------------------------------------------------

int dnxPluginLocate(char * command, DnxPlugin ** plugin)
{
   char baseName[DNX_MAX_PLUGIN_NAME + 1];
   int ret;

   assert(gInitialized);
   assert(command && plugin);

   // isolate the plugin base name
   if ((ret = dnxPluginBaseName(command, baseName, DNX_MAX_PLUGIN_NAME)) != DNX_OK)
      return ret;

   /** @todo Search plugin chain. */

   // HACK: Hardwired to only find the check_nrpe plugin
#ifdef USE_NRPE_MODULE
   return (strcmp(baseName, "check_nrpe") ? DNX_ERR_NOTFOUND : DNX_OK);
#else
   return DNX_ERR_NOTFOUND;
#endif
}

//----------------------------------------------------------------------------

int dnxPluginBaseName(char * command, char * baseName, int maxData)
{
   char * cp, * ep, * base;
   int len;

   // skip leading whitespace
   for (cp = command; *cp && *cp <= ' '; cp++)
      ;

   if (!*cp) return DNX_ERR_INVALID; // no basename - string is empty

   // determine end of token
   for (ep=cp; *ep && *ep > ' '; ep++);
   if (ep == cp)
      return DNX_ERR_INVALID; // no basename - string is empty

   // determine start of base name within this token
   for (base = (ep-1); base > cp && *base != '/'; base--);
   if (base == (ep-1))
      return DNX_ERR_INVALID; // no basename - token ends with '/'
   if (*base == '/')
      cp = base+1;

   // validate base name length
   if ((len = (ep - cp)) > maxData)
      return DNX_ERR_MEMORY;  // insufficient room for base name

   memcpy(baseName, cp, len);
   baseName[len] = 0;

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxPluginInternal(DnxPlugin * plugin, char * command, int * resCode, 
      char * resData, int maxData, int timeout)
{
   char temp_buffer[MAX_INPUT_BUFFER + 1];
   char * argv[DNX_MAX_ARGV];
   int argc, len, ret;

   assert(gInitialized);

   /** @todo Validate plugin parameter. */

   assert(command && resCode && resData && maxData > 1 && timeout >= 0);

   // clear the result data buffer
   *resData = 0;

   // break-up command string into vectors
   if ((ret = dnxPluginVector(command, &argc, argv, DNX_MAX_ARGV)) != DNX_OK)
   {
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      sprintf(resData, "Unable to vectorize plugin command line %d", ret);
      return ret;
   }

   /** @todo Invoke plugin entry-point via DnxPlugin structure. */

   // HACK: Only works with static check_nrpe plugin for the moment
#ifdef USE_NRPE_MODULE
   *resCode = mod_nrpe(argc, argv, resData);
#else
   *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
   strcpy(resData, "(DNX: Internal NRPE modules unavailable)");
#endif

   // check for no output condition
   if (!resData[0])
      strcpy(resData, "(DNX: No output!)");

   // test for exception conditions:
   temp_buffer[0] = 0;

   // test for out-of-range plugin exit code
   if (*resCode < DNX_PLUGIN_RESULT_OK || *resCode > DNX_PLUGIN_RESULT_UNKNOWN)
   {
      len = strlen(temp_buffer);
      sprintf(temp_buffer+len, "[EC %d]", ((*resCode < 256) ? *resCode : (*resCode >> 8)));
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
   }

   // prepend any error condition messages to the plugin output
   if (temp_buffer[0])
   {
      strncat(temp_buffer, resData, MAX_INPUT_BUFFER);
      temp_buffer[MAX_INPUT_BUFFER] = 0;
      strncpy(resData, temp_buffer, maxData);
      resData[maxData-1] = 0;
   }

   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxPluginExternal(char * command, int * resCode, char * resData, 
      int maxData, int timeout)
{
   char temp_buffer[MAX_INPUT_BUFFER + 1];
   char temp_cmd[MAX_PLUGIN_PATH + 1];
   char * plugin, * cp, * bp, * ep;
   struct timeval tv;
   PFILE * pf;
   fd_set fd_read;
   int p_out, p_err;
   int count, fdmax;
   int len, isErrOutput = 0;
   time_t start_time;

   assert(gInitialized);
   assert(command && resCode && resData && maxData > 1);

   // initialize plugin output buffer
   *resData = 0;

   // find non-whitespace beginning of command string
   for (cp = command; *cp && *cp <= ' '; cp++)
      ;

   if (!*cp)
   {
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      sprintf(resData, "dnxPluginExternal: empty plugin command string");
      return DNX_OK;
   }

   // see if we are restricting plugin path
   if (gPluginPath)
   {
      // find end of plugin base name
      for (bp = ep = cp; *ep && *ep > ' '; ep++)
         if (*ep == '/') 
            bp = ep + 1;

      if (bp == ep)
      {
         *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
         sprintf(resData, "dnxPluginExternal: invalid plugin command string");
         return DNX_OK;
      }

      // verify that the restructured plugin path doesn't exceed our maximum
      if ((len = strlen(gPluginPath) + strlen(bp)) > MAX_PLUGIN_PATH)
      {
         *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
         sprintf(resData, "dnxPluginExternal: plugin command string exceeds max command string size");
         return DNX_OK;
      }

      // construct controlled plugin path
      strcpy(temp_cmd, gPluginPath);
      strcat(temp_cmd, bp);
      plugin = temp_cmd;
   }
   else
      plugin = cp;

   // execute the plugin check command
   if ((pf = pfopen(plugin, "r")) == 0)
   {
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      sprintf(resData, "dnxPluginExternal: pfopen failed with errno %d", errno);
      return DNX_OK;
   }

   // retrieve file descriptors for pipe's stdout/stderr
   p_out = fileno(PF_OUT(pf));
   p_err = fileno(PF_ERR(pf));

   // compute highest descriptor, plus one
   fdmax = ((p_out > p_err) ? p_out : p_err) + 1;

   // setup select on pipe's stdout and stderr
   FD_ZERO(&fd_read);
   FD_SET(p_out, &fd_read);
   FD_SET(p_err, &fd_read);

   // setup read timeout on pipe
   tv.tv_sec  = timeout;
   tv.tv_usec = 0L;

   // used for computing remaining time on pipe reads
   time(&start_time);

   // wait for some data to show up on the pipe
   /** @todo We can't count on only a single select call here. */
   if ((count = select(fdmax, &fd_read, 0, 0, &tv)) < 0)
   {
      // select error
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      sprintf(resData, "dnxPluginExternal: select on plugin pipe failed with errno %d", errno);
      pfkill(pf, SIGTERM);
      pfclose(pf);
      return DNX_OK;
   }
   else if (count == 0)
   {
      // plugin timeout
      *resCode = DNX_PLUGIN_RESULT_CRITICAL;
      strcpy(resData, "(DNX: Plugin Timeout)");
      pfkill(pf, SIGTERM);
      pfclose(pf);
      return DNX_OK;
   }

   // data is available on the pipe, so now we read it.
   if (FD_ISSET(p_out, &fd_read))   // first, check stdout
   {
      // consume plugin's stdout
      while (!resData[0] && fgets(resData, maxData, PF_OUT(pf)) != 0)
         strip(resData);
      while(fgets(temp_buffer, MAX_INPUT_BUFFER, PF_OUT(pf)));
   }

   if (!resData[0] && FD_ISSET(p_err, &fd_read))   // if nothing on stdout, then check stderr
   {
      // consume plugin's stderr
      while (!resData[0] && fgets(resData, maxData, PF_ERR(pf)) != 0)
         strip(resData);
      while(fgets(temp_buffer, MAX_INPUT_BUFFER, PF_ERR(pf)));

      isErrOutput = 1;
   }

   // check for no output condition
   if (!resData[0])
   {
      strcpy(resData, "(DNX: No output!)");
      isErrOutput = 0;
   }

   // close the pipe and harvest the exit code
   *resCode = (pfclose(pf) >> 8);

   // test for exception conditions:
   temp_buffer[0] = 0;

   // test for stderr output
   if (isErrOutput)
   {
      // prefix stderr message with [STDERR] disclaimer
      strcpy(temp_buffer, "[STDERR]");
   }

   // test for out-of-range plugin exit code
   if (*resCode < DNX_PLUGIN_RESULT_OK || *resCode > DNX_PLUGIN_RESULT_UNKNOWN)
   {
      len = strlen(temp_buffer);
      sprintf(temp_buffer+len, "[EC %d]", ((*resCode < 256) ? *resCode : (*resCode >> 8)));
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
   }

   // prepend any error condition messages to the plugin output
   if (temp_buffer[0])
   {
      strncat(temp_buffer, resData, MAX_INPUT_BUFFER);
      temp_buffer[MAX_INPUT_BUFFER] = 0;
      strncpy(resData, temp_buffer, maxData);
      resData[maxData-1] = 0;
   }

   return DNX_OK;
}

//----------------------------------------------------------------------------

char * dnxFgets(char * data, int size, FILE * fp, int timeout)
{
   struct timeval tv;
   fd_set fd_read;
   int count, fdmax;

   assert(data && size > 0 && fp && timeout > 0);

   data[0] = 0;

   // retrieve file descriptor
   fdmax = fileno(fp);

   // setup select
   FD_ZERO(&fd_read);
   FD_SET(fdmax, &fd_read);

   // setup read timeout on pipe
   tv.tv_sec  = timeout;
   tv.tv_usec = 0L;

   // wait for some data to show up on the pipe
   if ((count = select(fdmax + 1, &fd_read, 0, 0, &tv)) < 0)
      return 0;      // select error
   else if (count == 0)
      return 0;      // plugin timeout

   return fgets(data, size, fp);
}

//----------------------------------------------------------------------------

int dnxPluginVector(char * command, int * argc, char ** argv, int maxargs)
{
   char * cp, * ep;
   int idx = 0;
   int ret = DNX_OK;

   assert(command && argc && argv && maxargs > 0);

   // loop through the command string
   ep = command;
   while (idx < maxargs)
   {
      // find beginning of the next token
      for (cp=ep; *cp && *cp <= ' '; cp++);
      if (!*cp)
         break;   // No more tokens

      // search for end of token
      for (ep=cp; *ep && *ep > ' '; ep++);
      if (ep == cp)
         break;   // No more tokens

      // add this token to the arg vector array
      if ((argv[idx] = (char *)xmalloc((ep-cp)+1)) == 0)
      {
         ret = DNX_ERR_MEMORY;
         break;
      }
      memcpy(argv[idx], cp, (ep-cp));
      argv[idx][ep-cp] = 0;

      idx++;   // Increment arg vector index
   }

   // append null arg vector
   argv[idx] = 0;

   *argc = idx;   // aet the arg vector total

   return ret;
}

//----------------------------------------------------------------------------

int dnxPluginInit(char * pluginPath)
{
   int len, extra = 0;

   assert(!gInitialized);

   // clear private globals
   gPluginPath = 0;
   gModules = 0;
   gPlugins = 0;

   if (pluginPath)
   {
      if ((len = strlen(pluginPath)) < 1 || len > MAX_PLUGIN_PREFIX)
      {
         dnxSyslog(LOG_ERR, "dnxPluginInit: Invalid plugin path");
         return DNX_ERR_INVALID;
      }

      // ensure that the plugin path prefix is absolute
      if (*pluginPath != '/')
      {
         dnxSyslog(LOG_ERR, "dnxPluginInit: Plugin path is not absolute");
         return DNX_ERR_INVALID;
      }

      // ensure that plugin path has trailing '/'
      extra = (pluginPath[len-1] == '/') ? 0 : 1;
      if ((gPluginPath = (char *)xmalloc(len + 1 + extra)) == 0)
         return DNX_ERR_MEMORY;
      strcpy(gPluginPath, pluginPath);
      if (extra)
         strcat(gPluginPath, "/");
   }

   gInitialized = 1;

   return DNX_OK;
}

//----------------------------------------------------------------------------

void dnxPluginRelease(void)
{
   assert(gInitialized);

   if (gPluginPath)
   {
      xfree(gPluginPath);
      gPluginPath = 0;
   }

   /** @todo Release module and plugin chains. */

   gInitialized = 0;
}

/*--------------------------------------------------------------------------*/

