#include "dnxProtocol.h"
#include "dnxXml.h"
#include "dnxError.h"
#include "common/dnxTransport.h"
#include <assert.h>
#include <string.h>

//----------------------------------------------------------------------------

/** Request a job from the registrar (client).
 *
 * @param[in] channel - the channel from which to receive the job request.
 * @param[out] pReg - the address of storage into which the request should
 *    be read from @p channel.
 * @param[in] address - the address to which @p pReg should be sent. This
 *    parameter is optional, and may be specified as NULL, in which case the
 *    channel address will be used.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxSendNodeRequest(DnxChannel * channel, DnxNodeRequest * pReg, char * address)
{
   DnxXmlBuf xbuf;

   assert(channel && pReg);

   // create the XML message
   dnxXmlOpen (&xbuf, "NodeRequest");
   dnxXmlAdd  (&xbuf, "XID",     DNX_XML_XID,  &pReg->xid);
   dnxXmlAdd  (&xbuf, "GUID",    DNX_XML_XID,  &pReg->xid);    // old format - for bc
   dnxXmlAdd  (&xbuf, "ReqType", DNX_XML_INT,  &pReg->reqType);
   dnxXmlAdd  (&xbuf, "JobCap",  DNX_XML_UINT, &pReg->jobCap);
   dnxXmlAdd  (&xbuf, "Capacity",DNX_XML_UINT, &pReg->jobCap); // old format - for bc
   dnxXmlAdd  (&xbuf, "TTL",     DNX_XML_UINT, &pReg->ttl);
   dnxXmlAdd  (&xbuf, "Hostname", DNX_XML_STR, pReg->hn);   
   dnxXmlClose(&xbuf);

   dnxDebug(3, "dnxSendNodeRequest: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);

   // send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}

//----------------------------------------------------------------------------

/** Wait for a job from the dispatcher (client).
 *
 * @param[in] channel - the channel from which to receive the job request.
 * @param[out] pJob - the address of storage into which the job request
 *    should be read from @p channel.
 * @param[out] address - the address of storage in which to return the address
 *    of the sender. This parameter is optional and may be passed as NULL. If
 *    non-NULL, it should be large enough to store sockaddr_* data.
 * @param[in] timeout - the maximum number of seconds the caller is willing to
 *    wait before accepting a timeout error.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxWaitForJob(DnxChannel * channel, DnxJob * pJob, char * address, int timeout)
{
   DnxXmlBuf xbuf;
   int ret;

   assert(channel && pJob);
   memset(pJob, 0, sizeof *pJob);

   // await a message from the specified channel
   xbuf.size = sizeof xbuf.buf - 1;
   if ((ret = dnxGet(channel, xbuf.buf, &xbuf.size, timeout, address)) != DNX_OK)
      return ret;

   // decode the XML message
   xbuf.buf[xbuf.size] = 0;
   dnxDebug(3, "dnxWaitForJob: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);

   // verify this is a "Job" message
   if ((ret = dnxXmlCmpStr(&xbuf, "Request", "Job")) != DNX_OK)
      return ret;

   // decode the job's XID (support older GUID format as well)
   if ((ret = dnxXmlGet(&xbuf, "XID", DNX_XML_XID, &pJob->xid)) != DNX_OK
         && (ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_XID, &pJob->xid)) != DNX_OK)
      return ret;

   // decode the job's state
   if ((ret = dnxXmlGet(&xbuf, "State", DNX_XML_INT, &pJob->state)) != DNX_OK)
      return ret;

   // decode the job's priority
   if ((ret = dnxXmlGet(&xbuf, "Priority", DNX_XML_INT, &pJob->priority)) != DNX_OK)
      return ret;

   // decode the job's timeout
   if ((ret = dnxXmlGet(&xbuf, "Timeout", DNX_XML_INT, &pJob->timeout)) != DNX_OK)
      return ret;

   // decode the job's command
   return dnxXmlGet(&xbuf, "Command", DNX_XML_STR, &pJob->cmd);
}

//------------------------------------------------------------------------------
//<SM 10/08 Job Ack Mod>
//This function handles acknowledgement of a job recieved from the server to the client.
int dnxSendJobAck(DnxChannel* channel, DnxJob *pJob,char * address)
{
    DnxXmlBuf xbuf;

    dnxXmlOpen (&xbuf, "JobAck");
    dnxXmlAdd  (&xbuf, "XID",      DNX_XML_XID,  &pJob->xid);
    dnxXmlClose(&xbuf);

    dnxDebug(3, "dnxSendJobAck: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);
       // send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}
//<SM 10/08 Job Ack Mod End>

//---------------------------------------------------------------------

/** Wait for a management request to come in (client).
 *
 * @param[in] channel - the channel from which to read a management request.
 * @param[out] pRequest - the address of storage in which to return the
 *    management request.
 * @param[out] address - the address of storage in which to return the address
 *    of the sender. This parameter is optional and may be passed as NULL. If
 *    non-NULL, it should be large enough to store sockaddr_* data.
 * @param[in] timeout - the maximum number of seconds the caller is willing to
 *    wait before accepting a timeout error.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxWaitForMgmtRequest(DnxChannel * channel, DnxMgmtRequest * pRequest,char * address, int timeout)
{
   DnxXmlBuf xbuf;
   int ret;
   char * addr;
   assert(channel && pRequest);

   memset(pRequest, 0, sizeof *pRequest);

   // await a message from the specified channel
   xbuf.size = sizeof xbuf.buf - 1;
   if ((ret = dnxGet(channel, xbuf.buf, &xbuf.size, timeout, address)) != DNX_OK)
      return ret;

   // decode the XML message
   xbuf.buf[xbuf.size] = 0;
   //addr = ntop(address);
   //dnxDebug(3, "dnxWaitForMgmtRequest: XML msg(%d bytes)=%s. from %s", xbuf.size, xbuf.buf, addr);
   //xfree(addr);
   // verify this is a "MgmtRequest" message
   if ((ret = dnxXmlCmpStr(&xbuf, "Request", "MgmtRequest")) != DNX_OK)
      return ret;

   // decode the Manager's XID (support older GUID format as well).
   if ((ret = dnxXmlGet(&xbuf, "XID", DNX_XML_XID, &pRequest->xid)) != DNX_OK
         && (ret = dnxXmlGet(&xbuf, "GUID", DNX_XML_XID, &pRequest->xid)) != DNX_OK)
      return ret;

   // decode the management request

   ret = dnxXmlGet(&xbuf, "Action", DNX_XML_STR, &pRequest->action);

   return ret;
}



//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

/** Report a job result to the collector (client).
 *
 * @param[in] channel - the channel on which to send @p pResult.
 * @param[in] pResult - the result data to be sent on @p channel.
 * @param[in] address - the address to which @p pResult should be sent. This
 *    parameter is optional, and may be specified as NULL, in which case the
 *    channel address will be used.
 *
 * @return Zero on success, or a non-zero error value.
 */
