//
// x_invinit.c: this file is part of the SL toolchain.
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

sl_def(foo, void, sl_glfparm(float, a)) { } sl_enddef

// XFAIL: *:C (invalid initializer)
sl_def(t_main, void)
{
  sl_create(,,,,,,, foo, sl_glfarg(float, b, "hello"));
  sl_sync();
}
sl_enddef
