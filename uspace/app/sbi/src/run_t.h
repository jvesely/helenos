/*
 * Copyright (c) 2010 Jiri Svoboda
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

#ifndef RUN_T_H_
#define RUN_T_H_

#include "list_t.h"

/** Block activation record
 *
 * One block AR is created for each block that we enter. A variable declaration
 * statement inserts the variable here. Upon exiting the block we pop from the
 * stack, thus all the variables declared in that block are forgotten.
 */
typedef struct run_block_ar {
	/** Variables in this block */
	intmap_t vars; /* of rdata_var_t */
} run_block_ar_t;

/** Function activation record
 *
 * One is created whenever a function is invoked.
 */
typedef struct run_fun_ar {
	/** Definition of function being invoked */
	struct stree_symbol *fun_sym;

	/** Block activation records */
	list_t block_ar; /* of run_block_ar_t */
} run_fun_ar_t;

/** Thread activation record
 *
 * We can walk the list of function ARs to get a function call backtrace.
 */
typedef struct run_thread_ar {
	/** Function activation records. */
	list_t fun_ar; /* of run_fun_ar_t */
} run_thread_ar_t;

/** Runner state object */
typedef struct run {
	/** Code of the program being executed */
	struct stree_program *program;

	/** Thread-private state */
	run_thread_ar_t *thread_ar;
} run_t;

#endif
