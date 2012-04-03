/*
 * Copyright (c) 2008 Lukas Mejdrech
 * Copyright (c) 2012 Jiri Svoboda
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

/** @addtogroup udp
 * @{
 */

/**
 * @file Socket provider
 */

#include <async.h>
#include <byteorder.h>
#include <errno.h>
#include <inet/inet.h>
#include <io/log.h>
#include <ipc/services.h>
#include <ipc/socket.h>
#include <net/modules.h>
#include <net/socket.h>
#include <ns.h>

#include "sock.h"
#include "std.h"
#include "udp_type.h"
#include "ucall.h"

#define FRAGMENT_SIZE 1024

/** Free ports pool start. */
#define UDP_FREE_PORTS_START		1025

/** Free ports pool end. */
#define UDP_FREE_PORTS_END		65535

static int last_used_port = UDP_FREE_PORTS_START - 1;
static socket_ports_t gsock;

static void udp_sock_connection(ipc_callid_t iid, ipc_call_t *icall, void *arg);

int udp_sock_init(void)
{
	int rc;

	socket_ports_initialize(&gsock);

	async_set_client_connection(udp_sock_connection);

	rc = service_register(SERVICE_UDP);
	if (rc != EOK)
		return EEXIST;

	return EOK;
}

static void udp_free_sock_data(socket_core_t *sock_core)
{
	udp_sockdata_t *socket;

	socket = (udp_sockdata_t *)sock_core->specific_data;
	(void)socket;
}

static void udp_sock_notify_data(socket_core_t *sock_core)
{
	log_msg(LVL_DEBUG, "udp_sock_notify_data(%d)", sock_core->socket_id);
	async_exch_t *exch = async_exchange_begin(sock_core->sess);
	async_msg_5(exch, NET_SOCKET_RECEIVED, (sysarg_t)sock_core->socket_id,
	    FRAGMENT_SIZE, 0, 0, 1);
	async_exchange_end(exch);
}

static void udp_sock_socket(udp_client_t *client, ipc_callid_t callid, ipc_call_t call)
{
	udp_sockdata_t *sock;
	socket_core_t *sock_core;
	int sock_id;
	int rc;
	ipc_call_t answer;

	log_msg(LVL_DEBUG, "udp_sock_socket()");
	sock = calloc(sizeof(udp_sockdata_t), 1);
	if (sock == NULL) {
		async_answer_0(callid, ENOMEM);
		return;
	}

	fibril_mutex_initialize(&sock->lock);
	sock->client = client;
	sock->laddr.ipv4 = UDP_IPV4_ANY;

	rc = udp_uc_create(&sock->assoc);
	if (rc != EOK) {
		udp_uc_destroy(sock->assoc);
		free(sock);
		async_answer_0(callid, rc);
		return;
	}

	sock_id = SOCKET_GET_SOCKET_ID(call);
	rc = socket_create(&client->sockets, client->sess, sock, &sock_id);
	if (rc != EOK) {
		async_answer_0(callid, rc);
		return;
	}

	sock_core = socket_cores_find(&client->sockets, sock_id);
	assert(sock_core != NULL);
	sock->sock_core = sock_core;


	refresh_answer(&answer, NULL);
	SOCKET_SET_SOCKET_ID(answer, sock_id);

	SOCKET_SET_DATA_FRAGMENT_SIZE(answer, FRAGMENT_SIZE);
	SOCKET_SET_HEADER_SIZE(answer, sizeof(udp_header_t));
	answer_call(callid, EOK, &answer, 3);
}

