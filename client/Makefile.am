EXTRA_DIST = dnxClientTest.sh test

sbin_PROGRAMS = dnxClient

noinst_HEADERS =\
 dnxPlugin.h\
 dnxWLM.h

dnxClient_SOURCES =\
 dnxClientMain.c\
 dnxPlugin.c\
 dnxWLM.c

dnxClient_CPPFLAGS =\
 -DSYSCONFDIR=\"$(sysconfdir)\"\
 -DSYSRUNPATH=\"$(sysrunpath)\"\
 -DDNXUSER=\"$(dnx_user)\"\
 -DDNXGROUP=\"$(dnx_group)\"\
 -I$(top_srcdir)/common

dnxClient_LDADD = ../common/libcmn.la

# DNX client unit tests

# ---------------------------------------------------------------------------
# client unit tests
#
TESTS = dnxClientTest.sh
