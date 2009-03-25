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

/** Implements the DNX communications methods.
 *
 * @file dnxProtocol.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxProtocol.h"

#include "dnxError.h"
#include "dnxDebug.h"
#include "dnxTransport.h"
#include "dnxXml.h"
#include "dnxLogging.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <assert.h>

//----------------------------------------------------------------------------

/** Create a transaction id (XID) from a type, serial number and slot value.
 * 
 * @param[out] pxid - the address of storage for the XID to be returned.
 * @param[in] xType - the request type to be stored in the XID.
 * @param[in] xSerial - the serial number to be stored in the XID.
 * @param[in] xSlot - the slot number to be stored in the XID.
 * 
 * @return Always returns zero.
 */
int dnxMakeXID(DnxXID * pxid, DnxObjType xType, unsigned long xSerial, 
      unsigned long xSlot)
{
   assert(pxid && xType >= 0 && xType < DNX_OBJ_MAX);

   pxid->objType   = xType;
   pxid->objSerial = xSerial;
   pxid->objSlot   = xSlot;

   return DNX_OK;
}

//----------------------------------------------------------------------------

/** Compare two XID's for equality; return a boolean value.   
 * 
 * @param[in] pxa - a reference to the first XID to be compared.
 * @param[in] pxb - a reference to the second XID to be compared.
 * 
 * @return A boolean value; false (0) if @p pxa is NOT equal to @p pxa; true
 * (!false) if @p pxa IS equal to @p pxb.
 */
int dnxEqualXIDs(DnxXID * pxa, DnxXID * pxb)
{
   return pxa->objType == pxb->objType 
         && pxa->objSerial == pxb->objSerial 
         && pxa->objSlot == pxb->objSlot;
}

//--------------------------------------------------------------------------*/
/** Issue a management request to the client agent (server).
 * 
 * @param[in] channel - the channel on which to send the management request.
 * @param[out] pRequest - the management request to be sent.
 * @param[in] address - the address to which @p pRequest should be sent. This 
 *    parameter is optional, and may be specified as NULL, in which case the 
 *    channel address will be used.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxSendMgmtRequest(DnxChannel * channel, DnxMgmtRequest * pRequest, char * address)
{
   DnxXmlBuf xbuf;

   assert(channel && pRequest);

   // create the XML message
   dnxXmlOpen (&xbuf, "MgmtRequest");
   dnxXmlAdd  (&xbuf, "XID",    DNX_XML_XID, &pRequest->xid);
   dnxXmlAdd  (&xbuf, "GUID",   DNX_XML_XID, &pRequest->xid);  // old format - for bc
   dnxXmlAdd  (&xbuf, "Action", DNX_XML_STR,  pRequest->action);
   dnxXmlClose(&xbuf);

   dnxDebug(3, "dnxSendMgmtRequest: XML msg(%d bytes)=%s. to %s", xbuf.size, xbuf.buf, address);

   // send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}

//----------------------------------------------------------------------------

/** Issue a management reply to the server (client).
 * 
 * @param[in] channel - the channel on which to send the management request.
 * @param[out] pReply - the management request to be sent.
 * @param[in] address - the address to which @p pRequest should be sent. This 
 *    parameter is optional, and may be specified as NULL, in which case the 
 *    channel address will be used.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxSendMgmtReply(DnxChannel * channel, DnxMgmtReply * pReply, char * address)
{
   DnxXmlBuf xbuf;

   assert(channel && pReply);

   // create the XML message
   dnxXmlOpen (&xbuf, "MgmtReply");
   dnxXmlAdd  (&xbuf, "XID",    DNX_XML_XID, &pReply->xid);
   dnxXmlAdd  (&xbuf, "Status", DNX_XML_INT, &pReply->status);
   dnxXmlAdd  (&xbuf, "Result", DNX_XML_STR,  pReply->reply);
   dnxXmlClose(&xbuf);

   char * addr = ntop(address);
   dnxDebug(3, "dnxSendMgmtReply: XML msg(%d bytes)=%s to %s.", xbuf.size, xbuf.buf,addr);
   xfree(addr);
   // send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}

/** Wait for a management reply to come in (server).
 * 
 * @param[in] channel - the channel from which to read a management request.
 * @param[out] pReply - the address of storage in which to return the 
 *    management reply.
 * @param[out] address - the address of storage in which to return the address
 *    of the sender. This parameter is optional and may be passed as NULL. If
 *    non-NULL, it should be large enough to store sockaddr_* data.
 * @param[in] timeout - the maximum number of seconds the caller is willing to
 *    wait before accepting a timeout error.
 * 
 * @return Zero on success, or a non-zero error value.
 */
