//
// x_brscope.ut.c: this file is part of the SL toolchain.
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

#include <libutc.h>

ut_def(foo, int) {} ut_enddef

[[]]// XFAIL: C (use of br before it becomes visible)
ut_def(t_main, void)
{
  int b;
  ut_create(f,,,,,, int, foo);
  b = ut_getbr(f);
  ut_sync(f);
}
ut_enddef
