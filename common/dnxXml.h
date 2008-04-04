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

/** Types and definitions for DNX XML functionality.
 *
 * @file dnxXml.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXXML_H_
#define _DNXXML_H_

#include "dnxTransport.h"  // for DNX_MAX_MSG

typedef enum DnxXmlType
{
   DNX_XML_UNKNOWN = 0,
   DNX_XML_SHORT,
   DNX_XML_USHORT,
   DNX_XML_INT,
   DNX_XML_UINT,
   DNX_XML_LONG,
   DNX_XML_ULONG,
   DNX_XML_STR_UNESCAPED,
   DNX_XML_XID,
   DNX_XML_STR
} DnxXmlType;

typedef struct DnxXmlBuf
{
   char buf[DNX_MAX_MSG];
   unsigned size;
} DnxXmlBuf;

int dnxXmlOpen(DnxXmlBuf * xbuf, char * tag);
int dnxXmlAdd(DnxXmlBuf * xbuf, char * xTag, DnxXmlType xType, void * xData);
int dnxXmlGet(DnxXmlBuf * xbuf, char * xTag, DnxXmlType xType, void * xData);
int dnxXmlCmpStr(DnxXmlBuf * xbuf, char * xTag, char * cmpstr);
int dnxXmlClose(DnxXmlBuf * xbuf);

#endif   /* _DNXXML_H_ */

