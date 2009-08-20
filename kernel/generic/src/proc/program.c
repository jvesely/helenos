/*
 * Copyright (c) 2001-2004 Jakub Jermar
 * Copyright (c) 2008 Jiri Svoboda
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

/** @addtogroup genericproc
 * @{
 */

/**
 * @file
 * @brief	Running userspace programs.
 */

#include <main/uinit.h>
#include <proc/thread.h>
#include <proc/task.h>
#include <proc/uarg.h>
#include <mm/as.h>
#include <mm/slab.h>
#include <arch.h>
#include <adt/list.h>
#include <ipc/ipc.h>
#include <ipc/ipcrsc.h>
#include <security/cap.h>
#include <lib/elf.h>
#include <errno.h>
#include <syscall/copy.h>
#include <proc/program.h>

#ifndef LOADED_PROG_STACK_PAGES_NO
#define LOADED_PROG_STACK_PAGES_NO 1
#endif

/**
 * Points to the binary image used as the program loader. All non-initial
 * tasks are created from this executable image.
 */
void *program_loader = NULL;

/** Create a program using an existing address space.
 *
 * @param as		Address space containing a binary program image.
 * @param entry_addr	Program entry-point address in program address space.
 * @param name		Name to set for the program's task.
 * @param p		Buffer for storing program information.
 */
void program_create(as_t *as, uintptr_t entry_addr, char *name, program_t *p)
{
	as_area_t *a;
	uspace_arg_t *kernel_uarg;

	kernel_uarg = (uspace_arg_t *) malloc(sizeof(uspace_arg_t), 0);
	kernel_uarg->uspace_entry = (void *) entry_addr;
	kernel_uarg->uspace_stack = (void *) USTACK_ADDRESS;
	kernel_uarg->uspace_thread_function = NULL;
	kernel_uarg->uspace_thread_arg = NULL;
	kernel_uarg->uspace_uarg = NULL;
	
	p->task = task_create(as, name);
	ASSERT(p->task);

	/*
	 * Create the data as_area.
	 */
	a = as_area_create(as, AS_AREA_READ | AS_AREA_WRITE | AS_AREA_CACHEABLE,
	    LOADED_PROG_STACK_PAGES_NO * PAGE_SIZE, USTACK_ADDRESS,
	    AS_AREA_ATTR_NONE, &anon_backend, NULL);

	/*
	 * Create the main thread.
	 */
	p->main_thread = thread_create(uinit, kernel_uarg, p->task,
	    THREAD_FLAG_USPACE, "uinit", false);
	ASSERT(p->main_thread);
}

/** Parse an executable image in the kernel memory.
 *
 * If the image belongs to a program loader, it is registered as such,
 * (and *task is set to NULL). Otherwise a task is created from the
 * executable image. The task is returned in *task.
 *
 * @param image_addr	Address of an executable program image.
 * @param name		Name to set for the program's task.
 * @param p		Buffer for storing program info. If image_addr
 *			points to a loader image, p->task will be set to
 *			NULL and EOK will be returned.
 *
 * @return EOK on success or negative error code.
 */
int program_create_from_image(void *image_addr, char *name, program_t *p)
{
	as_t *as;
	unsigned int rc;

	as = as_create(0);
	ASSERT(as);

	rc = elf_load((elf_header_t *) image_addr, as, 0);
	if (rc != EE_OK) {
		as_destroy(as);
		p->task = NULL;
		p->main_thread = NULL;
		if (rc != EE_LOADER)
			return ENOTSUP;
		
		/* Register image as the program loader */
		ASSERT(program_loader == NULL);
		program_loader = image_addr;
		return EOK;
	}

	program_create(as, ((elf_header_t *) image_addr)->e_entry, name, p);

	return EOK;
}

/** Create a task from the program loader image.
 *
 * @param p	Buffer for storing program info.
 * @param name	Name to set for the program's task.
 *
 * @return EOK on success or negative error code.
 */
int program_create_loader(program_t *p, char *name)
{
	as_t *as;
	unsigned int rc;
	void *loader;

	as = as_create(0);
	ASSERT(as);

	loader = program_loader;
	if (!loader) return ENOENT;

	rc = elf_load((elf_header_t *) program_loader, as, ELD_F_LOADER);
	if (rc != EE_OK) {
		as_destroy(as);
		return ENOENT;
	}

	program_create(as, ((elf_header_t *) program_loader)->e_entry,
	    name, p);

	return EOK;
}

/** Make program ready.
 *
 * Switch program's main thread to the ready state.
 *
 * @param p Program to make ready.
 */
void program_ready(program_t *p)
{
	thread_ready(p->main_thread);
}

/** Syscall for creating a new loader instance from userspace.
 *
 * Creates a new task from the program loader image and sets
 * the task name.
 *
 * @param name			Name to set on the new task (typically the same
 *				as the command used to execute it).
 *
 * @return 0 on success or an error code from @ref errno.h.
 */
unative_t sys_program_spawn_loader(char *uspace_name, size_t name_len)
{
	program_t p;
	int rc;
	char namebuf[TASK_NAME_BUFLEN];

	/* Cap length of name and copy it from userspace. */

	if (name_len > TASK_NAME_BUFLEN - 1)
		name_len = TASK_NAME_BUFLEN - 1;

	rc = copy_from_uspace(namebuf, uspace_name, name_len);
	if (rc != 0)
		return (unative_t) rc;

	namebuf[name_len] = 0;

	/* Spawn the new task. */

	rc = program_create_loader(&p, namebuf);
	if (rc != 0)
		return rc;

	// FIXME: control the capabilities
	cap_set(p.task, cap_get(TASK));

	program_ready(&p);

	return EOK;
}

/** @}
 */
