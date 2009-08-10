//
// ptl_vars.h: this file is part of the SL toolchain.
//
// Copyright (C) 2008, 2009 Universiteit van Amsterdam.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// The complete GNU General Public Licence Notice can be found as the
// `COPYING' file in the root directory.
//

#ifndef SLC_PTL_VARS_H
# define SLC_PTL_VARS_H

#include "ptl_debug.h"

namespace uTC
{

#ifdef UTC_CONFIG_PTHREAD_CONDITIONAL

// Conditionals are implemented as a linked list of waiting threads
// that keep checking the conditional's state
struct pthread_link_t;
struct pthread_cond_t
{
    pthread_link_t* first;
    pthread_link_t* last;
    int             state;
};

int pthread_cond_init(pthread_cond_t* cond, void*);
int pthread_cond_destroy(pthread_cond_t* cond);
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
int pthread_cond_signal(pthread_cond_t* cond);
int pthread_cond_broadcast(pthread_cond_t* cond);

#endif

#ifdef UTC_CONFIG_PTHREAD_TSD

// Map indexed by pthread_t thread id's, containing voidpointers
typedef std::map<pthread_t,void*> pthread_key_t_map;

// A key is just a pointer to a specific map
typedef pthread_key_t_map* pthread_key_t;

// Note: destr_function is ignored, but included for compatibility reasons
int pthread_key_create(pthread_key_t *key, void (*destr_function) (void *));
int pthread_key_delete(pthread_key_t key);
void* pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, void *pointer);

#endif

}

#endif // ! SLC_PTL_VARS_H
