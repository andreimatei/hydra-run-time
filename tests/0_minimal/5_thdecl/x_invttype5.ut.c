// m4_include(proto.m4)
#include <libutc.h>

ut_decl(foo, int, ut_shparm(int, a), ut_glparm(int, b));

// XFAIL: C (incompatible parameter type)
ut_decl((*foop), int, ut_glparm(int, a), ut_shparm(float, b)) = &foo;

ut_decl((*foop2), int, ut_glparm(float, a), ut_shparm(int, b)) = &foo;
