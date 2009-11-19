//
// xaxpy.c: this file is part of the SL toolchain.
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

#define INT long

sl_def(FUNCTION[[]]_mt, void,
       sl_glfparm(FLOAT, a),
       sl_glparm(const FLOAT*restrict, sx),
       sl_glparm(FLOAT*restrict, sy),
       sl_glparm(INT, incx),
       sl_glparm(INT, incy))
{
  sl_index(i);
  const FLOAT *restrict lsx = sl_getp(sx) + i * sl_getp(incx);
  FLOAT *restrict lsy = sl_getp(sy) + i * sl_getp(incy);
  *lsy = sl_getp(a) * (*lsx) + (*lsy);
}
sl_enddef

sl_def(FUNCTION, void,
       sl_glparm(INT, n),
       sl_glfparm(FLOAT, a),
       sl_glparm(const FLOAT*, sx),
       sl_glparm(INT, incx),
       sl_glparm(FLOAT*, sy),
       sl_glparm(INT, incy))
{
  INT ix = (sl_getp(incx) < 0) ? ((-sl_getp(n) + 1) * sl_getp(incx)) : 0;
  INT iy = (sl_getp(incy) < 0) ? ((-sl_getp(n) + 1) * sl_getp(incy)) : 0;

  sl_create(,, 0, sl_getp(n),,,,
	    FUNCTION[[]]_mt,
	    sl_glfarg(FLOAT, , sl_getp(a)),
	    sl_glarg(const FLOAT*, , sl_getp(sx) + ix),
	    sl_glarg(FLOAT*, , sl_getp(sy) + iy),
	    sl_glarg(INT, , sl_getp(incx)),
	    sl_glarg(INT, , sl_getp(incy)));
  sl_sync();
}
sl_enddef
