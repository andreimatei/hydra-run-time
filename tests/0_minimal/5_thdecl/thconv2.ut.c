// m4_include(proto.m4)
#include <libutc.h>

ut_def(foo, int, ut_shparm(int, a), ut_glparm(int, b)) {} ut_enddef

ut_decl((*foop), unsigned, ut_shparm(int, a), ut_glparm(int, b)) = &foo; 

ut_def(t_main, void) { } ut_enddef
