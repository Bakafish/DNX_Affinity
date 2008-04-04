if ENABLE_DEBUG_HEAP
  dbgheap_srcs = dnxHeap.c
endif

noinst_LTLIBRARIES = libcmn.la

noinst_HEADERS =\
 dnxCfgParser.h\
 dnxDebug.h\
 dnxError.h\
 dnxHeap.h\
 dnxLogging.h\
 dnxMsgQ.h\
 dnxProtocol.h\
 dnxSleep.h\
 dnxTSPI.h\
 dnxTcp.h\
 dnxTransport.h\
 dnxUdp.h\
 dnxXml.h\
 pfopen.h\
 utesthelp.h

libcmn_la_SOURCES =\
 dnxCfgParser.c\
 dnxError.c\
 dnxLogging.c\
 dnxMsgQ.c\
 dnxProtocol.c\
 dnxSleep.c\
 dnxTcp.c\
 dnxTransport.c\
 dnxUdp.c\
 dnxXml.c\
 pfopen.c\
 $(dbgheap_srcs)

# ---------------------------------------------------------------------------
# common code unit tests
#
TESTS = dnxCfgParserTest dnxXmlTest
check_PROGRAMS = dnxCfgParserTest dnxXmlTest

dnxCfgParserTest_SOURCES = dnxCfgParser.c dnxError.c $(dbgheap_srcs)
dnxCfgParserTest_CPPFLAGS = -DDNX_CFGPARSER_TEST

dnxXmlTest_SOURCES = dnxXml.c dnxError.c $(dbgheap_srcs)
dnxXmlTest_CPPFLAGS = -DDNX_XML_TEST

