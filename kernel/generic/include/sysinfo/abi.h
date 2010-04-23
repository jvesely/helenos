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

/** @addtogroup generic
 * @{
 */
/** @file
 * Data structures passed between kernel sysinfo and user space.
 */

#ifndef KERN_ABI_H_
#define KERN_ABI_H_

/** Number of load components */
#define LOAD_STEPS        3

/** Maximum task name size */
#define TASK_NAME_BUFLEN  20

/** Thread states */
typedef enum {
	/** It is an error, if thread is found in this state. */
	Invalid,
	/** State of a thread that is currently executing on some CPU. */
	Running,
	/** Thread in this state is waiting for an event. */
	Sleeping,
	/** State of threads in a run queue. */
	Ready,
	/** Threads are in this state before they are first readied. */
	Entering,
	/** After a thread calls thread_exit(), it is put into Exiting state. */
	Exiting,
	/** Threads that were not detached but exited are Lingering. */
	Lingering
} state_t;

/** Statistics about a single CPU
 *
 */
typedef struct {
	unsigned int id;         /**< CPU ID as stored by kernel */
	bool active;             /**< CPU is activate */
	uint16_t frequency_mhz;  /**< Frequency in MHz */
	uint64_t idle_ticks;     /**< Number of idle kernel quanta */
	uint64_t busy_ticks;     /**< Number of busy kernel quanta */
} stats_cpu_t;

/** Physical memory statistics
 *
 */
typedef struct {
	uint64_t total;    /**< Total physical memory (bytes) */
	uint64_t unavail;  /**< Unavailable (reserved, firmware) bytes */
	uint64_t used;     /**< Allocated physical memory (bytes) */
	uint64_t free;     /**< Free physical memory (bytes) */
} stats_physmem_t;

/** IPC statistics
 *
 * Associated with a task.
 *
 */
typedef struct {
	uint64_t call_sent;           /**< IPC calls sent */
	uint64_t call_recieved;       /**< IPC calls received */
	uint64_t answer_sent;         /**< IPC answers sent */
	uint64_t answer_recieved;     /**< IPC answers received */
	uint64_t irq_notif_recieved;  /**< IPC IRQ notifications */
	uint64_t forwarded;           /**< IPC messages forwarded */
} stats_ipc_t;

/** Statistics about a single task
 *
 */
typedef struct {
	task_id_t task_id;            /**< Task ID */
	char name[TASK_NAME_BUFLEN];  /**< Task name (in kernel) */
	size_t virtmem;               /**< Size of VAS (bytes) */
	size_t threads;               /**< Number of threads */
	uint64_t ucycles;             /**< Number of CPU cycles in user space */
	uint64_t kcycles;             /**< Number of CPU cycles in kernel */
	stats_ipc_t ipc_info;         /**< IPC statistics */
} stats_task_t;

/** Statistics about a single thread
 *
 */
typedef struct {
	thread_id_t thread_id;  /**< Thread ID */
	task_id_t task_id;      /**< Associated task ID */
	state_t state;          /**< Thread state */
	int priority;           /**< Thread priority */
	uint64_t ucycles;       /**< Number of CPU cycles in user space */
	uint64_t kcycles;       /**< Number of CPU cycles in kernel */
	bool on_cpu;            /**< Associated with a CPU */
	unsigned int cpu;       /**< Associated CPU ID (if on_cpu is true) */
} stats_thread_t;

/** Load fixed-point value */
typedef uint32_t load_t;

#endif

/** @}
 */
