//
// x_addrarg.sl: this file is part of the slc project.
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
// $Id$
//

sl_def(foo, void, sl_shparm(int, x)) {} sl_enddef

// XFAIL: *:C (shared has no address)
sl_def(t_main, void)
{
  int *a;
  sl_create(f1,,,,,,,foo, sl_sharg(int, b, 0));
  a = &(sl_geta(b));
  sl_sync(f1);
}
sl_enddef
