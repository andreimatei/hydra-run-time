m4_init()
# slc.m4: this file is part of the SL toolchain.
# 
# Copyright (C) 2008,2009 Universiteit van Amsterdam
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# The complete GNU General Public Licence Notice can be found as the
# `COPYING' file in the root directory.
#
#
# $Id$

# m4_step: increment variable by 1
m4_define([[m4_step]],[[m4_define([[$1]], m4_incr($1))]])

# sl_anon: anonymous identifier generator
m4_define([[_sl_anonct]],0)
m4_define([[sl_anonymous]], [[m4_step([[_sl_anonct]])[[__slanon]]_sl_anonct]])

# sl_breakable() - helper macro, returns 1 if type is non-void or
# non-empty
m4_define([[sl_breakable]],[[m4_case([[$1]],[[void]],0,[[]],0,1)]])

# load implementation-specific definitions
m4_include([[slimpl.m4]])

# sl_proccall() - helper macro, encapsulate a singleton create
m4_define([[sl_proccall]],[[do { sl_create(,,,,,,,$@); sl_sync(); } while(0)]])

# sl_begin_header() / sl_end_header() - helper macros, protect against multiple inclusion
m4_define([[sl_begin_header]],[[m4_dnl
m4_ifndef([[$1_found]],[[m4_define([[$1_found]],1)m4_divert_push(0)]],[[m4_divert_push([[KILL]])]])m4_dnl
]])

m4_define([[sl_end_header]],[[m4_dnl
m4_divert_pop()
]])

# Provide a helper M4 macro that defeats
# extra quoting after preprocessing
m4_define([[sl_cquote]], [[']])
m4_define([[sl_cdquote]], [["]])

# Initialize C mode: disable m4 comments, 
# stop diverting.
m4_changecom(//)
m4_wrap_lifo([[m4_divert_pop(0)]])
m4_divert_push(0)
