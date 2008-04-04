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

/** Types and definitions for low-level transport service providers.
 * 
 * Transport service providers must implement the transport service provider
 * interface (TSPI), and then populate the iDnxChannel structure, so that 
 * the Transport interface has access to its I/O methods.
 *
 * @file dnxTSPI.h
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXTSPI_H_
#define _DNXTSPI_H_

/** The generic Transport Service Provider Interface (TSPI) structure. */
typedef struct iDnxChannel_
{
   /** Transport open connection method. */
   int (*txOpen)(struct iDnxChannel_ * icp, int active);

   /** Transport close connection method. */
   int (*txClose)(struct iDnxChannel_ * icp);

   /** Transport read data method. */
   int (*txRead)(struct iDnxChannel_ * icp, char * buf, int * size, 
         int timeout, char * src);

   /** Transport write data method. */
   int (*txWrite)(struct iDnxChannel_ * icp, char * buf, int size, 
         int timeout, char * dst);

   /** Transport destructor. */
   void (*txDelete)(struct iDnxChannel_ * icp);

} iDnxChannel;

/** Transport Service Provider initialization function.
 * 
 * Initializes the exporting TSPI module and returns a connection constructor 
 * method that may be called with a URL whose scheme presumably matches the 
 * transport. 
 * 
 * URL scheme-matching is a higher level function; by the time the URL reaches 
 * the connection constructor, it is assumed that the scheme already matches 
 * the type of connection being constructed. The constructor merely parses the 
 * scheme-specific portion, which should contain binding information, eg., host 
 * name and port number for IP-based transports.
 * 
 * The connection constructor returned in @p ptxAlloc accepts a URL (from which 
 * it parses scheme-specific connection parameters) and returns a pointer to a 
 * newly allocated (but unconnected) connection object. It returns zero on 
 * success, or a non-zero error value.
 * 
 * @param[out] ptxAlloc - the address of storage for returning a pointer to
 *    the transport-specific connection constructor. 
 * 
 * @return Zero on success, or a non-zero error value.
 */
extern int dnxTSPInit(int (**ptxAlloc)(char * url, iDnxChannel ** icpp));

/** Transport Service Provider clean-up function.
 * 
 * Deinitializes the exporting TSPI module.
 */
extern void dnxTSPExit(void);

#endif   /* _DNXTSPI_H_ */

