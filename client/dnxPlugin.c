//	dnxPlugin.c
//
//	Utility routines to support plugin loading and execution.
//
//	Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
//	First Written:   2006-09-09
//	Last Modified:   2007-03-21
//
//	License:
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License version 2 as
//	published by the Free Software Foundation.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <sys/types.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>

#include "dnxError.h"
#include "dnxPlugin.h"
#include "pfopen.h"


//
//	Constants
//

#define MAX_PLUGIN_PREFIX	1024
#define MAX_PLUGIN_PATH		2048
#define DNX_MAX_ARGV		256
#define MAX_INPUT_BUFFER	1024
#define DNX_MAX_PLUGIN_NAME	255


//
//	Structures
//


//
//	Globals
//

static DNX_MODULE *gModules;	// Module Chain
static DNX_MODULE *gPlugins;	// Plugin Chain
static char *gPluginPath;		// Plugin Path
static int gInitialized = 0;	// Initialization flag


//
//	Prototypes
//

// HACK: The NRPE module is hardwired for now...
#ifdef USE_NRPE_MODULE
extern int mod_nrpe(int argc, char **argv, char *resData);
#endif

static void strip (char *buffer);


//----------------------------------------------------------------------------
// Initializes the plugin utility library

int dnxPluginInit (char *pluginPath)
{
	int len, extra = 0;

	if (gInitialized)
	{
		syslog(LOG_ERR, "dnxPluginInit: Already initialized");
		return DNX_ERR_INVALID;	// Already initialized
	}

	// Clear private globals
	gPluginPath = NULL;
	gModules = NULL;
	gPlugins = NULL;

	// Validate parameters
	if (pluginPath)
	{
		if ((len = strlen(pluginPath)) < 1 || len > MAX_PLUGIN_PREFIX)
		{
			syslog(LOG_ERR, "dnxPluginInit: Invalid plugin path");
			return DNX_ERR_INVALID;	// Invalid path
		}

		// Ensure that the plugin path prefix is absolute
		if (*pluginPath != '/')
		{
			syslog(LOG_ERR, "dnxPluginInit: Plugin path is not absolute");
			return DNX_ERR_INVALID;	// Invalid path
		}

		// Ensure that plugin path has trailing '/'
		extra = (pluginPath[len-1] == '/') ? 0 : 1;
		if ((gPluginPath = (char *)malloc(len+1+extra)) == NULL)
			return DNX_ERR_MEMORY;
		strcpy(gPluginPath, pluginPath);
		if (extra)
			strcat(gPluginPath, "/");
	}

	gInitialized = 1;	// Module is initialized

	return DNX_OK;
}

//----------------------------------------------------------------------------
// Releases the plugin utility library

int dnxPluginRelease (void)
{
	if (!gInitialized)
		return DNX_ERR_INVALID;	// Not initialized

	gInitialized = 0;	// Module is de-initialized

	if (gPluginPath)
	{
		free(gPluginPath);
		gPluginPath = NULL;
	}

	// TODO: Release module and plugin chains

	return DNX_OK;
}

//----------------------------------------------------------------------------
// Registers a plugin with entry points

int dnxPluginRegister (char *szPlugin, char *szErrMsg)
{
	if (!gInitialized)
		return DNX_ERR_INVALID;	// Not initialized

	return DNX_OK;
}

//----------------------------------------------------------------------------
// Loads plugin module into memory

int dnxPluginLoad (DNX_MODULE *module)
{
	if (!gInitialized)
		return DNX_ERR_INVALID;	// Not initialized

	return DNX_OK;
}

//----------------------------------------------------------------------------
// Unloads plugin module from memory

int dnxPluginUnload (DNX_MODULE *module)
{
	if (!gInitialized)
		return DNX_ERR_INVALID;	// Not initialized

	return DNX_OK;
}

//----------------------------------------------------------------------------
// Finds and executes the plugin

int dnxPluginExecute (char *command, int *resCode, char *resData, int maxData, int timeout)
{
	DNX_PLUGIN *plugin;
	int ret;

	if (!gInitialized)
		return DNX_ERR_INVALID;	// Not initialized

	// Validate parameters
	if (!command || !resCode || !resData || maxData < 2)
		return DNX_ERR_INVALID;

	// See if this is an internal or external plugin
	if ((ret = dnxPluginLocate(command, &plugin)) == DNX_OK)
	{
		// Execute internally
		ret = dnxPluginInternal(plugin, command, resCode, resData, maxData, timeout);
	}
	else if (ret == DNX_ERR_NOTFOUND)
	{
		// Execute externally
		ret = dnxPluginExternal(command, resCode, resData, maxData, timeout);
	}
	else
	{
		*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
		sprintf(resData, "Unable to isolate plugin base name %d", ret);
	}

	return ret;
}

