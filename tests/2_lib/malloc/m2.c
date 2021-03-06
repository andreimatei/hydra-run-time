//
// m2.c: this file is part of the SL toolchain.
//
// Copyright (C) 2009,2010 Universiteit van Amsterdam.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// The complete GNU General Public Licence Notice can be found as the
// `COPYING' file in the root directory.
//

#include <stdlib.h>
#include <stdio.h>

sl_def(t_main, void)
{
   char *p1; char *p2;

   p1 = (char*)malloc(10);
   p2 = (char*)malloc(10);

   p1[0] = '4';
   p1[1] = '\0';
   p2[0] = '2';
   p2[1] = '\0';

   puts(p1); puts(p2);

   free(p1);
   free(p2);

}
sl_enddef
