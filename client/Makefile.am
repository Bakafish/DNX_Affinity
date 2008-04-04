EXTRA_DIST = test

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
 -I$(top_srcdir)/common

dnxClient_LDADD = ../common/libcmn.la

