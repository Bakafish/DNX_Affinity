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

/** Test utility to read DNX Worker Node config file.
 *
 * @file readCfg.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define DNX_MAX_CFG_LINE	2048

typedef struct _DnxGlobalData_ {
	char *channelAgent;
	char *channelDispatcher;
	char *channelCollector;
	long  poolInitial;
	long  poolMin;
	long  poolMax;
	long  poolGrow;
	long  threadRequestTimeout;
	long  threadMaxTimeouts;
	long  threadIdle;
	long  logFacility;
	char *logEmail;
	char *pluginPath;
} DnxGlobalData;

typedef enum _DnxVarType_ { DNX_VAR_ERR = 0, DNX_VAR_STR, DNX_VAR_INT, DNX_VAR_DBL } DnxVarType;

typedef struct _DnxVarMap_ {
	char *szVar;
	DnxVarType varType;
	void *varStorage;
} DnxVarMap;

static DnxGlobalData globalData;

static DnxVarMap DnxVarDictionary[] = {
{ "channelAgent",         DNX_VAR_STR, NULL },
{ "channelDispatcher",    DNX_VAR_STR, NULL },
{ "channelCollector",     DNX_VAR_STR, NULL },
{ "poolInitial",          DNX_VAR_INT, NULL },
{ "poolMin",              DNX_VAR_INT, NULL },
{ "poolMax",              DNX_VAR_INT, NULL },
{ "poolGrow",             DNX_VAR_INT, NULL },
{ "threadRequestTimeout", DNX_VAR_INT, NULL },
{ "threadMaxTimeouts",    DNX_VAR_INT, NULL },
{ "threadIdle",           DNX_VAR_INT, NULL },
{ "logFacility",          DNX_VAR_STR, NULL },
{ "logEmail",             DNX_VAR_STR, NULL },
{ "pluginPath",           DNX_VAR_STR, NULL },
{ NULL, DNX_VAR_ERR, NULL }
};

void initGlobals (void);
void displayGlobals (char *title);
int parseFile (char *szFile);
int parseLine (char *szFile, int lineNo, char *szLine);
int validateVariable (char *szVar, char *szVal);
int strTrim (char *szLine);


//----------------------------------------------------------------------------

int main (int argc, char **argv)
{
	int ret = 0;

	// Check usage
	if (argc != 2)
	{
		fprintf(stderr, "usage: readCfg config-file-name\n");
		exit(1);
	}

	// Initialize global data
	initGlobals();

	// Display global variables: Before
	displayGlobals("\n\nBEFORE:\n=======");

	// Parse config file
	if ((ret = parseFile(argv[1])) == 0)
	{
		// Display global variables: After
		displayGlobals("\n\nAFTER:\n======");
	}

	return ret;
}

//----------------------------------------------------------------------------

void initGlobals (void)
{
	// 'cause C doesn't allow non-constant initializers in static structures
	DnxVarDictionary[ 0].varStorage = &(globalData.channelAgent);
	DnxVarDictionary[ 1].varStorage = &(globalData.channelDispatcher);
	DnxVarDictionary[ 2].varStorage = &(globalData.channelCollector);
	DnxVarDictionary[ 3].varStorage = &(globalData.poolInitial);
	DnxVarDictionary[ 4].varStorage = &(globalData.poolMin);
	DnxVarDictionary[ 5].varStorage = &(globalData.poolMax);
	DnxVarDictionary[ 6].varStorage = &(globalData.poolGrow);
	DnxVarDictionary[ 7].varStorage = &(globalData.threadRequestTimeout);
	DnxVarDictionary[ 8].varStorage = &(globalData.threadMaxTimeouts);
	DnxVarDictionary[ 9].varStorage = &(globalData.threadIdle);
	DnxVarDictionary[10].varStorage = &(globalData.logFacility);
	DnxVarDictionary[11].varStorage = &(globalData.logEmail);
	DnxVarDictionary[12].varStorage = &(globalData.pluginPath);
}

//----------------------------------------------------------------------------

void displayGlobals (char *title)
{
	static char *varFormat[] = { "ERROR", "%s", "%ld", "%f" };
	DnxVarMap *pMap;

	// Display title, is specified
	if (title)
		puts(title);

	// Dump values of global variables
	for (pMap = DnxVarDictionary; pMap->szVar; pMap++)
	{
		printf("%s = ", pMap->szVar);
		switch (pMap->varType)
		{
		case DNX_VAR_STR:
			printf(varFormat[pMap->varType], *((char **)(pMap->varStorage)));
			break;
		case DNX_VAR_INT:
			printf(varFormat[pMap->varType], *((long *)pMap->varStorage));
			break;
		case DNX_VAR_DBL:
			printf(varFormat[pMap->varType], *((double *)pMap->varStorage));
			break;
		default:
			printf("UNKNOWN-VAR-TYPE");
		}
		printf("\n");
	}
}

//----------------------------------------------------------------------------

int parseFile (char *szFile)
{
	char szLine[DNX_MAX_CFG_LINE];
	FILE *fp;
	int lineNo;
	int ret = 0;

	// Open the config file
	if ((fp = fopen(szFile, "r")) != NULL)
	{
		lineNo = 0;	// Clear line counter

		while (fgets(szLine, sizeof(szLine), fp) != NULL)
		{
			if ((ret = parseLine(szFile, lineNo, szLine)) != 0)
				break;	// Encountered error condition
		}

		// Close config file
		fclose(fp);
	}
	else
	{
		fprintf(stderr, "readCfg: Unable to open %s: %s\n", szFile, strerror(errno));
		ret = 2;
	}

	return ret;
}

//----------------------------------------------------------------------------

int parseLine (char *szFile, int lineNo, char *szLine)
{
	char *szVar, *szVal;
	char *cp;

	// Strip comments
	if ((cp = strchr(szLine, '#')) != NULL)
		*cp = '\0';

	// Strip trailing whitespace
	strTrim(szLine);

	// Check for blank lines
	if (!*szLine)
		return 0;

	// Look for equivalence delimiter
	if ((cp = strchr(szLine, '=')) == NULL)
	{
		fprintf(stderr, "parseLine: Missing '=' equivalence operator\n");
		return 1;	// Parse error: no delimiter
	}
	*cp++ = '\0';

	for (szVar = szLine; *szVar && *szVar <= ' '; szVar++);
	if (strTrim(szVar) < 1)
	{
		fprintf(stderr, "%s: Line %d: Missing or invalid variable\n", szFile, lineNo);
		return 1;
	}

	for (szVal = cp; *szVal && *szVal <= ' '; szVal++);
	if (strTrim(szVal) < 1)
	{
		fprintf(stderr, "%s: Line %d: Missing or invalid assignment value\n", szFile, lineNo);
		return 1;
	}

	// Validate the variable and its value
	return validateVariable(szVar, szVal);
}

//----------------------------------------------------------------------------

int validateVariable (char *szVar, char *szVal)
{
	DnxVarMap *pMap;
	char *eptr;
	int ret = 0;

	// Validate input paramters
	if (!szVar || !szVal)
	{
		fprintf(stderr, "validateVariable: null parameter(s)\n");
		return 1;
	}

	// Lookup this variable in the global variable map
	for (pMap = DnxVarDictionary; pMap->szVar && strcmp(szVar, pMap->szVar); pMap++);

	// Store the variable value
	switch (pMap->varType)
	{
	case DNX_VAR_STR:
		*((char **)(pMap->varStorage)) = strdup(szVal);
		break;
	case DNX_VAR_INT:
		errno = 0;
		*((long *)(pMap->varStorage)) = strtol(szVal, &eptr, 0);
		if (*eptr || errno)
		{
			fprintf(stderr, "Invalid integer value for %s: %s\n", szVar, szVal);
			ret = 1;
		}
		break;
	case DNX_VAR_DBL:
		errno = 0;
		*((double *)(pMap->varStorage)) = strtod(szVal, &eptr);
		if (*eptr || errno)
		{
			fprintf(stderr, "Invalid double value for %s: %s\n", szVar, szVal);
			ret = 1;
		}
		break;
	default:
		fprintf(stderr, "Unknown variable: %s\n", szVar);
		ret = 1;
	}

	return ret;
}

//----------------------------------------------------------------------------

int strTrim (char *szLine)
{
	char *cp;

	// Strip trailing whitespace
	for (cp = szLine + strlen(szLine) - 1; cp >= szLine && *cp <= ' '; cp--) *cp = '\0';

	return strlen(szLine);
}

//----------------------------------------------------------------------------
