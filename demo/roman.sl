//                                                             -*- m4 -*-
// roman.sl: this file is part of the slc project.
//
// Copyright (C) 2009 Universiteit van Amsterdam.
// All rights reserved.
//
// $Id$
//
m4_include(svp/roman.slh)
m4_include(svp/iomacros.slh)
m4_include(slr.slh)

slr_decl(slr_var(N, short, "number to print"));

// SLT_RUN: -dN=42

sl_def(t_main, void)
{
  if (!slr_len(N))
    puts("no number specified!\n");
  else {
    sl_proccall(roman, sl_glarg(short, x, slr_get(N)[0]));
    putc('\n');
  }
}
sl_enddef
