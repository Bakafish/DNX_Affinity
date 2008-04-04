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
 * cfg-var  = <any alpha numeric text>
 * cfg-val  = <any alpha numeric text>
 * 
 * In addition, these rules must be followed:
 * 
 * 1. White space may be found anywhere within the file.
 * 2. cfg-line constructs may not contain line breaks.
 * 3. Line comments of the form '#' <text> may be found anywhere.
 * 4. cfg-var constructs may not contain '=' characters.
 * 
 * @file dnxCfgParser.c
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxCfgParser.h"

#include "dnxError.h"
#include "dnxDebug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

#define DNX_MAX_CFG_LINE   2048     /*!< Longest allowed config file line. */

/** The internal definition of a DnxCfg object. */
typedef struct iDnxCfgParser
{
   /** A copy of the user-specified configuration file path name. */
   char * cfgfile;

   /** The user-specified variable dictionary. */
   DnxCfgDictionary * dict;

   /** The number of elements in the dictionary. */
   unsigned dictsz;

   /** A user-specified error handler function, called on parse error. */
   void (*errhandler)(int err, int line, char * buf, void * data);

   /** User specified context data for the error handler routine. */
   void * data;
} iDnxCfgParser;

/** A static array of allocated configuration file types. */
static DnxCfgType ptrtypes[] = { DNX_CFG_STRING, DNX_CFG_INT_ARRAY, 
      DNX_CFG_UNSIGNED_ARRAY, DNX_CFG_IP_ADDR, DNX_CFG_IP_ADDR_ARRAY, 
      DNX_CFG_URL, DNX_CFG_FSPATH };

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

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

/** Validate an address for correctness, and convert to sockaddr_storage.
 * 
 * @param[in] url - the URL to be validated.
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
   return 0;
}

//----------------------------------------------------------------------------
 
/** Validate a single variable and value against a dictionary. 
 * 
 * The value is parsed, converted according to the dictionary-specified 
 * type, and stored in the dictionary data field. If the type is a pointer
 * type, the existing value is freed, and new memory is allocated.
 * 
 * @param[in,out] dict - contains the variable names, types and addresses
 *    of storage for parsed values.
 * @param[in] dictsz - the number of elements in @p dict.
 * @param[in] var - the variable name to be validated.
 * @param[in] val - the string form of the value to be parsed, validated,
 *    and stored in @p dict.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int validate(DnxCfgDictionary * dict, size_t dictsz, 
      char * var, char * val)
{
   unsigned i;

   for (i = 0; i < dictsz; i++)
   {
      if (strcmp(dict[i].varname, var) == 0)
      {
         switch (dict[i].type)
         {
            case DNX_CFG_URL:
            case DNX_CFG_FSPATH:
            case DNX_CFG_STRING:
            {
               int ret;
               if (dict[i].type == DNX_CFG_URL && (ret = validateURL(val)) != 0)
                  return ret;
               if (dict[i].type == DNX_CFG_FSPATH && (ret = validateFSPath(val)) != 0)
                  return ret;
               *(char **)dict[i].data = xstrdup(val);
               break;
            }
            case DNX_CFG_UNSIGNED:
            case DNX_CFG_INT:
            {
               char * ep;
               int (*str2num)(char*,char**,int) = 
                     (dict[i].type == DNX_CFG_INT_ARRAY? 
                           (void*)strtol: (void*)strtoul);
               int n = str2num(val, &ep, 0);
               if (*ep != 0) return DNX_ERR_SYNTAX;
               *(int *)dict[i].data = n;
               break;
            }
            case DNX_CFG_UNSIGNED_ARRAY:
            case DNX_CFG_INT_ARRAY:
            {
               int j, cnt;
               int * array;
               char * bp, * p;
               int (*str2num)(char*,char**,int) = 
                     (dict[i].type == DNX_CFG_INT_ARRAY? 
                           (void*)strtol: (void*)strtoul);

               // count the number of array elements
               cnt = 1;
               p = val;
               while ((p = strchr(p, ',')) != 0)
                  cnt++, p++;

               // allocate space for ints + null
               if ((array = (int *)xmalloc((cnt + 1) * sizeof *array)) == 0)
                  return DNX_ERR_MEMORY;

               array[0] = cnt;   // store count in first integer slot

               // parse ints into array
               for (j = 0, bp = val; j < cnt; j++)
               {
                  char * ep;

                  // trim leading ws from this integer
                  while (isspace(*bp)) bp++;

                  // find next comma - trim trailing ws
                  if ((p = strchr(bp, ',')) != 0)
                  {
                     ep = p;
                     *p++ = 0;
                     while (ep > bp && isspace(ep[-1])) *--ep = 0;
                  }
                  array[j + 1] = str2num(bp, &ep, 0);
                  if (*ep != 0)
                  {
                     xfree(array);
                     return DNX_ERR_SYNTAX;
                  }
                  bp = p;
               }
               *(int **)dict[i].data = array;
               break;
            }
            case DNX_CFG_IP_ADDR:
            {
               int ret;
               struct sockaddr_storage * ss;

               if ((ss = (struct sockaddr_storage *)xmalloc(sizeof *ss)) == 0)
                  return DNX_ERR_MEMORY;
               if ((ret = validateIPAddr(val, ss)) != 0)
               {
                  xfree(ss);
                  return ret;
               }
               *(struct sockaddr_storage **)dict[i].data = ss;
               break;
            }
            case DNX_CFG_IP_ADDR_ARRAY:
            {
               struct sockaddr_storage ** array, * sp;
               char * bp, * p;
               int j, cnt;
   
               // count the number of array elements
               cnt = 1;
               p = val;
               while ((p = strchr(p, ',')) != 0)
                  cnt++, p++;
   
               // allocate space for ptr array + null, and structure array
               if ((array = (struct sockaddr_storage **)xmalloc(
                     (cnt + 1) * sizeof *array + cnt * sizeof **array)) == 0)
                  return DNX_ERR_MEMORY;

               // locate structure array at end of ptr array
               sp = (struct sockaddr_storage *)&array[cnt + 1];
   
               // parse addrs and ptrs into both arrays
               for (j = 0, bp = val; j < cnt; j++)
               {
                  int ret;

                  // trim leading ws from this address
                  while (isspace(*bp)) bp++;

                  // find end of address, terminate it, trim trailing ws
                  if ((p = strchr(bp, ',')) != 0)
                  {
                     char * ep = p;
                     *p++ = 0;
                     while (ep > bp && isspace(ep[-1])) *--ep = 0;
                  }
                  if ((ret = validateIPAddr(bp, sp)) != 0)
                  {
                     xfree(array);
                     return ret;
                  }
                  array[j] = sp++;
                  bp = p;
               }
               array[cnt] = 0;   // terminate ptr array
               *(struct sockaddr_storage ***)dict[i].data = array;
               break;
            }
            default:
               assert(0);  // shouldn't happen - types are already validated
         }
         return 0;
      }
   }
   return DNX_ERR_NOTFOUND;
}

//----------------------------------------------------------------------------
 
/** Trim trailing white space from a string buffer.
 * 
 * @param[in/out] buf - the buffer to be trimmed.
 * 
 * @return - the new length of @p buf.
 */
