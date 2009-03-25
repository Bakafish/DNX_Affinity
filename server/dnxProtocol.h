#ifndef DNXSERVERPROTOCOL_H_INCLUDED
#define DNXSERVERPROTOCOL_H_INCLUDED
#include "../common/dnxProtocol.h"

int dnxWaitForResult(DnxChannel * channel, DnxResult * pResult, char * address, int timeout);
int dnxSendJob(DnxChannel * channel, DnxJob * pJob, char * address);
int dnxWaitForNodeRequest(DnxChannel * channel, DnxNodeRequest * pReg, char * address, int timeout);
int dnxWaitForResult(DnxChannel * channel, DnxResult * pResult, char * address, int timeout);



#endif // DNXSERVERPROTOCOL_H_INCLUDED
