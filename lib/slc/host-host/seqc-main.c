//
// seqc-main.c: this file is part of the slc project.
//
// Copyright (C) 2008 The CSA group.
//
// $Id$
//
#include "../t-seqc/utcimpl.h"

extern void t_main(struct ut_famdata*);

void lib_main(void) {
  struct { } args;
  struct ut_famdata root;
  root.ix = root.be = 0;
  root.li = root.st = 1;
  root.f = t_main;
  root.a = &args;
  root.ex = 0;
  t_main(&root);
}
