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

/** Implements DNX XML functionality.
 *
 * @file dnxXml.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxXml.h"

#include "dnxProtocol.h"
#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxLogging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <assert.h>

#define DNX_XML_MIN_HEADER 32

/** @todo Implement int dnxXmlTypeSize(DnxXmlType). */

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** Convert an opaque pointer to C data into a dnx xml string format.
 * 
 * @param[in] xType - the C data type to be converted to an xml string.
 * @param[in] xData - an opaque pointer to the C data to be converted.
 * @param[out] buf - the address of storage for the returned xml string.
 * @param[in] size - the maximum number of bytes that may be written
 *    to @p buf.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxXmlToString(DnxXmlType xType, void * xData, char * buf, int size)
{
   int ret = DNX_OK;

   assert(xData && buf && size > 0);

   *buf = 0;

   switch (xType)
   {
      case DNX_XML_SHORT:
         snprintf(buf, size, "%hd", *((short *)xData));
         break;

      case DNX_XML_USHORT:
         snprintf(buf, size, "%hu", *((unsigned short *)xData));
         break;

      case DNX_XML_INT:
         snprintf(buf, size, "%d", *((int *)xData));
         break;

      case DNX_XML_UINT:
         snprintf(buf, size, "%u", *((unsigned int *)xData));
         break;

      case DNX_XML_LONG:
         snprintf(buf, size, "%ld", *((long *)xData));
         break;

      case DNX_XML_ULONG:
         snprintf(buf, size, "%lu", *((unsigned long *)xData));
         break;

      case DNX_XML_STR:
         strncpy(buf, (char *)xData, size);
         buf[size - 1] = 0;
         break;

      case DNX_XML_XID:
         snprintf(buf, size, "%u-%lu-%lu", ((DnxXID *)xData)->objType, 
               ((DnxXID *)xData)->objSerial, ((DnxXID *)xData)->objSlot);
         break;

      default:
         ret = DNX_ERR_INVALID;
   }
   return ret;
}

//----------------------------------------------------------------------------

/** Locate and return an xml string element by tag value.
 * 
 * @param[in] xbuf - the dnx xml buffer to search for @p xTag.
 * @param[in] xTag - the tag to search @p xbuf for.
 * @param[in] xType - the C data type of the element - not used.
 * @param[out] buf - the address of storage for the xml element matching 
 *    the xml tag in @p xTag.
 * @param[in] size - the maximum number of bytes that may be written
 *    to @p buf.
 * 
 * @return Zero on success, or a non-zero error value.
 */
static int dnxXmlGetTagValue(DnxXmlBuf * xbuf, char * xTag, DnxXmlType xType, 
      char * buf, int size)
{
   char * cp, * ep, * value;
   int len;
   int ret = DNX_OK;

   // validate parameters
   if (!xbuf || !xTag || !buf || size < 1)
      return DNX_ERR_INVALID;

   *buf = 0;   // initialize user buffer

   // search XML buffer for specified tag;

   // search for opening bracket
   cp = xbuf->buf;
   while ((cp = strchr(cp, '<')) != NULL)
   {
      cp++;

      if (*cp == '/')
         continue;   // not a match

      // search for end-bracket
      if ((ep = strchr(cp, '>')) == NULL)
      {
         ret = DNX_ERR_SYNTAX;
         break;   // error - unmatched XML brackets
      }

      // see if we've matched open-tag
      if (strncmp(cp, xTag, (ep-cp)))
      {
         cp = ep+1;  // reset beginning pointer for next search
         continue;   // not a match
      }

      value = cp = ep + 1; // beginning of tag-value

end_tag:

      // find opening bracket of end-tag
      if ((ep = strchr(cp, '<')) == NULL)
      {
         ret = DNX_ERR_SYNTAX;
         break;   // error - missing closing tag
      }

      len = ep - value; // length of tag-value

      // verify that this is a closing tag
      if (*(cp = ep + 1) != '/')
         goto end_tag;  // not a match
      cp++;

      // search for end-bracket
      if ((ep = strchr(cp, '>')) == NULL)
      {
         ret = DNX_ERR_SYNTAX;
         break;   // error - nmatched XML brackets
      }

      // see if we've matched the end-tag
      if (strncmp(cp, xTag, ep - cp))
         goto end_tag;  // not a match

      // get min of tag-value-length or conversion buffer size.
      if (len >= size)
         len = size - 1;

      // copy tag value to local conversion buffer
      if (len > 0)
         memcpy(buf, value, len);
      buf[len] = 0;

      break;   // success
   }
   return ret;
}

