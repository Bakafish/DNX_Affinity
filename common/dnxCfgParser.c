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
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

#define DNX_MAX_CFG_LINE   2048     //!< Longest allowed config file line.

/** The internal type of a DNX configuration parser object. 
 * 
 * Keeping the user value pointers in a separate array makes it simpler to 
 * manipulate them during processing.
 */
typedef struct iDnxCfgParser
{
   char * cfgfile;                  //!< The configuration file name.
   char ** cfgdefs;                 //!< The default configuration values.
   char ** cmdover;                 //!< The command-line overrides.
   DnxCfgDict * dict;               //!< The configuration dictionary.
   unsigned dictsz;                 //!< The number of elements in @em dict.
   DnxCfgValidator_t * vfp;         //!< The user validator function.
   char * curcfg;                   //!< The current config string cache.
   size_t ccsize;                   //!< The current config string length.
} iDnxCfgParser;

/** A typedef describing a single type variable parser. */
typedef int DnxVarParser_t(char * val, DnxCfgType type, void * prval);

/** A typedef describing a single type variable formatter. */
typedef char * DnxVarFormatter_t(char * var, DnxCfgType type, void * prval);
   
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
 
/** Make a dynamic copy of a dnx configuration dictionary.
 * 
 * Also clears all values referred to by the @em valptr fields. This system
 * really only supports two data types: int and pointer. If a value is not
 * one of the int types, then it's a pointer type.
 *
 * @param[in] dict - the dictionary describing the field value types.
 * @param[out] dictszp - the address of storage for the counted size of the
 *    dictionary.
 * 
 * @return A pointer to a copy of @p dict, or NULL if out of memory.
 */
