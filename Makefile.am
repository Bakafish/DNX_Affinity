EXTRA_DIST = LEGAL TODO etc rpm nagios

if DO_DOXYGEN
  OPT_DOXYGEN = doc
endif

SUBDIRS = common server client $(OPT_DOXYGEN)

doxygen:
	cd doc && $(MAKE) $(AM_MAKEFLAGS) $@

dist-hook:
	rm -rf `find $(distdir) -name .svn`
