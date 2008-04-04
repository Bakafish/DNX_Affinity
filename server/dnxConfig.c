//	dnxConfig.c
//
//	Parses DNX Server config file.
//
//	Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
//	First Written:   2006-07-11
//	Last Modified:   2007-08-22
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dnxError.h"
#include "dnxChannel.h"
#include "dnxConfig.h"
#include "dnxNebMain.h"
#include "dnxLogging.h"


//
//	Constants
//

#define DNX_MAX_CFG_LINE	2048


//
//	Structures
//

typedef enum _DnxVarType_ { DNX_VAR_ERR = 0, DNX_VAR_STR, DNX_VAR_INT, DNX_VAR_DBL } DnxVarType;

typedef struct _DnxVarMap_ {
	char *szVar;
	DnxVarType varType;
	void *varStorage;
} DnxVarMap;


//
//	Globals
//

extern DnxGlobalData dnxGlobalData;

static DnxVarMap DnxVarDictionary[] = {
{ "channelDispatcher",    DNX_VAR_STR, NULL },
{ "channelCollector",     DNX_VAR_STR, NULL },
{ "authWorkerNodes",      DNX_VAR_STR, NULL },
{ "maxNodeRequests",      DNX_VAR_INT, NULL },
{ "minServiceSlots",      DNX_VAR_INT, NULL },
{ "expirePollInterval",   DNX_VAR_INT, NULL },
{ "localCheckPattern",    DNX_VAR_STR, NULL },
{ "syncScript",           DNX_VAR_STR, NULL },
{ "logFacility",          DNX_VAR_STR, NULL },
{ "auditWorkerJobs",      DNX_VAR_STR, NULL },
{ "debug",                DNX_VAR_INT, NULL },
{ NULL, DNX_VAR_ERR, NULL }
};


//
//	Prototypes
//

void displayGlobals (char *title);
int parseLine (char *szFile, int lineNo, char *szLine);
int validateVariable (char *szVar, char *szVal);
int strTrim (char *szLine);


//----------------------------------------------------------------------------

void initGlobals (void)
{
	// 'cause C doesn't allow non-constant initializers in static structures
	DnxVarDictionary[ 0].varStorage = &(dnxGlobalData.channelDispatcher);
	DnxVarDictionary[ 1].varStorage = &(dnxGlobalData.channelCollector);
	DnxVarDictionary[ 2].varStorage = &(dnxGlobalData.authWorkerNodes);
	DnxVarDictionary[ 3].varStorage = &(dnxGlobalData.maxNodeRequests);
	DnxVarDictionary[ 4].varStorage = &(dnxGlobalData.minServiceSlots);
	DnxVarDictionary[ 5].varStorage = &(dnxGlobalData.expirePollInterval);
	DnxVarDictionary[ 6].varStorage = &(dnxGlobalData.localCheckPattern);
	DnxVarDictionary[ 7].varStorage = &(dnxGlobalData.syncScript);
	DnxVarDictionary[ 8].varStorage = &(dnxGlobalData.logFacility);
	DnxVarDictionary[ 9].varStorage = &(dnxGlobalData.auditWorkerJobs);
	DnxVarDictionary[10].varStorage = &(dnxGlobalData.debug);
}

//----------------------------------------------------------------------------

void displayGlobals (char *title)
{
	static char *varFormat[] = { "ERROR", "%s", "%ld", "%f" };
	DnxVarMap *pMap;

	// Display title, if specified
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
		dnxSyslog(LOG_ERR, "readCfg: Unable to open %s: %s", szFile, strerror(errno));
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
		dnxSyslog(LOG_ERR, "parseLine: Missing '=' equivalence operator");
		return 1;	// Parse error: no delimiter
	}
	*cp++ = '\0';

	for (szVar = szLine; *szVar && *szVar <= ' '; szVar++);
	if (strTrim(szVar) < 1)
	{
		dnxSyslog(LOG_ERR, "%s: Line %d: Missing or invalid variable", szFile, lineNo);
		return 1;
	}

	for (szVal = cp; *szVal && *szVal <= ' '; szVal++);
	if (strTrim(szVal) < 1)
	{
		dnxSyslog(LOG_ERR, "%s: Line %d: Missing or invalid assignment value", szFile, lineNo);
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
		dnxSyslog(LOG_ERR, "validateVariable: null parameter(s)");
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
			dnxSyslog(LOG_ERR, "Invalid integer value for %s: %s", szVar, szVal);
			ret = 1;
		}
		break;
	case DNX_VAR_DBL:
		errno = 0;
		*((double *)(pMap->varStorage)) = strtod(szVal, &eptr);
		if (*eptr || errno)
		{
			dnxSyslog(LOG_ERR, "Invalid double value for %s: %s", szVar, szVal);
			ret = 1;
		}
		break;
	default:
		dnxSyslog(LOG_ERR, "Unknown variable: %s", szVar);
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
