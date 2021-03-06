//
// gen_table_1.c: this file is part of the SL program suite.
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

#include <math.h>
#include <stdio.h>
#include <assert.h>

// #define PI 3.1415926535897932384626433832795
#define PI 0x3.243F6A8885A308D313198A2E03707344A4093822299F31D008p0L

#define INTEGER unsigned
#define PRINTF_MOD1 "" /* integer modifier */

#define FLOATTYPE long double
#define SIN sinl
#define COS cosl
#define LITTERAL_SUFFIX "L"
#define PRINTF_MOD2 "L" /* fp modifier */

enum constants { M = TABLE_SIZE, N = 1 << M };
static  FLOATTYPE sc_table[M][N/2][2];

int main(void) {

  INTEGER k, i, w, w2, LE2;


  for (k = 1; k < M; ++k) {
    for (i = 0; i < N/2; ++i) {
      LE2    = 1 << (k - 1);
      w      = i % LE2;
      FLOATTYPE d0 = (FLOATTYPE)w * (FLOATTYPE)(PI / LE2);
      FLOATTYPE d1 = COS(d0);
      FLOATTYPE d2 = -SIN(d0);
      sc_table[k-1][i][0] = d1;
      sc_table[k-1][i][1] = d2;
    }
  }

  printf("static const cpx_t sc_table[/*0..M-1*/ %d][/*0..N/2-1*/ %d] = { \n", M, N/2-1);
  for (k = 1; k < M; ++k) {
    printf(" { /* k = %d: */\n", k);
    for (i = 0; i < N/2-1; ++i) {
      printf("  { /* i = %d: */ %" PRINTF_MOD2 "a" LITTERAL_SUFFIX ","
	     "\t%" PRINTF_MOD2 "a" LITTERAL_SUFFIX " }, \n",
	     i,
	     sc_table[k-1][i][0],
	     sc_table[k-1][i][1]);
    }
    printf(" },\n");
  }
  printf("};\n");

  return 0;
}
