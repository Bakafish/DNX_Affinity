EXTRA_DIST = dnxClient.cfg.in dnxServer.cfg.in dnxcld.generic-init.in dnxcld.init.in

edit = sed \
   -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
   -e 's|@sysrundir[@]|$(sysrundir)|g' \
   -e 's|@syslogdir[@]|$(syslogdir)|g' \
   -e 's|@libexecdir[@]|$(libexecdir)|g' \
   -e 's|@dnx_user[@]|$(dnx_user)|g' \
   -e 's|@dnx_group[@]|$(dnx_group)|g' \
   -e 's|@prefix[@]|$(prefix)|g'

all: dnxClient.cfg dnxServer.cfg dnxcld.generic-init dnxcld.init

# The link from rcdnxcld to dnxcld needs to be a relative reference because 
# we can't link by name to a file in a staged installation directory, and a
# hard link won't work across file systems. We'll just assume we're talking
# about the real /etc directory.

# Install init script and soft link to it from sbin directory
install-initscript: dnxcld.generic-init dnxcld.init
	$(install_sh) -d $(DESTDIR)$(initrddir)
	if [ -f /etc/SuSE-release ]; then \
	   $(install_sh) -m 0755 dnxcld.init $(DESTDIR)$(initrddir)/dnxcld; \
	else \
	   $(install_sh) -m 0755 dnxcld.generic-init $(DESTDIR)$(initrddir)/dnxcld; \
	fi
	$(LN_S) -f $(initrddir)/dnxcld $(DESTDIR)$(sbindir)/rcdnxcld

# Install *sample* config files
install-cfg: install-clientcfg install-servercfg

install-clientcfg: dnxClient.cfg
	$(install_sh) -d $(DESTDIR)$(sysconfdir)
	$(install_sh) -m 0644 dnxClient.cfg $(DESTDIR)$(sysconfdir)/dnxClient.cfg
	if [ `id -u` -eq 0 ] && [ -z ${DESTDIR} ]; then \
	   chown $(dnx_user):$(dnx_group) $(DESTDIR)$(sysconfdir)/dnxClient.cfg; \
	fi

install-servercfg: dnxServer.cfg
	$(install_sh) -d $(DESTDIR)$(sysconfdir)
	$(install_sh) -m 644 dnxServer.cfg $(DESTDIR)$(sysconfdir)/dnxServer.cfg
	if [ `id -u` -eq 0 ] && [ -z ${DESTDIR} ]; then \
	   chown $(nagios_user):$(nagios_group) $(DESTDIR)$(sysconfdir)/dnxServer.cfg; \
	fi

# Build executable scripts
dnxcld.generic-init dnxcld.init: Makefile
	rm -f $@ $@.tmp
	$(edit) '$(srcdir)/$@.in' > $@.tmp
	chmod +x $@.tmp
	chmod a-w $@.tmp
	mv $@.tmp $@

# Build sample configuration files
dnxClient.cfg dnxServer.cfg: Makefile
	rm -f $@ $@.tmp
	$(edit) '$(srcdir)/$@.in' > $@.tmp
	mv $@.tmp $@

dnxcld.init: $(srcdir)/dnxcld.init.in
dnxcld.generic-init: $(srcdir)/dnxcld.generic-init.in
dnxClient.cfg: $(srcdir)/dnxClient.cfg.in
dnxServer.cfg: $(srcdir)/dnxServer.cfg.in

CLEANFILES = dnxClient.cfg dnxServer.cfg dnxcld.init dnxcld.generic-init

