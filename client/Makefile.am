EXTRA_DIST = test

sbin_PROGRAMS = dnxClient

noinst_HEADERS =\
 dnxPlugin.h\
 dnxWLM.h\
 dnxWorker.h

dnxClient_SOURCES =\
 dnxClientMain.c\
 dnxPlugin.c\
 dnxWLM.c\
 dnxWorker.c

INCLUDES =\
 -I$(top_srcdir)/common

dnxClient_LDADD = ../common/libcmn.la

