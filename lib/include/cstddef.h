//
// cstddef.h: this file is part of the slc project.
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
// $Id$
//
#ifndef __CSTDDEF_H__
# define __CSTDDEF_H__

#ifdef __mt_freestanding__

#if defined(__alpha__)||defined(__mtalpha__)

/* 7.17.2 types */

typedef long ptrdiff_t;
typedef unsigned long size_t;

typedef int wchar_t;

#else
# warning No stddef.h definitions available for this target.
#endif

/* 7.17.3 macros */

#define NULL ((void *)0)

#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)

#else

#ifdef __cplusplus
#include <cstddef>
#else
#include <stddef.h>
#endif

#endif

#endif
