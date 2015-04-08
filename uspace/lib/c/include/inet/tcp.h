/*
 * Copyright (c) 2015 Jiri Svoboda
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

#ifndef LIBC_INET_TCP_H_
#define LIBC_INET_TCP_H_

#include <inet/addr.h>
#include <inet/endpoint.h>
#include <inet/inet.h>

typedef struct {
} tcp_conn_t;

typedef struct {
} tcp_listener_t;

typedef struct {
	void (*connected)(tcp_conn_t *);
	void (*conn_failed)(tcp_conn_t *);
	void (*conn_reset)(tcp_conn_t *);
	void (*data_avail)(tcp_conn_t *);
	void (*urg_data)(tcp_conn_t *);
} tcp_cb_t;

typedef struct {
	void (*new_conn)(tcp_listener_t *, tcp_conn_t *);
} tcp_listen_cb_t;

typedef struct {
} tcp_t;

extern int tcp_create(tcp_t **);
extern void tcp_destroy(tcp_t *);
extern int tcp_conn_create(tcp_t *, inet_ep2_t *, tcp_cb_t *, void *,
    tcp_conn_t **);
extern void tcp_conn_destroy(tcp_conn_t *);
extern void *tcp_conn_userptr(tcp_conn_t *);
extern int tcp_listener_create(tcp_t *, inet_ep_t *, tcp_listen_cb_t *, void *,
    tcp_cb_t *, void *, tcp_listener_t **);
extern void tcp_listener_destroy(tcp_listener_t *);
extern void *tcp_listener_userptr(tcp_listener_t *);

extern int tcp_conn_wait_connected(tcp_conn_t *);
extern int tcp_conn_send(tcp_conn_t *, const void *, size_t);
extern int tcp_conn_send_fin(tcp_conn_t *);
extern int tcp_conn_push(tcp_conn_t *);
extern void tcp_conn_reset(tcp_conn_t *);

extern int tcp_conn_recv(tcp_conn_t *, void *, size_t, size_t *);
extern int tcp_conn_recv_wait(tcp_conn_t *, void *, size_t, size_t *);


#endif

/** @}
 */
