//
// x_invsh3.c: this file is part of the SL toolchain.
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

[[]]// XFAIL: *:C
// (multiple storage specifiers)

sl_decl(foo, void, sl_shparm(register int, a));

sl_def(bar, void, sl_shparm(register int, a)) { } sl_enddef
