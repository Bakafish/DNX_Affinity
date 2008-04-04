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

/** Types and definitions for DNX transport layer.
 *
 * Connection targets for dnxConnect are specified as message queue names.
 * Message queue names are specified in the configuration file, and by 
 * DNX_MSG_REGISTER messages. The configuration file specification is of the 
 * form:
 * 
 *    [MessageQueues]
 *       MessageQueueName = URL
 * 
 * The currently supported URLs are:
 * 
 *    1. dnx+tcp://hostname:port
 *    2. dnx+udp://hostname:port       (currently the only one in use)
 *    3. dnx+msgq://message-queue-ID
 * 
 * Currently configured message queue names are:
 * 
 *    1. Scheduler   -  Dispatchers use this to communicate with the Nagios 
 *                      scheduler.
 *    2. Jobs        -  Workers use this to receive jobs from dispatchers and 
 *                      the WLM (for shutdown).
 *    3. Results     -  Workers use this to post completed Jobs to the 
 *                      collector.
 *    4. Collector   -  Local collectors use this to communicate with the 
 *                      master collector.
 * 
 * @file dnxTransport.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXTRANSPORT_H_
#define _DNXTRANSPORT_H_

/** The maximum length of a DNX transport URL. */
#define DNX_MAX_URL 1023

/** The maximum length of a DNX message. */
#define DNX_MAX_MSG        1024

/** An abstraction for DnxChannel. */
typedef struct { int unused; } DnxChannel;

int dnxChanMapAdd(char * name, char * url);
void dnxChanMapDelete(char * name);

int dnxConnect(char * name, int active, DnxChannel ** channel);
void dnxDisconnect(DnxChannel * channel);

int dnxGet(DnxChannel * channel, char * buf, int * size, int timeout, char * src);
int dnxPut(DnxChannel * channel, char * buf, int size, int timeout, char * dst);

int dnxChanMapInit(char * fileName);
void dnxChanMapRelease(void);

#endif   /* _DNXTRANSPORT_H_ */

