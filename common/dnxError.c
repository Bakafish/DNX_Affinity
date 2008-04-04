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

//----------------------------------------------------------------------------

/** Return the last error code stored in the global dnx error variable.
 *
 * @return The last error value stored.
 */
dnxError dnxGetLastError(void)
{
   return gLastError;
}

//----------------------------------------------------------------------------

/** Set the global dnx error variable to some dnx error value.
 *
 * @param[in] eno - the value to be set.
 */
void dnxSetLastError(dnxError eno)
{
   gLastError = eno;
}

//----------------------------------------------------------------------------

/** Return an error string that matches a specified dnx error code.
 * 
 * @param[in] eno - the error code for which a string representation is 
 *    desired.
 *
 * @return A pointer to a statically allocated string representation of the
 * error code specified in @p errno.
 */
char * dnxErrorString(dnxError eno)
{
   static char * errCatalog[] = 
   {
      "A-OK, Okey-Dokey, Rock-On",
      "Invalid arguments or parameters",
      "Resource is exhausted",
      "Invalid or malformed URL",
      "Resource is already initialized/deinitialized",
      "Resource already exists",
      "Unsupported protocol",
      "Out of memory",
      "Channel open error",
      "Message size is out of bounds",
      "Message transmission failure",
      "Message reception failure",
      "Invalid communications address",
      "Requested resource was not found",
      "Incorrect or invalid XML message",
      "Threading error",
      "Timeout error",
      "Resource is busy",
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

