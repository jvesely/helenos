/*
 * Copyright (c) 2005 Jakub Vana
 * Copyright (c) 2005 Jakub Jermar
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

#include <print.h>
#include <debug.h>

#include <test.h>
#include <atomic.h>
#include <proc/thread.h>

#include <arch.h>
#include <arch/arch.h>


#define THREADS   150
#define ATTEMPTS  100

#define E_10e8   271828182
#define PI_10e8  3141592

static inline long double sqrt(long double a)
{
	long double x =	1;
	long double lx = 0;
	
	if (a < 0.00000000000000001)
		return 0;
		
	while (x != lx) {
		lx = x;
		x = (x + (a / x)) / 2;
	}
	
	return x;
}

static atomic_t threads_ok;
static atomic_t threads_fault;
static waitq_t can_start;

static void e(void *data)
{
	int i;
	double e, d, le, f;
	
	thread_detach(THREAD);
	
	waitq_sleep(&can_start);
	
	for (i = 0; i<ATTEMPTS; i++) {
		le = -1;
		e = 0;
		f = 1;
		
		for (d = 1; e != le; d *= f, f += 1) {
			le = e;
			e = e + 1 / d;
		}
		
		if ((int) (100000000 * e) != E_10e8) {
			TPRINTF("tid%" PRIu64 ": e*10e8=%zd should be %" PRIun "\n", THREAD->tid, (unative_t) (100000000 * e), (unative_t) E_10e8);
			atomic_inc(&threads_fault);
			break;
		}
	}
	atomic_inc(&threads_ok);
}

static void pi(void *data)
{
	int i;
	double lpi, pi;
	double n, ab, ad;
	
	thread_detach(THREAD);
	
	waitq_sleep(&can_start);
	
	for (i = 0; i < ATTEMPTS; i++) {
		lpi = -1;
		pi = 0;
		
		for (n = 2, ab = sqrt(2); lpi != pi; n *= 2, ab = ad) {
			double sc, cd;
			
			sc = sqrt(1 - (ab * ab / 4));
			cd = 1 - sc;
			ad = sqrt(ab * ab / 4 + cd * cd);
			lpi = pi;
			pi = 2 * n * ad;
		}
		
		if ((int) (1000000 * pi) != PI_10e8) {
			TPRINTF("tid%" PRIu64 ": pi*10e8=%zd should be %" PRIun "\n", THREAD->tid, (unative_t) (1000000 * pi), (unative_t) (PI_10e8 / 100));
			atomic_inc(&threads_fault);
			break;
		}
	}
	atomic_inc(&threads_ok);
}

const char *test_fpu1(void)
{
	unsigned int i;
	atomic_count_t total = 0;
	
	waitq_initialize(&can_start);
	atomic_set(&threads_ok, 0);
	atomic_set(&threads_fault, 0);
	
	TPRINTF("Creating %u threads... ", 2 * THREADS);
	
	for (i = 0; i < THREADS; i++) {
		thread_t *t;
		
		if (!(t = thread_create(e, NULL, TASK, 0, "e", false))) {
			TPRINTF("could not create thread %u\n", 2 * i);
			break;
		}
		thread_ready(t);
		total++;
		
		if (!(t = thread_create(pi, NULL, TASK, 0, "pi", false))) {
			TPRINTF("could not create thread %u\n", 2 * i + 1);
			break;
		}
		thread_ready(t);
		total++;
	}
	
	TPRINTF("ok\n");
	
	thread_sleep(1);
	waitq_wakeup(&can_start, WAKEUP_ALL);
	
	while (atomic_get(&threads_ok) != total) {
		TPRINTF("Threads left: %d\n", total - atomic_get(&threads_ok));
		thread_sleep(1);
	}
	
	if (atomic_get(&threads_fault) == 0)
		return NULL;
	
	return "Test failed";
}
