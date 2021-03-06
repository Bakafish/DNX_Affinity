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
 dnxTimer.h\
 dnxNode.h\
 dnxProtocol.h

dnxServer_la_SOURCES =\
 dnxCollector.c\
 dnxDispatcher.c\
 dnxJobList.c\
 dnxNebMain.c\
 dnxQueue.c\
 dnxRegistrar.c\
 dnxTimer.c\
 dnxNode.c\
 dnxProtocol.c

dnxServer_la_CPPFLAGS =\
 -fPIC\
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

dnxJobListTest_SOURCES = dnxJobList.c
dnxJobListTest_CPPFLAGS = -DDNX_JOBLIST_TEST -I$(top_srcdir)/common
dnxJobListTest_LDFLAGS = ../common/libcmn.la

dnxQueueTest_SOURCES = dnxQueue.c
dnxQueueTest_CPPFLAGS = -DDNX_QUEUE_TEST -I$(top_srcdir)/common
dnxQueueTest_LDFLAGS = ../common/libcmn.la

dnxTimerTest_SOURCES = dnxTimer.c
dnxTimerTest_CPPFLAGS = -DDNX_TIMER_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-@nagios_target@/include
dnxTimerTest_LDFLAGS = ../common/libcmn.la

dnxCollectorTest_SOURCES = dnxCollector.c
dnxCollectorTest_CPPFLAGS = -DDNX_COLLECTOR_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-@nagios_target@/include
dnxCollectorTest_LDFLAGS = ../common/libcmn.la

dnxDispatcherTest_SOURCES = dnxDispatcher.c
dnxDispatcherTest_CPPFLAGS = -DDNX_DISPATCHER_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-@nagios_target@/include
dnxDispatcherTest_LDFLAGS = ../common/libcmn.la

dnxRegistrarTest_SOURCES = dnxRegistrar.c
dnxRegistrarTest_CPPFLAGS = -DDNX_REGISTRAR_TEST -I$(top_srcdir)/common\
 -I$(top_srcdir)/nagios/nagios-@nagios_target@/include
dnxRegistrarTest_LDFLAGS = ../common/libcmn.la

