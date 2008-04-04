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

/** Types and definitions for DNX Error management.
 *
 * @file dnxError.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXERROR_H_
#define _DNXERROR_H_

#define MAX_ERR_STR           1023

#define DNX_OK                0  // A-OK, Okey-Dokey, Rock-On
                              
#define DNX_ERR_INVALID       1  // Invalid arguments or parameters
#define DNX_ERR_CAPACITY      2  // Out of channel slots or XML buffer space
#define DNX_ERR_BADURL        3  // Invalid, malformed URL
#define DNX_ERR_ALREADY       4  // Already init or deinit
#define DNX_ERR_EXIST         5  // Channel already exists
#define DNX_ERR_UNSUPPORTED   6  // Unsupported protocol
#define DNX_ERR_MEMORY        7  // Out of memory
#define DNX_ERR_OPEN          8  // Channel open error
#define DNX_ERR_SIZE          9  // Message size is out of bounds
#define DNX_ERR_SEND          10 // Message transmission failure
#define DNX_ERR_RECEIVE       11 // Message reception failure
#define DNX_ERR_ADDRESS       12 // Invalid communications address
#define DNX_ERR_NOTFOUND      13 // Requested resource was not found
#define DNX_ERR_SYNTAX        14 // Incorrect/invalid XML message
#define DNX_ERR_THREAD        15 // Thread error
#define DNX_ERR_TIMEOUT       16 // Timeout
#define DNX_ERR_BUSY          17 // Resource is busy

#define DNX_ERR_LASTERROR     17

typedef int dnxError;

dnxError dnxGetLastError(void);
void dnxSetLastError(dnxError errno);
char * dnxErrorString(dnxError errno);

#endif   /* _DNXERROR_H_ */

