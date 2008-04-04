SUBDIRS = test

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
 -DSYSLOGDIR=\"$(syslogdir)\"\
 -DSYSRUNPATH=\"$(sysrundir)\"\
 -DDNXUSER=\"$(dnx_user)\"\
 -DDNXGROUP=\"$(dnx_group)\"\
 -I$(top_srcdir)/common

dnxClient_LDADD = ../common/libcmn.la

install-exec-hook:
	$(mkdir_p) $(DESTDIR)$(sysrundir)
	if [ `id -u` -eq 0 ]; then \
	   chown $(dnx_user):$(dnx_group) $(DESTDIR)$(sysrundir); \
	fi
	if [ ! -e $(DESTDIR)$(syslogdir) ]; then \
	   $(mkdir_p) $(DESTDIR)$(syslogdir); \
	   if [ `id -u` -eq 0 ]; then \
	      chown $(dnx_user):$(dnx_group) $(DESTDIR)$(syslogdir); \
	   fi; \
	fi
