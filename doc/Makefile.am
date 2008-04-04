EXTRA_DIST = dnx.dxy DNX_Workflow.pdf readme.txt

CLEANFILES = doxywarn.txt html/*

.PHONEY: doxygen

distdir=$(PACKAGE)-doxy-$(VERSION).$(REVISION)

doxygen:
	doxygen doxyfile
	tar chof - html | bzip2 -9 -c >$(distdir).tar.bz2
	zip -r9 $(distdir).zip html
