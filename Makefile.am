EXTRA_DIST = LEGAL TODO nagios

SUBDIRS = etc obs common server stats client doc

# Additional root-level targets - mostly installation and docs
docs install-docs:
	cd doc && $(MAKE) $(AM_MAKEFLAGS) $@	

install-cfg install-clientcfg install-servercfg install-initscript:
	cd etc && $(MAKE) $(AM_MAKEFLAGS) $@

install-client:
	cd client && $(MAKE) $(AM_MAKEFLAGS) install

install-server:
	cd common && $(MAKE) $(AM_MAKEFLAGS) install
	cd server && $(MAKE) $(AM_MAKEFLAGS) install
	cd stats && $(MAKE) $(AM_MAKEFLAGS) install

dist-hook:
	rm -rf `find $(distdir) -name .svn`

rpmmacros = --define='_rpmdir $(PWD)' --define='_srcrpmdir $(PWD)'\
 --define='_sourcedir $(PWD)' --define='_specdir $(PWD)'\
 --define='_builddir $(PWD)'

rpmcheck:
	-rm -rf _rpm
	@which rpm &> /dev/null; \
        if [ $$? -ne 0 ]; then \
          echo "*** This make target requires an rpm-based linux distribution."; \
          exit 1; \
        fi

srcrpm: rpmcheck dist
	rpmbuild -bs $(rpmmacros) --nodeps obs/$(PACKAGE_TARNAME).spec

rpms: rpmcheck dist
	rpmbuild -ba $(rpmmacros) --nodeps --buildroot=$(PWD)/_rpm obs/$(PACKAGE_TARNAME).spec
	rpmarch=`rpm --showrc | grep ^build\ arch | sed 's/\(.*: \)\(.*\)/\2/'`; \
        test -z $${rpmarch} || ( mv $${rpmarch}/* .; rm -rf $${rpmarch} )
