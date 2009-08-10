## sl.mk: this file is part of the SL toolchain.
## 
## Copyright (C) 2009 Universiteit van Amsterdam
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## The complete GNU General Public Licence Notice can be found as the
## `COPYING' file in the root directory.
##

SLC_LOCAL = $(abs_top_builddir)/tools/bin/slc


SLC_VARS = \
	SLC_INCDIR=$(abs_top_srcdir)/tools/include:$(abs_top_builddir)/tools/include:$(abs_top_srcdir)/lib/include:$(abs_top_builddir)/lib/include \
	SLC_LIBDIR=$(abs_top_srcdir)/tools/lib:$(abs_top_builddir)/tools/lib:$(abs_top_srcdir)/lib:$(abs_top_builddir)/lib \
	SLC_DATADIR=$(abs_top_srcdir)/tools/lib:$(abs_top_builddir)/tools/lib:$(abs_top_srcdir)/lib:$(abs_top_builddir)/lib \
	SPP=$(abs_top_srcdir)/tools/bin/spp \
	SCU=$(abs_top_srcdir)/tools/bin/scu \
	SAG=$(abs_top_srcdir)/tools/bin/sag \
	CCE=$(abs_top_builddir)/tools/bin/cce \
	SLR=$(abs_top_builddir)/tools/bin/slr \
	SLT=$(abs_top_builddir)/tools/bin/slt \
	CM4=$(abs_top_builddir)/tools/bin/cm4 \
	SLC=$(SLC_LOCAL)

SLC_RUN = $(SLC_VARS) $(SLC_LOCAL)

SUFFIXES = .x

slc_verbose = $(slc_verbose_$(V))
slc_verbose_ = $(slc_verbose_$(AM_DEFAULT_VERBOSITY))
slc_verbose_0 = @echo '  SLC    $@';

slc_shverbose = $(slc_shverbose_$(V))
slc_shverbose_ = $(slc_shverbose_$(AM_DEFAULT_VERBOSITY))
slc_shverbose_0 = :

if ENABLE_MTALPHA
slc_ifmtalpha =
else
slc_ifmtalpha = :
endif

SLC_BEFORE = function slc_compile() { \
	$(slc_shverbose) set -x ; \
	if test -n "$$SLC_OUT"; then rm -f "$$SLC_OUT" "$$SLC_OUT".{mtalpha,ptl,seqc}; fi && \
	echo "  SLC    $$SLC_OUT".seqc && \
	$(SLC_LOCAL) $${SLC_OUT:+-o "$$SLC_OUT".seqc} -b seqc "$$@" -I$(srcdir) $(AM_CFLAGS) $(CFLAGS) && \
	echo "  SLC    $$SLC_OUT".ptl && \
        $(SLC_LOCAL) $${SLC_OUT:+-o "$$SLC_OUT".ptl} -b ptl "$$@" -I$(srcdir) $(AM_CXXFLAGS) $(CXXFLAGS) && \
	$(slc_ifmtalpha) echo "  SLC    $$SLC_OUT".mtalpha && \
        $(slc_ifmtalpha) $(SLC_LOCAL) $${SLC_OUT:+-o "$$SLC_OUT".mtalpha} -b ppp "$$@" -I$(srcdir) && \
	if test -n "$$SLC_OUT"; then \
	  printf '\#! /bin/sh\n' >"$@" && \
	  printf 'echo "This script is only a stub. Run the actual program with:"\n' >>"$@" && \
	  printf 'echo; echo "\tslr "$$(basename "$$0" .x)".bin.XXXX"; echo; exit 1\n' >>"$@"; \
	fi; }

.c.x:
	@$(SLC_BEFORE); $(SLC_VARS) SLC_OUT="${@:.x=.bin}" slc_compile $<


