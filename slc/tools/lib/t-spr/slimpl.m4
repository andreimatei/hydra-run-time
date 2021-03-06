# t-spr/slimpl.m4: this file is part of the SL toolchain.
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
#  Macro definitions for the sequential C syntax
# ###############################################

m4_define([[_sl_crcnt]],0)

# C braces
m4_define([[sl_lbr]], [[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'scope', 'body':[r"""]]
]])

m4_define([[sl_rbr]], [[m4_dnl
[["""],'loc_end':r"""]]__file__:__line__[["""}, r"""]]m4_dnl
]])

# memories and kinds

m4_define([[sl_array]], [[m4_dnl
[[{'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'memkind','name':'array',]]m4_dnl
[['itemtype': [r""" $1 """],]]m4_dnl
[['size': [r""" $2 """]}]]m4_dnl
]])

m4_define([[sl_multiarray]], [[m4_dnl
[[{'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'memkind','name':'multiarray',]]m4_dnl
[['size': [r""" $1 """]}]]m4_dnl
]])

m4_define([[sl_mem]], [[m4_dnl
[[ """, {'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'mem','name': """$1"""}, r""" ]]m4_dnl
]])

m4_define([[sl_desc]], [[m4_dnl
[[ """, {'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'memdesc',]]m4_dnl
[['kind': $1,]]m4_dnl
[['name': """ $2 """,]]m4_dnl
[['rhs': [r""" $3 """]}, r""" ]]m4_dnl
]])

m4_define([[sl_alloc]], [[m4_dnl
[[ """, {'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'memalloc',]]m4_dnl
[['kind': $1,]]m4_dnl
[['name': """$2"""}, r""" ]]m4_dnl
]])


m4_define([[sl_restrict]], [[m4_dnl
[[ """, {'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'memrestrict',]]m4_dnl
[['name': """$1""",]]m4_dnl
[['rhs': """$2""",]]m4_dnl
[['offset': [r""" $3 """],]]m4_dnl
[['size': [r""" $4 """]}, r""" ]]m4_dnl
]])


m4_define([[sl_extend]], [[m4_dnl
[[ """, {'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'memextend',]]m4_dnl
[['name':"""$1""",]]m4_dnl
[['rhs':"""$2"""}, r""" ]]m4_dnl
]])

m4_define([[sl_activate]], [[m4_dnl
[[ """, {'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'memactivate',]]m4_dnl
[['lhs':"""]]m4_ifblank([[$2]],,[[$1]])[[""",]]m4_dnl
[['rhs':"""]]m4_ifblank([[$2]],[[$1]],[[$2]])[["""}, r""" ]]m4_dnl
]])

