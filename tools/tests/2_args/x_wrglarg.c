//
// x_wrglarg.sl: this file is part of the slc project.
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

sl_def(foo, void, sl_glparm(int, a))
{ }
sl_enddef

// XFAIL: *:[TPC] (global is immutable)
sl_def(t_main, void)
{
  sl_create(,,,,,,, foo, sl_glarg(int, a, 10));
  sl_seta(a, 10);
  sl_sync();
}
sl_enddef