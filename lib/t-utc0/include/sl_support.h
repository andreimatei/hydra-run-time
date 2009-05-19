//                                                             -*- C++ -*-
// sl_support.h: this file is part of the slc project.
//
// Copyright (C) 2009 Universiteit van Amsterdam.
// All rights reserved.
//
// $Id$
//
#ifndef SLC_SL_SUPPORT_H
# define SLC_SL_SUPPORT_H

#include "svp_os.h"

#define __svp_break(Val) do { \
    __asm__ __volatile__("break %0" : : "rI"(Val)); \
    while(1) __nop();				    \
  } while(0);

#define __svp_breakf(Val) do { \
    __asm__ __volatile__("breakf %0" : : "f"(Val)); \
    while(1) __nop();				    \
  } while(0);

#define __svp_kill(Val) __asm__ __volatile__("kill %0" : : "r"(Val))

typedef long sl_family_t;
typedef long sl_place_t;

#define PLACE_LOCAL   0
#define PLACE_DEFAULT 1

#define SVP_ENORMAL 0
#define SVP_EBROKEN 1
#define SVP_EKILLED 2

#endif // ! SLC_SL_SUPPORT_H
