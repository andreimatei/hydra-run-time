//
// f7.sl: this file is part of the SL toolchain.
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

m4_include(svp/iomacros.slh);

int a;

int* foo() { return &a; }

sl_def(t_main, void)
{
  int * p = (int*) sl_funcall(,ptr,foo);
  printf("%d\n", &a - p);
}
sl_enddef