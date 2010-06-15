//
// bitblt.c: this file is part of the SL program suite.
//
// Copyright (C) 2009,2010 Universiteit van Amsterdam.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// The complete GNU General Public Licence Notice can be found as the
// `COPYING' file in the root directory.
//

#include <stdio.h>
#include <assert.h>
#include <svp/fibre.h>
#include <svp/testoutput.h>

sl_def(bitblt, void,
       sl_glparm(unsigned long*restrict, img),
       sl_glparm(unsigned long*restrict, mask),
       sl_glparm(unsigned long*restrict, sprite))
{
  sl_index(i);
  sl_getp(img)[i] = (sl_getp(img)[i] & sl_getp(mask)[i]) | sl_getp(sprite)[i];
}
sl_enddef

sl_def(printarray, void,
       sl_shparm(long, token),
       sl_glparm(unsigned long*restrict, img))
{
  sl_index(i);
  long x = sl_getp(token);
  printf("%lu\n", sl_getp(img)[i]);
  sl_setp(token, x);
}
sl_enddef

#ifdef min
#undef min
#endif
#define min(A, B) ((A) < (B) ? (A) : (B))

// SLT_RUN: -f TEST.d

sl_def(t_main, void)
{
  assert(fibre_tag(0) < 2 && fibre_rank(0) == 1);
  assert(fibre_tag(1) < 2 && fibre_rank(1) == 1);
  assert(fibre_tag(2) < 2 && fibre_rank(2) == 1);
  size_t len = min(fibre_shape(0)[0], min(fibre_shape(1)[0], fibre_shape(2)[0]));
  sl_create(,,,len,,,, bitblt,
	    sl_glarg(unsigned long*restrict, , (unsigned long*)fibre_data(0)),
	    sl_glarg(unsigned long*restrict, , (unsigned long*)fibre_data(1)),
	    sl_glarg(unsigned long*restrict, , (unsigned long*)fibre_data(2)));
  sl_sync();
  sl_create(,,,len,,,, printarray,
	    sl_sharg(long, token, 0),
	    sl_glarg(unsigned long*restrict, , (unsigned long*)fibre_data(0)));
  sl_sync();
}
sl_enddef
