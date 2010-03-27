/*
 * Copyright (c) 2001-2004 Jakub Jermar
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

/** @addtogroup main
 * @{
 */

/**
 * @file
 * @brief Kernel initialization thread.
 *
 * This file contains kinit kernel thread which carries out
 * high level system initialization.
 *
 * This file is responsible for finishing SMP configuration
 * and creation of userspace init tasks.
 */

#include <main/kinit.h>
#include <config.h>
#include <arch.h>
#include <proc/scheduler.h>
#include <proc/task.h>
#include <proc/thread.h>
#include <proc/program.h>
#include <panic.h>
#include <func.h>
#include <cpu.h>
#include <arch/asm.h>
#include <mm/page.h>
#include <arch/mm/page.h>
#include <mm/as.h>
#include <mm/frame.h>
#include <print.h>
#include <memstr.h>
#include <console/console.h>
#include <interrupt.h>
#include <console/kconsole.h>
#include <security/cap.h>
#include <lib/rd.h>
#include <ipc/ipc.h>
#include <debug.h>
#include <str.h>

#ifdef CONFIG_SMP
#include <smp/smp.h>
#endif /* CONFIG_SMP */

#include <synch/waitq.h>
#include <synch/spinlock.h>

#define ALIVE_CHARS  4

#ifdef CONFIG_KCONSOLE
static char alive[ALIVE_CHARS] = "-\\|/";
#endif

#define INIT_PREFIX      "init:"
#define INIT_PREFIX_LEN  5

/** Kernel initialization thread.
 *
 * kinit takes care of higher level kernel
 * initialization (i.e. thread creation,
 * userspace initialization etc.).
 *
 * @param arg Not used.
 */
void kinit(void *arg)
{
#if defined(CONFIG_SMP) || defined(CONFIG_KCONSOLE)
	thread_t *thread;
#endif
	
	/*
	 * Detach kinit as nobody will call thread_join_timeout() on it.
	 */
	thread_detach(THREAD);
	
	interrupts_disable();
	
#ifdef CONFIG_SMP
	if (config.cpu_count > 1) {
		waitq_initialize(&ap_completion_wq);
		/*
		 * Create the kmp thread and wait for its completion.
		 * cpu1 through cpuN-1 will come up consecutively and
		 * not mess together with kcpulb threads.
		 * Just a beautification.
		 */
		thread = thread_create(kmp, NULL, TASK, THREAD_FLAG_WIRED, "kmp", true);
		if (thread != NULL) {
			spinlock_lock(&thread->lock);
			thread->cpu = &cpus[0];
			spinlock_unlock(&thread->lock);
			thread_ready(thread);
		} else
			panic("Unable to create kmp thread.");
		thread_join(thread);
		thread_detach(thread);
	}
	
	if (config.cpu_count > 1) {
		size_t i;
		
		/*
		 * For each CPU, create its load balancing thread.
		 */
		for (i = 0; i < config.cpu_count; i++) {
			thread = thread_create(kcpulb, NULL, TASK, THREAD_FLAG_WIRED, "kcpulb", true);
			if (thread != NULL) {
				spinlock_lock(&thread->lock);
				thread->cpu = &cpus[i];
				spinlock_unlock(&thread->lock);
				thread_ready(thread);
			} else
				printf("Unable to create kcpulb thread for cpu" PRIs "\n", i);
		}
	}
#endif /* CONFIG_SMP */
	
	/*
	 * At this point SMP, if present, is configured.
	 */
	arch_post_smp_init();
	
#ifdef CONFIG_KCONSOLE
	if (stdin) {
		/*
		 * Create kernel console.
		 */
		thread = thread_create(kconsole_thread, NULL, TASK, 0, "kconsole", false);
		if (thread != NULL)
			thread_ready(thread);
		else
			printf("Unable to create kconsole thread\n");
	}
#endif /* CONFIG_KCONSOLE */
	
	interrupts_enable();
	
	/*
	 * Create user tasks, load RAM disk images.
	 */
	size_t i;
	program_t programs[CONFIG_INIT_TASKS];
	
	for (i = 0; i < init.cnt; i++) {
		if (init.tasks[i].addr % FRAME_SIZE) {
			printf("init[%" PRIs "].addr is not frame aligned\n", i);
			continue;
		}
		
		/*
		 * Construct task name from the 'init:' prefix and the
		 * name stored in the init structure (if any).
		 */
		
		char namebuf[TASK_NAME_BUFLEN];
		
		const char *name = init.tasks[i].name;
		if (name[0] == 0)
			name = "<unknown>";
		
		ASSERT(TASK_NAME_BUFLEN >= INIT_PREFIX_LEN);
		str_cpy(namebuf, TASK_NAME_BUFLEN, INIT_PREFIX);
		str_cpy(namebuf + INIT_PREFIX_LEN,
		    TASK_NAME_BUFLEN - INIT_PREFIX_LEN, name);

		int rc = program_create_from_image((void *) init.tasks[i].addr,
		    namebuf, &programs[i]);
		
		if ((rc == 0) && (programs[i].task != NULL)) {
			/*
			 * Set capabilities to init userspace tasks.
			 */
			cap_set(programs[i].task, CAP_CAP | CAP_MEM_MANAGER |
			    CAP_IO_MANAGER | CAP_PREEMPT_CONTROL | CAP_IRQ_REG);
			
			if (!ipc_phone_0)
				ipc_phone_0 = &programs[i].task->answerbox;
		} else if (rc == 0) {
			/* It was the program loader and was registered */
		} else {
			/* RAM disk image */
			int rd = init_rd((rd_header_t *) init.tasks[i].addr, init.tasks[i].size);
			
			if (rd != RE_OK)
				printf("Init binary %" PRIs " not used (error %d)\n", i, rd);
		}
	}

	/*
	 * Run user tasks.
	 */
	for (i = 0; i < init.cnt; i++) {
		if (programs[i].task != NULL)
			program_ready(&programs[i]);
	}

#ifdef CONFIG_KCONSOLE
	if (!stdin) {
		thread_sleep(10);
		printf("kinit: No stdin\nKernel alive: .");
		
		unsigned int i = 0;
		while (true) {
			printf("\b%c", alive[i % ALIVE_CHARS]);
			thread_sleep(1);
			i++;
		}
	}
#endif /* CONFIG_KCONSOLE */
}

/** @}
 */
