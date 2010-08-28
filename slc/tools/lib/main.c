//
// main.c: this file is part of the SL toolchain.
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

#include <stdio.h>
#include <svp/delegate.h>

extern sl_place_t __main_place_id;

// at this point __main_place_id has been set
// by the standard library (if linked), or is
// set to PLACE_DEFAULT.

sl_decl(t_main, void);

int main(int argc, char **argv) {
    int ret;
    //printf("in libslmain; creating t_main on place %d (PLACE_GROUP = %d);\n", __main_place_id, PLACE_GROUP);
    //printf("in libslmain; creating t_main on place %d;\n", __main_place_id);
    sl_create(, __main_place_id, ,,,,, t_main);
    sl_sync(ret);
    return ret;
}
