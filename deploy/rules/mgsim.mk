# -*- makefile -*-

MGSIM_TARGETS = \
     $(SLDIR)/bin/mgsim-alpha \
     $(SLDIR)/bin/mgsim-sparc

$(SOURCES)/mgsim-$(MGSIM_REV)/configure:
	mkdir -p $(SOURCES)
	(cd $(SOURCES) && \
	    $(SVN) export -r$(MGSIM_REV) $(MGSIM_REPO) mgsim-$(MGSIM_REV) && \
	    cd mgsim-$(MGSIM_REV) && \
	    ./bootstrap)


$(SLDIR)/bin/mgsim-alpha $(SLDIR)/bin/mgsim-sparc: $(SOURCES)/mgsim-$(MGSIM_REV)/configure
	mkdir -p $(BUILD)/mgsim-$(MGSIM_REV)
	rm -f $(PREFIX)/slreqs-current
	ln -s $(REQDIR) $(PREFIX)/slreqs-current
	(SRC=$$PWD/$(SOURCES)/mgsim-$(MGSIM_REV); cd $(BUILD)/mgsim-$(MGSIM_REV) && \
	 PATH=$(PREFIX)/slreqs-current/bin:$$PATH $$SRC/configure --prefix=$(SLDIR) && \
	 $(MAKE) $(MAKE_FLAGS) && \
	 $(MAKE) install)
	rm -f $(PREFIX)/sl-current
	ln -s $(SLDIR) $(PREFIX)/sl-current

