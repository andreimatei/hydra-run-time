//
// thconv3.ut.c: this file is part of the SL toolchain.
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

ut_def(bar, char*, ut_glparm(char*, a)) {} ut_enddef

ut_decl((*barp), char*, ut_glparm(void*, a)) = &bar;

ut_def(t_main, void) { } ut_enddef