static DnxCfgDict * copyDictionary(DnxCfgDict * dict, unsigned * dictszp)
{
   DnxCfgDict * cpy;
   size_t bufsz = 0;
   unsigned cnt = 0;
   char * sptr;

   assert(dict);

   // calculate space required for copy; add one for null-terminator
   while (dict[cnt].varname)
      bufsz += strlen(dict[cnt++].varname) + 1;
   cnt++;

   // allocate space for array copy with buffer space for strings and values
   if ((cpy = (DnxCfgDict *)xmalloc(cnt * sizeof *dict + bufsz)) == 0)
      return 0;

   // find string buffer pointer
   sptr = (char *)&cpy[cnt];

   // store parameter-based return values; return count of real elements
   *dictszp = --cnt;

   // copy the dictionary; zero user values
   cpy[cnt].varname = 0;
   while (cnt--)
   {
      size_t strsz = strlen(dict[cnt].varname) + 1;
      memcpy(sptr, dict[cnt].varname, strsz);
      cpy[cnt].varname = sptr;
      cpy[cnt].type = dict[cnt].type;
      cpy[cnt].valptr = dict[cnt].valptr;
      if (cpy[cnt].type != DNX_CFG_INT && cpy[cnt].type != DNX_CFG_UNSIGNED 
            && cpy[cnt].type != DNX_CFG_BOOL)
         *(void **)cpy[cnt].valptr = 0;
      else
         *(unsigned *)cpy[cnt].valptr = 0;
      sptr += strsz;
   }
   return cpy;
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

/** Validate and return a null-terminated array of strings.
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

/** Validate and return a boolean.
 * 
 * Values may be specified with ON, OFF, TRUE, FALSE, YES, NO, 0 or non-zero
 * numeric values. Anything else is treated as a syntax error.
 * 
 * @param[in] val - the value to be validated.
 * @param[in] type - the high-level type (BOOL) of @p val.
 * @param[in,out] prval - the address of storage for the returned boolean.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int parseBool(char * val, DnxCfgType type, unsigned * prval)
{
   assert(type == DNX_CFG_BOOL);
   if (strcasecmp(val, "YES") == 0 || strcasecmp(val, "TRUE") == 0 
         || strcasecmp(val, "ON") == 0 || strtoul(val, 0, 0) != 0)
      *prval = 1;
   else if (strcasecmp(val, "NO") == 0 || strcasecmp(val, "FALSE") == 0 
         || strcasecmp(val, "OFF") == 0 || (val[0] == '0' && val[1] == 0))
      *prval = 0;
   else
      return DNX_ERR_SYNTAX;
   return DNX_OK;
}

//----------------------------------------------------------------------------
 
/** Validate and convert a single variable/value pair against a dictionary. 
 * 
 * The value is parsed, converted according to the dictionary-specified 
 * type, and stored in the corresponding @p vptrs data field. If the type is
 * a pointer type, the existing value is freed, and new memory is allocated.
 * 
 * @param[in] var - the variable name to be validated.
 * @param[in] val - the string form of the value to be parsed, validated,
 * @param[in] dict - an array of legal variable names and types.
 * @param[out] vptrs - an array of return value storage locations, where 
 *    each element is the storage location for the variable indicated by the
 *    corresponding location in the @p dict array.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxParseCfgVar(char * var, char * val, DnxCfgDict * dict, 
      void ** vptrs)
{
   static DnxVarParser_t * parsetbl[] =
   {
      (DnxVarParser_t *)parseString,
      (DnxVarParser_t *)parseStringArray,
      (DnxVarParser_t *)parseIntOrUnsigned,
      (DnxVarParser_t *)parseIntOrUnsignedArray,
      (DnxVarParser_t *)parseIntOrUnsigned,
      (DnxVarParser_t *)parseIntOrUnsignedArray,
      (DnxVarParser_t *)parseString,
      (DnxVarParser_t *)parseString,
      (DnxVarParser_t *)parseBool,
   };
   
   unsigned i;

   for (i = 0; dict[i].varname; i++)
      if (strcmp(dict[i].varname, var) == 0)
      {
         assert(dict[i].type >= 0 && dict[i].type < elemcount(parsetbl));
         return parsetbl[dict[i].type](val, dict[i].type, &vptrs[i]);
      }

   return DNX_ERR_INVALID; // invalid entry - no dictionary mapping
}
   
//----------------------------------------------------------------------------
 
/** Parse a single line of a configuration file.
 * 
 * @param[in] s - a buffer containing the line text to be parsed. This
 *    buffer is constant and will not be written to.
 * @param[in] dict - the configuration dictionary.
 * @param[out] vptrs - an array of return storage addresses for parsed values.
 *    Each element returns the parsed value for the corresponding element of 
 *    the @p dict array.
 * 
 * @return Zero on success, or a non-zero error value. Possible error 
 * values include DNX_OK (on success), DNX_ERR_SYNTAX or DNX_ERR_MEMORY.
 */
static int dnxParseCfgLine(char * s, DnxCfgDict * dict, void ** vptrs)
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
   ret = dnxParseCfgVar(cpy, val, dict, vptrs);

   xfree(cpy);

   return ret;
}

//----------------------------------------------------------------------------
 
/** Apply an array of configuration parameters to a variable array.
 * 
 * Each parameter has the same format as a line in a configuration file.
 * 
 * @param[in] sap - the null-terminated configuration string set to be 
 *    applied to @p vptrs. This parameter may be NULL, in which case nothing
 *    is done and DNX_OK is returned.
 * @param[in] dict - the configuration dictionary to use.
 * @param[out] vptrs - an array of return storage addresses for parsed values.
 *    Each element returns the parsed value for the corresponding element of 
 *    the @p dict array.
 * 
 * @return Zero on success, or a non-zero error value. Possible return values 
 * include DNX_OK (on success) or DNX_ERR_MEMORY.
 */
