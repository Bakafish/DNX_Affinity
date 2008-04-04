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

/** Parses DNX Worker Node config file.
 *
 * @file dnxConfig.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_CLIENT_IMPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dnxError.h"
#include "dnxChannel.h"
#include "dnxConfig.h"
#include "dnxClientMain.h"

#define DNX_MAX_CFG_LINE   2048

typedef enum _DnxVarType_ 
{ 
   DNX_VAR_ERR = 0, DNX_VAR_STR, DNX_VAR_INT, DNX_VAR_DBL 
} DnxVarType;

typedef struct _DnxVarMap_ 
{
   char * szVar;
   DnxVarType varType;
   void * varStorage;
} DnxVarMap;

extern DnxGlobalData dnxGlobalData;

static DnxVarMap DnxVarDictionary[] = 
{
   { "channelAgent",         DNX_VAR_STR, NULL },
   { "channelDispatcher",    DNX_VAR_STR, NULL },
   { "channelCollector",     DNX_VAR_STR, NULL },
   { "poolInitial",          DNX_VAR_INT, NULL },
   { "poolMin",              DNX_VAR_INT, NULL },
   { "poolMax",              DNX_VAR_INT, NULL },
   { "poolGrow",             DNX_VAR_INT, NULL },
   { "wlmPollInterval",      DNX_VAR_INT, NULL },
   { "wlmShutdownGracePeriod", DNX_VAR_INT, NULL },
   { "threadRequestTimeout", DNX_VAR_INT, NULL },
   { "threadMaxTimeouts",    DNX_VAR_INT, NULL },
   { "threadTtlBackoff",     DNX_VAR_INT, NULL },
   { "logFacility",          DNX_VAR_STR, NULL },
   { "pluginPath",           DNX_VAR_STR, NULL },
   { "maxResultBuffer",      DNX_VAR_INT, NULL },
   { "debug",                DNX_VAR_INT, NULL },
   { NULL, DNX_VAR_ERR, NULL }
};

void displayGlobals (char *title);
int parseLine (char *szFile, int lineNo, char *szLine);
int validateVariable (char *szVar, char *szVal);
int strTrim (char *szLine);

//----------------------------------------------------------------------------

void initGlobals (void)
{
   // 'cause C doesn't allow non-constant initializers in static structures
   DnxVarDictionary[ 0].varStorage = &(dnxGlobalData.channelAgent);
   DnxVarDictionary[ 1].varStorage = &(dnxGlobalData.channelDispatcher);
   DnxVarDictionary[ 2].varStorage = &(dnxGlobalData.channelCollector);
   DnxVarDictionary[ 3].varStorage = &(dnxGlobalData.poolInitial);
   DnxVarDictionary[ 4].varStorage = &(dnxGlobalData.poolMin);
   DnxVarDictionary[ 5].varStorage = &(dnxGlobalData.poolMax);
   DnxVarDictionary[ 6].varStorage = &(dnxGlobalData.poolGrow);
   DnxVarDictionary[ 7].varStorage = &(dnxGlobalData.wlmPollInterval);
   DnxVarDictionary[ 8].varStorage = &(dnxGlobalData.wlmShutdownGracePeriod);
   DnxVarDictionary[ 9].varStorage = &(dnxGlobalData.threadRequestTimeout);
   DnxVarDictionary[10].varStorage = &(dnxGlobalData.threadMaxTimeouts);
   DnxVarDictionary[11].varStorage = &(dnxGlobalData.threadTtlBackoff);
   DnxVarDictionary[12].varStorage = &(dnxGlobalData.logFacility);
   DnxVarDictionary[13].varStorage = &(dnxGlobalData.pluginPath);
   DnxVarDictionary[14].varStorage = &(dnxGlobalData.maxResultBuffer);
   DnxVarDictionary[15].varStorage = &(dnxGlobalData.debug);
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
      lineNo = 0; // Clear line counter

      while (fgets(szLine, sizeof(szLine), fp) != NULL)
      {
         if ((ret = parseLine(szFile, lineNo, szLine)) != 0)
            break;   // Encountered error condition
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
      return 1;   // Parse error: no delimiter
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

/*--------------------------------------------------------------------------*/

