/*
 * Copyright (c) 2010 Martin Decky
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

/** @addtogroup abs32le
 * @{
 */
/** @file
 */

#ifndef KERN_abs32le_ATOMIC_H_
#define KERN_abs32le_ATOMIC_H_

#include <typedefs.h>
#include <arch/barrier.h>
#include <preemption.h>
#include <verify.h>

ATOMIC static inline void atomic_inc(atomic_t *val)
    REQUIRES(val->count < ATOMIC_COUNT_MAX)
{
	/* On real hardware the increment has to be done
	   as an atomic action. */
	
	val->count++;
}

ATOMIC static inline void atomic_dec(atomic_t *val)
    REQUIRES(val->count > ATOMIC_COUNT_MIN)
{
	/* On real hardware the decrement has to be done
	   as an atomic action. */
	
	val->count--;
}

ATOMIC static inline atomic_count_t atomic_postinc(atomic_t *val)
    REQUIRES(val->count < ATOMIC_COUNT_MAX)
{
	/* On real hardware both the storing of the previous
	   value and the increment have to be done as a single
	   atomic action. */
	
	atomic_count_t prev = val->count;
	
	val->count++;
	return prev;
}

ATOMIC static inline atomic_count_t atomic_postdec(atomic_t *val)
    REQUIRES(val->count > ATOMIC_COUNT_MIN)
{
	/* On real hardware both the storing of the previous
	   value and the decrement have to be done as a single
	   atomic action. */
	
	atomic_count_t prev = val->count;
	
	val->count--;
	return prev;
}

#define atomic_preinc(val)  (atomic_postinc(val) + 1)
#define atomic_predec(val)  (atomic_postdec(val) - 1)

ATOMIC static inline atomic_count_t test_and_set(atomic_t *val)
{
	/* On real hardware the retrieving of the original
	   value and storing 1 have to be done as a single
	   atomic action. */
	
	atomic_count_t prev = val->count;
	val->count = 1;
	return prev;
}

ATOMIC static inline atomic_count_t arch_atomic_get(atomic_t *val)
{
	/* This function is not needed on real hardware, it just
	   duplicates the functionality of atomic_get(). It is
	   defined here because atomic_get() is an inline function
	   declared in a header file which we are included in. */
	
	return val->count;
}

static inline void atomic_lock_arch(atomic_t *val)
{
	do {
		while (arch_atomic_get(val));
	} while (test_and_set(val));
}

#endif

/** @}
 */
