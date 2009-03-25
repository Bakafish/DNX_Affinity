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

/** An abstract data type for a DNX plugin object. */
typedef struct { int unused; } DnxPlugin;

/** An abstract data type for a DNX module object. */
typedef struct { int unused; } DnxModule;

static iDnxModule * gModules; /*!< The loaded module chain. */
static iDnxPlugin * gPlugins; /*!< The loaded plugin chain. */
static char * gPluginPath;    /*!< The configured plugin path. */
static int gInitialized = 0;  /*!< The module initialization flag. */

// HACK: The NRPE module is hardwired for now...
#ifdef USE_NRPE_MODULE
extern int mod_nrpe(int argc, char ** argv, char * resData);
#endif

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** Perform a time sensitive fgets.
 * 
 * @param[out] data - the address of storage for returning data from @p fp.
 * @param[in] size - the maximum size of @p data in bytes.
 * @param[in] fp - the file pointer to be read.
 * @param[in] timeout - the maximum number of seconds to wait for data to be
 *    returned before failing with a timeout error.
 * 
 * @return The address of @p data on success, or NULL on error.
 * 
 * @note Currently not used.
 */
static char * dnxFgets(char * data, int size, FILE * fp, int timeout)
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
   ep = buf + strlen(buf);
   while (ep > buf && !isspace(ep[-1])) ep--;
   *ep = 0;                      // terminate at last space (or existing null)
   cp = buf;
   while (isspace(*cp)) cp++;
   memmove(buf, cp, ep - cp);    // move zero or more characters
}

//----------------------------------------------------------------------------

/** Register a dnx plugin with entry points.
 * 
 * @param[in] szPlugin - the name of the plugin to be registered.
 * @param[in] szErrMsg - an error message to be displayed if the plugin
 *    could not be registered.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @note Currently not used.
 */
static int dnxPluginRegister(char * szPlugin, char * szErrMsg)
{
   assert(gInitialized);
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Load a dnx plugin module into memory.
 * 
 * @param[in] module - the name of the module to be loaded.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @note Currently not used.
 */
static int dnxPluginLoad(DnxModule * module)
{
   iDnxModule * imod = (iDnxModule *)module;
   assert(gInitialized);
   assert(module);
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Unload a dnx plugin module from memory.
 * 
 * @param[in] module - the name of the module to be unloaded.
 * 
 * @note Currently not used.
 */
static void dnxPluginUnload(DnxModule * module)
{
   iDnxPlugin * imod = (iDnxPlugin *)module;
   assert(gInitialized);
   assert(module);
}

//----------------------------------------------------------------------------

/** Isolate the base name of a plugin command.
 * 
 * @param[in] command - the command for which to have the base name isolated.
 * @param[out] baseName - the address of storage for the returned base name.
 * @param[in] maxData - the maximum size of the @p baseName buffer.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxPluginBaseName(char * command, char * baseName, int maxData)
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

/** Search for a plugin in the plugin chain.
 * 
 * @param[in] command - the command to be executed.
 * @param[out] plugin - the address of storage for the located plugin to 
 *    be returned.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxPluginLocate(char * command, DnxPlugin ** plugin)
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

/** Convert a dnx plugin string to a vector array.
 * 
 * The @p command buffer is modified such that each command argument is 
 * null-terminated on return.
 * 
 * @param[in] command - the string to be converted.
 * @param[out] argc - the address of storage for the number of elements 
 *    actually returned in @p argv.
 * @param[out] argv - the address of storage for returning a null-terminated 
 *    array of pointers to white-space-separated arguments in @p command.
 * @param[in] maxargs - the maximum number of entries in the @p argv array.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxPluginVector(char * command, int * argc, char ** argv, int maxargs)
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

/** Executes an internal plugin module.
 * 
 * @param[in] plugin - the plugin module to execute against @p command.
 * @param[in] command - the command to have @p plugin execute.
 * @param[out] resCode - the address of storage for the result code returned
 *    by @p plugin.
 * @param[out] resData - the resulting STDOUT text from the execution 
 *    of @p command by @p plugin.
 * @param[in] maxData - the maximum size of the @p resData buffer.
 * @param[in] timeout - the maximum number of seconds to wait for @p plugin
 *    to complete execution of @p command before returning a timeout error.
 * @param[in] myaddr - the address (in human readable format) of this DNX node.
 */
static void dnxPluginInternal(DnxPlugin * plugin, char * command, int * resCode, 
      char * resData, int maxData, int timeout, char * myaddr)
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
      resData += sprintf(resData, "(DNX: Vectorize command-line failed!)");
      if (myaddr)
         sprintf(resData, " (dnx node %s)", myaddr);
      return;
   }

   /** @todo Invoke plugin entry-point via DnxPlugin structure. */

   // HACK: Only works with static check_nrpe plugin for the moment
#ifdef USE_NRPE_MODULE
   *resCode = mod_nrpe(argc, argv, resData);
#else
   {
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      resData += sprintf(resData, "(DNX: Internal NRPE modules unavailable!)");
      if (myaddr)
         sprintf(resData, " (dnx node %s)", myaddr);
   }
#endif

   // check for no output condition
   if (!resData[0])
   {

      resData += sprintf(resData, "(No output!)");
      if (myaddr)
         sprintf(resData, " (dnx node %s)", myaddr);
   }

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
}

