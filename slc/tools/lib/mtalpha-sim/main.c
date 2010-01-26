//
// main.c: this file is part of the SL toolchain.
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

#include <svp/gomt.h>

extern void t_main(void);

extern sl_place_t __main_place_id;

int main(void) {
// at this point __main_place_id has been set
// by the standard library (if linked), or is
// set to PLACE_DEFAULT.
    return INVOKE_MT(__main_place_id, t_main);
}
