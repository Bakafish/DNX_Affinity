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

/** Implements the DNX Error Reporting functions.
 *
 * @file dnxError.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

/*!@defgroup DNX_COMMON_IMPL DNX Common Services Implementation 
 * @defgroup DNX_COMMON_IFC  DNX Common Services Interface
 */

#include "dnxError.h"

#include <errno.h>
#include <string.h>

#define elemcount(x) (sizeof(x)/sizeof(*(x)))

static dnxError gLastError = DNX_OK;      /*!< Last known error code. */

/** @todo Create a mechanism for preserving error stack details. */

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

dnxError dnxGetLastError(void) { return gLastError; }

//----------------------------------------------------------------------------

void dnxSetLastError(dnxError eno) { gLastError = eno; }

//----------------------------------------------------------------------------

char * dnxErrorString(dnxError eno)
{
   static char * errCatalog[] = 
   {
      "A-OK, Okey-Dokey, Rock-On",
      "Invalid value",
      "Resource is exhausted",
      "Invalid or malformed URL",
      "Resource is already initialized/deinitialized",
      "Resource already exists",
      "Unsupported operation",
      "Out of memory",
      "Channel open error",
      "Message size is out of bounds",
      "Message transmission failure",
      "Message reception failure",
      "Invalid communications address",
      "Resource was not found",
      "Incorrect or invalid XML message",
      "Threading error",
      "Timeout error",
      "Resource is busy",
      "Access denied",
   };

   // check for system error first - return system error string
   if (eno < DNX_ERR_BASE)
      return strerror(eno);

   // adjust for dnx error base - return dnx error string
   eno -= DNX_ERR_BASE;
   return (char *)((eno < 0 || eno >= elemcount(errCatalog)) ? 
         "Unknown error code" : errCatalog[eno]);
}

/*--------------------------------------------------------------------------*/