int dnxSendResult(DnxChannel * channel, DnxResult * pResult, char * address)
{
   DnxXmlBuf xbuf;
   char * resData;

   assert(channel && pResult);

   if ((resData = pResult->resData) == 0 || *resData == 0)
      resData = "(DNX: No Output!)";

   // create the XML message
   dnxXmlOpen (&xbuf, "Result");
   dnxXmlAdd  (&xbuf, "XID",        DNX_XML_XID,  &pResult->xid);
   dnxXmlAdd  (&xbuf, "GUID",       DNX_XML_XID,  &pResult->xid); // old format - for bc
   dnxXmlAdd  (&xbuf, "State",      DNX_XML_INT,  &pResult->state);
   dnxXmlAdd  (&xbuf, "Delta",      DNX_XML_UINT, &pResult->delta);
   dnxXmlAdd  (&xbuf, "ResultCode", DNX_XML_INT,  &pResult->resCode);
   dnxXmlAdd  (&xbuf, "ResultData", DNX_XML_STR,   resData);
   dnxXmlClose(&xbuf);

   dnxDebug(3, "dnxSendResult: XML msg(%d bytes)=%s.", xbuf.size, xbuf.buf);

   // send it on the specified channel
   return dnxPut(channel, xbuf.buf, xbuf.size, 0, address);
}
