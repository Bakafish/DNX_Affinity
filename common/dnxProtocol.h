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

/** Types and definitions for DNX messaging protocol.
 * 
 * DNX messages are transport independent and have the following properties:
 * 
 *    1. Request - enumerated constant identifying the message payload
 *    2. XML (REST-style) body
 * 
 * Currently supported DNX messages are:
 * 
 *    1. DNX_MSG_NODE_REQUEST
 *    2. DNX_MSG_JOB
 *    3. DNX_MSG_RESULT
 *    4. DNX_MSG_MGMT_REQUEST
 *    5. DNX_MSG_MGMT_REPLY
 * 
@verbatim   

   ----------------------------------------------
   Structure: DNX_MSG_NODE_REQUEST
   Issued By: Worker       (dnxSendNodeRequest)
   Issued To: Registrar    (dnxWaitForNodeRequest)
   
     <dnxMessage>
       <Request>NodeRequest</Request>
       <XID>Xid:ObjType-ObjSerial-ObjSlot</XID>
       <ReqType>IntegerRequestType</ReqType>
       <JobCap>IntegerCapabilityCount</JobCap>
       <TTL>IntegerSeconds<TTL>
       <Hostname>StringHostname</Hostname>
     </dnxMessage>

   ----------------------------------------------
   Structure: DNX_MSG_JOB
   Issued By: Dispatcher   (dnxSendJob)
   Issued To: Worker       (dnxWaitForJob)
   
     <dnxMessage>
       <Request>Job</Request>
       <XID>Xid:ObjType-ObjSerial-ObjSlot</XID>
       <State>IntegerPending</State>
       <Priority>IntegerPriority</Priority>
       <Timeout>IntegerTimeout</Timeout>
       <Command>StringCommand param1 ... </Command>
     </dnxMessage>

   ----------------------------------------------
   Structure: DNX_MSG_RESULT
   Issued By: Worker       (dnxSendResult)
   Issued To: Collector    (dnxWaitForResult)
   
     <dnxMessage>
       <Request>Result</Request>
       <XID>Xid:ObjType-ObjSerial-ObjSlot</XID>
       <State>IntegerState</State>
       <Delta>IntegerSeconds</Delta>
       <ResultCode>IntegerResultCode</ResultCode>
       <ResultData>StringResult</ResultData>
     </dnxMessage>

   ----------------------------------------------
   Structure: DNX_MSG_MGMT_REQUEST
   Issued By: Server       (dnxSendMgmtRequest)
   Issued To: Client       (dnxWaitForMgmtRequest)
   
     <dnxMessage>
       <Request>MgmtRequest</Request>
       <XID>Xid:ObjType-ObjSerial-ObjSlot</XID>
       <Action>StringAction</Action>
     </dnxMessage>

   ----------------------------------------------
   Structure: DNX_MSG_MGMT_REPLY
   Issued By: Client       (dnxSendMgmtReply)
   Issued To: Server       (dnxWaitForMgmtReply)
   
     <dnxMessage>
       <Request>MgmtReply</Request>
       <XID>Xid:ObjType-ObjSerial-ObjSlot</XID>
       <Result>StringResponse</Result>
     </dnxMessage>

@endverbatim   
 * The DNX Objects are:
 * 
 *    1. DNX_JOB
 *    2. DNX_RESULT
 *    3. DNX_AGENT
 * 
 * Each DNX Job Object has:
 * 
 *    1. XID - Assigned at Job creation by the Scheduler
 *    2. State: Pending, Executing, Completed, Cancelled
 *    3. Execution Command
 *    4. Execution Parameters
 *    5. Execution Start Time
 *    5. Execution End Time
 *    6. Result Code
 *    7. Result Data
 * 
 * When and object is serialized and transmitted, only those relevant 
 * portions of the object are transmitted - order to optimize bandwith usage 
 * and processing time.
 * 
 * For example, a DNX Job object as transmitted to a Dispatcher, via a 
 * DNX_MSG_PUT_JOB, will only include the following DNX Job attributes:
 * 
 *    XID, State, Execution Command, Execution Parameters 
 *    and Execution Start Time.
 * 
 * Conversely, a DNX Job object as transmitted to a Collector, via a 
 * DNS_PUT_RESULT, will only include the following DNX Job attributes:
 * 
 *    XID, State, Execution Start Time, Execution End Time, Result Code, 
 *    and Result Data
 * 
 * @file dnxProtocol.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXPROTOCOL_H_
#define _DNXPROTOCOL_H_

#include <time.h>

#include "dnxTransport.h"

typedef struct DnxAffinityList
{
   char * name;                     //!< Name of Nagios Host group/dnxClient
   unsigned long long flag;         //!< Flag for affinity check
   struct DnxAffinityList * next;   //!< Next structure in linked list
} DnxAffinityList;


/** Defines the type of a DNX object in a network message. */
typedef enum DnxObjType
{
   DNX_OBJ_SCHEDULER = 0,
   DNX_OBJ_DISPATCHER,
   DNX_OBJ_WORKER,
   DNX_OBJ_COLLECTOR,
   DNX_OBJ_REAPER,
   DNX_OBJ_JOB,
   DNX_OBJ_MANAGER,
   DNX_OBJ_MAX
} DnxObjType;

