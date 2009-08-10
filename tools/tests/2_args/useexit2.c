//
// useexit2.sl: this file is part of the slc project.
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

#include <svp/compiler.h>

sl_def(foo, void) { } sl_enddef

sl_def(t_main, void)
{
  int r;
  sl_family_t f;
  sl_create(f,,,,,, void, foo);
  sl_kill(f);
  sl_sync(r);
  if (r == SVP_EKILLED)
    nop();
}
sl_enddef