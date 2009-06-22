//                                                             -*- C++ -*-
//
// ptl_debug.cc: this file is part of the slc project.
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
// $Id$
//

#include "ptl_debug.h"

#ifdef UTC_CONFIG_DEBUG


using namespace std;

namespace uTC
{

// This mutex is grabbed in DPRINT to avoid interleaving prints and possible
// problems with non threading-safe std::cerr implementations
pthread_mutex_t g_debug_mutex;

void debug_init()
{
    pthread_mutex_init(&g_debug_mutex, NULL);
}

void debug_cleanup()
{
    pthread_mutex_destroy(&g_debug_mutex);
}

#ifdef UTC_CONFIG_DEBUG_MUTEX

int __LOCK(const char* file, int line, const char* name, pthread_mutex_t* mutex)
{
    int retval;
    DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): WAITING");
    retval = pthread_mutex_lock(mutex);
    if(retval == 0)
    {
        DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): LOCKED");
    }
    else
    {
        DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): ERROR: " << strerror(retval));
    }

    return retval;
}

int __TRYLOCK(const char* file, int line, const char* name, pthread_mutex_t* mutex)
{
    int retval;
    DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): TRYING");
    retval = pthread_mutex_trylock(mutex);
    if (retval == 0)
    {
        DPRINT(file << ":"<< line << ":" << name << " (" << mutex << "): LOCKED");
        return 0;
    }
    else if (retval == EBUSY)
    {

        DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): BUSY");
    }
    else
    {
        DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): ERROR: " << strerror(retval));
    }
    return retval;
}

int __UNLOCK(const char* file, int line, const char* name, pthread_mutex_t* mutex)
{
    int retval;
    DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): UNLOCKING");
    retval = pthread_mutex_unlock(mutex);
    if(retval == 0)
    {
        DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): UNLOCKED");
    }
    else
    {
        DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): ERROR: " << strerror(retval));
    }

    return retval;
}

int __CONDWAIT(const char* file, int line, const char* name, pthread_cond_t* cond, pthread_mutex_t* mutex)
{
    int retval;
    DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): UNLOCKED (COND)");
    retval = pthread_cond_wait(cond, mutex);
    if(retval == 0)
    {
        DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): LOCKED (COND)");
    }
    else
    {
        DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): ERROR: " << strerror(retval));
    }

    return retval;
}

int __DESTROY(const char* file, int line, const char* name, pthread_mutex_t* mutex)
{
    int retval;
    DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): DESTROYING");
    retval = pthread_mutex_destroy(mutex);
    if(retval == 0)
    {
        DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): DESTROYED");
    }
    else
    {
        DPRINT(file << ":" << line << ":" << name << " (" << mutex << "): ERROR: " << strerror(retval));
    }

    return retval;
}

// End #ifdef UTC_CONFIG_DEBUG_MUTEX
#endif

// end namespace uTC
}

// end #ifdef UTC_CONFIG_DEBUG
#endif // ! SLC_LIB_HOST_HOST_PTL_PTL_DEBUG_CC
