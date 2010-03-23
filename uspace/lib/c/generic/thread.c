/*
 * Copyright (c) 2006 Jakub Jermar
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

#include <thread.h>
#include <libc.h>
#include <stdlib.h>
#include <libarch/faddr.h>
#include <kernel/proc/uarg.h>
#include <fibril.h>
#include <str.h>
#include <async.h>

#ifndef THREAD_INITIAL_STACK_PAGES_NO
#define THREAD_INITIAL_STACK_PAGES_NO 1
#endif

/** Main thread function.
 *
 * This function is called from __thread_entry() and is used
 * to call the thread's implementing function and perform cleanup
 * and exit when thread returns back. Do not call this function
 * directly.
 *
 * @param uarg Pointer to userspace argument structure.
 */
void __thread_main(uspace_arg_t *uarg)
{
	fibril_t *f;

	f = fibril_setup();
	__tcb_set(f->tcb);

	uarg->uspace_thread_function(uarg->uspace_thread_arg);
	/* XXX: we cannot free the userspace stack while running on it */
//	free(uarg->uspace_stack);
//	free(uarg);

	/* If there is a manager, destroy it */
	async_destroy_manager();
	fibril_teardown(f);

	thread_exit(0);
}

/** Create userspace thread.
 *
 * This function creates new userspace thread and allocates userspace
 * stack and userspace argument structure for it.
 *
 * @param function Function implementing the thread.
 * @param arg Argument to be passed to thread.
 * @param name Symbolic name of the thread.
 * @param tid Thread ID of the newly created thread.
 *
 * @return Zero on success or a code from @ref errno.h on failure.
 */
int thread_create(void (* function)(void *), void *arg, const char *name,
    thread_id_t *tid)
{
	char *stack;
	uspace_arg_t *uarg;
	int rc;

	stack = (char *) malloc(getpagesize() * THREAD_INITIAL_STACK_PAGES_NO);
	if (!stack)
		return -1;
		
	uarg = (uspace_arg_t *) malloc(sizeof(uspace_arg_t));
	if (!uarg) {
		free(stack);
		return -1;
	}
	
	uarg->uspace_entry = (void *) FADDR(__thread_entry);
	uarg->uspace_stack = (void *) stack;
	uarg->uspace_thread_function = function;
	uarg->uspace_thread_arg = arg;
	uarg->uspace_uarg = uarg;
	
	rc = __SYSCALL4(SYS_THREAD_CREATE, (sysarg_t) uarg, (sysarg_t) name,
	    (sysarg_t) str_size(name), (sysarg_t) tid);
	
	if (rc) {
		/*
		 * Failed to create a new thread.
		 * Free up the allocated structures.
		 */
		free(uarg);
		free(stack);
	}

	return rc;
}

/** Terminate current thread.
 *
 * @param status Exit status. Currently not used.
 */
void thread_exit(int status)
{
	__SYSCALL1(SYS_THREAD_EXIT, (sysarg_t) status);
	for (;;)
		;
}

/** Detach thread.
 *
 * Currently not implemented.
 *
 * @param thread TID.
 */
void thread_detach(thread_id_t thread)
{
}

/** Join thread.
 *
 * Currently not implemented.
 *
 * @param thread TID.
 *
 * @return Thread exit status.
 */
int thread_join(thread_id_t thread)
{
	return 0;
}

/** Get current thread ID.
 *
 * @return Current thread ID.
 */
thread_id_t thread_get_id(void)
{
	thread_id_t thread_id;

	(void) __SYSCALL1(SYS_THREAD_GET_ID, (sysarg_t) &thread_id);

	return thread_id;
}

/** @}
 */