//----------------------------------------------------------------------------
// Searches for a plugin in the plugin chain

int dnxPluginLocate (char *command, DNX_PLUGIN **plugin)
{
	char baseName[DNX_MAX_PLUGIN_NAME+1];
	int ret;

	if (!gInitialized)
		return DNX_ERR_INVALID;	// Not initialized

	// Validate parameters
	if (!command || !plugin)
		return DNX_ERR_INVALID;

	// Isolate the plugin base name
	if ((ret = dnxPluginBaseName(command, baseName, DNX_MAX_PLUGIN_NAME)) != DNX_OK)
		return ret;

	// TODO: Search plugin chain

	// HACK: Hardwired to only find the check_nrpe plugin
#ifdef USE_NRPE_MODULE
	return (strcmp(baseName, "check_nrpe") ? DNX_ERR_NOTFOUND : DNX_OK);
#else
	return DNX_ERR_NOTFOUND;
#endif
}

//----------------------------------------------------------------------------
// Isolate base name of a plugin command

int dnxPluginBaseName (char *command, char *baseName, int maxData)
{
	char *cp, *ep, *base;
	int len;

	// Skip leading whitespace
	for (cp=command; *cp && *cp <= ' '; cp++);
	if (!*cp)
		return DNX_ERR_INVALID;	// No basename - string is empty

	// Determine end of token
	for (ep=cp; *ep && *ep > ' '; ep++);
	if (ep == cp)
		return DNX_ERR_INVALID;	// No basename - string is empty

	// Determine start of base name within this token
	for (base = (ep-1); base > cp && *base != '/'; base--);
	if (base == (ep-1))
		return DNX_ERR_INVALID;	// No basename - token ends with '/'
	if (*base == '/')
		cp = base+1;

	// Validate base name length
	if ((len = (ep - cp)) > maxData)
		return DNX_ERR_MEMORY;	// Insufficient room for base name

	memcpy(baseName, cp, len);
	baseName[len] = '\0';

	return DNX_OK;
}

//----------------------------------------------------------------------------
// Executes an internal plugin module

int dnxPluginInternal (DNX_PLUGIN *plugin, char *command, int *resCode, char *resData, int maxData, int timeout)
{
	char temp_buffer[MAX_INPUT_BUFFER+1];
	char *argv[DNX_MAX_ARGV];
	int argc, len, ret;

	if (!gInitialized)
		return DNX_ERR_INVALID;	// Not initialized

	// Validate parameters (TODO: Check plugin parameter)
	if (!command || !resCode || !resData || maxData < 2 || timeout < 0)
		return DNX_ERR_INVALID;

	// Clear the result data buffer
	*resData = '\0';

	// Break-up command string into vectors
	if ((ret = dnxPluginVector(command, &argc, argv, DNX_MAX_ARGV)) != DNX_OK)
	{
		*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
		sprintf(resData, "Unable to vectorize plugin command line %d", ret);
		return ret;
	}

	// TODO: Invoke plugin entry-point via DNX_PLUGIN structure

	// HACK: Only works with static check_nrpe plugin for the moment
#ifdef USE_NRPE_MODULE
	*resCode = mod_nrpe(argc, argv, resData);
#else
	*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
	strcpy(resData, "(DNX: Internal NRPE modules unavailable)");
#endif

	// Check for no output condition
	if (!resData[0])
		strcpy(resData, "(DNX: No output!)");

	// Test for exception conditions:
	temp_buffer[0] = '\0';

	// Test for out-of-range plugin exit code
	if (*resCode < DNX_PLUGIN_RESULT_OK || *resCode > DNX_PLUGIN_RESULT_UNKNOWN)
	{
		len = strlen(temp_buffer);
		sprintf(temp_buffer+len, "[EC %d]", ((*resCode < 256) ? *resCode : (*resCode >> 8)));
		*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
	}

	// Prepend any error condition messages to the plugin output
	if (temp_buffer[0])
	{
		strncat(temp_buffer, resData, MAX_INPUT_BUFFER);
		temp_buffer[MAX_INPUT_BUFFER] = '\0';
		strncpy(resData, temp_buffer, maxData);
		resData[maxData-1] = '\0';
	}

	return DNX_OK;
}