static int applyCfgSetString(char ** sap, DnxCfgDict * dict, void ** vptrs)
{
   assert(dict && vptrs);

   if (!sap) 
      return DNX_OK;

   while (*sap)
   {
      int ret;
      if ((ret = dnxParseCfgLine(*sap++, dict, vptrs)) != 0)
         return ret;
   }
   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Free memory in all pointer types in a value array.
 * 
 * If @p vptrs is NULL, then the values referred to by @p dict will be
 * freed, otherwise the values referred to by @p vptrs will be freed.
 * 
 * @param[in] dict - the configuration dictionary that defines the types
 *    and values to be freed in case @p vptrs is null.
 * @param[in] vptrs - the value pointer array whose pointer values are to be 
 *    freed. Optional; if this parameter is NULL, then the value pointers in
 *    @p dict are freed instead.
 */
static void freeArrayPtrs(DnxCfgDict * dict, void ** vptrs)
{
   unsigned i, j;

   assert(dict);

   for (i = 0; dict[i].varname; i++)
      if (dict[i].type != DNX_CFG_INT && dict[i].type != DNX_CFG_UNSIGNED 
            && dict[i].type != DNX_CFG_BOOL)
         xfree(vptrs? vptrs[i]: *(void **)dict[i].valptr);
}

//----------------------------------------------------------------------------

/** Format a string configuration value.
 * 
 * @param[in] var - the variable name to be used during formatting.
 * @param[in] type - the type of the variable to be formatted.
 * @param[in] prval - the string value to be formatted.
 * 
 * @return A pointer to an allocated buffer containing the formatted
 * configuration line, or NULL on memory allocation failure. The caller is 
 * responsible for freeing the memory returned.
 */
static char * formatString(char * var, DnxCfgType type, char * prval)
{
   size_t len = prval? strlen(prval): 0;
   char * cfg;

   assert(var);
   assert(type == DNX_CFG_STRING || type == DNX_CFG_URL || type == DNX_CFG_FSPATH);

   // var + '=' + val + '\n' + '\0'
   if ((cfg = (char *)xmalloc(strlen(var) + 1 + len + 2)) != 0)
      sprintf(cfg, "%s=%s\n", var, prval? prval: "");

   return cfg;
}

//----------------------------------------------------------------------------

/** Format a string array configuration value.
 * 
 * @param[in] var - the variable name to be used during formatting.
 * @param[in] type - the type of the variable to be formatted.
 * @param[in] prval - the null-terminated array of strings to be formatted.
 * 
 * @return A pointer to an allocated buffer containing the formatted
 * configuration line, or NULL on memory allocation failure. The caller is 
 * responsible for freeing the memory returned.
 */
static char * formatStringArray(char * var, DnxCfgType type, char ** prval)
{
   char ** cp = prval;
   size_t len = 0;
   char * cfg;

   assert(var && type == DNX_CFG_STRING_ARRAY);

   if (cp)
      while (*cp)
         len += strlen(*cp++) + 1;  // add one for trailing ',' or '\n'

   // var + '=' + val + '\n' + '\0'
   if ((cfg = (char *)xmalloc(strlen(var) + 1 + len + 1)) != 0)
   {
      char * cp = cfg;
      cp += sprintf(cp, "%s=", var);
      if (prval)
         while (*prval)
            cp += sprintf(cp, "%s,", *prval++);
      if (cp[-1] == ',') cp--;   // overwrite trailing ','
      *cp++ = '\n';
      *cp = 0;
   }
   return cfg;
}

//----------------------------------------------------------------------------

/** Format an int or unsigned configuration value.
 * 
 * @param[in] var - the variable name to be used during formatting.
 * @param[in] type - the type of the variable to be formatted.
 * @param[in] prval - the integer to be formatted.
 * 
 * @return A pointer to an allocated buffer containing the formatted
 * configuration line, or NULL on memory allocation failure. The caller is 
 * responsible for freeing the memory returned.
 */
static char * formatIntOrUnsigned(char * var, DnxCfgType type, int prval)
{
   char intbuf[16];
   size_t len;
   char * cfg;

   assert(var && (type == DNX_CFG_INT || type == DNX_CFG_UNSIGNED));

   len = sprintf(intbuf, type == DNX_CFG_INT? "%d": "%u", prval);

   // var + '=' + val + '\n' + '\0'
   if ((cfg = (char *)xmalloc(strlen(var) + 1 + len + 2)) != 0)
      sprintf(cfg, "%s=%s\n", var, intbuf);

   return cfg;
}

//----------------------------------------------------------------------------

/** Format an int or unsigned array configuration value.
 * 
 * @param[in] var - the variable name to be used during formatting.
 * @param[in] type - the type of the variable to be formatted.
 * @param[in] prval - the array of integers to be formatted. The first value
 *    indicates the number of integers following.
 * 
 * @return A pointer to an allocated buffer containing the formatted
 * configuration line, or NULL on memory allocation failure. The caller is 
 * responsible for freeing the memory returned.
 */
static char * formatIntOrUnsignedArray(char * var, DnxCfgType type, int * prval)
{
   size_t len = prval? prval[0] * 16: 0;
   char * cfg;

   assert(var);
   assert(type == DNX_CFG_INT_ARRAY || type == DNX_CFG_UNSIGNED_ARRAY);

   // var + '=' + val + '\n' + '\0'
   if ((cfg = (char *)xmalloc(strlen(var) + 1 + len + 2)) != 0)
   {
      int i;
      char * cp = cfg;
      cp += sprintf(cp, "%s=", var);
      if (prval)
         for (i = 1; i <= prval[0]; i++)
            cp += sprintf(cp, type == DNX_CFG_INT_ARRAY? "%d,": "%u,", prval[i]);
      if (cp[-1] == ',') cp--;   // overwrite trailing ','
      *cp++ = '\n';
      *cp = 0;
   }
   return cfg;
}

//----------------------------------------------------------------------------

/** Format a boolean configuration value.
 * 
 * @param[in] var - the variable name to be used during formatting.
 * @param[in] type - the type of the variable to be formatted.
 * @param[in] prval - the string value to be formatted.
 * 
 * @return A pointer to an allocated buffer containing the formatted
 * configuration line, or NULL on memory allocation failure. The caller is 
 * responsible for freeing the memory returned.
 */
static char * formatBool(char * var, DnxCfgType type, unsigned prval)
{
   char * cfg;

   assert(var);
   assert(type == DNX_CFG_BOOL);

   // var + '=' + YES + '\n' + '\0'
   if ((cfg = (char *)xmalloc(strlen(var) + 1 + 3 + 2)) != 0)
      sprintf(cfg, "%s=%s\n", var, prval? "YES" : "NO");

   return cfg;
}

//----------------------------------------------------------------------------

/** Format an allocated buffer containing the current configuration.
 * 
 * @param[in] dict - the config dictionary to be formatted into a string.
 * @param[out] ccsizep - the address of storage for returning the size in bytes
 *    of the returned allocated string buffer.
 * 
 * @return A pointer to an allocated string buffer, or NULL on memory
 * allocation failure.
 */
static char * buildCurrentCfgCache(DnxCfgDict * dict, size_t * ccsizep)
{
   static DnxVarFormatter_t * fmttbl[] =
   {
      (DnxVarFormatter_t *)formatString,
      (DnxVarFormatter_t *)formatStringArray,
      (DnxVarFormatter_t *)formatIntOrUnsigned,
      (DnxVarFormatter_t *)formatIntOrUnsignedArray,
      (DnxVarFormatter_t *)formatIntOrUnsigned,
      (DnxVarFormatter_t *)formatIntOrUnsignedArray,
      (DnxVarFormatter_t *)formatString,
      (DnxVarFormatter_t *)formatString,
      (DnxVarFormatter_t *)formatBool,
   };

   char * cfg = 0;
   size_t cfgsz = 1;     // start with one for null-terminator
   unsigned i;

   assert(dict && ccsizep);

   for (i = 0; dict[i].varname; i++)
   {
      char * str, * newcfg;
      size_t len;

      if ((str = fmttbl[dict[i].type](dict[i].varname, 
               dict[i].type, *(void **)dict[i].valptr)) == 0
            || (newcfg = (char *)xrealloc(cfg, cfgsz + (len = strlen(str)))) == 0)
      {
         xfree(str);
         xfree(cfg);
         return 0;
      }
      cfg = newcfg;
      memcpy(&cfg[cfgsz - 1], str, len);
      xfree(str);
      cfgsz += len;
      cfg[cfgsz - 1] = 0;
   }

   // remove trailing '\n'
   if (cfgsz > 1 && cfg[cfgsz - 2] == '\n')
      cfg[--cfgsz - 1] = 0;

   *ccsizep = cfgsz;

   return cfg;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

int dnxCfgParserCreate(char * cfgdefs, char * cfgfile, char * cmdover, 
      DnxCfgDict * dict, DnxCfgValidator_t * vfp, DnxCfgParser ** cpp)
{
   iDnxCfgParser * icp;

   assert(dict && cpp);

   // allocate an internal config parser object; clear it
   if ((icp = (iDnxCfgParser *)xmalloc(sizeof *icp)) == 0)
      return DNX_ERR_MEMORY;
   memset(icp, 0, sizeof *icp);

   // copy all specified values and structures
   if (cfgfile && (icp->cfgfile = xstrdup(cfgfile)) == 0
         || cfgdefs && (icp->cfgdefs = strToStrArray(cfgdefs, '\n')) == 0
         || cmdover && (icp->cmdover = strToStrArray(cmdover, '\n')) == 0
         || (icp->dict = copyDictionary(dict, &icp->dictsz)) == 0)
   {
      xfree(icp->cmdover);
      xfree(icp->cfgdefs);
      xfree(icp->cfgfile);
      xfree(icp);
      return DNX_ERR_MEMORY;
   }
   icp->vfp = vfp;
   *cpp = (DnxCfgParser *)icp;
   return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxCfgParserParse(DnxCfgParser * cp, void * passthru)
{
   iDnxCfgParser * icp = (iDnxCfgParser *)cp;
   void ** vptrs;
   int ret;

   assert(cp);

   // allocate a temp array of values; zero it out so we don't free bad ptrs
   if ((vptrs = (void **)xmalloc(icp->dictsz * sizeof *vptrs)) == 0)
      return DNX_ERR_MEMORY;
   memset(vptrs, 0, icp->dictsz * sizeof *vptrs);

   // apply config defaults
   if ((ret = applyCfgSetString(icp->cfgdefs, icp->dict, vptrs)) != 0)
   {
      xfree(vptrs);
      return ret;
   }

   // parse configuration file
   if (icp->cfgfile)
   {
      FILE * fp;
      if ((fp = fopen(icp->cfgfile, "r")) == 0)
         ret = errno == EACCES? DNX_ERR_ACCESS : DNX_ERR_NOTFOUND;
      else
      {
         int line = 0;
         char buf[DNX_MAX_CFG_LINE];

         while (fgets(buf, sizeof buf, fp) != 0)
         {
            int err;
            line++;
            if ((err = dnxParseCfgLine(buf, icp->dict, vptrs)) != 0)
            {
               dnxLog("cfgParser [%s]: Syntax error on line %d: %s.", 
                     icp->cfgfile, line, dnxErrorString(err));
               if (!ret) ret = err; // return only the first error
            }
         }
         fclose(fp);
      }
   }

#define INTSWAP(a,b) do { int n = (a); (a) = (int)(intptr_t)(b); (b) = (void *)(intptr_t)n; } while(0)
#define PTRSWAP(a,b) do { void * p = (a); (a) = (b); (b) = p; } while (0)

   // apply command line overrides; validate configuration; store values
   if (!ret && (ret = applyCfgSetString(icp->cmdover, icp->dict, vptrs)) == 0
         && (!icp->vfp || (ret = icp->vfp(icp->dict, vptrs, passthru)) == 0))
   {
      unsigned i;

      // swap new for old values in vptrs
      for (i = 0; i < icp->dictsz; i++)
         if (icp->dict[i].type == DNX_CFG_INT 
               || icp->dict[i].type == DNX_CFG_UNSIGNED
               || icp->dict[i].type == DNX_CFG_BOOL)
            INTSWAP(*(int *)icp->dict[i].valptr, vptrs[i]);
         else
            PTRSWAP(*(void **)icp->dict[i].valptr, vptrs[i]);

      // free current config cache string
      xfree(icp->curcfg);
      icp->curcfg = 0;
      icp->ccsize = 0;
   }

   // free either new or old values; free temp array
   freeArrayPtrs(icp->dict, vptrs);
   xfree(vptrs);

   return ret;
}

//----------------------------------------------------------------------------

int dnxCfgParserGetCfg(DnxCfgParser * cp, char * buf, size_t * bufszp)
{
   iDnxCfgParser * icp = (iDnxCfgParser *)cp;

   assert(cp && bufszp && (buf || !*bufszp));

   // build current config if not already there
   if (icp->curcfg == 0
         && (icp->curcfg = buildCurrentCfgCache(icp->dict, &icp->ccsize)) == 0)
      return DNX_ERR_MEMORY;

   // copy what we can into buffer; return required/used size
   if (buf && *bufszp)
   {
      size_t cpysz = *bufszp > icp->ccsize? icp->ccsize: *bufszp;
      memcpy(buf, icp->curcfg, cpysz - 1);
      buf[cpysz - 1] = 0;
   }
   *bufszp = icp->ccsize;

   return 0;
}

//----------------------------------------------------------------------------

void dnxCfgParserDestroy(DnxCfgParser * cp)
{
   iDnxCfgParser * icp = (iDnxCfgParser *)cp;

   assert(cp);

   freeArrayPtrs(icp->dict, 0);

   xfree(icp->dict);
   xfree(icp->cmdover);
   xfree(icp->cfgdefs);
   xfree(icp->cfgfile);
   xfree(icp->curcfg);
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
   "testCfgUrl = http://www.example.com\n"                                    \
   "testCfgFSPath = /some/path\n"                                             \
   "testCfgBool = YES\n"

#define TEST_CFG_CONTENTS                                                     \
   "testCfgString=some string\n"                                              \
   "testCfgStringArray=This is,a test,of the,string array.\n"                 \
   "testCfgInt1=-10024\n"                                                     \
   "testCfgInt2=82\n"                                                         \
   "testCfgInt3=-57\n"                                                        \
   "testCfgInt4=102\n"                                                        \
   "testCfgIntArray=-1,87,3,2,32,3,1,-23,-112,2,234\n"                        \
   "testCfgUnsigned=332245235\n"                                              \
   "testCfgUnsignedArray=2342,234,234,4,2342,2342,234234\n"                   \
   "testCfgUrl=http://www.example.com\n"                                      \
   "testCfgFSPath=/some/path\n"                                               \
   "testCfgBool=YES"
     
static int verbose;

IMPLEMENT_DNX_SYSLOG(verbose);
IMPLEMENT_DNX_DEBUG(verbose);

int main(int argc, char ** argv)
{
   char *             testCfgString;
   char **            testCfgStringArray;
   int                testCfgInt1;
   int                testCfgInt2;
   int                testCfgInt3;
   int                testCfgInt4;
   int *              testCfgIntArray;
   unsigned           testCfgUnsigned;
   unsigned *         testCfgUnsignedArray;
   char *             testCfgUrl;
   char *             testCfgFSPath;
   unsigned           testCfgBool;

   DnxCfgDict dict[] = 
   {
      { "testCfgString",       DNX_CFG_STRING,         &testCfgString        },
      { "testCfgStringArray",  DNX_CFG_STRING_ARRAY,   &testCfgStringArray   },
      { "testCfgInt1",         DNX_CFG_INT,            &testCfgInt1          },
      { "testCfgInt2",         DNX_CFG_INT,            &testCfgInt2          },
      { "testCfgInt3",         DNX_CFG_INT,            &testCfgInt3          },
      { "testCfgInt4",         DNX_CFG_INT,            &testCfgInt4          },
      { "testCfgIntArray",     DNX_CFG_INT_ARRAY,      &testCfgIntArray      },
      { "testCfgUnsigned",     DNX_CFG_UNSIGNED,       &testCfgUnsigned      },
      { "testCfgUnsignedArray",DNX_CFG_UNSIGNED_ARRAY, &testCfgUnsignedArray },
      { "testCfgUrl",          DNX_CFG_URL,            &testCfgUrl           },
      { "testCfgFSPath",       DNX_CFG_FSPATH,         &testCfgFSPath        },
      { "testCfgBool",         DNX_CFG_BOOL,           &testCfgBool          },
      { 0 },
   };
   char * defs = "testCfgInt2 = 82\ntestCfgInt3 = -67\ntestCfgInt4 = 101";
   char * cmds = "testCfgInt4 = 102\n";

   char * StrArray_cmp[] = {"This is","a test","of the","string array."};
   int IntArray_cmp[] = {11,-1,87,3,2,32,3,1,-23,-112,2,234};
   int UnsignedArray_cmp[] = {7,2342,234,234,4,2342,2342,234234};

   char test_cfg[] = TEST_CFG_CONTENTS;

   int i;
   FILE * fp;
   DnxCfgParser * cp;
   char * cfg;
   size_t bufsz;

   verbose = argc > 1 ? 1 : 0;

   CHECK_TRUE((fp = fopen(TEST_FILE_NAME, "w")) != 0);
   fputs(TEST_FILE_CONTENTS, fp);      
   fclose(fp);

   CHECK_ZERO(dnxCfgParserCreate(defs, TEST_FILE_NAME, cmds, dict, 0, &cp));
   CHECK_ZERO(dnxCfgParserParse(cp, 0));

   CHECK_TRUE(strcmp(testCfgString, "some string") == 0);

   for (i = 0; i < elemcount(StrArray_cmp); i++)
      CHECK_TRUE(strcmp(testCfgStringArray[i], StrArray_cmp[i]) == 0);

   CHECK_TRUE(testCfgInt1 == -10024);
   CHECK_TRUE(testCfgInt2 == 82);
   CHECK_TRUE(testCfgInt3 == -57);
   CHECK_TRUE(testCfgInt4 == 102);

   for (i = 0; i < elemcount(IntArray_cmp); i++)
      CHECK_TRUE(testCfgIntArray[i] == IntArray_cmp[i]);

   CHECK_TRUE(testCfgUnsigned == 332245235);

   for (i = 0; i < elemcount(UnsignedArray_cmp); i++)
      CHECK_TRUE(testCfgUnsignedArray[i] == UnsignedArray_cmp[i]);

   CHECK_ZERO(strcmp(testCfgUrl, "http://www.example.com"));
   CHECK_ZERO(strcmp(testCfgFSPath, "/some/path"));

   CHECK_TRUE(testCfgBool != 0);

   // test getcfg
   bufsz = 0;
   CHECK_ZERO(dnxCfgParserGetCfg(cp, 0, &bufsz));
   CHECK_NONZERO(cfg = (char *)xmalloc(bufsz));
   CHECK_ZERO(dnxCfgParserGetCfg(cp, cfg, &bufsz));
   CHECK_TRUE(memcmp(cfg, test_cfg, bufsz) == 0);
   CHECK_TRUE(bufsz == sizeof test_cfg);
   xfree(cfg);

   // test reparse here...
   
   dnxCfgParserDestroy(cp);

   remove(TEST_FILE_NAME);

#ifdef DEBUG_HEAP
   CHECK_ZERO(dnxCheckHeap());
#endif

   return 0;
}

#endif   /* DNX_CFGPARSER_TEST */

/*--------------------------------------------------------------------------*/

