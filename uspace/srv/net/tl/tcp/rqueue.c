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

#include <adt/prodcons.h>
#include <errno.h>
#include <io/log.h>
#include <stdlib.h>
#include <thread.h>
#include "rqueue.h"
#include "state.h"
#include "tcp_type.h"

static prodcons_t rqueue;

void tcp_rqueue_init(void)
{
	prodcons_initialize(&rqueue);
}

/** Bounce segment directy into receive queue without constructing the PDU.
 *
 * This is for testing purposes only.
 */
void tcp_rqueue_bounce_seg(tcp_sockpair_t *sp, tcp_segment_t *seg)
{
	tcp_sockpair_t rident;

	log_msg(LVL_DEBUG, "tcp_rqueue_bounce_seg()");

	/* Reverse the identification */
	rident.local = sp->foreign;
	rident.foreign = sp->local;

	tcp_rqueue_insert_seg(&rident, seg);
}

void tcp_rqueue_insert_seg(tcp_sockpair_t *sp, tcp_segment_t *seg)
{
	tcp_rqueue_entry_t *rqe;
	log_msg(LVL_DEBUG, "tcp_rqueue_insert_seg()");

	rqe = calloc(1, sizeof(tcp_rqueue_entry_t));
	if (rqe == NULL) {
		log_msg(LVL_ERROR, "Failed allocating RQE.");
		return;
	}

	rqe->sp = *sp;
	rqe->seg = seg;

	prodcons_produce(&rqueue, &rqe->link);
}

static void tcp_rqueue_thread(void *arg)
{
	link_t *link;
	tcp_rqueue_entry_t *rqe;

	log_msg(LVL_DEBUG, "tcp_rqueue_thread()");

	while (true) {
		link = prodcons_consume(&rqueue);
		rqe = list_get_instance(link, tcp_rqueue_entry_t, link);

		tcp_as_segment_arrived(&rqe->sp, rqe->seg);
	}
}

void tcp_rqueue_thread_start(void)
{
	thread_id_t tid;
        int rc;

	log_msg(LVL_DEBUG, "tcp_rqueue_thread_start()");

	rc = thread_create(tcp_rqueue_thread, NULL, "rqueue", &tid);
	if (rc != EOK) {
		log_msg(LVL_ERROR, "Failde creating rqueue thread.");
		return;
	}
}

/**
 * @}
 */
