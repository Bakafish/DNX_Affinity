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

dnxServer_la_CPPFLAGS =\
 -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-@nagios_target@/include

dnxServer_la_LIBADD = ../common/libcmn.la

dnxServer_la_LDFLAGS = -module -avoid-version

# ---------------------------------------------------------------------------
# server-side unit tests
#
TESTS = dnxJobListTest dnxTimerTest
check_PROGRAMS = dnxJobListTest dnxTimerTest

dnxJobListTest_SOURCES = dnxJobList.c @top_srcdir@/common/dnxError.c
dnxJobListTest_CPPFLAGS = -DDEBUG -DDNX_JOBLIST_TEST -I$(top_srcdir)/common

dnxTimerTest_SOURCES = dnxTimer.c @top_srcdir@/common/dnxError.c
dnxTimerTest_CPPFLAGS = -DDEBUG -DDNX_TIMER_TEST\
 -I$(top_srcdir)/common -I$(top_srcdir)/nagios/nagios-2.7/include

