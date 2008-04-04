EXTRA_DIST = dnx.dxy DNX_Workflow.pdf readme.txt

distdir=$(PACKAGE)-doxy-$(VERSION)

doxygen:
	doxygen doxyfile
	tar chof - html | gzip -9 -c >$(distdir).tar.gz

CLEANFILES = doxywarn.txt html/*

