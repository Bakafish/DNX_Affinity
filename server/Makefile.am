EXTRA_DIST = test sync_plugins.pl

pkglib_LTLIBRARIES = dnxServer.la

noinst_HEADERS =\
 dnxCollector.h\
 dnxDispatcher.h\
 dnxJobList.h\
 dnxNebMain.h\
 dnxQueue.h\
 dnxRegistrar.h\
 dnxTimer.h

dnxServer_la_SOURCES =\
 dnxCollector.c\
 dnxConfig.c\
 dnxDispatcher.c\
 dnxJobList.c\
 dnxLogging.c\
 dnxNebMain.c\
 dnxQueue.c\
 dnxRegistrar.c\
 dnxTimer.c

INCLUDES =\
 -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-2.7/include

dnxServer_la_LIBADD = ../common/libcmn.la

dnxServer_la_LDFLAGS = -module -avoid-version