static void udp_sock_bind(udp_client_t *client, ipc_callid_t callid, ipc_call_t call)
{
	int rc;
	struct sockaddr_in *addr;
	size_t addr_size;
	socket_core_t *sock_core;
	udp_sockdata_t *socket;
	udp_sock_t fsock;
	udp_error_t urc;

	log_msg(LVL_DEBUG, "udp_sock_bind()");
	log_msg(LVL_DEBUG, " - async_data_write_accept");

	addr = NULL;

	rc = async_data_write_accept((void **) &addr, false, 0, 0, 0, &addr_size);
	if (rc != EOK) {
		async_answer_0(callid, rc);
		goto out;
	}

	log_msg(LVL_DEBUG, " - call socket_bind");
	rc = socket_bind(&client->sockets, &gsock, SOCKET_GET_SOCKET_ID(call),
	    addr, addr_size, UDP_FREE_PORTS_START, UDP_FREE_PORTS_END,
	    last_used_port);
	if (rc != EOK) {
		async_answer_0(callid, rc);
		goto out;
	}

	if (addr_size != sizeof(struct sockaddr_in)) {
		async_answer_0(callid, EINVAL);
		goto out;
	}

	log_msg(LVL_DEBUG, " - call socket_cores_find");
	sock_core = socket_cores_find(&client->sockets, SOCKET_GET_SOCKET_ID(call));
	if (sock_core == NULL) {
		async_answer_0(callid, ENOENT);
		goto out;
	}

	socket = (udp_sockdata_t *)sock_core->specific_data;

	fsock.addr.ipv4 = uint32_t_be2host(addr->sin_addr.s_addr);
	fsock.port = uint16_t_be2host(sock_core->port);
	urc = udp_uc_set_local(socket->assoc, &fsock);

	switch (urc) {
	case UDP_EOK:
		rc = EOK;
		break;
/*	case TCP_ENOTEXIST:
		rc = ENOTCONN;
		break;
	case TCP_ECLOSING:
		rc = ENOTCONN;
		break;
	case TCP_ERESET:
		rc = ECONNABORTED;
		break;*/
	default:
		assert(false);
	}

	log_msg(LVL_DEBUG, " - success");
	async_answer_0(callid, rc);
out:
	if (addr != NULL)
		free(addr);
}

static void udp_sock_listen(udp_client_t *client, ipc_callid_t callid, ipc_call_t call)
{
	log_msg(LVL_DEBUG, "udp_sock_listen()");
	async_answer_0(callid, ENOTSUP);
}

static void udp_sock_connect(udp_client_t *client, ipc_callid_t callid, ipc_call_t call)
{
	log_msg(LVL_DEBUG, "udp_sock_connect()");
	async_answer_0(callid, ENOTSUP);
}

static void udp_sock_accept(udp_client_t *client, ipc_callid_t callid, ipc_call_t call)
{
	log_msg(LVL_DEBUG, "udp_sock_accept()");
	async_answer_0(callid, ENOTSUP);
}

static void udp_sock_sendto(udp_client_t *client, ipc_callid_t callid, ipc_call_t call)
{
	int socket_id;
	int fragments;
	int index;
	struct sockaddr_in *addr;
	size_t addr_size;
	socket_core_t *sock_core;
	udp_sockdata_t *socket;
	udp_sock_t fsock, *fsockp;
	ipc_call_t answer;
	ipc_callid_t wcallid;
	size_t length;
	uint8_t buffer[FRAGMENT_SIZE];
	udp_error_t urc;
	int rc;

	log_msg(LVL_DEBUG, "udp_sock_send()");

	addr = NULL;

	if (IPC_GET_IMETHOD(call) == NET_SOCKET_SENDTO) {
		rc = async_data_write_accept((void **) &addr, false,
		    0, 0, 0, &addr_size);
		if (rc != EOK) {
			async_answer_0(callid, rc);
			goto out;
		}

		if (addr_size != sizeof(struct sockaddr_in)) {
			async_answer_0(callid, EINVAL);
			goto out;
		}

		fsock.addr.ipv4 = uint32_t_be2host(addr->sin_addr.s_addr);
		fsock.port = uint16_t_be2host(addr->sin_port);
		fsockp = &fsock;
	} else {
		fsockp = NULL;
	}

	socket_id = SOCKET_GET_SOCKET_ID(call);
	fragments = SOCKET_GET_DATA_FRAGMENTS(call);
	SOCKET_GET_FLAGS(call);

	sock_core = socket_cores_find(&client->sockets, socket_id);
	if (sock_core == NULL) {
		async_answer_0(callid, ENOTSOCK);
		goto out;
	}

	if (sock_core->port == 0) {
		/* Implicitly bind socket */
		rc = socket_bind(&client->sockets, &gsock, SOCKET_GET_SOCKET_ID(call),
		    addr, addr_size, UDP_FREE_PORTS_START, UDP_FREE_PORTS_END,
		    last_used_port);
		if (rc != EOK) {
			async_answer_0(callid, rc);
			goto out;
		}
	}

	socket = (udp_sockdata_t *)sock_core->specific_data;
	fibril_mutex_lock(&socket->lock);

	assert(socket->assoc != NULL);

	for (index = 0; index < fragments; index++) {
		if (!async_data_write_receive(&wcallid, &length)) {
			fibril_mutex_unlock(&socket->lock);
			async_answer_0(callid, EINVAL);
			goto out;
		}

		if (length > FRAGMENT_SIZE)
			length = FRAGMENT_SIZE;

		rc = async_data_write_finalize(wcallid, buffer, length);
		if (rc != EOK) {
			fibril_mutex_unlock(&socket->lock);
			async_answer_0(callid, rc);
			goto out;
		}

		urc = udp_uc_send(socket->assoc, fsockp, buffer, length, 0);

		switch (urc) {
		case UDP_EOK:
			rc = EOK;
			break;
/*		case TCP_ENOTEXIST:
			rc = ENOTCONN;
			break;
		case TCP_ECLOSING:
			rc = ENOTCONN;
			break;
		case TCP_ERESET:
			rc = ECONNABORTED;
			break;*/
		default:
			assert(false);
		}

		if (rc != EOK) {
			fibril_mutex_unlock(&socket->lock);
			async_answer_0(callid, rc);
			goto out;
		}
	}

	refresh_answer(&answer, NULL);
	SOCKET_SET_DATA_FRAGMENT_SIZE(answer, FRAGMENT_SIZE);
	answer_call(callid, EOK, &answer, 2);
	fibril_mutex_unlock(&socket->lock);
out:
	if (addr != NULL)
		free(addr);
}