/** Defines the type of a DNX worker node network request. */
typedef enum DnxReqType
{
   DNX_REQ_REGISTER = 0, 
   DNX_REQ_DEREGISTER, 
   DNX_REQ_ACK, 
   DNX_REQ_NAK 
} DnxReqType;

/** Defines the state of a DNX job. */
typedef enum DnxJobState
{
   DNX_JOB_NULL = 0,    // Job is empty
   DNX_JOB_UNBOUND,     // We have Job, but no Client is assigned
   DNX_JOB_PENDING,     // We are waiting to be dispatched
   DNX_JOB_INPROGRESS,  // We are waiting to get the result
   DNX_JOB_COMPLETE,    // Result is received
   DNX_JOB_EXPIRED,     // Job is expired
   DNX_JOB_ACKNOWLEDGED // Job has been acked and is ready for purging
} DnxJobState;

/** The maximum number of bytes in a DNX message address buffer. */
#define DNX_MAX_ADDRESS 64

/** The maximum number of bytes in a DNX message hostname buffer. */
#define MAX_HOSTNAME 253            // DNS max via ISC

/** DNX wire transaction ID structure. */
typedef struct DnxXID
{
   DnxObjType objType;              //!< Type of object sending message.
   unsigned long objSerial;         //!< Serial number of this request.
   unsigned long objSlot;           //!< Request queue slot number.
} DnxXID;

/** Request job wire structure. */
typedef struct DnxNodeRequest
{
   DnxXID xid;                      //!< Worker node transaction ID.
   DnxReqType reqType;              //!< Request type.
   unsigned int jobCap;             //!< Job capacity.
   unsigned int ttl;                //!< Request Time-To-Live (in seconds).
   char address[DNX_MAX_ADDRESS];   //!< Source address. (should be initialized as at least the same size as  a struct sockaddr_storage)
   unsigned long long flags;        //!< Affinity groups bitmask (not transmitted).
   time_t expires;                  //!< Job expiration time (not transmitted).
   time_t retry;                    //!< Time to attempt to resubmit if no Ack recieved
   char * addr;                     //!< Source address as char * for easier logging later (not transmitted)
   char * hn;                       //!< Source Hostname (not transmitted)
} DnxNodeRequest;

/** Send job wire structure. */
typedef struct DnxJob
{
   DnxXID xid;                      //!< Job transaction id.
   unsigned int timestamp;          //!< Packet trasmit timestamp
   DnxJobState state;               //!< Job state.
   int priority;                    //!< Execution Priority.
   int timeout;                     //!< Max job execution time.
   char * cmd;                      //!< Contains command plus arguments.
   char address[DNX_MAX_ADDRESS];   //!< Source address.
} DnxJob;

/** Send job results wire structure. */
typedef struct DnxResult
{
   DnxXID xid;                      //!< Job transaction id.
   unsigned int timestamp;          //!< Packet trasmit timestamp
   DnxJobState state;               //!< Job state.
   unsigned int delta;              //!< Job execution time delta.
   int resCode;                     //!< Job result code.
   char * resData;                  //!< Job result data.
   char address[DNX_MAX_ADDRESS];   //!< Source address.
} DnxResult;

/** DNX management request wire structure. */
typedef struct DnxMgmtRequest
{
   DnxXID xid;                      //!< Generated manager transaction id.
   char * action;                   //!< Request: SHUTDOWN, RELOAD, STATUS.
   char address[DNX_MAX_ADDRESS];   //!< Source address.
} DnxMgmtRequest;

/** DNX management response wire structure. */
typedef struct DnxMgmtReply
{
   DnxXID xid;                      //!< Reflected manager transaction id.
   DnxReqType status;               //!< Request status: ACK or NAK.
   char * reply;                    //!< Reply data (only valid for STATUS request).
   char address[DNX_MAX_ADDRESS];   //!< Source address.
} DnxMgmtReply;



int dnxSendMgmtRequest(DnxChannel * channel, DnxMgmtRequest * pRequest, char * address);
int dnxSendMgmtReply(DnxChannel * channel, DnxMgmtReply * pReply, char * address);
int dnxWaitForMgmtReply(DnxChannel * channel, DnxMgmtReply * pReply, char * address, int timeout);
int dnxSendJobAck(DnxChannel* channel, DnxJob *pAck, char * address);

int dnxMakeXID(DnxXID * pxid, DnxObjType xType, unsigned long xSerial, unsigned long xSlot);
int dnxEqualXIDs(DnxXID * pxa, DnxXID * pxb);
#endif   /* _DNXPROTOCOL_H_ */

