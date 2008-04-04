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

/** Parses a standard Unix config file.
 * 
 * The file format follows a rather trival format:
 * 
 * cfg-file = cfg-line [, cfg-line]
 * cfg-line = cfg-var '=' cfg-val '\n'
 * cfg-var  = (any alpha numeric text)
 * cfg-val  = (any alpha numeric text)
 * 
 * In addition, these rules must be followed:
 * 
 * 1. White space may be found anywhere within the file.
 * 2. cfg-line constructs may not contain line breaks.
 * 3. Line comments of the form '#' (any text) may be found anywhere.
 * 4. cfg-var constructs may not contain '=' characters.
 * 
 * @file dnxCfgParser.c
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxCfgParser.h"

#include "dnxLogging.h"
#include "dnxError.h"
#include "dnxDebug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

#define DNX_MAX_CFG_LINE   2048     //!< Longest allowed config file line.

/** The internal type of a DNX configuration parser object. */
typedef struct iDnxCfgParser
{
   char * cfgfile;
   char ** cfgdefs;
   char ** cmdover;
   DnxCfgDict * dict;
} iDnxCfgParser;

/** A typedef describing a single type variable parser. */
typedef int dnxVarParser_t(char * val, DnxCfgType type, void * prval);
   
/** A static array of allocated configuration file types. */
static DnxCfgType ptrtypes[] = { DNX_CFG_STRING, DNX_CFG_STRING_ARRAY, 
      DNX_CFG_INT_ARRAY, DNX_CFG_UNSIGNED_ARRAY, DNX_CFG_ADDR, 
      DNX_CFG_ADDR_ARRAY, DNX_CFG_URL, DNX_CFG_FSPATH };

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** Convert a delimited string into a null-terminated array of strings.
 * 
 * @param[in] str - the delimited string to be converted.
 * @param[in] delim - the delimiter character for which to search.
 * 
 * @return A null-terminated array of pointers to strings, or null on 
 *    memory allocation failure.
 */
static char ** strToStrArray(char * str, char delim)
{
   int cnt;
   size_t strsz;
   char * p, ** sap;

   // first check for empty string
   if (!str) return 0;

   // count elements first
   (cnt = 1), p = str;
   while ((p = strchr(p, delim)) != 0)
      p++, cnt++;

   // allocate char* array and string buffer
   strsz = strlen(str) + 1;
   if ((sap = (char **)xmalloc((cnt + 1) * sizeof *sap + strsz)) == 0)
      return 0;

   // copy string into storage buffer at end of array
   p = (char *)&sap[cnt + 1];
   memcpy(p, str, strsz);

   // store pointers in ptr array, and terminate strings
   (cnt = 0), sap[cnt++] = p;
   while ((p = strchr(p, delim)) != 0)
      (*p++ = 0), sap[cnt++] = p;
   sap[cnt] = 0;

   return sap;
}

//----------------------------------------------------------------------------
 
/** Zero all value array pointer values.
 *
 * @param[in] dict - the dictionary describing the field value types.
 * @param[out] ppvals - the value array whose pointer types are to be cleared.
 */
static void clearPtrValues(DnxCfgDict * dict, void * ppvals[])
{
   unsigned i, j;

   for (i = 0; dict[i].varname; i++)
      for (j = 0; j < elemcount(ptrtypes); j++)
         if (dict[i].type == ptrtypes[j])
         {
            *(void **)ppvals[i] = 0;
            break;
         }
}

//----------------------------------------------------------------------------
 
/** Validate a URL for correctness.
 * 
 * @param[in] url - the URL to be validated.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @todo Implement validateURL.
 */
static int validateURL(char * url) { return 0; }

//----------------------------------------------------------------------------
 
/** Validate a file system path for correctness.
 * 
 * @param[in] path - the path to be validated.
 * 
 * @return Zero on success, or a non-zero error value.
 * 
 * @todo Implement validateFSPath.
 */
static int validateFSPath(char * path) { return 0; }

//----------------------------------------------------------------------------

