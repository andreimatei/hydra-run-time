//
// keyblit.c: this file is part of the SL program suite.
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

sl_def(key_blit, void,
       sl_glparm(unsigned long*restrict, img),
       sl_glparm(unsigned long*restrict, sprite),
       sl_glparm(unsigned long, key))
{
  sl_index(i);
  if (sl_getp(sprite)[i] != sl_getp(key))
    sl_getp(img)[i] = sl_getp(sprite)[i];
}
sl_enddef

// SLT_RUN: -f TEST.d

#ifdef SIMPLE_MAIN
extern unsigned long img[];
extern unsigned long sprite[];

sl_def(t_main, void)
{
  sl_create(,,,10,,,, key_blit,
	    sl_glarg(unsigned long*restrict, , img),
	    sl_glarg(unsigned long*restrict, , sprite),
	    sl_glarg(unsigned long, , 3));
  sl_sync();

}
sl_enddef

unsigned long img[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
unsigned long sprite[10] = {3, 11, 3, 22, 3, 3, 3, 33, 3, 3};

#else //! SIMPLE_MAIN

#include <cstdio.h>
#include <cassert.h>
#include <svp/fibre.h>

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


sl_def(t_main, void)
{
  assert(fibre_tag(0) < 2 && fibre_rank(0) == 1);
  assert(fibre_tag(1) < 2 && fibre_rank(1) == 1);
  assert(fibre_tag(2) < 2 && fibre_rank(2) == 0);
  size_t len = min(fibre_shape(0)[0], fibre_shape(1)[0]);
  sl_create(,,,len,,,, key_blit,
	    sl_glarg(unsigned long*restrict, , (unsigned long*)fibre_data(0)),
	    sl_glarg(unsigned long*restrict, , (unsigned long*)fibre_data(1)),
	    sl_glarg(unsigned long, , *(unsigned long*)fibre_data(2)));
  sl_sync();
  sl_create(,,,len,,,, printarray,
	    sl_sharg(long, , 0),
	    sl_glarg(unsigned long*restrict, , (unsigned long*)fibre_data(0)));
  sl_sync();
}
sl_enddef

#endif

[[]]
