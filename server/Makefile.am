EXTRA_DIST = test sync_plugins.pl

pkglib_LTLIBRARIES = dnxServer.la

noinst_HEADERS =\
 dsaudit.h\
 dscollector.h\
 dsconfig.h\
 dsdispatcher.h\
 dsjoblist.h\
 dsnebmain.h\
 dsqueue.h\
 dsregistrar.h\
 dstimer.h

dnxServer_la_SOURCES =\
 dsaudit.c\
 dscollector.c\
 dsconfig.c\
 dsdispatcher.c\
 dsjoblist.c\
 dslogging.c\
 dsnebmain.c\
 dsqueue.c\
 dsregistrar.c\
 dstimer.c

INCLUDES =\
 -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-2.7/include

dnxServer_la_LIBADD = ../common/libcmn.la

dnxServer_la_LDFLAGS = -module -avoid-version