/** Validate and return a copy of a string.
 * 
 * Any non-zero value passed in @p prval is assumed to be a pointer to 
 * previously allocated memory. It will be freed before reassigning to 
 * the new value.
 * 
 * @param[in] val - the value to be validated.
 * @param[in] type - the high-level type (URL, FSPATH, or STRING) of @p val.
 * @param[in,out] prval - the address of storage for the returned allocated
 *    string buffer address.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int parseString(char * val, DnxCfgType type, char ** prval)
{
   int ret;
   char * str;
   assert(type == DNX_CFG_URL || type == DNX_CFG_FSPATH 
         || type == DNX_CFG_STRING);
   if (type == DNX_CFG_URL && (ret = validateURL(val)) != 0)
      return ret;
   if (type == DNX_CFG_FSPATH && (ret = validateFSPath(val)) != 0)
      return ret;
   if ((str = xstrdup(val)) == 0)
      return DNX_ERR_MEMORY;
   xfree(*prval);    // free old, return new
   *prval = str;
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Trim leading and trailing white space from a string.
 * 
 * Leading white space is trimmed by returning a pointer to the first non-
 * white-space character in @p s. Trailing white space is trimmed by null-
 * terminating @p s after the last non-white-space character.
 * 
 * @param[in] s - the string to be trimmed.
 * 
 * @return A pointer to the first non-white space character in @p s.
 */
static char * strtrim(char * s)
{
   assert(s);
   while (isspace(*s)) s++;
   if (*s)
   {
      size_t l = strlen(s);
      while (l && isspace(s[l - 1])) l--;
      s[l] = 0;
   }
   return s;
}

//----------------------------------------------------------------------------

