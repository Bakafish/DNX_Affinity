EXTRA_DIST = dnxClient.cfg.in dnxServer.cfg.in dnxcld.init.in

edit = sed \
   -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
   -e 's|@libexecdir[@]|$(libexecdir)|g' \
   -e 's|@sysrunpath[@]|$(sysrunpath)|g' \
   -e 's|@dnx_user[@]|$(dnx_user)|g' \
   -e 's|@dnx_group[@]|$(dnx_group)|g' \
   -e 's|@localstatedir[@]|$(localstatedir)|g' \
   -e 's|@prefix[@]|$(prefix)|g'

all: dnxClient.cfg dnxServer.cfg dnxcld.init

# The link from rcdnxcld to dnxcld needs to be a relative reference because 
# we can't link by name to a file in $DESTDIR, and a hard link won't work 
# across file systems. We'll just assume we're talking about the real /etc.

# Install init script and soft link to it from sbin directory
install-initscript: dnxcld.init
	$(mkdir_p) $(DESTDIR)@initrddir@
	$(INSTALL_SCRIPT) dnxcld.init $(DESTDIR)@initrddir@/dnxcld
	$(LN_S) -f @initrddir@/dnxcld $(DESTDIR)@sbindir@/rcdnxcld

# Install sample config files
install-cfg: install-clientcfg install-servercfg

install-clientcfg: dnxClient.cfg
	$(mkdir_p) $(DESTDIR)@sysconfdir@
	$(INSTALL_DATA) --owner=@dnx_user@ --group=@dnx_group@ \
           dnxClient.cfg $(DESTDIR)@sysconfdir@

install-servercfg: dnxServer.cfg
	$(mkdir_p) $(DESTDIR)@sysconfdir@
	$(INSTALL_DATA) --owner=@nagios_user@ --group=@nagios_group@ \
           dnxServer.cfg $(DESTDIR)@sysconfdir@

# Build executable scripts
dnxcld.init: Makefile
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

