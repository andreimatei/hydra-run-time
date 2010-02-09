//
// io.h: this file is part of the SL toolchain.
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

#ifndef SLC_SVP_IO_H
# define SLC_SVP_IO_H

#include <svp/argslot.h>
#include <cstddef.h>

#warning this header is deprecated. Use cstdio.h instead.

/* Print a single character to the console output. */
sl_decl(svp_io_putc, void,
	sl_glparm(char, c));

/* Print a nul-terminated string to the console output. */
sl_decl(svp_io_puts, void,
	sl_glparm(const char *, gstr));

/* Print an arbitrary number of bytes to the console output. */
sl_decl(svp_io_write, void,
	sl_glparm(void *, gptr),
	sl_glparm(size_t, gsize));

/* Print a floating point number to the console output. */
/* Note: the floating point is normalized before printing. */
sl_decl(svp_io_putf, void,
	sl_glfparm(double, gx),
	sl_glparm(unsigned, gprec),
	sl_glparm(unsigned, gbase));

/* Print an unsigned integer to the console output. */
sl_decl(svp_io_putun, void,
	sl_glparm(uint64_t, gn),
	sl_glparm(unsigned, gbase));

/* Print a nul-terminated string to the console output. */
sl_decl(svp_io_puts, void,
	sl_glparm(const char *, gstr));

/* Print a floating point number to the console output. */
/* Note: the floating point is normalized before printing. */
sl_decl(svp_io_putf, void,
	sl_glfparm(double, gx),
	sl_glparm(unsigned, gprec),
	sl_glparm(unsigned, gbase));

/* Print a signed integer to the console output. */
sl_decl(svp_io_putn, void,
	sl_glparm(int64_t, gn),
	sl_glparm(unsigned, gbase));

/* Format and print arguments to the console output. */
/* Recognized formats:
   %c - single character
   %s - nul-terminated string
   %f - floating point number in base 10 with 7 digits of precision
   %g - floating point number in base 10 with 15 digits of precision
   %d - signed integer in base 10
   %u - unsigned integer in base 10
   %x - unsigned integer in base 16
   %% - the character % itself.
*/
sl_decl(svp_io_printf, void,
	sl_glparm(const char*, gfmt),
	sl_glparm(const unsigned char*, gsz),
	sl_glparm(svp_arg_slot*, gdata));

#endif // ! SLC_SVP_IO_H
