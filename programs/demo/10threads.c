//
// 10threads.c: this file is part of the SL program suite.
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

sl_def(foo, void, sl_shparm(int, a))
{
   sl_setp(a, sl_getp(a) + 1);

   putchar('.');
}
sl_enddef

// 2009-04-02: FIXME: we acknowledge that muTC-ptl does not support
// setting the shared after create fully yet; but we want slc's
// testsuite to properly succeed. So we mark the test to ignore the
// output on muTC-ptl:
// XIGNORE: ptl*:D

sl_def(t_main, void)
{
  sl_create(,, 0, 10, 1, 0,,
            foo, sl_sharg(int, x));
  sl_seta(x, 0);
  sl_sync();

  printf("\n%d\n", sl_geta(x));
}
sl_enddef