/** Validate and return a null-terminated array of sockaddr_storage objects.
 * 
 * Any non-zero value passed in @p prval is assumed to be a pointer to 
 * previously allocated array memory. It will be freed before reassigning to 
 * the new buffer value.
 * 
 * @param[in] val - the value to be validated.
 * @param[in] type - the high-level type (ADDR_ARRAY) of @p val.
 * @param[in,out] prval - the address of storage for the returned allocated
 *    array buffer address.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int parseStringArray(char * val, DnxCfgType type, char *** prval)
{
   char ** array;
   int i;

   assert(type == DNX_CFG_STRING_ARRAY);

   // parse value string into sub-string array
   if ((array = strToStrArray(val, ',')) == 0)
      return DNX_ERR_MEMORY;

   // trim trailing and leading white space on each sub-string
   for (i = 0; array[i]; i++)
      array[i] = strtrim(array[i]);

   xfree(*prval);
   *prval = array;
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Validate and an integer or unsigned integer value.
 * 
 * @param[in] val - the value to be validated.
 * @param[in] type - the high-level type (INT or UNSIGNED) of @p val.
 * @param[in,out] prval - the address of storage for the returned int.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int parseIntOrUnsigned(char * val, DnxCfgType type, int * prval)
{
   char * ep;
   long (*str2num)(char*, char**, int) = 
         (type == DNX_CFG_INT? (void*)strtol: (void*)strtoul);
   long n = str2num(val, &ep, 0);
   assert(type == DNX_CFG_INT || type == DNX_CFG_UNSIGNED);
   if (*ep != 0) return DNX_ERR_SYNTAX;
   *prval = (int)n;
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Validate and return a buffer of integers beginning with a count.
 * 
 * Any non-zero value passed in @p prval is assumed to be a pointer to 
 * previously allocated array memory. It will be freed before reassigning to 
 * the new buffer value.
 * 
 * The buffer is returned with one extra element; the first element is a 
 * count of the remaining elements of the array.
 * 
 * @param[in] val - the value to be validated.
 * @param[in] type - the high-level type (INT_ or UNSIGNED_ARRAY) of @p val.
 * @param[in,out] prval - the address of storage for the returned allocated
 *    array buffer address.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int parseIntOrUnsignedArray(char * val, DnxCfgType type, int ** prval)
{
   int * array;
   char ** sa;
   int i;

   assert(type == DNX_CFG_INT_ARRAY || type == DNX_CFG_UNSIGNED_ARRAY);

   // parse value string into a sub-string array
   if ((sa = strToStrArray(val, ',')) == 0)
      return DNX_ERR_MEMORY;

   // count sub-strings and trim trailing and leading white space
   for (i = 0; sa[i]; i++)
      sa[i] = strtrim(sa[i]);

   // allocate space for count + ints
   if ((array = (int *)xmalloc((i + 1) * sizeof *array)) == 0)
      return xfree(sa), DNX_ERR_MEMORY;

   // setup for call to parseIntOrUnsigned
   type = (type == DNX_CFG_INT_ARRAY)? DNX_CFG_INT : DNX_CFG_UNSIGNED;

   // convert each value in sa to an int or unsigned in array
   array[0] = i;   // store count in first integer slot
   for (i = 0; sa[i]; i++)
   {
      int ret;
      if ((ret = parseIntOrUnsigned(sa[i], type, &array[i + 1])) != 0)
         return xfree(array), xfree(sa), ret;
   }
   xfree(sa);
   xfree(*prval);
   *prval = array;
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Validate an address for correctness, and convert to sockaddr_storage.
 * 
 * @param[in] addr - the URL to be validated.
 * @param[out] ss - the address of storage for the converted address.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int validateIPAddr(char * addr, struct sockaddr_storage * ss)
{
   int ret;
   struct addrinfo * ai;
   if ((ret = getaddrinfo(addr, 0, 0, &ai)) != 0)
      return ret;
   memcpy(ss, ai[0].ai_addr, ai[0].ai_addrlen);
   freeaddrinfo(ai);
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Parse, validate and return an allocated sockaddr_storage object.
 * 
 * Any non-zero value passed in @p prval is assumed to be a pointer to 
 * previously allocated sockaddr_storage object memory. It will be freed 
 * before reassigning to the new object address value.
 * 
 * @param[in] val - the value to be validated.
 * @param[in] type - the high-level type (ADDR) of @p val.
 * @param[in,out] prval - the address of storage for the returned allocated
 *    sockaddr_storage object address.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int parseAddr(char * val, DnxCfgType type, 
      struct sockaddr_storage ** prval)
{
   int ret;
   struct sockaddr_storage * ss;
   assert(type == DNX_CFG_ADDR);
   if ((ss = (struct sockaddr_storage *)xmalloc(sizeof *ss)) == 0)
      return DNX_ERR_MEMORY;
   if ((ret = validateIPAddr(val, ss)) != 0)
      return xfree(ss), ret;
   xfree(*prval);
   *prval = ss;
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Validate and return a null-terminated array of sockaddr_storage objects.
 * 
 * Any non-zero value passed in @p prval is assumed to be a pointer to 
 * previously allocated array memory. It will be freed before reassigning to 
 * the new buffer value.
 * 
 * @param[in] val - the value to be validated.
 * @param[in] type - the high-level type (ADDR_ARRAY) of @p val.
 * @param[in,out] prval - the address of storage for the returned allocated
 *    array buffer address.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int parseAddrArray(char * val, DnxCfgType type, 
      struct sockaddr_storage *** prval)
{
   struct sockaddr_storage ** array, * sp;
   char ** sa;
   int i;

   assert(type == DNX_CFG_ADDR_ARRAY);

   // parse value string into a sub-string array
   if ((sa = strToStrArray(val, ',')) == 0)
      return DNX_ERR_MEMORY;

   // count sub-strings and trim trailing and leading white space
   for (i = 0; sa[i]; i++)
      sa[i] = strtrim(sa[i]);

   // allocate space for count + ints
   if ((array = (struct sockaddr_storage **)xmalloc(
         (i + 1) * sizeof *array + i * sizeof **array)) == 0)
      return xfree(sa), DNX_ERR_MEMORY;

   // locate structure array at end of ptr array
   sp = (struct sockaddr_storage *)&array[i + 1];

   // parse addrs and ptrs into both arrays
   for (i = 0; sa[i]; i++)
   {
      int ret;
      struct sockaddr_storage * ssp = 0;
      if ((ret = parseAddr(sa[i], DNX_CFG_ADDR, &ssp)) != 0)
         return xfree(array), xfree(sa), ret;
      memcpy(sp, ssp, sizeof *sp);
      array[i] = sp++;
      xfree(ssp);
   }
   array[i] = 0;   // terminate ptr array
   xfree(sa);
   xfree(*prval);
   *prval = array;
   return DNX_OK;
}

//----------------------------------------------------------------------------
 
/** Validate and convert a single variable/value pair against a dictionary. 
 * 
 * The value is parsed, converted according to the dictionary-specified 
 * type, and stored in the corresponding @p ppvals data field. If the type is
 * a pointer type, the existing value is freed, and new memory is allocated.
 * 
 * @param[in] var - the variable name to be validated.
 * @param[in] val - the string form of the value to be parsed, validated,
 * @param[in] dict - an array of legal variable names and types.
 * @param[out] ppvals - an array of return value storage locations, where 
 *    each element is the storage location for the variable indicated by the
 *    corresponding location in the @p dict array.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxParseCfgVar(char * var, char * val, DnxCfgDict * dict, 
      void * ppvals[])
{
   static dnxVarParser_t * parsetbl[] =
   {
      (dnxVarParser_t *)parseString,
      (dnxVarParser_t *)parseStringArray,
      (dnxVarParser_t *)parseIntOrUnsigned,
      (dnxVarParser_t *)parseIntOrUnsignedArray,
      (dnxVarParser_t *)parseIntOrUnsigned,
      (dnxVarParser_t *)parseIntOrUnsignedArray,
      (dnxVarParser_t *)parseAddr,
      (dnxVarParser_t *)parseAddrArray,
      (dnxVarParser_t *)parseString,
      (dnxVarParser_t *)parseString,
   };
   
   unsigned i;

   for (i = 0; dict[i].varname; i++)
      if (strcmp(dict[i].varname, var) == 0)
      {
         assert(dict[i].type >= 0 && dict[i].type < elemcount(parsetbl));
         return parsetbl[dict[i].type](val, dict[i].type, ppvals[i]);
      }

   return DNX_ERR_INVALID; // invalid entry - no dictionary mapping
}
   
//----------------------------------------------------------------------------
 
/** Parse a single line of a configuration file.
 * 
 * @param[in] s - a buffer containing the line text to be parsed. This
 *    buffer is constant and will not be written to.
 * @param[in] dict - the configuration dictionary.
 * @param[out] ppvals - an array of return storage addresses for parsed values.
 *    Each element returns the parsed value for the corresponding element of 
 *    the @p dict array.
 * 
 * @return Zero on success, or a non-zero error value. Possible error 
 * values include DNX_OK (on success), DNX_ERR_SYNTAX or DNX_ERR_MEMORY.
 */
