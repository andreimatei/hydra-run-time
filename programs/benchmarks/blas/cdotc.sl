//
// cdotc.sl: this file is part of the slc project.
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

m4_define(COMPLEX, blas_complex)
m4_define(FLOAT, float)
m4_define(FUNCTION, cdotc)
m4_define(OP1, +)
m4_define(OP2, -)
m4_include(templates/xdotc.sl)