static int strtrim(char * buf)
{
   char * cp = buf + strlen(buf);

   assert(buf);

   while (cp > buf && isspace(cp[-1])) cp--;

   *cp = 0;

   return cp - buf;
}

//----------------------------------------------------------------------------
 
/** Parse a single line of a configuration file.
 * 
 * @param[in] icp - the parser object on which to act.
 * @param[in] buf - a buffer containing the line text.
 * 
 * @return Zero on success, or a non-zero error value; possibilities include 
 * DNX_ERR_SYNTAX or any error returned by the user-supplied @p validator 
 * function.
 */
static int parseCfgLine(iDnxCfgParser * icp, char * buf)
{
   char * var, * val;

   // trim comment from end of line
   if ((var = strchr(buf, '#')) != 0)
      *var = 0;

   // trim trailing whitespace and check for empty lines
   if (strtrim(buf) < 1)
      return 0;

   // look for assignment operator
   if ((val = strchr(buf, '=')) == 0)
      return DNX_ERR_SYNTAX;

   *val++ = 0;    // terminate variable name, move pointer to value string

   // trim leading whitespace on variable name
   var = buf;
   while (isspace(*var))
      var++;

   // trim trailing white space, and check for empty variable name
   if (strtrim(var) < 1)
      return DNX_ERR_SYNTAX;

   // trim leading white space from value
   while (isspace(*val))
      val++;

   // trim trailing white space, and check for empty value string
   if (strtrim(val) < 1)
      return DNX_ERR_SYNTAX;

   // store and validate the variable and its value
   return validate(icp->dict, icp->dictsz, var, val);
}

//----------------------------------------------------------------------------
 
