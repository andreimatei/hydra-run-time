// SLT_RUN:  M=3 -dPi  Pf=1 -dPc  BR=1
// SLT_RUN:  M=4 -dPi  Pf=1 -dPc  BR=1
// SLT_RUN:  M=4 -dPi  Pf=1 -dPc  BR=1 -n 3

// FIXME: as of 2004-04-05 the output of this
// program seems implementation dependent;
// so ignore the diff during testing:
// XIGNORE: *:D
m4_include(fft_test.sl)
m4_include(fft_impl2.sl)
m4_include(fft_extra.sl)

