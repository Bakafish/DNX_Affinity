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

/** Test harness for the dnxPlugin.c module.
 *
 * @file plugtest.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "dnxError.h"
#include "dnxPlugin.h"

#define MAX_RES_DATA	1024


int main (int argc, char **argv)
{
	int i, myArgc;
	char *myArgv[256];
	char *command;
	int resCode;
	char resData[MAX_RES_DATA+1];
	int ret;

	// Initialize the plugin module
	if ((ret = dnxPluginInit("/usr/local/nagios/libexec/")) != DNX_OK)
	//if ((ret = dnxPluginInit(NULL)) != DNX_OK)
	{
		fprintf(stderr, "Failed to initialize DNX Plugin Module: %d\n", ret);
		exit(1);
	}

	// Test plugin base name parsing
	if (argc > 1)
	{
		printf("Parsing command: \"%s\"\n", argv[1]);
		fflush(stdout);
		if ((ret = dnxPluginBaseName(argv[1], resData, MAX_RES_DATA)) == DNX_OK)
			printf("Plugin base name = \"%s\"\n", resData);
		else
			fprintf(stderr, "Base name parsing failed: %d\n", ret);
	}

	// Set test command string
	//command = "  /usr/local/bin/wanker   -c rock -v on  wak bak pak   ZZZ   ";
	//command = "  wanker   -c rock -v on  wak bak pak   ZZZ   ";
	//command = "  check_procs   -c rock -v on  wak bak pak   ZZZ   ";
	//command = "  check_procs   ";
	//command = "  sleep   15   ";
	//command = "  sleep   5   ";
	//command = "  check_nrpe   ";
	//command = "  /opt/nagios/libexec/check_nrpe -H localhost -c check_procs_name -a 1:4 1:4 sendmail ";
	command = "  /bin/true   ";

	printf("Test Command: \"%s\"\n", command);
	fflush(stdout);

	// Test command line vectorization
	if ((ret = dnxPluginVector(command, &myArgc, myArgv, 256)) != DNX_OK)
	{
		fprintf(stderr, "Failed to vectorize command string: %d\n", ret);
		exit(2);
	}

	// Display command vectors
	printf("Vectored command string: %d vectors\n", myArgc);
	for (i=0; i < myArgc; i++)
		printf("%02d: \"%s\"\n", i, myArgv[i]);
	fflush(stdout);

	// Test plugin execution
	printf("Testing command execution:\n");
	fflush(stdout);

	if ((ret = dnxPluginExecute(command, &resCode, resData, MAX_RES_DATA, 10)) != DNX_OK)
	{
		fprintf(stderr, "Failed to execute plugin: %d\n", ret);
		exit(3);
	}

	// Display plugin execution results
	printf("Command Execution results:\n");
	printf("Result Code = %d\n", resCode);
	printf("Result Data = \"%s\"\n", resData);
	fflush(stdout);
	
	// Release the plugin module
	if ((ret = dnxPluginRelease()) != DNX_OK)
	{
		fprintf(stderr, "Failed to release DNX Plugin Module: %d\n", ret);
		exit(4);
	}

	return 0;
}
