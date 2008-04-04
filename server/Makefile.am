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
 dnxDispatcher.c\
 dnxJobList.c\
 dnxNebMain.c\
 dnxQueue.c\
 dnxRegistrar.c\
 dnxTimer.c

dnxServer_la_CPPFLAGS =\
 -DSYSCONFDIR=\"$(sysconfdir)\"\
 -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-@nagios_target@/include

dnxServer_la_LIBADD = ../common/libcmn.la

dnxServer_la_LDFLAGS = -module -avoid-version

# ---------------------------------------------------------------------------
# server-side unit tests
#
TESTS =\
 dnxJobListTest\
 dnxQueueTest\
 dnxTimerTest\
 dnxCollectorTest\
 dnxDispatcherTest\
 dnxRegistrarTest

check_PROGRAMS =\
 dnxJobListTest\
 dnxQueueTest\
 dnxTimerTest\
 dnxCollectorTest\
 dnxDispatcherTest\
 dnxRegistrarTest

dnxJobListTest_SOURCES = dnxJobList.c @top_srcdir@/common/dnxError.c
dnxJobListTest_CPPFLAGS = -DDNX_JOBLIST_TEST -I$(top_srcdir)/common

dnxQueueTest_SOURCES = dnxQueue.c @top_srcdir@/common/dnxError.c
dnxQueueTest_CPPFLAGS = -DDNX_QUEUE_TEST -I$(top_srcdir)/common

dnxTimerTest_SOURCES = dnxTimer.c @top_srcdir@/common/dnxSleep.c\
 @top_srcdir@/common/dnxError.c
dnxTimerTest_CPPFLAGS = -DDNX_TIMER_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-2.7/include

dnxCollectorTest_SOURCES = dnxCollector.c @top_srcdir@/common/dnxError.c\
 @top_srcdir@/common/dnxSleep.c
dnxCollectorTest_CPPFLAGS = -DDNX_COLLECTOR_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-2.7/include

dnxDispatcherTest_SOURCES = dnxDispatcher.c @top_srcdir@/common/dnxError.c\
 @top_srcdir@/common/dnxSleep.c
dnxDispatcherTest_CPPFLAGS = -DDNX_DISPATCHER_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-2.7/include

dnxRegistrarTest_SOURCES = dnxRegistrar.c @top_srcdir@/common/dnxError.c\
 @top_srcdir@/common/dnxSleep.c
dnxRegistrarTest_CPPFLAGS = -DDNX_REGISTRAR_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-2.7/include

