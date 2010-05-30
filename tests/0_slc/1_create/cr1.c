//
// cr1.c: this file is part of the SL toolchain.
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

sl_def(foo, void) {} sl_enddef

sl_def(t_main, void)
{
  sl_create(,,1,10,2,3,, foo);
  sl_sync();

  sl_create(,,23,10,-3,3,, foo);
  sl_sync();
}
sl_enddef
