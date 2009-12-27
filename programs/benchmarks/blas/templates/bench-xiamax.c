//
// bench-xiamax.c: this file is part of the SL program suite.
//
// Copyright (C) 2009 Universiteit van Amsterdam.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// The complete GNU General Public Licence Notice can be found as the
// `COPYING' file in the root directory.
//

#include "blasbench.h"

BEGIN_VARS
DEF_COUNTER(n)
DEF_ARRAY_IN(sx)
DEF_COUNTER(incx)
DEF_COUNTER(imax)
END_VARS

BEGIN_READ
READ_COUNTER(n)
READ_ARRAY_IN(sx, n)
READ_COUNTER(incx)
END_READ

BEGIN_PREPARE
END_PREPARE

BEGIN_WORK
     sl_create(,,,,,,, FUNCTION,
	       sl_sharg(long, ret, 0),
	       sl_glarg(long, , USE_VAR(n)),
	       sl_glarg(const FLOAT*, , USE_VAR(sx)),
	       sl_glarg(long, , USE_VAR(incx)));
sl_sync();
USE_VAR(imax) = sl_geta(ret);
END_WORK

BEGIN_OUTPUT
PRINT_COUNTER(imax)
END_OUTPUT

BEGIN_TEARDOWN
FREE_ARRAY_IN(sx)
END_TEARDOWN

BEGIN_DESC
BENCH_TITLE("BLAS: I_AMAX")
BENCH_AUTHOR("kena")
BENCH_DESC("Return index of max(X)")
END_DESC
