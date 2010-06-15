//
// va.c: this file is part of the SL toolchain.
//
// Copyright (C) 2010 Universiteit van Amsterdam.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// The complete GNU General Public Licence Notice can be found as the
// `COPYING' file in the root directory.
//

#include <stdarg.h>
#include <svp/testoutput.h>
#include <svp/compiler.h>

noinline double select2(int s, va_list ap)
{
    int x = va_arg(ap, double);
    int y = va_arg(ap, int);
    return s ? x : y;
}

noinline double select(int s, ...)
{
  va_list ap;
  va_start(ap, s);
  double d = select2(s, ap);
  va_end(ap);
  return d;
}

sl_def(t_main, void)
{
    int a = select(0, 1.41, 123);
    int b = select(1, 3.14, 456);

    output_int(a, 1);
    output_char('\n', 1);
    output_int(b, 1);
    output_char('\n', 1);
}
sl_enddef
