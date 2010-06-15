//
// assert.c: this file is part of the SL toolchain.
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

#include <assert.h>

int y = 4;

sl_def(t_main, void)
{
  assert(1 == 1);
  int x = 3;
  assert(x == 3);
  assert(y == 4);
}
sl_enddef
