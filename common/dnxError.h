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

#define MAX_ERR_STR           1023                 //!< Maximum error string length

#define DNX_OK                0                    //!< A-OK, Okey-Dokey, Rock-On

#define DNX_ERR_BASE          500                  //!< Base all error values here
                              
#define DNX_ERR_INVALID       (DNX_ERR_BASE + 1 )  //!< Invalid arguments or parameters
#define DNX_ERR_CAPACITY      (DNX_ERR_BASE + 2 )  //!< Out of channel slots or XML buffer space
#define DNX_ERR_BADURL        (DNX_ERR_BASE + 3 )  //!< Invalid, malformed URL
#define DNX_ERR_ALREADY       (DNX_ERR_BASE + 4 )  //!< Already init or deinit
#define DNX_ERR_EXIST         (DNX_ERR_BASE + 5 )  //!< Channel already exists
#define DNX_ERR_UNSUPPORTED   (DNX_ERR_BASE + 6 )  //!< Unsupported protocol
#define DNX_ERR_MEMORY        (DNX_ERR_BASE + 7 )  //!< Out of memory
#define DNX_ERR_OPEN          (DNX_ERR_BASE + 8 )  //!< Channel open error
#define DNX_ERR_SIZE          (DNX_ERR_BASE + 9 )  //!< Message size is out of bounds
#define DNX_ERR_SEND          (DNX_ERR_BASE + 10)  //!< Message transmission failure
#define DNX_ERR_RECEIVE       (DNX_ERR_BASE + 11)  //!< Message reception failure
#define DNX_ERR_ADDRESS       (DNX_ERR_BASE + 12)  //!< Invalid communications address
#define DNX_ERR_NOTFOUND      (DNX_ERR_BASE + 13)  //!< Requested resource was not found
#define DNX_ERR_SYNTAX        (DNX_ERR_BASE + 14)  //!< Incorrect/invalid XML message
#define DNX_ERR_THREAD        (DNX_ERR_BASE + 15)  //!< Thread error
#define DNX_ERR_TIMEOUT       (DNX_ERR_BASE + 16)  //!< Timeout
#define DNX_ERR_BUSY          (DNX_ERR_BASE + 17)  //!< Resource is busy
#define DNX_ERR_ACCESS        (DNX_ERR_BASE + 18)  //!< Access denied
#define DNX_ERR_EXPIRED       (DNX_ERR_BASE + 19)  //!< Resource is expired

/** A type abstraction for a DNX error value. */
typedef int dnxError;

/** Return the last error code stored in the global dnx error variable.
 *
 * @return The last error value stored.
 */
dnxError dnxGetLastError(void);

/** Set the global dnx error variable to some dnx error value.
 *
 * @param[in] eno - the value to be set.
 */
void dnxSetLastError(dnxError eno);

/** Return an error string that matches a specified dnx error code.
 * 
 * @param[in] eno - the error code for which a string representation is 
 *    desired.
 *
 * @return A pointer to a statically allocated string representation of the
 * error code specified in @p errno.
 */
char * dnxErrorString(dnxError eno);

#endif   /* _DNXERROR_H_ */

