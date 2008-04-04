# DNX Client Makefile

all:	dnxClient

# Location of NRPE pre-compiled module
#NRPE_MODULE=../nrpe-2.6/src/mod_nrpe.o ../nrpe-2.6/src/utils.o
NRPE_MODULE=


# Compile and link flags
CC=gcc
CFLAGS=-Wall -c -g -I../include
CFLAGS2=-Wall -g
#LDFLAGS=-lpthread -lssl -lcrypto
LDFLAGS=-lpthread


# Modules common to both the DNX Client and Server
COMMON_OBJS=dnxError.o dnxTransport.o dnxProtocol.o dnxTcp.o dnxUdp.o \
	dnxMsgQ.o dnxXml.o pfopen.o

CLIENT_OBJS=dnxClientMain.o dnxConfig.o dnxWLM.o dnxWorker.o dnxPlugin.o dnxLogging.o $(NRPE_MODULE)

dnxClient:	$(CLIENT_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS2) -o dnxClient $(CLIENT_OBJS) $(COMMON_OBJS) $(LDFLAGS)


# DNX Common modules
dnxError.o:	../common/dnxError.c
	$(CC) $(CFLAGS) $<

dnxTransport.o:	../common/dnxTransport.c
	$(CC) $(CFLAGS) $<

dnxProtocol.o:	../common/dnxProtocol.c
	$(CC) $(CFLAGS) $<

dnxTcp.o:	../common/dnxTcp.c
	$(CC) $(CFLAGS) $<

dnxUdp.o:	../common/dnxUdp.c
	$(CC) $(CFLAGS) $<

dnxMsgQ.o:	../common/dnxMsgQ.c
	$(CC) $(CFLAGS) $<

dnxXml.o:	../common/dnxXml.c
	$(CC) $(CFLAGS) $<

pfopen.o:	../common/pfopen.c
	$(CC) $(CFLAGS) $<


# Test programs
chantest:	chantest.o $(COMMON_OBJS)
	gcc $(CFLAGS2) -o chantest chantest.o $(COMMON_OBJS) $(LDFLAGS)
