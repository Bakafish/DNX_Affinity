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

/** Escape the text within XML strings - compliant with W3C.
 * 
 * Routine donated by William Leibzon. Thanks William!
 * 
 * @param[out] outstr - escaped string is returned in this buffer.
 * @param[in] instr - string to be escaped is passed in this buffer.
 * @param[in] maxbuf - the maximum number of bytes in @p outstr on entry.
 * 
 * @return Zero on success, or a non-zero error code.
 */
static int dnxXmlEscapeStr(char * outstr, char * instr, int maxbuf)
{
   int i,op;
   int ret = DNX_OK;
   
   for (i = 0, op = 0; i < strlen(instr) && i < maxbuf && ret == DNX_OK; i++) 
   {
      switch(instr[i]) 
      {
         case 38: // & -> &amp;
            if (op + 5 < maxbuf) 
            {
               memcpy(outstr + op, "&amp;", 5);
               op += 5;
            }
            else 
               ret = DNX_ERR_CAPACITY;
            break;
         case 60: // < -> &lt;
            if (op + 4 < maxbuf) 
            {
               memcpy(outstr + op, "&lt;", 4);
               op += 4;
            }
            else 
               ret = DNX_ERR_CAPACITY;
            break;
         case 62: // > -> &gt;
            if (op + 4 < maxbuf) 
            {
               memcpy(outstr + op, "&gt;", 4);
               op += 4;
            }
            else ret = DNX_ERR_CAPACITY;
            break;
         case 34: // " -> &quot;
            if (op + 6 < maxbuf) 
            {
               memcpy(outstr + op, "&qout;", 6);
               op += 6;
            }
            else 
               ret = DNX_ERR_CAPACITY;
            break;
         case 39: // ' -> &apos;
            if (op + 6 < maxbuf) 
            {
               memcpy(outstr + op, "&apos;", 6);
               op += 6;
            }
            else 
               ret = DNX_ERR_CAPACITY;
            break;
         default:
            if (op + 1 < maxbuf) 
            {
               outstr[op] = instr[i];
               op++;
            }
            else 
               ret = DNX_ERR_CAPACITY;
            break;
      }
   }
   if (i >= maxbuf)
      ret = DNX_ERR_CAPACITY;
   if (ret != DNX_ERR_CAPACITY)
      outstr[op] = 0;
   return ret;
}

//----------------------------------------------------------------------------

/** Un-Escape the text within XML strings - compliant with W3C.
 * 
 * Routine donated by William Leibzon. Thanks William!
 * 
 * @param[out] outstr - unescaped string is returned in this buffer.
 * @param[in] instr - string to be unescaped is passed in this buffer.
 * @param[in] maxbuf - the maximum number of bytes in @p outstr on entry.
 * 
 * @return Zero on success, or a non-zero error code.
 */
static int dnxXmlUnescapeStr(char * outstr, char * instr, int maxbuf)
{
   int i, op;
   int ret = DNX_OK;
   char * temp;
   int tempnum;
   
   for (i = 0, op = 0; i < strlen(instr) && i < maxbuf && ret == DNX_OK; i++, op++) 
   {
      if (instr[i] == 38)
      {  // &
         if (strncmp(instr + i, "&amp;", 5) == 0)
         {
            outstr[op] = '&';
            i += 4;
         }
         else if (strncmp(instr + i, "&lt;", 4) == 0)
         {
            outstr[op] = '<';
            i += 3;
         }
         else if (strncmp(instr + i, "&gt;", 4) == 0)
         {
            outstr[op] = '>';
            i+=3;
         }
         else if (strncmp(instr + i, "&qout;", 6) == 0)
         {
            outstr[op] = 34;
            i+=5;
         }
         else if (strncmp(instr + i, "&apos;", 6) == 0)
         {
            outstr[op] = 39;
            i+=5;
         }
         else if ((temp = memchr(instr + i, ';', maxbuf-i)) != 0)
         {
            if (instr[i+1] == '#') 
            {  // Handle cases like &#39;
               errno = 0;
               tempnum = strtol(instr + i, 0, 0);
               if (errno == ERANGE || tempnum < 0 || tempnum > 255) 
               {
                  ret = DNX_ERR_SYNTAX;
                  dnxDebug(2, "dnxXmlUnescapeStr: invalid unescape #, "
                              "instr=%s, i=%d, num=%d", instr, i, tempnum);
               }
               else
                  outstr[op]=(int)tempnum;
               i = temp-instr;
            }
            else 
            {  // Unsupported XML escape sequence
               ret = DNX_ERR_SYNTAX;
               dnxDebug(2, "dnxXmlUnescapeStr: unsupported xml escape "
                           "sequence, instr=%s, i=%d", instr, i);
            }
         }
         else 
         {  // This was not an escape sequence
            ret = DNX_ERR_SYNTAX;
            dnxDebug(2, "dnxXmlUnescapeStr: inappropriate escape "
                        "sequence, instr=%s, i=%d", instr, i);
         }
      }
      else 
         outstr[op]=instr[i];
   }
   if (i >= maxbuf)
      ret = DNX_ERR_CAPACITY;
   if (ret != DNX_ERR_CAPACITY)
      outstr[op] = 0;
   return ret;
}

