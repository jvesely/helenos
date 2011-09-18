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
 * @file
 */

#include <async.h>
#include <errno.h>
#include <stdio.h>
#include <thread.h>
#include "state.h"
#include "tcp_type.h"

#include "test.h"

static void test_srv(void *arg)
{
	tcp_conn_t *conn;
	tcp_sock_t sock;

	printf("test_srv()\n");
	sock.port = 1024;
	sock.addr.ipv4 = 0x7f000001;
	tcp_uc_open(80, &sock, ap_passive, &conn);
}

static void test_cli(void *arg)
{
	tcp_conn_t *conn;
	tcp_sock_t sock;

	printf("test_cli()\n");

	sock.port = 80;
	sock.addr.ipv4 = 0x7f000001;
	
	async_usleep(1000*1000*3);
	tcp_uc_open(1024, &sock, ap_active, &conn);
}

void tcp_test(void)
{
	thread_id_t srv_tid;
	thread_id_t cli_tid;
	int rc;

	printf("tcp_test()\n");

	rc = thread_create(test_srv, NULL, "test_srv", &srv_tid);
	if (rc != EOK) {
		printf("Failed to create server thread.\n");
		return;
	}

	rc = thread_create(test_cli, NULL, "test_cli", &cli_tid);
	if (rc != EOK) {
		printf("Failed to create client thread.\n");
		return;
	}
}

/**
 * @}
 */
