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

/** DNX XML definitions.
 *
 * @file dnxXml.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX
 */

#ifndef _DNXXML_H_
#define _DNXXML_H_

// Obtain definition of DNX_MAX_MSG constant
#include "dnxChannel.h"
// Obtain definition of DnxGuid
#include "dnxProtocol.h"

typedef enum _DnxXmlMsg_ {
	DNX_XML_UNKNOWN = 0,
	DNX_XML_SHORT,
	DNX_XML_USHORT,
	DNX_XML_INT,
	DNX_XML_UINT,
	DNX_XML_LONG,
	DNX_XML_ULONG,
	DNX_XML_STR,
	DNX_XML_GUID
} DnxXmlType;

typedef struct _DnxXmlBuf_ {
	char buf[DNX_MAX_MSG];
	unsigned size;
} DnxXmlBuf;


int dnxXmlOpen (DnxXmlBuf *xbuf, char *tag);
int dnxXmlAdd  (DnxXmlBuf *xbuf, char *xTag, DnxXmlType xType, void *xData);
int dnxXmlGet  (DnxXmlBuf *xbuf, char *xTag, DnxXmlType xType, void *xData);
int dnxXmlClose(DnxXmlBuf *xbuf);
int dnxMakeGuid (DnxGuid *pGuid, DnxObjType xType, unsigned long xSerial, unsigned long xSlot);

#endif   /* _DNXXML_H_ */
