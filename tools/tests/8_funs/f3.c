//
// f3.sl: this file is part of the SL toolchain.
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

#include <svp/testoutput.h>

int foo() { return 123; }

sl_def(t_main, void)
{
  long x = sl_funcall(,long,foo);
  output_int(x, 1); output_char('\n', 1);
}
sl_enddef
