//
// assert.h: this file is part of the SL toolchain.
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

#ifndef SLC_SVP_ASSERT_H
# define SLC_SVP_ASSERT_H

#include <assert.h>

#warning "this header is deprecated. Use <assert.h> instead."

#define svp_assert(e) assert(e)

#endif // ! SLC_SVP_ASSERT_H