//----------------------------------------------------------------------------

/** Execute an external command line.
 * 
 * @param[in] command - the command to be executed.
 * @param[out] resCode - the address of storage for the result code returned
 *    by @p command.
 * @param[out] resData - the resulting STDOUT text from the execution 
 *    of @p command.
 * @param[in] maxData - the maximum size of the @p resData buffer.
 * @param[in] timeout - the maximum number of seconds to wait for @p command 
 *    to complete before returning a timeout error.
 * @param[in] myaddr - the address (in human readable format) of this DNX node.
 */

 static void dnxPluginExternal(char * command, int * resCode, char * resData, int maxData, int timeout, char * myaddr)
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
   cp = NULL;

   assert(gInitialized);
   assert(command && resCode && resData && maxData > 1);

   // initialize plugin output buffer
   *resData = 0;

   // find non-whitespace beginning of command string
   for (cp = command; *cp && *cp <= ' '; cp++);

   if (!*cp)
   {

      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      resData += sprintf(resData, "(DNX: Empty check command-line!)");
      if (myaddr)
         sprintf(resData, " (dnx node %s)", myaddr);
      return;
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
         resData += sprintf(resData, "(DNX: Invalid check command-line!");
         if (myaddr)
            sprintf(resData, " (dnx node %s)", myaddr);
         return;
      }

      // verify that the restructured plugin path doesn't exceed our maximum
      if ((len = strlen(gPluginPath) + strlen(bp)) > MAX_PLUGIN_PATH)
      {
         *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
         resData += sprintf(resData, "(DNX: Check command-line exceeds max size!)");
         if (myaddr)
            sprintf(resData, " (dnx node %s)", myaddr);
         return;
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
      resData += sprintf(resData, "(DNX: pfopen failed, %s!)", strerror(errno));

      if (myaddr)
         sprintf(resData, " (dnx node %s)", myaddr);

      if(pf)
        xfree(pf);

      return;
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
   // @todo We can't count on only a single select call here.
   if ((count = select(fdmax, &fd_read, 0, 0, &tv)) < 0)
   {
      // select error
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      resData += sprintf(resData, "(DNX: select failed on pipe, %s!)", strerror(errno));
      if (myaddr)
         sprintf(resData, " (dnx node %s)", myaddr);

      pfkill(pf, SIGTERM);
      pfclose(pf);

      return;
   }
   else if (count == 0)
   {
      // plugin timeout
      *resCode = DNX_PLUGIN_RESULT_CRITICAL;
      resData += sprintf(resData, "(DNX: Plugin Timed Out)");
      if (myaddr)
         sprintf(resData, " (dnx node %s)", myaddr);

      pfkill(pf, SIGTERM);
      sleep(1);
      pfkill(pf, SIGKILL);

      pfclose(pf);

      return;
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
      resData += sprintf(resData, "(No output!)");
      if (myaddr)
         sprintf(resData, " (dnx node %s)", myaddr);
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
      resData[maxData - 1] = 0;
   }

}

 /*
static void dnxPluginExternal(char * command, int * resCode, char * resData, 
      int maxData, int timeout, char * myaddr)
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
   cp = NULL;

   assert(gInitialized);
   assert(command && resCode && resData && maxData > 1);

   // initialize plugin output buffer
   *resData = 0;

   // find non-whitespace beginning of command string
   for (cp = command; *cp && *cp <= ' '; cp++);

   if (!*cp)
   {
      cp = resData;
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      cp += sprintf(cp, "(DNX: Empty check command-line!)");
      if (myaddr)
         sprintf(cp, " (dnx node %s)", myaddr);
      return;
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
         cp = resData;
         *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
         cp += sprintf(cp, "(DNX: Invalid check command-line!");
         if (myaddr)
            sprintf(cp, " (dnx node %s)", myaddr);
         return;
      }

      // verify that the restructured plugin path doesn't exceed our maximum
      if ((len = strlen(gPluginPath) + strlen(bp)) > MAX_PLUGIN_PATH)
      {
         cp = resData;
         *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
         cp += sprintf(cp, "(DNX: Check command-line exceeds max size!)");
         if (myaddr)
            sprintf(cp, " (dnx node %s)", myaddr);
         return;
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
      cp = resData;
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      cp += sprintf(cp, "(DNX: pfopen failed, %s!)", strerror(errno));
      if (myaddr)
         sprintf(cp, " (dnx node %s)", myaddr);
      if(pf)
        xfree(pf);

      if(cp)
        xfree(cp);

      return;
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
   // @todo We can't count on only a single select call here.
   if ((count = select(fdmax, &fd_read, 0, 0, &tv)) < 0)
   {
      // select error
      cp = resData;
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      cp += sprintf(cp, "(DNX: select failed on pipe, %s!)", strerror(errno));
      if (myaddr)
         sprintf(cp, " (dnx node %s)", myaddr);
      pfkill(pf, SIGTERM);
      pfclose(pf);

      return;
   }
   else if (count == 0)
   {
      // plugin timeout
      cp = resData;
      *resCode = DNX_PLUGIN_RESULT_CRITICAL;
      cp += sprintf(cp, "(DNX: Plugin Timed Out)");
      if (myaddr)
         sprintf(cp, " (dnx node %s)", myaddr);
      pfkill(pf, SIGTERM);
      pfclose(pf);

      return;
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
      cp = resData;
      cp += sprintf(cp, "(No output!)");
      if (myaddr)
         sprintf(cp, " (dnx node %s)", myaddr);
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
      resData[maxData - 1] = 0;
   }

}
*/

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

void dnxPluginExecute(char * command, int * resCode, char * resData, int maxData, int timeout, char * myaddr)
{
   DnxPlugin * plugin;
   int ret;

   assert(gInitialized);
   assert(command && resCode && resData && maxData > 1);

   dnxDebug(2, "dnxPluginExecute: Executing %s", command);

   // see if this is an internal or external plugin
   if ((ret = dnxPluginLocate(command, &plugin)) == DNX_OK)
   {
      dnxPluginInternal(plugin, command, resCode, resData,maxData, timeout, myaddr);
   }else if (ret == DNX_ERR_NOTFOUND){
      dnxPluginExternal(command, resCode, resData,maxData, timeout, myaddr);
   }else{
      *resCode = DNX_PLUGIN_RESULT_UNKNOWN;
      resData += sprintf(resData, "(DNX: Unable to isolate check base name!)");
      if (myaddr)
         sprintf(resData, " (dnx node %s)", myaddr);
   }
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
         dnxLog("Invalid plugin path.");
         return DNX_ERR_INVALID;
      }

      // ensure that the plugin path prefix is absolute
      if (*pluginPath != '/')
      {
         dnxLog("Plugin path is not absolute.");
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

