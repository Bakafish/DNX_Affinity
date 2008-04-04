EXTRA_DIST = dnxClient.cfg.in dnxServer.cfg.in dnxcld.init.in

edit = sed \
   -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
   -e 's|@libexecdir[@]|$(libexecdir)|g' \
   -e 's|@localstatedir[@]|$(localstatedir)|g' \
   -e 's|@prefix[@]|$(prefix)|g'

all: dnxClient.cfg dnxServer.cfg dnxcld.init

install-initscript: dnxcld.init
	$(mkdir_p) @initrddir@
	$(INSTALL_SCRIPT) dnxcld.init @initrddir@/dnxcld
	$(LN_S) @initrddir@/dnxcld @sbindir@/rcdnxcld

install-cfg: dnxClient.cfg dnxServer.cfg
	$(mkdir_p) @sysconfdir@
	$(INSTALL_DATA) dnxClient.cfg @sysconfdir@
	$(INSTALL_DATA) dnxServer.cfg @sysconfdir@

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

clean-local:
	rm -f dnxClient.cfg dnxServer.cfg dnxcld.init

