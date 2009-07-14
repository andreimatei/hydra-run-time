//m4_define([[SHFWA]]) //shared float work around
m4_define(KERNEL, [[11]])
m4_include(livermore.slh)
//---------------------------------
// Livemore Loops -- SLC (uTC)
// M.A.Hicks, CSA Group, UvA
// Implementation based of various
// reference implementations
// including the original FORTRAN
// but mostly from
// Roy Longbottom, 1996.
//---------------------------------
//      LIVERMORE KERNEL 11
//        First Sum
//---------------------------------

//---------------------------------
// x[0] = y[0];
// for ( k=1 ; k<n ; k++ )
// {
//   x[k] = x[k-1] + y[k];
// }
//---------------------------------

sl_def(innerk11,void,
	sl_glparm(double*,xl),
	sl_glparm(double*,yl),
      	sl_shfparm(double,lastit))
{
	sl_index(iteration);
	double temp = sl_getp(lastit) + sl_getp(yl)[iteration];
	sl_getp(xl)[iteration] = temp;
	sl_setp(lastit,temp);
}
sl_enddef

sl_def(kernel,void)
{
	sl_create(,, 1,inner[KERNEL],1,SHARED_BLOCK,,innerk11,
     		sl_glarg(double*,xxl,x),
		sl_glarg(double*,yyl,y),
		sl_shfarg(double,llastit,y[0]));
	sl_sync();
}
sl_enddef