/** Free memory in all pointer types in a dictionary and clear pointer values.
 * 
 * @param[in] dict - the dictionary to be cleared.
 * @param[in] dictsz - the number of elements in @p dict.
 */
void freePtrTypes(DnxCfgDictionary * dict, size_t dictsz)
{
   unsigned i, j;

   for (i = 0; i < dictsz; i++)
      for (j = 0; j < elemcount(ptrtypes); j++)
         if (dict[i].type == ptrtypes[j])
         {
            xfree(*(void **)dict[i].data);
            *(void **)dict[i].data = 0;
            break;
         }
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

int dnxCfgParserCreate(char * cfgfile, DnxCfgDictionary * dict, size_t dictsz,
   void (*errhandler)(int err, int line, char * buf, void * data),
   void * data, DnxCfgParser ** pcp)
{
   iDnxCfgParser * icp;
   int i, j, ret;

   assert(cfgfile && dict && dictsz && pcp);

   if ((icp = (iDnxCfgParser *)xmalloc(sizeof *icp)) == 0
         || (icp->cfgfile = xstrdup(cfgfile)) == 0)
   {
      xfree(icp);
      return DNX_ERR_MEMORY;
   }
   icp->dict = dict;
   icp->dictsz = dictsz;
   icp->errhandler = errhandler;
   icp->data = data;

   // set all pointer types in the dictionary to zero
   for (i = 0; i < dictsz; i++)
   {
      assert(dict[i].type >= DNX_CFG_STRING && dict[i].type <= DNX_CFG_FSPATH);

      for (j = 0; j < elemcount(ptrtypes); j++)
         if (dict[i].type == ptrtypes[j])
         {
            *(void **)dict[i].data = 0;
            break;
         }
   }

   *pcp = (DnxCfgParser *)icp;

   return ret;
}

//----------------------------------------------------------------------------
 
int dnxCfgParserParse(DnxCfgParser * cp)
{
   iDnxCfgParser * icp = (iDnxCfgParser *)cp;
   int line, ret = DNX_ERR_NOTFOUND;
   char buf[DNX_MAX_CFG_LINE];
   FILE * fp;

   assert(cp);

   freePtrTypes(icp->dict, icp->dictsz);

   if ((fp = fopen(icp->cfgfile, "r")) != 0)
   {
      ret = line = 0;
      while (fgets(buf, sizeof(buf), fp) != 0)
      {
         line++;
         if ((ret = parseCfgLine(icp, buf)) != 0)
         {
            if (!icp->errhandler) break;
            icp->errhandler(ret, line, buf, icp->data);
         }
      }
      fclose(fp);
   }
   return ret;
}

//----------------------------------------------------------------------------
 
void dnxCfgParserDestroy(DnxCfgParser * cp)
{
   iDnxCfgParser * icp = (iDnxCfgParser *)cp;

   assert(cp);

   freePtrTypes(icp->dict, icp->dictsz);
   xfree(icp->cfgfile);
   xfree(icp);
}

/*--------------------------------------------------------------------------
                                 TEST MAIN

   From within dnx/server, compile with GNU tools using this command line:
    
      gcc -DDEBUG -DDNX_CFGPARSER_TEST -g -O0 -o dnxCfgParserTest \
         dnxCfgParser.c dnxError.c

   Alternatively, a heap check may be done with the following command line:

      gcc -DDEBUG -DDEBUG_HEAP -DDNX_CFGPARSER_TEST -g -O0 -o \
         dnxCfgParserTest dnxCfgParser.c dnxError.c dnxHeap.c

  --------------------------------------------------------------------------*/

#ifdef DNX_CFGPARSER_TEST

#include <stdarg.h>

/* test-bed helper macros */
#define CHECK_ZERO(expr)                                                      \
do {                                                                          \
   int ret;                                                                   \
   if ((ret = (expr)) != 0)                                                   \
   {                                                                          \
      fprintf(stderr, "FAILED: '%s'\n  at %s(%d).\n  error %d: %s\n",         \
            #expr, __FILE__, __LINE__, ret, dnxErrorString(ret));             \
      exit(1);                                                                \
   }                                                                          \
} while (0)
#define CHECK_TRUE(expr)                                                      \
do {                                                                          \
   if (!(expr))                                                               \
   {                                                                          \
      fprintf(stderr, "FAILED: Boolean(%s)\n  at %s(%d).\n",                  \
            #expr, __FILE__, __LINE__);                                       \
      exit(1);                                                                \
   }                                                                          \
} while (0)
#define CHECK_NONZERO(expr)   CHECK_ZERO(!(expr))
#define CHECK_FALSE(expr)     CHECK_TRUE(!(expr))

#define TEST_FILE_NAME "cfgtest.cfg"
#define TEST_FILE_CONTENTS                                                    \
   "# Test Configuration File\n\n"                                            \
   "   testCfgString = some string\n"                                         \
   "testCfgInt = -10024\n"                                                    \
   "testCfgIntArray=-1, 87,3   ,2, 32,3,1,-23,  -112,2,234\n"                 \
   "testCfgUnsigned = 332245235\n"                                            \
   "testCfgUnsignedArray = 2342, 234,234,4,  2342  ,2342  ,234234 \n"         \
   " testCfgIpAddr = 127.0.0.1\n"                                             \
   "testCfgIpAddrArray = localhost,10.1.1.1, 10.1.1.2 ,10.1.1.3\n"            \
   "testCfgUrl = http://www.example.com\n"                                    \
   "testCfgFSPath = /some/path\n"
     
static verbose;

static void test_errhandler(int err, int line, char * buf, void * data) 
{
   if (verbose)
      printf("test_errhandler: Error %d, Line %d, Buffer '%s'.\n", err, line, buf);
}
int dnxDebug(int l, char * f, ... )
{
   if (verbose) { va_list a; va_start(a,f); vprintf(f,a); va_end(a); puts(""); }
   return 0;
}

int main(int argc, char ** argv)
{
   char *               testCfgString;
   int                  testCfgInt;
   int *                testCfgIntArray;
   unsigned             testCfgUnsigned;
   unsigned *           testCfgUnsignedArray;
   struct sockaddr *    testCfgIpAddr;
   struct sockaddr **   testCfgIpAddrArray;
   char *               testCfgUrl;
   char *               testCfgFSPath;

   DnxCfgDictionary dict[] = 
   {
      {"testCfgString",       DNX_CFG_STRING,         &testCfgString       },
      {"testCfgInt",          DNX_CFG_INT,            &testCfgInt          },
      {"testCfgIntArray",     DNX_CFG_INT_ARRAY,      &testCfgIntArray     },
      {"testCfgUnsigned",     DNX_CFG_UNSIGNED,       &testCfgUnsigned     },
      {"testCfgUnsignedArray",DNX_CFG_UNSIGNED_ARRAY, &testCfgUnsignedArray},
      {"testCfgIpAddr",       DNX_CFG_IP_ADDR,        &testCfgIpAddr       },
      {"testCfgIpAddrArray",  DNX_CFG_IP_ADDR_ARRAY,  &testCfgIpAddrArray  },
      {"testCfgUrl",          DNX_CFG_URL,            &testCfgUrl          },
      {"testCfgFSPath",       DNX_CFG_FSPATH,         &testCfgFSPath       },
   };

   DnxCfgParser * cp;
   iDnxCfgParser * icp;
   FILE * fp;

   char Addr_cmp[]      = {0,0,127,0,0,1};
   char AddrArray1_cmp[]= {0,0,10,1,1,1};
   char AddrArray2_cmp[]= {0,0,10,1,1,2};
   char AddrArray3_cmp[]= {0,0,10,1,1,3};

   verbose = argc > 1 ? 1 : 0;

   CHECK_TRUE((fp = fopen(TEST_FILE_NAME, "w")) != 0);
   fputs(TEST_FILE_CONTENTS, fp);      
   fclose(fp);

   CHECK_ZERO(dnxCfgParserCreate(TEST_FILE_NAME, dict, elemcount(dict),
      test_errhandler, 0, &cp));

   icp = (iDnxCfgParser *)cp;
   CHECK_ZERO(strcmp(icp->cfgfile, TEST_FILE_NAME));
   CHECK_TRUE(icp->data == 0);
   CHECK_TRUE(icp->dict == dict);
   CHECK_TRUE(icp->dictsz == elemcount(dict));
   CHECK_TRUE(icp->errhandler == test_errhandler);

   CHECK_ZERO(dnxCfgParserParse(cp));

   CHECK_ZERO(strcmp(testCfgString, "some string"));

   CHECK_TRUE(testCfgInt == -10024);

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

   dnxCfgParserDestroy(cp);

   remove(TEST_FILE_NAME);

#ifdef DEBUG_HEAP
   CHECK_ZERO(dnxCheckHeap());
#endif

   return 0;
}

#endif   /* DNX_CFGPARSER_TEST */

/*--------------------------------------------------------------------------*/

