//
// arggen.c: this file is part of the SL toolchain.
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

#include <cstddef.h>
#include <svp/testoutput.h>

#ifdef FIBRE_DATA
#include "fdata.c"
#else
#include "data.c"
#endif

sl_def(do_print, void, sl_shparm(long, tok))
{
  sl_index(i);
  const char *ptr = (const char*)(void*)i;
  char c = *ptr;
  long t = sl_getp(tok);
  output_char(c, 1);
  sl_setp(tok, t);
}
sl_enddef

sl_def(t_main, void)
{
  sl_create(,, (long)&__slr_data, ((long)&__slr_data)+sizeof(__slr_data),,,,
	    do_print, sl_sharg(long, , 0));
  sl_sync();
}
sl_enddef
