EXTRA_DIST = sync_plugins.pl

libexec_SCRIPTS = sync_plugins.pl

lib_LTLIBRARIES = dnxServer.la

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
 -DSYSLOGDIR=\"$(syslogdir)\"\
 -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-$(nagios_target)/include

dnxServer_la_LIBADD = ../common/libcmn.la

dnxServer_la_LDFLAGS = -module -avoid-version

install-exec-hook:
	if [ `id -u` -eq 0 ] && [ -z ${DESTDIR} ]; then \
	   chown $(nagios_user):$(nagios_group) $(DESTDIR)$(libexecdir); \
	   chown $(nagios_user):$(nagios_group) $(DESTDIR)$(libexecdir)/sync_plugins.pl; \
	fi
	rm $(DESTDIR)$(libdir)/dnxServer.a
	rm $(DESTDIR)$(libdir)/dnxServer.la
	if [ ! -e $(syslogdir) ]; then \
	   $(install_sh) -d $(syslogdir); \
	   if [ `id -u` -eq 0 ] && [ -z ${DESTDIR} ]; then \
	      chown $(nagios_user):$(nagios_group) $(syslogdir); \
	   fi; \
	fi

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

dnxJobListTest_SOURCES = dnxJobList.c\
 ../common/dnxError.c ../common/dnxHeap.c
dnxJobListTest_CPPFLAGS = -DDNX_JOBLIST_TEST -I$(top_srcdir)/common

dnxQueueTest_SOURCES = dnxQueue.c\
 ../common/dnxError.c ../common/dnxHeap.c
dnxQueueTest_CPPFLAGS = -DDNX_QUEUE_TEST -I$(top_srcdir)/common

dnxTimerTest_SOURCES = dnxTimer.c\
 ../common/dnxSleep.c ../common/dnxError.c ../common/dnxHeap.c
dnxTimerTest_CPPFLAGS = -DDNX_TIMER_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-@nagios_target@/include

dnxCollectorTest_SOURCES = dnxCollector.c\
 ../common/dnxError.c ../common/dnxSleep.c ../common/dnxHeap.c
dnxCollectorTest_CPPFLAGS = -DDNX_COLLECTOR_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-@nagios_target@/include

dnxDispatcherTest_SOURCES = dnxDispatcher.c\
 ../common/dnxError.c ../common/dnxSleep.c ../common/dnxHeap.c
dnxDispatcherTest_CPPFLAGS = -DDNX_DISPATCHER_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-@nagios_target@/include

dnxRegistrarTest_SOURCES = dnxRegistrar.c\
 ../common/dnxError.c ../common/dnxSleep.c ../common/dnxHeap.c
dnxRegistrarTest_CPPFLAGS = -DDNX_REGISTRAR_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-@nagios_target@/include

