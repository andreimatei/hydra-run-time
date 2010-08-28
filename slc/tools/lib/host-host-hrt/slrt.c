//
// slrt.c: this file is part of the SL toolchain.
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

#include <svp/delegate.h>
//#include <delegate.h>

#ifndef __hrt
sl_place_t __main_place_id = PLACE_DEFAULT;
#else
sl_place_t __main_place_id = {0,1,-1,-1,-1};  // FIXME: can we get rid of this special case and initialize __main_place_id with PLACE_DEFAULT, given that PLACE_DEFAULT is a const struct?
#endif

const char *__tag__ = "\0slr_runner:host:";
const char *__datatag__ = "\0slr_datatag:seqc-seqc_o-host-host-seqc:";

#include "../load.c"