static void udp_sock_recvfrom(udp_client_t *client, ipc_callid_t callid, ipc_call_t call)
{
	int socket_id;
	int flags;
	size_t addr_length, length;
	socket_core_t *sock_core;
	udp_sockdata_t *socket;
	ipc_call_t answer;
	ipc_callid_t rcallid;
	uint8_t buffer[FRAGMENT_SIZE];
	size_t data_len;
	xflags_t xflags;
	udp_error_t urc;
	struct sockaddr_in addr;
	udp_sock_t rsock;
	int rc;

	log_msg(LVL_DEBUG, "%p: udp_sock_recv[from]()", client);

	socket_id = SOCKET_GET_SOCKET_ID(call);
	flags = SOCKET_GET_FLAGS(call);

	sock_core = socket_cores_find(&client->sockets, socket_id);
	if (sock_core == NULL) {
		async_answer_0(callid, ENOTSOCK);
		return;
	}

	socket = (udp_sockdata_t *)sock_core->specific_data;
	fibril_mutex_lock(&socket->lock);

	if (socket->assoc == NULL) {
		fibril_mutex_unlock(&socket->lock);
		async_answer_0(callid, ENOTCONN);
		return;
	}

	(void)flags;

	urc = udp_uc_receive(socket->assoc, buffer, FRAGMENT_SIZE, &data_len,
	    &xflags, &rsock);
	log_msg(LVL_DEBUG, "**** udp_uc_receive done");

	switch (urc) {
	case UDP_EOK:
		rc = EOK;
		break;
/*	case TCP_ENOTEXIST:
	case TCP_ECLOSING:
		rc = ENOTCONN;
		break;
	case TCP_ERESET:
		rc = ECONNABORTED;
		break;*/
	default:
		assert(false);
	}

	log_msg(LVL_DEBUG, "**** udp_uc_receive -> %d", rc);
	if (rc != EOK) {
		fibril_mutex_unlock(&socket->lock);
		async_answer_0(callid, rc);
		return;
	}

	if (IPC_GET_IMETHOD(call) == NET_SOCKET_RECVFROM) {
		/* Fill addr */
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = host2uint32_t_be(rsock.addr.ipv4);
		addr.sin_port = host2uint16_t_be(rsock.port);

		log_msg(LVL_DEBUG, "addr read receive");
		if (!async_data_read_receive(&rcallid, &addr_length)) {
			fibril_mutex_unlock(&socket->lock);
			async_answer_0(callid, EINVAL);
			return;
		}

		if (addr_length > sizeof(addr))
			addr_length = sizeof(addr);

		log_msg(LVL_DEBUG, "addr read finalize");
		rc = async_data_read_finalize(rcallid, &addr, addr_length);
		if (rc != EOK) {
			fibril_mutex_unlock(&socket->lock);
			async_answer_0(callid, EINVAL);
			return;
		}
	}

	log_msg(LVL_DEBUG, "data read receive");
	if (!async_data_read_receive(&rcallid, &length)) {
		fibril_mutex_unlock(&socket->lock);
		async_answer_0(callid, EINVAL);
		return;
	}

	if (length > data_len)
		length = data_len;

	log_msg(LVL_DEBUG, "data read finalize");
	rc = async_data_read_finalize(rcallid, buffer, length);

	if (length < data_len && rc == EOK)
		rc = EOVERFLOW;

	SOCKET_SET_READ_DATA_LENGTH(answer, length);
	answer_call(callid, EOK, &answer, 1);

	/* Push one fragment notification to client's queue */
	udp_sock_notify_data(sock_core);
	fibril_mutex_unlock(&socket->lock);
}