static int dnxParseCfgLine(char * s, DnxCfgDict * dict, void * ppvals[])
{
   char * cpy, * val;
   int ret;

   // trim comment from end of line
   if ((val = strchr(s, '#')) != 0) *val = 0;

   // trim leading and trailing ws; return success on empty line
   if (*(s = strtrim(s)) == 0) return 0;

   // look for assignment operator; must be in the middle of the text
   if (*s == '=' || (val = strchr(s, '=')) == 0 || val[1] == 0)
      return DNX_ERR_SYNTAX;

   // make a working copy of the remaining buffer text
   if ((cpy = xstrdup(s)) == 0)
      return DNX_ERR_MEMORY;

   // copy remaining text; reset val ptr; terminate copy at '='
   val = &cpy[val - s];
   *val++ = 0;

   // trim trailing space from copy, leading space from value
   strtrim(cpy);
   val = strtrim(val);
      
   // validate, convert and store the variable and its value
   ret = dnxParseCfgVar(cpy, val, dict, ppvals);

   xfree(cpy);

   return ret;
}

//----------------------------------------------------------------------------
 
/** Apply an array of configuration parameters to a variable array.
 * 
 * Each parameter has the same format as a line in a configuration file.
 * 
 * @param[in] sap - the null-terminated configuration string set to be 
 *    applied to @p ppvals. This parameter may be NULL, in which case nothing
 *    is done and DNX_OK is returned.
 * @param[in] dict - the configuration dictionary to use.
 * @param[out] ppvals - an array of return storage addresses for parsed values.
 *    Each element returns the parsed value for the corresponding element of 
 *    the @p dict array.
 * 
 * @return Zero on success, or a non-zero error value. Possible return values 
 * include DNX_OK (on success) or DNX_ERR_MEMORY.
 */
