//
// x_extcreatedefs.sl: this file is part of the slc project.
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
// $Id$
//

sl_decl(ext, void);

// XFAIL: *:L (symbol not defined)

sl_def(t_main, void)
{
  sl_create(,,,,,,, ext);
  sl_sync();
}
sl_enddef
