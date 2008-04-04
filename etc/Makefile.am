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

install-initscript: dnxcld.init
	$(mkdir_p) $(DESTDIR)@initrddir@
	$(INSTALL_SCRIPT) dnxcld.init $(DESTDIR)@initrddir@/dnxcld
	ln $(DESTDIR)@initrddir@/dnxcld $(DESTDIR)@sbindir@/rcdnxcld

install-cfg: dnxClient.cfg dnxServer.cfg
	$(mkdir_p) $(DESTDIR)@sysconfdir@
	$(INSTALL_DATA) dnxClient.cfg $(DESTDIR)@sysconfdir@
	$(INSTALL_DATA) dnxServer.cfg $(DESTDIR)@sysconfdir@

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

