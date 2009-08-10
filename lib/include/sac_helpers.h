//
// sac_helpers.h: this file is part of the SL toolchain.
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

#ifndef SLC_SAC_HELPERS_H
# define SLC_SAC_HELPERS_H

#include <cstddef.h>
#include <cmalloc.h>

#ifdef __mt_freestanding__

extern size_t strlen(const char*);
extern char* strncpy(char *, const char*, size_t);

#else

#include <string.h>

#endif

m4_define([[malloc]],[[m4_dnl
m4_warning([[using malloc as in C is discouraged. Use sl_funcall(... [[malloc]] ...) instead.]])m4_dnl
sl_funcall([[malloc_place]], [[ptr]], [[malloc]], sl_farg(size_t, [[$1]]))]])

m4_define([[free]],[[m4_dnl
m4_warning([[using free as in C is discouraged. Use sl_funcall(... [[free]] ...) instead.]])m4_dnl
sl_funcall([[malloc_place]], [[ptr]], [[free]], sl_farg(void*, [[$1]]))]])

m4_define([[strncpy]],[[m4_dnl
m4_warning([[using strncpy as in C is discouraged. Use sl_funcall(... [[strncpy]] ...) instead.]])m4_dnl
sl_funcall(, [[ptr]], [[strncpy]], sl_farg(char*, [[$1]]), sl_farg(const char*,[[$2]]), sl_farg(size_t, [[$3]]))]])

#endif // ! SLC_SAC_HELPERS_H