static void udp_sock_close(udp_client_t *client, ipc_callid_t callid, ipc_call_t call)
{
	int socket_id;
	socket_core_t *sock_core;
	udp_sockdata_t *socket;
	int rc;

	log_msg(LVL_DEBUG, "tcp_sock_close()");
	socket_id = SOCKET_GET_SOCKET_ID(call);

	sock_core = socket_cores_find(&client->sockets, socket_id);
	if (sock_core == NULL) {
		async_answer_0(callid, ENOTSOCK);
		return;
	}

	socket = (udp_sockdata_t *)sock_core->specific_data;
	fibril_mutex_lock(&socket->lock);

	assert(socket->assoc != NULL);
	udp_uc_destroy(socket->assoc);

	rc = socket_destroy(NULL, socket_id, &client->sockets, &gsock,
	    udp_free_sock_data);
	if (rc != EOK) {
		fibril_mutex_unlock(&socket->lock);
		async_answer_0(callid, rc);
		return;
	}

	fibril_mutex_unlock(&socket->lock);
	async_answer_0(callid, EOK);
}

static void udp_sock_getsockopt(udp_client_t *client, ipc_callid_t callid, ipc_call_t call)
{
	log_msg(LVL_DEBUG, "udp_sock_getsockopt()");
	async_answer_0(callid, ENOTSUP);
}

static void udp_sock_setsockopt(udp_client_t *client, ipc_callid_t callid, ipc_call_t call)
{
	log_msg(LVL_DEBUG, "udp_sock_setsockopt()");
	async_answer_0(callid, ENOTSUP);
}

static void udp_sock_connection(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	ipc_callid_t callid;
	ipc_call_t call;
	udp_client_t client;

	/* Accept the connection */
	async_answer_0(iid, EOK);

	client.sess = async_callback_receive(EXCHANGE_SERIALIZE);
	socket_cores_initialize(&client.sockets);

	while (true) {
		callid = async_get_call(&call);
		if (!IPC_GET_IMETHOD(call))
			break;

		log_msg(LVL_DEBUG, "udp_sock_connection: METHOD=%d\n",
		    (int)IPC_GET_IMETHOD(call));

		switch (IPC_GET_IMETHOD(call)) {
		case NET_SOCKET:
			udp_sock_socket(&client, callid, call);
			break;
		case NET_SOCKET_BIND:
			udp_sock_bind(&client, callid, call);
			break;
		case NET_SOCKET_LISTEN:
			udp_sock_listen(&client, callid, call);
			break;
		case NET_SOCKET_CONNECT:
			udp_sock_connect(&client, callid, call);
			break;
		case NET_SOCKET_ACCEPT:
			udp_sock_accept(&client, callid, call);
			break;
		case NET_SOCKET_SEND:
		case NET_SOCKET_SENDTO:
			udp_sock_sendto(&client, callid, call);
			break;
		case NET_SOCKET_RECV:
		case NET_SOCKET_RECVFROM:
			udp_sock_recvfrom(&client, callid, call);
			break;
		case NET_SOCKET_CLOSE:
			udp_sock_close(&client, callid, call);
			break;
		case NET_SOCKET_GETSOCKOPT:
			udp_sock_getsockopt(&client, callid, call);
			break;
		case NET_SOCKET_SETSOCKOPT:
			udp_sock_setsockopt(&client, callid, call);
			break;
		default:
			async_answer_0(callid, ENOTSUP);
			break;
		}
	}
}

/**
 * @}
 */
