//
// floatsh2.sl: this file is part of the slc project.
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

double one = 1.0l;
double zero = 0.0l;

sl_def(innerk3, void,
        sl_shfparm(double,ql))
{
	sl_setp(ql, sl_getp(ql) + one);
}
sl_enddef

sl_def(t_main, void)
{
	sl_create(,, ,2,,,,innerk3,
		     sl_shfarg(double, qql, zero));
	sl_sync();
}
sl_enddef