//----------------------------------------------------------------------------
// Executes an external plugin module

int dnxPluginExternal (char *command, int *resCode, char *resData, int maxData, int timeout)
{
	char temp_buffer[MAX_INPUT_BUFFER+1];
	char temp_cmd[MAX_PLUGIN_PATH+1];
	char *plugin, *cp, *bp, *ep;
	struct timeval tv;
	PFILE *pf;
	fd_set fd_read;
	int p_out, p_err;
	int count, fdmax;
	int len, isErrOutput = 0;
	time_t start_time;

	if (!gInitialized)
		return DNX_ERR_INVALID;	// Not initialized

	// Validate parameters
	if (!command || !resCode || !resData || maxData < 2)
	{
		syslog(LOG_ERR, "dnxPluginExternal: Invalid parameters");
		return DNX_ERR_INVALID;
	}

	// Initialize plugin output buffer
	*resData = '\0';

	// Find non-whitespace beginning of command string
	for (cp=command; *cp && *cp <= ' '; cp++);
	if (!*cp)
	{
		*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
		sprintf(resData, "dnxPluginExternal: empty plugin command string");
		return DNX_OK;
	}

	// See if we are restricting plugin path
	if (gPluginPath)
	{
		// Find end of plugin base name
		for (bp=ep=cp; *ep && *ep > ' '; ep++)
		{
			if (*ep == '/') bp = ep + 1;
		}
		if (bp == ep)
		{
			*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
			sprintf(resData, "dnxPluginExternal: invalid plugin command string");
			return DNX_OK;
		}

		// Verify that the restructured plugin path doesn't exceed our maximum
		if ((len = strlen(gPluginPath) + strlen(bp)) > MAX_PLUGIN_PATH)
		{
			*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
			sprintf(resData, "dnxPluginExternal: plugin command string exceeds max command string size");
			return DNX_OK;
		}

		// Construct controlled plugin path
		strcpy(temp_cmd, gPluginPath);
		strcat(temp_cmd, bp);
		plugin = temp_cmd;
	}
	else
		plugin = cp;

	// Execute the plugin check command
	if ((pf = pfopen(plugin, "r")) == NULL)
	{
		*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
		sprintf(resData, "dnxPluginExternal: pfopen failed with errno %d", errno);
		return DNX_OK;
	}

	// Retrieve file descriptors for pipe's stdout/stderr
	p_out = fileno(PF_OUT(pf));
	p_err = fileno(PF_ERR(pf));

	// Compute highest descriptor, plus one
	fdmax = ((p_out > p_err) ? p_out : p_err) + 1;

	// Setup select on pipe's stdout and stderr
	FD_ZERO(&fd_read);
	FD_SET(p_out, &fd_read);
	FD_SET(p_err, &fd_read);

	// Setup read timeout on pipe
	tv.tv_sec  = timeout;
	tv.tv_usec = 0L;

	// Used for computing remaining time on pipe reads
	time(&start_time);

	// Wait for some data to show up on the pipe
	if ((count = select(fdmax, &fd_read, NULL, NULL, &tv)) < 0)
	{
		// Select error
		*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
		sprintf(resData, "dnxPluginExternal: select on plugin pipe failed with errno %d", errno);
		pfkill(pf, SIGTERM);
		pfclose(pf);
		return DNX_OK;
	}
	else if (count == 0)
	{
		// Plugin timeout
		*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
		strcpy(resData, "(DNX: Plugin Timeout)");
		pfkill(pf, SIGTERM);
		pfclose(pf);
		return DNX_OK;
	}

	// Data is available on the pipe, so now we read it.
	if (FD_ISSET(p_out, &fd_read))	// First, check stdout
	{
		// Consume plugin's stdout
		while (!resData[0] && fgets(resData, maxData, PF_OUT(pf)) != NULL)
			strip(resData);
		while(fgets(temp_buffer, MAX_INPUT_BUFFER, PF_OUT(pf)));
	}

	if (!resData[0] && FD_ISSET(p_err, &fd_read))	// If nothing on stdout, then check stderr
	{
		// Consume plugin's stderr
		while (!resData[0] && fgets(resData, maxData, PF_ERR(pf)) != NULL)
			strip(resData);
		while(fgets(temp_buffer, MAX_INPUT_BUFFER, PF_ERR(pf)));

		isErrOutput = 1;
	}

	// Check for no output condition
	if (!resData[0])
	{
		strcpy(resData, "(DNX: No output!)");
		isErrOutput = 0;
	}

	// Close the pipe and harvest the exit code
	*resCode = (pfclose(pf) >> 8);

	// Test for exception conditions:
	temp_buffer[0] = '\0';

	// Test for stderr output
	if (isErrOutput)
	{
		// Prefix stderr message with [STDERR] disclaimer
		strcpy(temp_buffer, "[STDERR]");
	}

	// Test for out-of-range plugin exit code
	if (*resCode < DNX_PLUGIN_RESULT_OK || *resCode > DNX_PLUGIN_RESULT_UNKNOWN)
	{
		len = strlen(temp_buffer);
		sprintf(temp_buffer+len, "[EC %d]", ((*resCode < 256) ? *resCode : (*resCode >> 8)));
		*resCode = DNX_PLUGIN_RESULT_UNKNOWN;
	}

	// Prepend any error condition messages to the plugin output
	if (temp_buffer[0])
	{
		strncat(temp_buffer, resData, MAX_INPUT_BUFFER);
		temp_buffer[MAX_INPUT_BUFFER] = '\0';
		strncpy(resData, temp_buffer, maxData);
		resData[maxData-1] = '\0';
	}

	return DNX_OK;
}