int dnxWaitForMgmtReply(DnxChannel * channel, DnxMgmtReply * pReply, char * address, int timeout)
{
   DnxXmlBuf xbuf;
   int ret;

   assert(channel && pReply);

   memset(pReply, 0, sizeof *pReply);

   // await a message from the specified channel
   xbuf.size = sizeof xbuf.buf - 1;
   if ((ret = dnxGet(channel, xbuf.buf, &xbuf.size, timeout, address)) != DNX_OK)
      return ret;

   // decode the XML message
   xbuf.buf[xbuf.size] = 0;
   dnxDebug(3, "dnxWaitForMgmtReply: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);

   // verify this is a "MgmtRequest" message
   if ((ret = dnxXmlCmpStr(&xbuf, "Request", "MgmtReply")) != DNX_OK)
      return ret;

   // decode the Manager's XID.
   if ((ret = dnxXmlGet(&xbuf, "XID", DNX_XML_XID, &pReply->xid)) != DNX_OK)
      return ret;

   // decode the Manager's XID.
   if ((ret = dnxXmlGet(&xbuf, "Status", DNX_XML_INT, &pReply->status)) != DNX_OK)
      return ret;

   // decode the management request
   return dnxXmlGet(&xbuf, "Result", DNX_XML_STR, &pReply->reply);
}


// 
// //----------------------------------------------------------------------------
// 
// /** Wait for a node request (server).
//  * 
//  * @param[in] channel - the channel from which to receive the node request.
//  * @param[out] pReg - the address of storage into which the request should
//  *    be read from @p channel.
//  * @param[out] address - the address of storage in which to return the address
//  *    of the sender. This parameter is optional and may be passed as NULL. If
//  *    non-NULL, it should be large enough to store sockaddr_* data.
//  * @param[in] timeout - the maximum number of seconds the caller is willing to
//  *    wait before accepting a timeout error.
//  * 
//  * @return Zero on success, or a non-zero error value.
//  */
// int dnxWaitForNodeRequest(DnxChannel * channel, DnxNodeRequest * pReg, 
//       char * address, int timeout)
// {
//    DnxXmlBuf xbuf;
//    int ret;
// 
//    assert(channel && pReg);
// 
//    memset(pReg, 0, sizeof *pReg);
// 
//    // await a message from the specified channel
//    xbuf.size = sizeof xbuf.buf - 1;
//    if ((ret = dnxGet(channel, xbuf.buf, &xbuf.size, timeout, address)) != DNX_OK)
//       return ret;
// 
//    // decode the XML message:
//    xbuf.buf[xbuf.size] = 0;
//    dnxDebug(3, "dnxWaitForNodeRequest: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);
// 
//    // verify this is a "NodeRequest" message
//    if ((ret = dnxXmlCmpStr(&xbuf, "Request", "NodeRequest")) != DNX_OK)
//       return ret;
// 
//    // decode the hostname
//    if ((ret = dnxXmlGet(&xbuf, "Hostname", DNX_XML_STR, &pReg->hostname)) != DNX_OK)
//       return ret;
// 
//    // decode the worker node's XID (support older GUID format as well)
//    if ((ret = dnxXmlGet(&xbuf, "XID", DNX_XML_XID, &pReg->xid)) != DNX_OK
//          && (ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_XID, &pReg->xid)) != DNX_OK)
//       return ret;
// 
//    // decode request type
//    if ((ret = dnxXmlGet(&xbuf, "ReqType", DNX_XML_INT, &pReg->reqType)) != DNX_OK)
//       return ret;
// 
//    // decode job capacity (support strange mixture of JobCap and Capacity)
//    if ((ret = dnxXmlGet(&xbuf, "JobCap", DNX_XML_INT, &pReg->jobCap)) != DNX_OK
//          && (ret = dnxXmlGet(&xbuf, "Capacity", DNX_XML_INT, &pReg->jobCap)) != DNX_OK)
//       return ret;
//       
//    // decode job expiration (Time-To-Live in seconds)
//    return dnxXmlGet(&xbuf, "TTL", DNX_XML_INT, &pReg->ttl);
// }
// 
// //----------------------------------------------------------------------------
// 
// /** Request a job from the registrar (client).
//  * 
//  * @param[in] channel - the channel from which to receive the job request.
//  * @param[out] pReg - the address of storage into which the request should
//  *    be read from @p channel.
//  * @param[in] address - the address to which @p pReg should be sent. This 
//  *    parameter is optional, and may be specified as NULL, in which case the 
//  *    channel address will be used.
//  * 
//  * @return Zero on success, or a non-zero error value.
//  */
// int dnxSendNodeRequest(DnxChannel * channel, DnxNodeRequest * pReg, char * address)
// {
//    DnxXmlBuf xbuf;
// 
//    assert(channel && pReg);
//    
//    // create the XML message
//    dnxXmlOpen (&xbuf, "NodeRequest");
//    dnxXmlAdd  (&xbuf, "XID",      DNX_XML_XID,  &pReg->xid);
//    dnxXmlAdd  (&xbuf, "GUID",     DNX_XML_XID,  &pReg->xid);    // old format - for bc
//    dnxXmlAdd  (&xbuf, "ReqType",  DNX_XML_INT,  &pReg->reqType);
//    dnxXmlAdd  (&xbuf, "JobCap",   DNX_XML_UINT, &pReg->jobCap);
//    dnxXmlAdd  (&xbuf, "Capacity", DNX_XML_UINT, &pReg->jobCap); // old format - for bc
//    dnxXmlAdd  (&xbuf, "TTL",      DNX_XML_UINT, &pReg->ttl);
//    dnxXmlAdd  (&xbuf, "Hostname", DNX_XML_STR,  pReg->hostname);   
//    dnxXmlClose(&xbuf);
// 
//    dnxDebug(3, "dnxSendNodeRequest: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);
// 
//    // send it on the specified channel
//    return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
// }
// 
// //----------------------------------------------------------------------------
// 
// /** Wait for a job from the dispatcher (client).
//  * 
//  * @param[in] channel - the channel from which to receive the job request.
//  * @param[out] pJob - the address of storage into which the job request 
//  *    should be read from @p channel.
//  * @param[out] address - the address of storage in which to return the address
//  *    of the sender. This parameter is optional and may be passed as NULL. If
//  *    non-NULL, it should be large enough to store sockaddr_* data.
//  * @param[in] timeout - the maximum number of seconds the caller is willing to
//  *    wait before accepting a timeout error.
//  * 
//  * @return Zero on success, or a non-zero error value.
//  */
// int dnxWaitForJob(DnxChannel * channel, DnxJob * pJob, char * address, int timeout)
// {
//    DnxXmlBuf xbuf;
//    int ret;
// 
//    assert(channel && pJob);
// 
//    memset(pJob, 0, sizeof *pJob);
// 
//    // await a message from the specified channel
//    xbuf.size = sizeof xbuf.buf - 1;
//    if ((ret = dnxGet(channel, xbuf.buf, &xbuf.size, timeout, address)) != DNX_OK)
//       return ret;
// 
//    // decode the XML message
//    xbuf.buf[xbuf.size] = 0;
//    dnxDebug(3, "dnxWaitForJob: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);
// 
//    // verify this is a "Job" message
//    if ((ret = dnxXmlCmpStr(&xbuf, "Request", "Job")) != DNX_OK)
//       return ret;
// 
//    // decode the job's XID (support older GUID format as well)
//    if ((ret = dnxXmlGet(&xbuf, "XID", DNX_XML_XID, &pJob->xid)) != DNX_OK
//          && (ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_XID, &pJob->xid)) != DNX_OK)
//       return ret;
// 
//    // decode the job's state
//    if ((ret = dnxXmlGet(&xbuf, "State", DNX_XML_INT, &pJob->state)) != DNX_OK)
//       return ret;
// 
//    // decode the job's priority
//    if ((ret = dnxXmlGet(&xbuf, "Priority", DNX_XML_INT, &pJob->priority)) != DNX_OK)
//       return ret;
// 
//    // decode the job's timeout
//    if ((ret = dnxXmlGet(&xbuf, "Timeout", DNX_XML_INT, &pJob->timeout)) != DNX_OK)
//       return ret;
// 
//    // decode the job's command
//    return dnxXmlGet(&xbuf, "Command", DNX_XML_STR, &pJob->cmd);
// }
// 
// //----------------------------------------------------------------------------
// 
// /** Dispatch a job to a client node (server).
//  * 
//  * @param[in] channel - the channel on which to send @p pJob.
//  * @param[in] pJob - the job request to be sent on @p channel.
//  * @param[in] address - the address to which @p pJob should be sent. This 
//  *    parameter is optional, and may be specified as NULL, in which case the 
//  *    channel address will be used.
//  * 
//  * @return Zero on success, or a non-zero error value.
//  */
// int dnxSendJob(DnxChannel * channel, DnxJob * pJob, char * address)
// {
//    DnxXmlBuf xbuf;
// 
//    assert(channel && pJob && pJob->cmd && *pJob->cmd);
// 
//    // create the XML message
//    dnxXmlOpen (&xbuf, "Job");
//    dnxXmlAdd  (&xbuf, "XID",      DNX_XML_XID,  &pJob->xid);
//    dnxXmlAdd  (&xbuf, "GUID",     DNX_XML_XID,  &pJob->xid); // old format - for bc
//    dnxXmlAdd  (&xbuf, "State",    DNX_XML_INT,  &pJob->state);
//    dnxXmlAdd  (&xbuf, "Priority", DNX_XML_INT,  &pJob->priority);
//    dnxXmlAdd  (&xbuf, "Timeout",  DNX_XML_INT,  &pJob->timeout);
//    dnxXmlAdd  (&xbuf, "Command",  DNX_XML_STR,   pJob->cmd);
//    dnxXmlClose(&xbuf);
// 
//    dnxDebug(3, "dnxSendJob: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);
// 
//    // send it on the specified channel
//    return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
// }
// 
// //----------------------------------------------------------------------------
// 
// /** Collect job results from a client (server).
//  * 
//  * @param[in] channel - the channel from which to receive the job result.
//  * @param[out] pResult - the address of storage into which the job result
//  *    should be read from @p channel.
//  * @param[out] address - the address of storage in which to return the address
//  *    of the sender. This parameter is optional and may be passed as NULL. If
//  *    non-NULL, it should be large enough to store sockaddr_* data.
//  * @param[in] timeout - the maximum number of seconds the caller is willing to
//  *    wait before accepting a timeout error.
//  * 
//  * @return Zero on success, or a non-zero error value.
//  */
// int dnxWaitForResult(DnxChannel * channel, DnxResult * pResult, 
//       char * address, int timeout)
// {
//    DnxXmlBuf xbuf;
//    int ret;
// 
//    assert(channel && pResult);
// 
//    memset(pResult, 0, sizeof *pResult);
// 
//    // await a message from the specified channel
//    xbuf.size = sizeof xbuf.buf - 1;
//    if ((ret = dnxGet(channel, xbuf.buf, &xbuf.size, timeout, address)) != DNX_OK)
//       return ret;
// 
//    // decode the XML message
//    xbuf.buf[xbuf.size] = 0;
//    dnxDebug(3, "dnxWaitForResult: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);
// 
//    // verify this is a "Result" message
//    if ((ret = dnxXmlCmpStr(&xbuf, "Request", "Result")) != DNX_OK)
//       return ret;
// 
//    // decode the result's XID (support older GUID format as well)
//    if ((ret = dnxXmlGet(&xbuf, "XID", DNX_XML_XID, &pResult->xid)) != DNX_OK
//          && (ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_XID, &pResult->xid)) != DNX_OK)
//       return ret;
// 
//    // decode the result's state
//    if ((ret = dnxXmlGet(&xbuf, "State", DNX_XML_INT, &pResult->state)) != DNX_OK)
//       return ret;
// 
//    // decode the result's execution time delta
//    if ((ret = dnxXmlGet(&xbuf, "Delta", DNX_XML_UINT, &pResult->delta)) != DNX_OK)
//       return ret;
// 
//    // decode the result's result code
//    if ((ret = dnxXmlGet(&xbuf, "ResultCode", DNX_XML_INT, &pResult->resCode)) != DNX_OK)
//       return ret;
// 
//    // decode the result's result data
//    return dnxXmlGet(&xbuf, "ResultData", DNX_XML_STR, &pResult->resData);
// }
// 
// //----------------------------------------------------------------------------
// 
// /** Report a job result to the collector (client).
//  * 
//  * @param[in] channel - the channel on which to send @p pResult.
//  * @param[in] pResult - the result data to be sent on @p channel.
//  * @param[in] address - the address to which @p pResult should be sent. This 
//  *    parameter is optional, and may be specified as NULL, in which case the 
//  *    channel address will be used.
//  * 
//  * @return Zero on success, or a non-zero error value.
//  */
// int dnxSendResult(DnxChannel * channel, DnxResult * pResult, char * address)
// {
//    DnxXmlBuf xbuf;
//    char * resData;
// 
//    assert(channel && pResult);
// 
//    if ((resData = pResult->resData) == 0 || *resData == 0)
//       resData = "(DNX: No Output!)";
// 
//    // create the XML message
//    dnxXmlOpen (&xbuf, "Result");
//    dnxXmlAdd  (&xbuf, "XID",        DNX_XML_XID,  &pResult->xid);
//    dnxXmlAdd  (&xbuf, "GUID",       DNX_XML_XID,  &pResult->xid); // old format - for bc
//    dnxXmlAdd  (&xbuf, "State",      DNX_XML_INT,  &pResult->state);
//    dnxXmlAdd  (&xbuf, "Delta",      DNX_XML_UINT, &pResult->delta);
//    dnxXmlAdd  (&xbuf, "ResultCode", DNX_XML_INT,  &pResult->resCode);
//    dnxXmlAdd  (&xbuf, "ResultData", DNX_XML_STR,   resData);
//    dnxXmlClose(&xbuf);
// 
//    dnxDebug(3, "dnxSendResult: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);
// 
//    // send it on the specified channel
//    return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
// }
// 
// //----------------------------------------------------------------------------
// 
// /** Wait for a management request to come in (client).
//  * 
//  * @param[in] channel - the channel from which to read a management request.
//  * @param[out] pRequest - the address of storage in which to return the 
//  *    management request.
//  * @param[out] address - the address of storage in which to return the address
//  *    of the sender. This parameter is optional and may be passed as NULL. If
//  *    non-NULL, it should be large enough to store sockaddr_* data.
//  * @param[in] timeout - the maximum number of seconds the caller is willing to
//  *    wait before accepting a timeout error.
//  * 
//  * @return Zero on success, or a non-zero error value.
//  */
// int dnxWaitForMgmtRequest(DnxChannel * channel, DnxMgmtRequest * pRequest, 
//       char * address, int timeout)
// {
//    DnxXmlBuf xbuf;
//    int ret;
// 
//    assert(channel && pRequest);
// 
//    memset(pRequest, 0, sizeof *pRequest);
// 
//    // await a message from the specified channel
//    xbuf.size = sizeof xbuf.buf - 1;
//    if ((ret = dnxGet(channel, xbuf.buf, &xbuf.size, timeout, address)) != DNX_OK)
//       return ret;
// 
//    // decode the XML message
//    xbuf.buf[xbuf.size] = 0;
//    dnxDebug(3, "dnxWaitForMgmtRequest: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);
// 
//    // verify this is a "MgmtRequest" message
//    if ((ret = dnxXmlCmpStr(&xbuf, "Request", "MgmtRequest")) != DNX_OK)
//       return ret;
// 
//    // decode the Manager's XID (support older GUID format as well).
//    if ((ret = dnxXmlGet(&xbuf, "XID", DNX_XML_XID, &pRequest->xid)) != DNX_OK
//          && (ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_XID, &pRequest->xid)) != DNX_OK)
//       return ret;
// 
//    // decode the management request
//    return dnxXmlGet(&xbuf, "Action", DNX_XML_STR, &pRequest->action);
// }
// 
