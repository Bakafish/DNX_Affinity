# DNX Server NEB Module Makefile

all:	dnxServer

# Location of nagios source code
NAGIOS_SRC=/usr/local/src/nagios-2.7


# Compile and link flags
CC=gcc
CFLAGS=-Wall -c -g -fPIC -I$(NAGIOS_SRC)/include -I../include
CFLAGS2=-Wall -g -fPIC -shared
LDFLAGS=-lpthread


# Modules common to both the DNX Client and Server
COMMON_OBJS=dnxError.o dnxTransport.o dnxProtocol.o dnxTcp.o dnxUdp.o \
	dnxMsgQ.o dnxXml.o pfopen.o

# List of objects comprising the DNX Server NEB Module
SERVER_OBJS=dnxNebMain.o dnxConfig.o dnxJobList.o dnxCollector.o dnxDispatcher.o \
	dnxRegistrar.o dnxTimer.o dnxQueue.o dnxLogging.o


# DNX Server NEB Module target
dnxServer:	$(SERVER_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS2) -o dnxServer.o $(SERVER_OBJS) $(COMMON_OBJS) $(LDFLAGS)


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

