//
// argshadow.c: this file is part of the SL toolchain.
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

sl_def(foo, void, sl_glparm(int, a)) {} sl_enddef

struct blah { int x; } a;
sl_def(t_main, void)
{
  if (1) {
    sl_create(,,,,,,,foo, sl_glarg(int, a, 10)); // valid: different scope
    sl_sync();
    int z = sl_geta(a); // a still visible
  }
  a.x = 10; // previous a now visible
}
sl_enddef
