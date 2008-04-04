EXTRA_DIST = LEGAL TODO nagios

if DO_DOXYGEN
  OPT_DOXYGEN = doc
endif

SUBDIRS = etc common server client $(OPT_DOXYGEN)

doxygen:
	cd doc && $(MAKE) $(AM_MAKEFLAGS) $@

dist-hook:
	rm -rf `find $(distdir) -name .svn`
