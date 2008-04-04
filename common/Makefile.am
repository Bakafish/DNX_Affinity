noinst_LTLIBRARIES = libcmn.la

noinst_HEADERS =\
 dnxCfgParser.h\
 dnxChannel.h\
 dnxDebug.h\
 dnxError.h\
 dnxHeap.h\
 dnxLogging.h\
 dnxMsgQ.h\
 dnxProtocol.h\
 dnxSleep.h\
 dnxTcp.h\
 dnxTransport.h\
 dnxUdp.h\
 dnxXml.h\
 pfopen.h

libcmn_la_SOURCES =\
 dnxCfgParser.c\
 dnxError.c\
 dnxHeap.c\
 dnxLogging.c\
 dnxMsgQ.c\
 dnxProtocol.c\
 dnxSleep.c\
 dnxTcp.c\
 dnxTransport.c\
 dnxUdp.c\
 dnxXml.c\
 pfopen.c


# ---------------------------------------------------------------------------
# common code unit tests
#
TESTS = dnxCfgParserTest
check_PROGRAMS = dnxCfgParserTest

dnxCfgParserTest_SOURCES = dnxCfgParser.c dnxError.c
dnxCfgParserTest_CPPFLAGS = -DDNX_CFGPARSER_TEST

