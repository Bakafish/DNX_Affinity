#ifndef DNXPROTOCOL_CLIENT_H_INCLUDED
#define DNXPROTOCOL_CLIENT_H_INCLUDED
#include "../common/dnxProtocol.h"

int dnxSendNodeRequest(DnxChannel * channel, DnxNodeRequest * pReg, char * address);
int dnxWaitForJob(DnxChannel * channel, DnxJob * pJob, char * address, int timeout);
int dnxSendJobAck(DnxChannel* channel, DnxAck *pAck, char * address);
int dnxWaitForMgmtRequest(DnxChannel * channel, DnxMgmtRequest * pRequest,char * address, int timeout);
int dnxSendResult(DnxChannel * channel, DnxResult * pResult, char * address);
#endif // DNXPROTOCOL_H_INCLUDED
