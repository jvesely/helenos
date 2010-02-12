/*
 * Copyright (c) 2006 Ondrej Palkovsky
 * Copyright (c) 2007 Jakub Jermar
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

/** @addtogroup libc
 * @{
 */
/** @file
 */

#include <adt/list.h>
#include <fibril.h>
#include <thread.h>
#include <tls.h>
#include <malloc.h>
#include <unistd.h>
#include <stdio.h>
#include <arch/barrier.h>
#include <libarch/faddr.h>
#include <futex.h>
#include <assert.h>
#include <async.h>

#ifndef FIBRIL_INITIAL_STACK_PAGES_NO
#define FIBRIL_INITIAL_STACK_PAGES_NO	1
#endif

/**
 * This futex serializes access to ready_list, serialized_list and manager_list.
 */ 
static atomic_t fibril_futex = FUTEX_INITIALIZER;

static LIST_INITIALIZE(ready_list);
static LIST_INITIALIZE(serialized_list);
static LIST_INITIALIZE(manager_list);

static void fibril_main(void);

/** Number of threads that are executing a manager fibril. */
static int threads_in_manager;
/** Number of threads that are executing a manager fibril and are serialized. */
static int serialized_threads;	/* Protected by async_futex */
/** Fibril-local count of serialization. If > 0, we must not preempt */
static fibril_local int serialization_count;

/** Setup fibril information into TCB structure */
fibril_t *fibril_setup(void)
{
	fibril_t *f;
	tcb_t *tcb;

	tcb = __make_tls();
	if (!tcb)
		return NULL;

	f = malloc(sizeof(fibril_t));
	if (!f) {
		__free_tls(tcb);
		return NULL;
	}

	tcb->fibril_data = f;
	f->tcb = tcb;

	f->func = NULL;
	f->arg = NULL;
	f->stack = NULL;
	f->clean_after_me = NULL;
	f->retval = 0;
	f->flags = 0;

	return f;
}

void fibril_teardown(fibril_t *f)
{
	__free_tls(f->tcb);
	free(f);
}

/** Function that spans the whole life-cycle of a fibril.
 *
 * Each fibril begins execution in this function. Then the function implementing
 * the fibril logic is called.  After its return, the return value is saved.
 * The fibril then switches to another fibril, which cleans up after it.
 */
void fibril_main(void)
{
	fibril_t *f = __tcb_get()->fibril_data;

	/* Call the implementing function. */
	f->retval = f->func(f->arg);

	fibril_switch(FIBRIL_FROM_DEAD);
	/* not reached */
}

/** Switch from the current fibril.
 *
 * If calling with FIBRIL_TO_MANAGER parameter, the async_futex should be
 * held.
 *
 * @param stype		Switch type. One of FIBRIL_PREEMPT, FIBRIL_TO_MANAGER,
 * 			FIBRIL_FROM_MANAGER, FIBRIL_FROM_DEAD. The parameter
 * 			describes the circumstances of the switch.
 * @return		Return 0 if there is no ready fibril,
 * 			return 1 otherwise.
 */
