EXTRA_DIST = LEGAL TODO nagios

if DO_DOXYGEN
  OPT_DOXYGEN = doc
endif

SUBDIRS = etc obs common server client stats $(OPT_DOXYGEN)

doxygen:
	cd doc && $(MAKE) $(AM_MAKEFLAGS) $@

dist-hook:
	rm -rf `find $(distdir) -name .svn`

rpmarch = $(BUILDARCH)
rpmmacros = --define='_rpmdir $(PWD)' --define='_srcrpmdir $(PWD)'\
 --define='_sourcedir $(PWD)' --define='_specdir $(PWD)'\
 --define='_builddir $(PWD)'

rpmcheck:
	@which rpm &> /dev/null; \
        if [ $$? -ne 0 ]; then \
          echo "*** This make target requires an rpm-based linux distribution."; \
          exit 1; \
        fi

srcrpm: rpmcheck dist
	rpmbuild -bs $(rpmmacros) --nodeps obs/$(PACKAGE).spec

rpms: rpmcheck dist
	rpmbuild -ba $(rpmmacros) --nodeps --buildroot=$(PWD)/tmp obs/$(PACKAGE).spec
	mv $(rpmarch)/* .; rm -rf $(rpmarch)

