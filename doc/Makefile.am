EXTRA_DIST = dnx.dxy DNX_Workflow.pdf readme.txt

distdir=$(PACKAGE)-doxy-$(VERSION)

docs: $(distdir).tar.gz

install-docs: docs
	$(mkdir_p) $(DESTDIR)$(datadir)/doc
	$(INSTALL_DATA) $(distdir).tar.gz $(DESTDIR)$(datadir)/doc
	$(INSTALL_DATA) $(srcdir)/DNX_Workflow.pdf $(DESTDIR)$(datadir)/doc

$(distdir).tar.gz: html/index.html
	tar chof - html | gzip -9 -c >$@

# not perfect, but we can't depend on ALL source - config.h will have to do...

html/index.html: ../config.h doxyfile
	doxygen doxyfile

CLEANFILES = doxywarn.txt html/*

clean-local:
	rm -rf html
