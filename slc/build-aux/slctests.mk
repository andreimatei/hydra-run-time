## tests.mk: This file is part of the SL toolchain.
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

include $(top_srcdir)/build-aux/sl.mk

EXTRA_TEST_IMPL =

if ENABLE_CHECK_PTL
EXTRA_TEST_IMPL += ptl
endif

if ENABLE_CHECK_PPP
EXTRA_TEST_IMPL += ppp::-n~1 ppp
endif

if ENABLE_CHECK_UTC
EXTRA_TEST_IMPL += utc0:-O0 utc0:-O1 utcx
endif

SLT_IMPL_LIST ?= seqc $(EXTRA_TEST_IMPL)

TEST_EXTENSIONS = .sl .c
SL_LOG_COMPILER = \
	$(SLC_VARS) SRCDIR=$(srcdir) \
	SLT_IMPL_LIST="$(SLT_IMPL_LIST)" \
	DUMP_LOGS=1 TEXT_ONLY=1 SEQUENTIAL=1 \
	$(BASH) $(SLT)

C_LOG_COMPILER = $(SL_LOG_COMPILER)

.PHONY: check-slt
check-slt: $(check_DATA) $(TESTS)
	-echo; echo "Current directory:" `pwd`
	$(AM_V_at)$(SLC_VARS) SLT_IMPL_LIST="$(SLT_IMPL_LIST)" SRCDIR=$(srcdir) \
	    $(SLT_MANY) \
	    `for t in $(TESTS); do if test -r $$t; then echo $$t; else echo $(srcdir)/$$t; fi; done`

clean-local:
	$(AM_V_at)find . -name _\* -type d | xargs rm -rf