int fibril_switch(fibril_switch_type_t stype)
{
	int retval = 0;
	
	futex_down(&fibril_futex);
	
	if (stype == FIBRIL_PREEMPT && list_empty(&ready_list))
		goto ret_0;
	
	if (stype == FIBRIL_FROM_MANAGER) {
		if ((list_empty(&ready_list)) && (list_empty(&serialized_list)))
			goto ret_0;
		
		/*
		 * Do not preempt if there is not enough threads to run the
		 * ready fibrils which are not serialized.
		 */
		if ((list_empty(&serialized_list)) &&
		    (threads_in_manager <= serialized_threads)) {
			goto ret_0;
		}
	}
	
	/* If we are going to manager and none exists, create it */
	if ((stype == FIBRIL_TO_MANAGER) || (stype == FIBRIL_FROM_DEAD)) {
		while (list_empty(&manager_list)) {
			futex_up(&fibril_futex);
			async_create_manager();
			futex_down(&fibril_futex);
		}
	}
	
	fibril_t *srcf = __tcb_get()->fibril_data;
	if (stype != FIBRIL_FROM_DEAD) {
		
		/* Save current state */
		if (!context_save(&srcf->ctx)) {
			if (serialization_count)
				srcf->flags &= ~FIBRIL_SERIALIZED;
			
			if (srcf->clean_after_me) {
				/*
				 * Cleanup after the dead fibril from which we
				 * restored context here.
				 */
				void *stack = srcf->clean_after_me->stack;
				if (stack) {
					/*
					 * This check is necessary because a
					 * thread could have exited like a
					 * normal fibril using the
					 * FIBRIL_FROM_DEAD switch type. In that
					 * case, its fibril will not have the
					 * stack member filled.
					 */
					free(stack);
				}
				fibril_teardown(srcf->clean_after_me);
				srcf->clean_after_me = NULL;
			}
			
			return 1;	/* futex_up already done here */
		}
		
		/* Save myself to the correct run list */
		if (stype == FIBRIL_PREEMPT)
			list_append(&srcf->link, &ready_list);
		else if (stype == FIBRIL_FROM_MANAGER) {
			list_append(&srcf->link, &manager_list);
			threads_in_manager--;
		} else {
			/*
			 * If stype == FIBRIL_TO_MANAGER, don't put ourselves to
			 * any list, we should already be somewhere, or we will
			 * be lost.
			 */
		}
	}
	
	/* Avoid srcf being clobbered by context_save() */
	srcf = __tcb_get()->fibril_data;
	
	/* Choose a new fibril to run */
	fibril_t *dstf;
	if ((stype == FIBRIL_TO_MANAGER) || (stype == FIBRIL_FROM_DEAD)) {
		dstf = list_get_instance(manager_list.next, fibril_t, link);
		if (serialization_count && stype == FIBRIL_TO_MANAGER) {
			serialized_threads++;
			srcf->flags |= FIBRIL_SERIALIZED;
		}
		threads_in_manager++;
		
		if (stype == FIBRIL_FROM_DEAD) 
			dstf->clean_after_me = srcf;
	} else {
		if (!list_empty(&serialized_list)) {
			dstf = list_get_instance(serialized_list.next, fibril_t,
			    link);
			serialized_threads--;
		} else {
			dstf = list_get_instance(ready_list.next, fibril_t,
			    link);
		}
	}
	list_remove(&dstf->link);
	
	futex_up(&fibril_futex);
	context_restore(&dstf->ctx);
	/* not reached */
	
ret_0:
	futex_up(&fibril_futex);
	return retval;
}

/** Create a new fibril.
 *
 * @param func		Implementing function of the new fibril.
 * @param arg		Argument to pass to func.
 *
 * @return		Return 0 on failure or TLS of the new fibril.
 */
fid_t fibril_create(int (*func)(void *), void *arg)
{
	fibril_t *f;

	f = fibril_setup();
	if (!f) 
		return 0;
	f->stack = (char *) malloc(FIBRIL_INITIAL_STACK_PAGES_NO *
	    getpagesize());
	if (!f->stack) {
		fibril_teardown(f);
		return 0;
	}
	
	f->func = func;
	f->arg = arg;

	context_save(&f->ctx);
	context_set(&f->ctx, FADDR(fibril_main), f->stack,
	    FIBRIL_INITIAL_STACK_PAGES_NO * getpagesize(), f->tcb);

	return (fid_t) f;
}

/** Add a fibril to the ready list.
 *
 * @param fid		Pointer to the fibril structure of the fibril to be
 *			added.
 */
void fibril_add_ready(fid_t fid)
{
	fibril_t *f;

	f = (fibril_t *) fid;
	futex_down(&fibril_futex);
	if ((f->flags & FIBRIL_SERIALIZED))
		list_append(&f->link, &serialized_list);
	else
		list_append(&f->link, &ready_list);
	futex_up(&fibril_futex);
}

/** Add a fibril to the manager list.
 *
 * @param fid		Pointer to the fibril structure of the fibril to be
 *			added.
 */
void fibril_add_manager(fid_t fid)
{
	fibril_t *f;

	f = (fibril_t *) fid;

	futex_down(&fibril_futex);
	list_append(&f->link, &manager_list);
	futex_up(&fibril_futex);
}

/** Remove one manager from the manager list. */
void fibril_remove_manager(void)
{
	futex_down(&fibril_futex);
	if (list_empty(&manager_list)) {
		futex_up(&fibril_futex);
		return;
	}
	list_remove(manager_list.next);
	futex_up(&fibril_futex);
}

/** Return fibril id of the currently running fibril.
 *
 * @return fibril ID of the currently running fibril.
 *
 */
fid_t fibril_get_id(void)
{
	return (fid_t) __tcb_get()->fibril_data;
}

/** Disable preemption
 *
 * If the fibril wants to send several message in a row and does not want to be
 * preempted, it should start async_serialize_start() in the beginning of
 * communication and async_serialize_end() in the end. If it is a true
 * multithreaded application, it should protect the communication channel by a
 * futex as well.
 *
 */
void fibril_inc_sercount(void)
{
	serialization_count++;
}

/** Restore the preemption counter to the previous state. */
void fibril_dec_sercount(void)
{
	serialization_count--;
}

/** @}
 */
