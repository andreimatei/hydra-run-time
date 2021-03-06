# t-ptl/slimpl.m4: this file is part of the SL toolchain.
# 
# Copyright (C) 2008,2009,2010 Universiteit van Amsterdam
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
# ###############################################
#  Macro definitions for the muTC-ptl syntax
# ###############################################

m4_define([[_sl_crcnt]],0)

# Pass transparently thread definitions.
m4_define([[sl_def]],[[m4_dnl
m4_define([[sl_thparms]],m4_dquote(m4_shift2($@)))m4_dnl
void [[$1]]m4_if(m4_count(sl_thparms),0,(void),(sl_thparms))m4_dnl
]])

# end of thread
m4_define([[sl_end_thread]], [[return]])

# No special action at the end of a definition
m4_define([[sl_enddef]],[[m4_dnl
m4_ifdef([[_sl_increate]],[[m4_fatal(missing sync after create)]])m4_dnl
]])

# With the corecc syntax, a declaration looks the same as a definition.
m4_define([[sl_decl]], m4_defn([[sl_def]]))
m4_define([[sl_decl_fptr]], [[sl_decl((*[[$1]]), m4_shift($@))]])

# Pass transparently parameter declarations.
m4_define([[sl_shparm]], [[uTC::shared<[[$1]]>& __p_[[]]m4_ifblank([[$2]],sl_anonymous,[[$2]])]])
m4_define([[sl_glparm]], [[uTC::global<[[$1]]>& __p_[[]]m4_ifblank([[$2]],sl_anonymous,[[$2]])]])
m4_define([[sl_glparm_mutable]], [[m4_dnl
m4_error([[sl_glparm_mutable not implemented yet for this target]])]])
m4_copy([[sl_shparm]],[[sl_shfparm]])
m4_copy([[sl_glparm]],[[sl_glfparm]])
m4_copy([[sl_glparm_mutable]],[[sl_glfparm_mutable]])

# Pass transparently the index declaration.
m4_define([[sl_index]], [[uTC::index [[$1]]]])

# Pull shared and global argument declarations.
m4_define([[sl_sharg]],[[&::[[$1]]:m4_ifblank([[$2]],sl_anonymous,[[$2]]):m4_ifblank([[$3]],,[[= [[$3]]]])]])
m4_define([[sl_glarg]],[[:const:[[$1]]:m4_ifblank([[$2]],sl_anonymous,[[$2]]):m4_ifblank([[$3]],,[[= [[$3]]]])]])
m4_copy([[sl_sharg]],[[sl_shfarg]])
m4_copy([[sl_glarg]],[[sl_glfarg]])


m4_define([[sl_declarg]],[[m4_dnl
[[$3]] [[$2]] __a_[[$4]] m4_joinall([[:]],m4_shiftn(4,$@)); m4_dnl
]])m4_dnl

m4_define([[sl_pullarg]],[[m4_dnl
, [[$1]](__a_[[$4]]) m4_dnl
]])

m4_define([[sl_create]], [[m4_dnl
m4_ifdef([[_sl_increate]],[[m4_fatal(cannot nest create)]])m4_dnl
m4_define([[_sl_increate]],1)m4_dnl
m4_step([[_sl_crcnt]])m4_dnl
m4_define([[_sl_lbl]],__child[[]]_sl_crcnt)m4_dnl
m4_define([[_sl_brk]],_sl_lbl[[]]_brk)m4_dnl
m4_define([[_sl_place]],m4_ifblank([[$2]],[[uTC::PLACE_GROUP]],[[$2]]))m4_dnl
m4_define([[_sl_start]],m4_ifblank([[$3]],0,[[$3]]))m4_dnl
m4_define([[_sl_limit]],m4_ifblank([[$4]],1,[[$4]]))m4_dnl
m4_define([[_sl_step]],m4_ifblank([[$5]],1,[[$5]]))m4_dnl
m4_define([[_sl_block]],m4_ifblank([[$6]],0,[[$6]]))m4_dnl
m4_define([[_sl_thargs]],m4_dquote(m4_shiftn(8,$@)))m4_dnl
sl_family_t _sl_lbl; m4_dnl
m4_foreach([[_sl_arg]],m4_quote(_sl_thargs),[[m4_apply([[sl_declarg]],m4_split(_sl_arg,:))]]) m4_dnl
uTC::create(_sl_lbl,_sl_place,false,false,_sl_start,_sl_limit,_sl_step,_sl_block, m4_dnl
([[$8]]) m4_dnl
m4_foreach([[_sl_arg]],m4_quote(_sl_thargs),[[m4_apply([[sl_pullarg]],m4_split(_sl_arg,:))]]) m4_dnl
); m4_dnl
m4_ifblank([[$1]],,[[([[$1]]) = _sl_lbl]]) m4_dnl
]])


# Pass transparently the sync construct.
m4_define([[sl_sync]],[[m4_dnl
m4_ifndef([[_sl_increate]],[[m4_fatal(sync without create)]])m4_dnl
m4_ifblank([[$1]],,[[$1 = ]])uTC::sync(_sl_lbl)m4_dnl
m4_undefine([[_sl_increate]])m4_dnl
]])

# Pass transparently all references to argument/parameter
# names.
m4_define([[sl_geta]],[[__a_[[$1]]]])
m4_define([[sl_seta]],[[__a_[[$1]] = $2]])
m4_define([[sl_getp]],[[__p_[[$1]]]])
m4_define([[sl_setp]],[[__p_[[$1]] = [[$2]]]])

# Pass transparently break and kill
m4_define([[sl_break]],[[uTC::Break()]])
m4_define([[sl_kill]],[[uTC::kill([[$1]])]])

# Pass transparently the break id
m4_define([[sl_getbr]],[[$1]]_brk)