//----------------------------------------------------------------------------
// Performs time sensitive fgets

char *dnxFgets (char *data, int size, FILE *fp, int timeout)
{
	struct timeval tv;
	fd_set fd_read;
	int count, fdmax;

	// Validate input parameters
	if (!data || size < 1 || !fp || timeout < 1)
	{
		errno = EINVAL;
		return NULL;
	}

	data[0] = '\0';

	// Retrieve file descriptor
	fdmax = fileno(fp);

	// Setup select
	FD_ZERO(&fd_read);
	FD_SET(fdmax, &fd_read);

	// Setup read timeout on pipe
	tv.tv_sec  = timeout;
	tv.tv_usec = 0L;

	// Wait for some data to show up on the pipe
	if ((count = select(fdmax+1, &fd_read, NULL, NULL, &tv)) < 0)
		return NULL;		// Select error
	else if (count == 0)
		return NULL;		// Plugin timeout

	return fgets(data, size, fp);
}

//----------------------------------------------------------------------------
// Converts plugin string to vector array

int dnxPluginVector (char *command, int *argc, char **argv, int max)
{
	char *cp, *ep;
	int idx = 0;
	int ret = DNX_OK;


	// Validate parameters
	if (!command || !argc || !argv || max < 1)
		return DNX_ERR_INVALID;

	// Loop through the command string
	ep = command;
	while (idx < max)
	{
		// Find beginning of the next token
		for (cp=ep; *cp && *cp <= ' '; cp++);
		if (!*cp)
			break;	// No more tokens

		// Search for end of token
		for (ep=cp; *ep && *ep > ' '; ep++);
		if (ep == cp)
			break;	// No more tokens

		// Add this token to the arg vector array
		if ((argv[idx] = (char *)malloc((ep-cp)+1)) == NULL)
		{
			ret = DNX_ERR_MEMORY;
			break;
		}
		memcpy(argv[idx], cp, (ep-cp));
		argv[idx][ep-cp] = '\0';

		idx++;	// Increment arg vector index
	}

	// Append null arg vector
	argv[idx] = NULL;

	*argc = idx;	// Set the arg vector total

	return ret;
}

//----------------------------------------------------------------------------
// Strips leading and trailing whitespace from the specified string

static void strip (char *buffer)
{
	register char *cp, *ep;

	if (!buffer || !*buffer)
		return;

	/* strip end of string */
	for (ep = buffer + strlen(buffer) - 1; ep >= buffer && *ep <= ' '; ep--);
	*++ep = '\0';

	/* strip beginning of string (by shifting) */
	for (cp = buffer; *cp && *cp <= ' '; cp++);
	if (cp != buffer) memmove(buffer, cp, (ep-cp)+1);
}

//----------------------------------------------------------------------------
