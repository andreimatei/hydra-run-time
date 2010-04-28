//
// div2.c: this file is part of the SL toolchain.
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

#include <svp/div.h>
#include <cassert.h>
#include <cstdio.h>

sl_def(t_main, void)
{
  uint64_t x = 69, y = 5;
  divmodu(x, y);
  uint64_t x1 = x, y1 = y;
  while (x1--) putchar('.'); putchar('\n');
  while (y1--) putchar('.'); putchar('\n');
  assert(x == 4);
  assert(y == 13);
}
sl_enddef
