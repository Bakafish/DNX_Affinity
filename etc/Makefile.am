EXTRA_DIST = dnxClient.cfg.in dnxServer.cfg.in dnxcld.generic-init.in dnxcld.init.in

edit = sed \
   -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
   -e 's|@libexecdir[@]|$(libexecdir)|g' \
   -e 's|@sysrunpath[@]|$(sysrunpath)|g' \
   -e 's|@dnx_user[@]|$(dnx_user)|g' \
   -e 's|@dnx_group[@]|$(dnx_group)|g' \
   -e 's|@localstatedir[@]|$(localstatedir)|g' \
   -e 's|@prefix[@]|$(prefix)|g'

all: dnxClient.cfg dnxServer.cfg dnxcld.generic-init dnxcld.init

# The link from rcdnxcld to dnxcld needs to be a relative reference because 
# we can't link by name to a file in $DESTDIR, and a hard link won't work 
# across file systems. We'll just assume we're talking about the real /etc.

# Install init script and soft link to it from sbin directory
install-initscript: dnxcld.generic-init dnxcld.init
	$(mkdir_p) $(DESTDIR)@initrddir@
	if [ -f /etc/SuSE-release ]; then \
	   $(INSTALL_SCRIPT) dnxcld.init $(DESTDIR)@initrddir@/dnxcld; \
	else \
	   $(INSTALL_SCRIPT) dnxcld.generic-init $(DESTDIR)@initrddir@/dnxcld; \
	fi
	$(LN_S) -f @initrddir@/dnxcld $(DESTDIR)@sbindir@/rcdnxcld

# Install sample config files
install-cfg: install-clientcfg install-servercfg

install-clientcfg: install-clientcfg-nochown
	chown @dnx_user@:@dnx_group@ $(DESTDIR)@sysconfdir@/dnxClient.cfg

install-servercfg: install-servercfg-nochown
	chown @nagios_user@:@nagios_group@ $(DESTDIR)@sysconfdir@/dnxServer.cfg

# RPM support targets - rpmbuild doesn't like targets that chown things
install-cfg-nochown: install-clientcfg-nochown install-servercfg-nochown

install-clientcfg-nochown: dnxClient.cfg
	$(mkdir_p) $(DESTDIR)@sysconfdir@
	$(INSTALL_DATA) dnxClient.cfg $(DESTDIR)@sysconfdir@/dnxClient.cfg

install-servercfg-nochown: dnxServer.cfg
	$(mkdir_p) $(DESTDIR)@sysconfdir@
	$(INSTALL_DATA) dnxServer.cfg $(DESTDIR)@sysconfdir@/dnxServer.cfg

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
dnxClient.cfg: $(srcdir)/dnxClient.cfg.in
dnxServer.cfg: $(srcdir)/dnxServer.cfg.in

CLEANFILES = dnxClient.cfg dnxServer.cfg dnxcld.init

