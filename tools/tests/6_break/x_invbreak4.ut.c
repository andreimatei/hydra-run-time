//
// x_invbreak4.ut.c: this file is part of the SL toolchain.
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

//create checks break type
ut_def(foo, double)
{

  double y = 15.294;

  ut_break(y);

}
ut_enddef

[[]]// XFAIL: C (break variable incorrect type)
ut_def(t_main, void)
{
  ut_create(f,,1,10,2,3,int, foo);
  ut_sync(f);
}
ut_enddef
