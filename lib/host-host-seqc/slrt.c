// slrt.c: this file is part of the slc project.
//
// Copyright (C) 2009 Universiteit van Amsterdam.
// All rights reserved.
//
// $Id$
//
#include "sl_support.h"
#include "load.c"

extern void t_main(struct sl_famdata*);

// configuration tag for slr runner
const char *__tag__ = "slr_runner:host:";
const char *__datatag__ = "slr_datatag:seqc-host-host-seqc:";

int main(int argc, const char **argv) {
  load(argv[0], argv[1]);
  struct { } args;
  struct sl_famdata root;
  root.ix = root.be = 0;
  root.li = root.st = 1;
  root.f = t_main;
  root.a = &args;
  root.ex = 0;
  t_main(&root);
  return 0;
}
