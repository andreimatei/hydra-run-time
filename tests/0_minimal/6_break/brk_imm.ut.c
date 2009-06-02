//
// brk_imm.ut.c: this file is part of the slc project.
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

#include <libutc.h>

//test break with some control flow

ut_def(foo, int,
        ut_glparm(int, i))
{

  int x=ut_getp(i);

  if(x){
    ut_break(254);
  }else
    ut_break(255);

}
ut_enddef

ut_def(t_main, void)
{
  // do nothing
}
ut_enddef
