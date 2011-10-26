/*
 * Copyright (c) 2011 Jiri Svoboda
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

/** @addtogroup tcp
 * @{
 */

/**
 * @file TCP (Transmission Control Protocol) network module
 */

#include <async.h>
#include <errno.h>
#include <io/log.h>
#include <stdio.h>
#include <task.h>

#include "ncsim.h"
#include "rqueue.h"
#include "test.h"

#define NAME       "tcp"

int main(int argc, char **argv)
{
	int rc;

	printf(NAME ": TCP (Transmission Control Protocol) network module\n");

	rc = log_init(NAME, LVL_DEBUG);
	if (rc != EOK) {
		printf(NAME ": Failed to initialize log.\n");
		return 1;
	}

	printf(NAME ": Accepting connections\n");
//	task_retval(0);

	tcp_rqueue_init();
	tcp_rqueue_thread_start();

	tcp_ncsim_init();
	tcp_ncsim_thread_start();

	tcp_test();

	async_manager();

	/* Not reached */
	return 0;
}

/**
 * @}
 */