static int applyCfgSetString(char ** sap, DnxCfgDict * dict, void * ppvals[])
{
   int ret;

   assert(dict && ppvals);

   if (sap)
      while (*sap)
         if ((ret = dnxParseCfgLine(*sap++, dict, ppvals)) != 0)
            return ret;

   return DNX_OK;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

int dnxCfgParserCreate(char * cfgdefs, char * cfgfile, char * cmdover, 
      DnxCfgDict * dict, DnxCfgParser ** cpp)
{
   iDnxCfgParser * icp;
   int ret = DNX_ERR_MEMORY;

   assert(cfgfile && *cfgfile && dict && cpp);

   if ((icp = (iDnxCfgParser *)xmalloc(sizeof *icp)) == 0)
      return ret;
   memset(icp, 0, sizeof *icp);

   if ((icp->cfgfile = xstrdup(cfgfile)) == 0
         || cfgdefs && (icp->cfgdefs = strToStrArray(cfgdefs, '\n')) == 0
         || cmdover && (icp->cmdover = strToStrArray(cmdover, '\n')) == 0)
   {
      xfree(icp->cfgdefs);
      xfree(icp->cfgfile);
      xfree(icp);
      return ret;
   }
   icp->dict = dict;
   *cpp = (DnxCfgParser *)icp;
   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxCfgParserParse(DnxCfgParser * cp, void * ppvals[])
{
   iDnxCfgParser * icp = (iDnxCfgParser *)cp;
   char buf[DNX_MAX_CFG_LINE];
   int ret = DNX_OK, line = 0;
   FILE * fp;

   assert(cp && ppvals);

   // apply configuration defaults
   clearPtrValues(icp->dict, ppvals);
   if ((ret = applyCfgSetString(icp->cfgdefs, icp->dict, ppvals)) != 0)
      return ret;

   // parse configuration file
   if ((fp = fopen(icp->cfgfile, "r")) == 0)
      ret = errno == EACCES? DNX_ERR_ACCESS : DNX_ERR_NOTFOUND;
   else
   {
      while (fgets(buf, sizeof buf, fp) != 0)
      {
         int err;
         line++;
         if ((err = dnxParseCfgLine(buf, icp->dict, ppvals)) != 0)
         {
            dnxSyslog(LOG_ERR, "cfgParser [%s]: Syntax error on line %d: %s", 
                  icp->cfgfile, line, dnxErrorString(err));
            if (!ret) ret = err; // return only the first error
         }
      }
      fclose(fp);
   }

   // if no error so far, apply command line overrides
   if (ret == 0)
      ret = applyCfgSetString(icp->cmdover, icp->dict, ppvals);

   if (ret != 0)
      dnxCfgParserFreeCfgValues(cp, ppvals);

   return ret;
}

//----------------------------------------------------------------------------

void dnxCfgParserFreeCfgValues(DnxCfgParser * cp, void * ppvals[])
{
   iDnxCfgParser * icp = (iDnxCfgParser *)cp;
   unsigned i, j;

   assert(cp && ppvals);

   for (i = 0; icp->dict[i].varname; i++)
      for (j = 0; j < elemcount(ptrtypes); j++)
         if (icp->dict[i].type == ptrtypes[j])
         {
            xfree(*(void **)ppvals[i]);
            break;
         }
}

//----------------------------------------------------------------------------

void dnxCfgParserDestroy(DnxCfgParser * cp)
{
   iDnxCfgParser * icp = (iDnxCfgParser *)cp;

   assert(cp);

   xfree(icp->cmdover);
   xfree(icp->cfgdefs);
   xfree(icp->cfgfile);
   xfree(icp);
}

/*--------------------------------------------------------------------------
                                 TEST MAIN

   From within dnx/common, compile with GNU tools using this command line:
    
      gcc -DDEBUG -DDNX_CFGPARSER_TEST -g -O0 \
         -o dnxCfgParserTest dnxCfgParser.c dnxError.c

   Alternatively, a heap check may be done with the following command line:

      gcc -DDEBUG -DDEBUG_HEAP -DDNX_CFGPARSER_TEST -g -O0 \
         -o dnxCfgParserTest dnxCfgParser.c dnxError.c dnxHeap.c

  --------------------------------------------------------------------------*/

#ifdef DNX_CFGPARSER_TEST

#include "utesthelp.h"

#define TEST_FILE_NAME "cfgtest.cfg"
#define TEST_FILE_CONTENTS                                                    \
   "# Test Configuration File\n\n"                                            \
   "   testCfgString = some string\n"                                         \
   "testCfgStringArray = This is,a test, of the , string array.   \n"         \
   "testCfgInt1 = -10024\n"                                                   \
   "testCfgInt3 = -57\n"                                                      \
   "testCfgInt4 = 100\n"                                                      \
   "testCfgIntArray=-1, 87,3   ,2, 32,3,1,-23,  -112,2,234\n"                 \
   "testCfgUnsigned = 332245235\r\n"                                          \
   "testCfgUnsignedArray = 2342, 234,234,4,  2342  ,2342  ,234234 \n"         \
   " testCfgIpAddr = 127.0.0.1\n"                                             \
   "testCfgIpAddrArray = localhost,10.1.1.1, 10.1.1.2 ,10.1.1.3\r\n"          \
   "testCfgUrl = http://www.example.com\n"                                    \
   "testCfgFSPath = /some/path\n"
     
static verbose;

IMPLEMENT_DNX_SYSLOG(verbose);
IMPLEMENT_DNX_DEBUG(verbose);

int main(int argc, char ** argv)
{
   char *               testCfgString;
   char **              testCfgStringArray;
   int                  testCfgInt1;
   int                  testCfgInt2;
   int                  testCfgInt3;
   int                  testCfgInt4;
   int *                testCfgIntArray;
   unsigned             testCfgUnsigned;
   unsigned *           testCfgUnsignedArray;
   struct sockaddr *    testCfgIpAddr;
   struct sockaddr **   testCfgIpAddrArray;
   char *               testCfgUrl;
   char *               testCfgFSPath;

   DnxCfgDict dict[] = 
   {
      { "testCfgString",       DNX_CFG_STRING         },
      { "testCfgStringArray",  DNX_CFG_STRING_ARRAY   },
      { "testCfgInt1",         DNX_CFG_INT            },
      { "testCfgInt2",         DNX_CFG_INT            },
      { "testCfgInt3",         DNX_CFG_INT            },
      { "testCfgInt4",         DNX_CFG_INT            },
      { "testCfgIntArray",     DNX_CFG_INT_ARRAY      },
      { "testCfgUnsigned",     DNX_CFG_UNSIGNED       },
      { "testCfgUnsignedArray",DNX_CFG_UNSIGNED_ARRAY },
      { "testCfgIpAddr",       DNX_CFG_ADDR           },
      { "testCfgIpAddrArray",  DNX_CFG_ADDR_ARRAY     },
      { "testCfgUrl",          DNX_CFG_URL            },
      { "testCfgFSPath",       DNX_CFG_FSPATH         },
      { 0 },
   };

   void * pvals[] =
   {
      &testCfgString,
      &testCfgStringArray,
      &testCfgInt1,
      &testCfgInt2,
      &testCfgInt3,
      &testCfgInt4,
      &testCfgIntArray,
      &testCfgUnsigned,
      &testCfgUnsignedArray,
      &testCfgIpAddr,
      &testCfgIpAddrArray,
      &testCfgUrl,
      &testCfgFSPath,
   };

   char * defs = "testCfgInt2 = 82\ntestCfgInt3 = -67\ntestCfgInt4 = 101";
   char * cmds = "testCfgInt4 = 102\n";

   int i;
   FILE * fp;
   DnxCfgParser * cp;

   char Addr_cmp[]       = {0,0,127,0,0,1};
   char AddrArray1_cmp[] = {0,0,10,1,1,1};
   char AddrArray2_cmp[] = {0,0,10,1,1,2};
   char AddrArray3_cmp[] = {0,0,10,1,1,3};
   char * StrArray_cmp[] = {"This is","a test","of the","string array."};

   verbose = argc > 1 ? 1 : 0;

   CHECK_TRUE((fp = fopen(TEST_FILE_NAME, "w")) != 0);
   fputs(TEST_FILE_CONTENTS, fp);      
   fclose(fp);

   CHECK_ZERO(dnxCfgParserCreate(defs, TEST_FILE_NAME, cmds, dict, &cp));

   CHECK_ZERO(dnxCfgParserParse(cp, pvals));

   CHECK_TRUE(strcmp(testCfgString, "some string") == 0);
   for (i = 0; i < elemcount(StrArray_cmp); i++)
      CHECK_TRUE(strcmp(testCfgStringArray[i], StrArray_cmp[i]) == 0);
   CHECK_TRUE(testCfgStringArray[i] == 0);
   
   CHECK_TRUE(testCfgInt1 == -10024);
   CHECK_TRUE(testCfgInt2 == 82);
   CHECK_TRUE(testCfgInt3 == -57);
   CHECK_TRUE(testCfgInt4 == 102);
   CHECK_TRUE(testCfgIntArray[0] == 11);
   CHECK_TRUE(testCfgIntArray[1] == -1);
   CHECK_TRUE(testCfgIntArray[2] == 87);
   CHECK_TRUE(testCfgIntArray[3] == 3);
   CHECK_TRUE(testCfgIntArray[4] == 2);
   CHECK_TRUE(testCfgIntArray[5] == 32);
   CHECK_TRUE(testCfgIntArray[6] == 3);
   CHECK_TRUE(testCfgIntArray[7] == 1);
   CHECK_TRUE(testCfgIntArray[8] == -23);
   CHECK_TRUE(testCfgIntArray[9] == -112);
   CHECK_TRUE(testCfgIntArray[10] == 2);
   CHECK_TRUE(testCfgIntArray[11] == 234);

   CHECK_TRUE(testCfgUnsigned == 332245235);

   CHECK_TRUE(testCfgUnsignedArray[0] == 7);
   CHECK_TRUE(testCfgUnsignedArray[1] == 2342);
   CHECK_TRUE(testCfgUnsignedArray[2] == 234);
   CHECK_TRUE(testCfgUnsignedArray[3] == 234);
   CHECK_TRUE(testCfgUnsignedArray[4] == 4);
   CHECK_TRUE(testCfgUnsignedArray[5] == 2342);
   CHECK_TRUE(testCfgUnsignedArray[6] == 2342);
   CHECK_TRUE(testCfgUnsignedArray[7] == 234234);

   CHECK_ZERO(memcmp(testCfgIpAddr->sa_data, Addr_cmp, sizeof Addr_cmp));

   CHECK_ZERO(memcmp(testCfgIpAddrArray[0]->sa_data, Addr_cmp, sizeof Addr_cmp));
   CHECK_ZERO(memcmp(testCfgIpAddrArray[1]->sa_data, AddrArray1_cmp, sizeof AddrArray1_cmp));
   CHECK_ZERO(memcmp(testCfgIpAddrArray[2]->sa_data, AddrArray2_cmp, sizeof AddrArray2_cmp));
   CHECK_ZERO(memcmp(testCfgIpAddrArray[3]->sa_data, AddrArray3_cmp, sizeof AddrArray3_cmp));

   CHECK_ZERO(strcmp(testCfgUrl, "http://www.example.com"));
   CHECK_ZERO(strcmp(testCfgFSPath, "/some/path"));

   // test reparse here...
   
   dnxCfgParserFreeCfgValues(cp, pvals);
   dnxCfgParserDestroy(cp);

   remove(TEST_FILE_NAME);

#ifdef DEBUG_HEAP
   CHECK_ZERO(dnxCheckHeap());
#endif

   return 0;
}

#endif   /* DNX_CFGPARSER_TEST */

/*--------------------------------------------------------------------------*/

