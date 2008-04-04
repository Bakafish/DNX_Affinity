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

#include "dnxError.h"

static dnxError gLastError = DNX_OK;      // Last known error code

#if 0
static char gLastErrStr[MAX_ERR_STR+1];      // Last error message
#endif

static char * gErrCatalog[] = 
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
   "Incorrect or invalid XML message"
};

/** @todo Create mechanism for preserving error stack details. */

//----------------------------------------------------------------------------

dnxError dnxGetLastError (void)
{
   return gLastError;
}

//----------------------------------------------------------------------------

void dnxSetLastError (dnxError errno)
{
   gLastError = errno;
}

//----------------------------------------------------------------------------

char *dnxErrorString (dnxError errno)
{
   return (char *)((errno < 0 || errno > DNX_ERR_LASTERROR) ? "Unknown error code" : gErrCatalog[errno]);
}

/*--------------------------------------------------------------------------*/