m4_define([[sl_propagate]], [[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'mempropagate',]]m4_dnl
[['dir':"""$1""",]]m4_dnl 
[['name':"""$2"""}, r""" ]]m4_dnl
]])

# Thread definitions

m4_define([[_sl_doparm]], [[m4_dnl
[[{'loc':r"""]]__file__:__line__[[""",'type':"""$1""",'ctype':r""" $2 """,'name':"""]]m4_dnl
m4_ifblank([[$3]],sl_anonymous,[[$3]])[[]]m4_dnl
[["""}]]m4_dnl
]])

m4_define([[_sl_doparm_mem]], [[m4_dnl
[[{'loc':r"""]]__file__:__line__[[""",'type':"""$1_mem""",'kind': $2,'name':"""]]m4_dnl
m4_ifblank([[$3]],sl_anonymous,[[$3]])[[]]m4_dnl
[["""}]]m4_dnl
]])

m4_define([[sl_glparm_mem]], [[_sl_doparm_mem([[glparm]],[[$1]],[[$2]])]])
m4_define([[sl_shparm_mem]], [[_sl_doparm_mem([[shparm]],[[$1]],[[$2]])]])

m4_define([[sl_glparm]], [[_sl_doparm([[glparm]],[[$1]],[[$2]])]])
m4_define([[sl_glfparm]], [[_sl_doparm([[glfparm]],[[$1]],[[$2]])]])
m4_define([[sl_shparm]], [[_sl_doparm([[shparm]],[[$1]],[[$2]])]])
m4_define([[sl_shfparm]], [[_sl_doparm([[shfparm]],[[$1]],[[$2]])]])
m4_define([[sl_glparm_mutable]], [[_sl_doparm([[glparm_mutable]],[[$1]],[[$2]])]])
m4_define([[sl_glfparm_mutable]], [[_sl_doparm([[glfparm_mutable]],[[$1]],[[$2]])]])

m4_define([[sl__static]], [[[[""", {'type':'attr', 'name':'static'}, r"""]]]])
m4_define([[sl__naked]], [[[[""", {'type':'attr', 'name':'naked', 'flavor':r""" $1 """}, r"""]]]])
m4_define([[sl__gencallee]], [[[[""", {'type':'attr', 'name':'gencallee'}, r"""]]]])
m4_define([[sl__exclusive]], [[[[""", {'type':'attr', 'name':'exclusive'}, r"""]]]])

m4_define([[sl_def]],[[m4_dnl
m4_pushdef([[return]],[[sl_end_thread]])m4_dnl
m4_ifdef([[_sl_increate]],[[m4_fatal(missing sync after create)]])m4_dnl
m4_pushdef([[_sl_curfun]],[[$1]])m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'fundef','name':"""$1""",'params':[]]m4_dnl
m4_shift2($@)[[]]m4_dnl
[[],'extras':[r""" $2 """], 'body':[r"""]]
]])

m4_define([[sl_enddef]],[[m4_dnl
[[""" ],'loc_end':r"""]]__file__:__line__[["""}, r"""]]
m4_ifdef([[_sl_increate]],[[m4_fatal(missing sync after create)]])m4_dnl
m4_popdef([[return]])m4_dnl
m4_popdef([[_sl_curfun]])m4_dnl
]])

m4_define([[sl_end_thread]], [[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'end_thread'}, r"""]]m4_dnl
]])

m4_define([[sl_break]], [[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'break'}, r"""]]m4_dnl
]])

m4_define([[sl_decl]], [[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'decl','name':"""$1""",'params': []]m4_dnl
m4_shift2($@)m4_dnl
[[],'extras':[r""" $2 """], 'loc_end':r"""]]__file__:__line__[["""}, r"""]]m4_dnl
]])

m4_define([[sl_decl_fptr]], [[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'decl_funptr','name':"""$1""",'params': []]m4_dnl
m4_shift2($@)m4_dnl
[[],'extras':[r""" $2 """], 'loc_end':r"""]]__file__:__line__[["""}, r"""]]m4_dnl
]])

m4_define([[sl_typedef_fptr]], [[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'decl_funptr','name':"""$1""",'params': []]m4_dnl
m4_shift2($@)m4_dnl
[[],'extras':[{'type':'attr', 'name':'typedef'},r""" $2 """], 'loc_end':r"""]]__file__:__line__[["""}, r"""]]m4_dnl
]])

m4_define([[sl_index]], [[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'indexdecl','name':"""$1"""}, r"""]]m4_dnl
]])

m4_define([[sl_mf_spread]], [[[[""", {'type':'attr','name':'mapping','mf':'spread','n':[r""" $1 """]}, r""" ]]]])
m4_define([[sl_mf_distribute]], [[[[""", {'type':'attr','name':'mapping', 'mf':'distribute', 'n':[r""" $1 """], 'm':[r""" $2 """]}, r""" ]]]])
m4_define([[sl_mf_distribute_ext]], [[[[""", {'type':'attr','name':'mapping', 'mf':'distribute_ex', 'nodes':[r""" $1 """], 'procs':[r""" $2 """], 'tcs':[r""" $3 """], 'block':[r""" $4 """], 'default_place_policy':[r""" $5 """]}, r""" ]]]])