//----------------------------------------------------------------------------

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

      case DNX_XML_STR_UNESCAPED:
         assert(strlen((char *)xData) < size);
         strncpy(buf, (char *)xData, size);
         buf[size - 1] = 0;
         break;

      case DNX_XML_STR:
         assert(strlen((char *)xData) < size);
         ret = dnxXmlEscapeStr(buf, (char *)xData, size);
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
   int len, ret = DNX_OK;

   assert(xbuf && xTag && buf && size);

   // search for opening bracket
   cp = xbuf->buf;
   while ((cp = strchr(cp, '<')) != 0)
   {
      cp++;

      if (*cp == '/')
         continue;   // not a match

      // search for end-bracket
      if ((ep = strchr(cp, '>')) == 0)
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
      if ((ep = strchr(cp, '<')) == 0)
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
      if ((ep = strchr(cp, '>')) == 0)
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
   if (xData && (ret = dnxXmlToString(xType, xData, buf, sizeof buf)) != DNX_OK)
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
   char * temp;

   // extract the value of the specified tag from the XML buffer
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

      case DNX_XML_STR_UNESCAPED:
         if ((*((char **)xData) = xstrdup(buf)) == 0)
            ret = DNX_ERR_MEMORY;
         break;

      case DNX_XML_STR:
         if ((temp = xstrdup(buf)) == 0)
            ret = DNX_ERR_MEMORY;
         else
         {
            ret = dnxXmlUnescapeStr(temp, buf, sizeof buf);
            *(char **)xData = temp;
         }
         break;

      case DNX_XML_XID:
         // the format of a XID is: "objType-objSerial-objSlot",
         // where objType, objSerial and objSlot are unsigned integers
         if ((cp = strchr(buf, '-')) == 0)
         {
            ret = DNX_ERR_SYNTAX;   // missing XID separator
            break;
         }
         *cp++ = 0;  // now buf points to objType and cp points to objSerial
   
         if ((ep = strchr(cp, '-')) == 0)
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
            ret = DNX_ERR_SYNTAX;
            break;
         }
         ((DnxXID *)xData)->objType = (DnxObjType)unum;
   
         // decode objSerial
         errno = 0;
         unum = strtoul(cp, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
         {
            ret = DNX_ERR_SYNTAX;
            break;
         }
         ((DnxXID *)xData)->objSerial = (unsigned long)unum;
   
         // decode objSlot
         errno = 0;
         unum = strtoul(ep, &lastchar, 0);
         if (errno == ERANGE || *lastchar)
         {
            ret = DNX_ERR_SYNTAX;
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

/** Compare a string with an XML node text value.
 * 
 * @param[in,out] xbuf - the buffer to be validated and closed.
 * @param[in] xTag - the tag for which to search in @p xbuf.
 * @param[in] cmpstr - the comparison string to match.
 * 
 * @return Zero on match; non-zero on not found, or no match.
 */
int dnxXmlCmpStr(DnxXmlBuf * xbuf, char * xTag, char * cmpstr)
{
   char buf[DNX_MAX_MSG];
   int ret;

   if ((ret = dnxXmlGetTagValue(xbuf, 
         xTag, DNX_XML_STR, buf, sizeof buf)) != DNX_OK)
      return ret;

   return strcmp(cmpstr, buf) == 0 ? DNX_OK : DNX_ERR_SYNTAX;
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

