EXTRA_DIST = LEGAL TODO etc nagios

if DO_DOXYGEN
  OPT_DOXYGEN = doc
endif

SUBDIRS = common server client $(OPT_DOXYGEN)

dist-hook:
	rm -rf `find $(distdir) -name .svn`