m4_define([[sl_create_ext]], [[m4_dnl
m4_pushdef([[_sl_increate]],1)m4_dnl
m4_step([[_sl_crcnt]])m4_dnl
m4_pushdef([[_sl_lbl]],F[[]]_sl_crcnt)m4_dnl
[[""",]]m4_dnl
[[{'type':'create',]]m4_dnl
[['loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['fid':[r""" $1 """],]]m4_dnl
[['extras':[r""" $7 """],]]m4_dnl
[['fun':[r""" $8 """],]]m4_dnl
[['lbl':']]_sl_lbl[[',]]m4_dnl
[['place':[r""" ]]m4_ifblank([[$2]],PLACE_DEFAULT,[[$2]])[[ """],]]m4_dnl
[['start':[r""" ]]m4_ifblank([[$3]],0,[[$3]])[[ """],]]m4_dnl
[['limit':[r""" ]]m4_ifblank([[$4]],1,[[$4]])[[ """],]]m4_dnl
[['step':[r""" ]]m4_ifblank([[$5]],1,[[$5]])[[ """],]]m4_dnl
[['mapping':[r""" $6 """],]]m4_dnl
[['block':["0"],]]m4_dnl
[['args':[]]m4_shiftn(8,$@)[[],]]m4_dnl
[['body': [r"""]]
]])


m4_define([[sl_create]], [[m4_dnl
m4_pushdef([[_sl_increate]],1)m4_dnl
m4_step([[_sl_crcnt]])m4_dnl
m4_pushdef([[_sl_lbl]],F[[]]_sl_crcnt)m4_dnl
[[""",]]m4_dnl
[[{'type':'create',]]m4_dnl
[['loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['fid':[r""" $1 """],]]m4_dnl
[['extras':[r""" $7 """],]]m4_dnl
[['fun':[r""" $8 """],]]m4_dnl
[['lbl':']]_sl_lbl[[',]]m4_dnl
[['place':[r""" ]]m4_ifblank([[$2]],PLACE_DEFAULT,[[$2]])[[ """],]]m4_dnl
[['start':[r""" ]]m4_ifblank([[$3]],0,[[$3]])[[ """],]]m4_dnl
[['limit':[r""" ]]m4_ifblank([[$4]],1,[[$4]])[[ """],]]m4_dnl
[['step':[r""" ]]m4_ifblank([[$5]],1,[[$5]])[[ """],]]m4_dnl
[['mapping':[],'block':[r""" ]]m4_ifblank([[$6]],0,[[$6]])[[ """],]]m4_dnl
[['args':[]]m4_shiftn(8,$@)[[],]]m4_dnl
[['body': [r"""]]
]])

m4_define([[_sl_doarg]], [[m4_dnl
[[{'loc':r"""]]__file__:__line__[[""",'type':'$1','ctype':"""$2""",]]m4_dnl
[['init':[r""" $4 """],'name':"""]]m4_dnl
m4_ifblank([[$3]],sl_anonymous,[[$3]])[[]]m4_dnl
[["""}]]m4_dnl
]])

m4_define([[_sl_doarg_mem]], [[m4_dnl
[[{'loc':r"""]]__file__:__line__[[""",'type':'$1_mem','kind': $2,'name':"""]]m4_dnl
m4_ifblank([[$3]],sl_anonymous,[[$3]])[[]]m4_dnl
[[""",'init':"""$4"""}]]m4_dnl
]])

m4_define([[sl_glarg_mem]], [[_sl_doarg_mem([[glarg]],[[$1]],[[$2]],[[$3]])]])
m4_define([[sl_sharg_mem]], [[_sl_doarg_mem([[sharg]],[[$1]],[[$2]],[[$3]])]])

