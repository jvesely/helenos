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
/** @file
 */

#ifndef TCP_TYPE_H
#define TCP_TYPE_H

#include <adt/list.h>
#include <sys/types.h>

struct tcp_conn;

typedef enum {
	/** Listen */
	st_listen,
	/** Syn-sent */
	st_syn_sent,
	/** Syn-received */
	st_syn_received,
	/** Established */
	st_established,
	/** Fin-wait-1 */
	st_fin_wait_1,
	/** Fin-wait-2 */
	st_fin_wait_2,
	/** Close-wait */
	st_close_wait,
	/** Closing */
	st_closing,
	/** Last-ack */
	st_last_ack,
	/** Time-wait */
	st_time_wait,
	/** Closed */
	st_closed
} tcp_cstate_t;

typedef enum {
	XF_PUSH		= 0x1,
	XF_URGENT	= 0x2
} xflags_t;

typedef enum {
	CTL_SYN		= 0x1,
	CTL_FIN		= 0x2,
	CTL_RST		= 0x4,
	CTL_ACK		= 0x8
} tcp_control_t;

typedef struct {
	uint32_t ipv4;
} netaddr_t;

typedef struct {
	netaddr_t addr;
	uint16_t port;
} tcp_sock_t;

typedef struct {
	tcp_sock_t local;
	tcp_sock_t foreign;
} tcp_sockpair_t;

typedef struct {
	struct tcp_conn *conn;
	list_t list;
} tcp_iqueue_t;

typedef struct tcp_conn {
	link_t link;

	/** Connection identification (local and foreign socket) */
	tcp_sockpair_t ident;

	/** Connection state */
	tcp_cstate_t cstate;

	/** Queue of incoming segments */
	tcp_iqueue_t incoming;

	/** Receive buffer */
	uint8_t *rcv_buf;
	/** Receive buffer size */
	size_t rcv_buf_size;
	size_t rcv_buf_used;

	/** Send unacknowledged */
	uint32_t snd_una;
	/** Send next */
	uint32_t snd_nxt;
	/** Send window */
	uint32_t snd_wnd;
	/** Send urgent pointer */
	uint32_t snd_up;
	/** Segment sequence number used for last window update */
	uint32_t snd_wl1;
	/** Segment acknowledgement number used for last window update */
	uint32_t snd_wl2;
	/** Initial send sequence number */
	uint32_t iss;

	/** Receive next */
	uint32_t rcv_nxt;
	/** Receive window */
	uint32_t rcv_wnd;
	/** Receive urgent pointer */
	uint32_t rcv_up;
	/** Initial receive sequence number */
	uint32_t irs;
} tcp_conn_t;

typedef struct {
	unsigned dummy;
} tcp_conn_status_t;

typedef struct {
	/** SYN, FIN */
	tcp_control_t ctrl;

	/** Segment sequence number */
	uint32_t seq;
	/** Segment acknowledgement number */
	uint32_t ack;
	/** Segment length in sequence space */
	uint32_t len;
	/** Segment window */
	uint32_t wnd;
	/** Segment urgent pointer */
	uint32_t up;

	/** Segment data, may be moved when trimming segment */
	void *data;
	/** Segment data, original pointer used to free data */
	void *dfptr;
} tcp_segment_t;

typedef enum {
	ap_active,
	ap_passive
} acpass_t;

typedef struct {
	link_t link;
	tcp_sockpair_t sp;
	tcp_segment_t *seg;
} tcp_rqueue_entry_t;

typedef struct {
	link_t link;
	tcp_segment_t *seg;
} tcp_iqueue_entry_t;

typedef enum {
	cp_continue,
	cp_done
} cproc_t;

#endif

/** @}
 */
