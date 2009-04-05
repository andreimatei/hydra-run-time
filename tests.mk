# 
# slc, a SL compiler.
# 
# Copyright (C) 2008,2009 Universiteit van Amsterdam..
# All rights reserved.
# 

SLC_VARS = \
	SLC_INCDIR=$(abs_top_srcdir)/include:$(abs_top_builddir)/include \
	SLC_LIBDIR=$(abs_top_srcdir)/lib:$(abs_top_builddir)/lib \
	SLC_DATADIR=$(abs_top_srcdir)/lib:$(abs_top_builddir)/lib \
	SPP=$(abs_top_srcdir)/bin/spp \
	SCU=$(abs_top_srcdir)/bin/scu \
	SAG=$(abs_top_srcdir)/bin/sag \
	CCE=$(abs_top_builddir)/bin/cce \
	SLR=$(abs_top_builddir)/bin/slr \
	SLC=$(abs_top_builddir)/bin/slc

SLC = $(SLC_VARS) $(abs_top_builddir)/bin/slc

TESTS_ENVIRONMENT = \
	$(SLC_VARS) \
	TEST_HERE=1 XIGNORE="*x:* utc0:[CLRD]" \
	$(SHELL) $(abs_top_builddir)/bin/slt