m4_define([[sl_glarg]], [[_sl_doarg([[glarg]],[[$1]],[[$2]],[[$3]])]])
m4_define([[sl_glfarg]], [[_sl_doarg([[glfarg]],[[$1]],[[$2]],[[$3]])]])
m4_define([[sl_sharg]], [[_sl_doarg([[sharg]],[[$1]],[[$2]],[[$3]])]])
m4_define([[sl_shfarg]], [[_sl_doarg([[shfarg]],[[$1]],[[$2]],[[$3]])]])

# Pass transparently the sync construct.
m4_define([[sl_sync]],[[m4_dnl
m4_ifndef([[_sl_increate]],[[m4_fatal(sync without create)]])m4_dnl
[["""],'loc_end':r"""]]__file__:__line__[[""",'sync':'normal',]]m4_dnl
[['result':[r""" $1 """]}, r"""]]
m4_popdef([[_sl_increate]])m4_dnl
m4_popdef([[_sl_lbl]])m4_dnl
]])

m4_define([[sl_detach]],[[m4_dnl
m4_ifndef([[_sl_increate]],[[m4_fatal(sync without create)]])m4_dnl
[["""],'loc_end':r"""]]__file__:__line__[[""",'sync':'detach'}, r"""]]m4_dnl
m4_popdef([[_sl_increate]])m4_dnl
m4_popdef([[_sl_lbl]])m4_dnl
]])

m4_define([[_sl_doget]],[[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'get$1','name':"""$2"""},r"""]]m4_dnl
]])

m4_define([[_sl_doset]],[[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'set$1','name':"""$2""",]]m4_dnl
[['rhs':[r""" $3 """]},r"""]]
]])

m4_define([[_sl_doget_mem]],[[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'getm$1','lhs':"""$2""",'name':"""$3"""},r"""]]m4_dnl
]])

m4_define([[_sl_doset_mem]],[[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",'type':'setm$1','name':"""$2""",'rhs':"""$3"""},r"""]]
]])

# Pass transparently all references to argument/parameter
# names.
m4_define([[sl_getma]],[[_sl_doget_mem([[a]], [[$1]], [[$2]])]])
m4_define([[sl_getmp]],[[_sl_doget_mem([[p]], [[$1]], [[$2]])]])
m4_define([[sl_setma]],[[_sl_doset_mem([[a]], [[$1]], [[$2]])]])
m4_define([[sl_setmp]],[[_sl_doset_mem([[p]], [[$1]], [[$2]])]])

m4_define([[sl_geta]],[[_sl_doget([[a]], [[$1]])]])
m4_define([[sl_getp]],[[_sl_doget([[p]], [[$1]])]])
m4_define([[sl_seta]],[[_sl_doset([[a]], [[$1]], [[$2]])]])
m4_define([[sl_setp]],[[_sl_doset([[p]], [[$1]], [[$2]])]])

m4_define([[sl_scatter_affine]],[[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'memscatter_affine',]]m4_dnl
[['name':"""$1""",]]m4_dnl
[['rhs':"""$2""",]]m4_dnl
[['a':[r""" ]]m4_ifblank([[$3]],1,[[$3]])[[ """],]]m4_dnl
[['b':[r""" ]]m4_ifblank([[$4]],0,[[$4]])[[ """],]]m4_dnl
[['c':[r""" ]]m4_ifblank([[$5]],0,[[$5]])[[ """]}, r"""]]m4_dnl
]])

m4_define([[sl_gather_affine]],[[m4_dnl
[[""", {'loc':r"""]]__file__:__line__[[""",]]m4_dnl
[['type':'memgather_affine',]]m4_dnl
[['a':[r""" ]]m4_ifblank([[$2]],1,[[$2]])[[ """],]]m4_dnl
[['b':[r""" ]]m4_ifblank([[$3]],0,[[$3]])[[ """],]]m4_dnl
[['c':[r""" ]]m4_ifblank([[$4]],0,[[$4]])[[ """],]]m4_dnl
[['name':"""$1"""}, r""" ]]m4_dnl
]])

# pass transparently function call
m4_define([[sl_funcall]], [[[[$3]](m4_shift3($@))]])



