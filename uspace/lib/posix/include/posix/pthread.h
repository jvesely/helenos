/*
 * Copyright (c) 2013 Vojtech Horky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup libposix
 * @{
 */
#ifndef POSIX_PTHREAD_H_
#define POSIX_PTHREAD_H_

#ifndef __POSIX_DEF__
#define __POSIX_DEF__(x) x
#endif

#include "libc/thread.h"
#include "time.h"

typedef thread_id_t pthread_t;

typedef struct {
	int dummy;
} pthread_attr_t;

typedef int pthread_key_t;

#define PTHREAD_MUTEX_RECURSIVE 1

typedef struct pthread_mutex {
	int dummy;
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }

typedef struct {
	int dummy;
} pthread_mutexattr_t;

typedef struct {
	int dummy;
} pthread_condattr_t;

typedef struct {
	int dummy;
} pthread_cond_t;

#define PTHREAD_COND_INITIALIZER { 0 }

extern pthread_t pthread_self(void);
extern int pthread_equal(pthread_t, pthread_t);
extern int pthread_create(pthread_t *, const pthread_attr_t *,
    void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **);
extern int pthread_detach(pthread_t);

extern int pthread_attr_init(pthread_attr_t *);
extern int pthread_attr_destroy(pthread_attr_t *);

extern int pthread_mutex_init(pthread_mutex_t *restrict,
    const pthread_mutexattr_t *restrict);
extern int pthread_mutex_destroy(pthread_mutex_t *);
extern int pthread_mutex_lock(pthread_mutex_t *);
extern int pthread_mutex_trylock(pthread_mutex_t *);
extern int pthread_mutex_unlock(pthread_mutex_t *);

extern int pthread_mutexattr_init(pthread_mutexattr_t *);
extern int pthread_mutexattr_destroy(pthread_mutexattr_t *);
extern int pthread_mutexattr_gettype(const pthread_mutexattr_t *restrict,
    int *restrict);
extern int pthread_mutexattr_settype(pthread_mutexattr_t *, int);

extern int pthread_cond_init(pthread_cond_t *restrict,
    const pthread_condattr_t *restrict);
extern int pthread_cond_destroy(pthread_cond_t *);
extern int pthread_cond_broadcast(pthread_cond_t *);
extern int pthread_cond_signal(pthread_cond_t *);
extern int pthread_cond_timedwait(pthread_cond_t *restrict,
    pthread_mutex_t *restrict, const struct __POSIX_DEF__(timespec) *restrict);
extern int pthread_cond_wait(pthread_cond_t *restrict,
    pthread_mutex_t *restrict);

extern int pthread_condattr_destroy(pthread_condattr_t *);
extern int pthread_condattr_init(pthread_condattr_t *);

extern void *pthread_getspecific(pthread_key_t);
extern int pthread_setspecific(pthread_key_t, const void *);
extern int pthread_key_delete(pthread_key_t);
extern int pthread_key_create(pthread_key_t *, void (*)(void*));

#endif

/** @}
 */