/*--------------------------------------------------------------------------
                              INTERFACE
  --------------------------------------------------------------------------*/

/** Open and write header information to a dnx xml buffer.
 * 
 * @param[out] xbuf - the dnx xml buffer to be opened.
 * @param[in] tag - the major xml request tag to write to @p xbuf.
 * 
 * @return Always returns zero.
 */
int dnxXmlOpen(DnxXmlBuf * xbuf, char * tag)
{
   assert(xbuf && tag);

   // initialize buffer with message container opening tag and request attribute
   xbuf->size = sprintf(xbuf->buf, "<dnxMessage><Request>%s</Request>", tag);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Add an XML data element to a dnx xml buffer.
 * 
 * @param[out] xbuf - the dnx xml buffer to be appended to.
 * @param[in] xTag - the xml tag to use for this new data element.
 * @param[in] xType - the C data type of the xml element data. 
 * @param[in] xData - an opaque pointer to a C data variable to be expressed
 *    in xml in @p xbuf.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxXmlAdd(DnxXmlBuf * xbuf, char * xTag, DnxXmlType xType, void * xData)
{
   char buf[DNX_MAX_MSG];
   int len, ret;

   assert(xbuf && xbuf->size >= DNX_XML_MIN_HEADER && xTag);

   // convert data element to string
   *buf = 0;
   if (xData && (ret = dnxXmlToString(xType, xData, buf, sizeof(buf))) != DNX_OK)
      return ret;
   
   // perform capacity check on XML buffer - 5 = number of brackets plus '/'
   if ((len = xbuf->size + strlen(xTag) * 2 + strlen(buf) + 5) >= DNX_MAX_MSG)
      return DNX_ERR_CAPACITY;

   // add to XML buffer
   xbuf->size += sprintf(xbuf->buf + xbuf->size, "<%s>%s</%s>", xTag, buf, xTag);

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Return the C data typed value associated with a specified tag.
 * 
 * @param[in] xbuf - the dnx xml buffer from which to extract a value.
 * @param[in] xTag - the tag for which to search in @p xbuf.
 * @param[in] xType - the C data type of the specified tag.
 * @param[out] xData - the address of storage for the returned C data value.
 *    Note that @p xData must be large enough to hold an element of the 
 *    specified C data type. In the case of a string, xData actually 
 *    accepts a char pointer, not character data of a specified length.
 *    Note also that the caller is responsible for freeing the memory 
 *    returned if @p xType is DNX_XML_STR.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxXmlGet(DnxXmlBuf * xbuf, char * xTag, DnxXmlType xType, void * xData)
{
   char buf[DNX_MAX_MSG];
   char * cp, * ep, * lastchar;
   unsigned long unum;
   long num;
   int ret = DNX_OK;

   // extract the value of the specified tag from the XML buffer
   buf[0] = 0;
   if ((ret = dnxXmlGetTagValue(xbuf, xTag, xType, buf, sizeof buf)) != DNX_OK)
      return ret;

   // convert tag value into target binary type
   switch (xType)
   {
      case DNX_XML_SHORT:
         errno = 0;
         num = strtol(buf, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
            ret = DNX_ERR_SYNTAX;
         else
            *(short *)xData = (short)num;
         break;

      case DNX_XML_USHORT:
         errno = 0;
         unum = strtoul(buf, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
            ret = DNX_ERR_SYNTAX;
         else
            *(unsigned short *)xData = (unsigned short)unum;
         break;

      case DNX_XML_INT:
         errno = 0;
         num = strtol(buf, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
            ret = DNX_ERR_SYNTAX;
         else
            *(int *)xData = (int)num;
         break;

      case DNX_XML_UINT:
         errno = 0;
         unum = strtoul(buf, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
            ret = DNX_ERR_SYNTAX;
         else
            *(unsigned int *)xData = (unsigned int)unum;
         break;

      case DNX_XML_LONG:
         errno = 0;
         num = strtol(buf, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
            ret = DNX_ERR_SYNTAX;
         else
            *(long *)xData = (long)num;
         break;

      case DNX_XML_ULONG:
         errno = 0;
         unum = strtoul(buf, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
            ret = DNX_ERR_SYNTAX;
         else
            *(unsigned long *)xData = (unsigned long)unum;
         break;

      case DNX_XML_STR:
         if ((*(char **)xData = xstrdup(buf)) == NULL)
         {
            dnxSyslog(LOG_ERR, "dnxXmlGet: DNX_XML_STR: Out of Memory");
            ret = DNX_ERR_MEMORY;
         }
         break;

      case DNX_XML_XID:
         // the format of a XID is: "objType-objSerial-objSlot",
         // where objType, objSerial and objSlot are unsigned integers
         if ((cp = strchr(buf, '-')) == NULL)
         {
            ret = DNX_ERR_SYNTAX;   // missing XID separator
            break;
         }
         *cp++ = 0;  // now buf points to objType and cp points to objSerial
   
         if ((ep = strchr(cp, '-')) == NULL)
         {
            ret = DNX_ERR_SYNTAX;   // missing XID separator
            break;
         }
         *ep++ = 0;  // now ep points to objSlot
   
         // decode objType
         errno = 0;
         unum = strtoul(buf, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
         {
            ret = DNX_ERR_SYNTAX;   // invalid number
            break;
         }
         ((DnxXID *)xData)->objType = (DnxObjType)unum;
   
         // decode objSerial
         errno = 0;
         unum = strtoul(cp, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
         {
            ret = DNX_ERR_SYNTAX;   // invalid number
            break;
         }
         ((DnxXID *)xData)->objSerial = (unsigned long)unum;
   
         // decode objSlot
         errno = 0;
         unum = strtoul(ep, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
         {
            ret = DNX_ERR_SYNTAX;   // invalid number
            break;
         }
         ((DnxXID *)xData)->objSlot = (unsigned long)unum;
         break;

      default:
         ret = DNX_ERR_INVALID;
   }
   return ret;
}

//----------------------------------------------------------------------------

/** Validate and close a dnx xml buffer.
 * 
 * @param[in,out] xbuf - the buffer to be validated and closed.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxXmlClose(DnxXmlBuf * xbuf)
{
   assert(xbuf && xbuf->size >= 0);

   if (xbuf->size > DNX_MAX_MSG)
      return DNX_ERR_CAPACITY;

   // append final message container tag
   strcat(xbuf->buf, "</dnxMessage>");
   xbuf->size = strlen(xbuf->buf);

   return DNX_OK;
}

/*--------------------------------------------------------------------------
                                 TEST MAIN

   From within dnx/common, compile with GNU tools using this command line:
    
      gcc -DDEBUG -DDNX_XML_TEST -g -O0 -o dnxXmlTest \
         dnxXml.c dnxError.c

   Alternatively, a heap check may be done with the following command line:

      gcc -DDEBUG -DDEBUG_HEAP -DDNX_XML_TEST -g -O0 -o dnxXmlTest \
         dnxXml.c dnxError.c dnxHeap.c

  --------------------------------------------------------------------------*/

#ifdef DNX_XML_TEST

#include "utesthelp.h"

static verbose;

IMPLEMENT_DNX_SYSLOG(verbose);
IMPLEMENT_DNX_DEBUG(verbose);

int main(int argc, char ** argv)
{
   static int lens[] = {35, 54, 74, 90, 107, 126, 146, 174, 204, 217};
   static char * testbuf = 
         "<dnxMessage>"
         "<Request>Test</Request>"
         "<Short>-100</Short>"
         "<UShort>100</UShort>"
         "<Int>-1000</Int>"
         "<UInt>1000</UInt>"
         "<Long>-10000</Long>"
         "<ULong>10000</ULong>"
         "<String>test string</String>"
         "<XID>6-12345678-87654321</XID>"
         "</dnxMessage>";

   DnxXmlBuf xbuf;

   short xshort = -100;
   unsigned short xushort = 100;
   int xint = -1000;
   unsigned int xuint = 1000;
   long xlong = -10000;
   unsigned long xulong = 10000;
   char * xstring = "test string";
   DnxXID xid;

   xid.objType = DNX_OBJ_MANAGER;
   xid.objSerial = 12345678;
   xid.objSlot = 87654321;

   verbose = argc > 1 ? 1 : 0;

   CHECK_ZERO(dnxXmlOpen(&xbuf, "Test"));
   CHECK_TRUE(memcmp(xbuf.buf, testbuf, lens[0]) == 0);

   CHECK_ZERO(dnxXmlAdd(&xbuf, "Short", DNX_XML_SHORT, &xshort));
   CHECK_TRUE(memcmp(xbuf.buf, testbuf, lens[1]) == 0);

   CHECK_ZERO(dnxXmlAdd(&xbuf, "UShort", DNX_XML_USHORT, &xushort));
   CHECK_TRUE(memcmp(xbuf.buf, testbuf, lens[2]) == 0);

   CHECK_ZERO(dnxXmlAdd(&xbuf, "Int", DNX_XML_INT, &xint));
   CHECK_TRUE(memcmp(xbuf.buf, testbuf, lens[3]) == 0);

   CHECK_ZERO(dnxXmlAdd(&xbuf, "UInt", DNX_XML_UINT, &xuint));
   CHECK_TRUE(memcmp(xbuf.buf, testbuf, lens[4]) == 0);

   CHECK_ZERO(dnxXmlAdd(&xbuf, "Long", DNX_XML_LONG, &xlong));
   CHECK_TRUE(memcmp(xbuf.buf, testbuf, lens[5]) == 0);

   CHECK_ZERO(dnxXmlAdd(&xbuf, "ULong", DNX_XML_ULONG, &xulong));
   CHECK_TRUE(memcmp(xbuf.buf, testbuf, lens[6]) == 0);

   CHECK_ZERO(dnxXmlAdd(&xbuf, "String", DNX_XML_STR, xstring));
   CHECK_TRUE(memcmp(xbuf.buf, testbuf, lens[7]) == 0);

   CHECK_ZERO(dnxXmlAdd(&xbuf, "XID", DNX_XML_XID, &xid));
   CHECK_TRUE(memcmp(xbuf.buf, testbuf, lens[8]) == 0);

   CHECK_ZERO(dnxXmlClose(&xbuf));
   CHECK_TRUE(memcmp(xbuf.buf, testbuf, lens[9]) == 0);

   CHECK_ZERO(dnxXmlGet(&xbuf, "Short",  DNX_XML_SHORT,  &xshort));
   CHECK_TRUE(xshort == -100);

   CHECK_ZERO(dnxXmlGet(&xbuf, "UShort", DNX_XML_USHORT, &xushort));
   CHECK_TRUE(xushort == 100);

   CHECK_ZERO(dnxXmlGet(&xbuf, "Int",    DNX_XML_INT,    &xint));
   CHECK_TRUE(xint == -1000);

   CHECK_ZERO(dnxXmlGet(&xbuf, "UInt",   DNX_XML_UINT,   &xuint));
   CHECK_TRUE(xuint == 1000);

   CHECK_ZERO(dnxXmlGet(&xbuf, "Long",   DNX_XML_LONG,   &xlong));
   CHECK_TRUE(xlong == -10000);

   CHECK_ZERO(dnxXmlGet(&xbuf, "ULong",  DNX_XML_ULONG,  &xulong));
   CHECK_TRUE(xulong == 10000);

   CHECK_ZERO(dnxXmlGet(&xbuf, "String", DNX_XML_STR, &xstring));
   CHECK_TRUE(strcmp(xstring, "test string") == 0);

   // we have to free all strings returned by dnxXmlGet - this is so broken
   xfree(xstring);

   CHECK_ZERO(dnxXmlGet(&xbuf, "XID",    DNX_XML_XID,    &xid));
   CHECK_TRUE(xid.objType == DNX_OBJ_MANAGER);
   CHECK_TRUE(xid.objSerial == 12345678);
   CHECK_TRUE(xid.objSlot == 87654321);

#ifdef DEBUG_HEAP
   CHECK_ZERO(dnxCheckHeap());
#endif

   return 0;
}

#endif   /* DNX_XML_TEST */

/*--------------------------------------------------------------------------*/

