/*
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

/** @addtogroup inet
 * @{
 */
/**
 * @file
 * @brief Internet Protocol service
 */

#include <adt/list.h>
#include <async.h>
#include <errno.h>
#include <fibril_synch.h>
#include <io/log.h>
#include <ipc/inet.h>
#include <ipc/services.h>
#include <loc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "addrobj.h"
#include "inet.h"
#include "inet_link.h"

#define NAME "inet"

static void inet_client_conn(ipc_callid_t iid, ipc_call_t *icall, void *arg);

static FIBRIL_MUTEX_INITIALIZE(client_list_lock);
static LIST_INITIALIZE(client_list);

static int inet_init(void)
{
	service_id_t sid;
	int rc;

	log_msg(LVL_DEBUG, "inet_init()");

	async_set_client_connection(inet_client_conn);

	rc = loc_server_register(NAME);
	if (rc != EOK) {
		log_msg(LVL_ERROR, "Failed registering server (%d).", rc);
		return EEXIST;
	}

	rc = loc_service_register(SERVICE_NAME_INET, &sid);
	if (rc != EOK) {
		log_msg(LVL_ERROR, "Failed registering service (%d).", rc);
		return EEXIST;
	}

	rc = inet_link_discovery_start();
	if (rc != EOK)
		return EEXIST;

	return EOK;
}

static void inet_callback_create_srv(inet_client_t *client, ipc_callid_t callid,
    ipc_call_t *call)
{
	log_msg(LVL_DEBUG, "inet_callback_create_srv()");

	async_sess_t *sess = async_callback_receive(EXCHANGE_SERIALIZE);
	if (sess == NULL) {
		async_answer_0(callid, ENOMEM);
		return;
	}

	client->sess = sess;
	async_answer_0(callid, EOK);
}

static int inet_send(inet_client_t *client, inet_dgram_t *dgram,
    uint8_t ttl, int df)
{
	inet_addrobj_t *addr;

	addr = inet_addrobj_find(&dgram->dest);
	if (addr != NULL) {
		/* Destination is directly accessible */
		return inet_addrobj_send_dgram(addr, dgram, ttl, df);
	}

	/* TODO: Gateways */
	log_msg(LVL_DEBUG, "inet_send: No route to destination.");
	return ENOENT;
}

static void inet_get_srcaddr_srv(inet_client_t *client, ipc_callid_t callid,
    ipc_call_t *call)
{
	log_msg(LVL_DEBUG, "inet_get_srcaddr_srv()");

	async_answer_0(callid, ENOTSUP);
}

static void inet_send_srv(inet_client_t *client, ipc_callid_t callid,
    ipc_call_t *call)
{
	inet_dgram_t dgram;
	uint8_t ttl;
	int df;
	int rc;

	log_msg(LVL_DEBUG, "inet_send_srv()");

	dgram.src.ipv4 = IPC_GET_ARG1(*call);
	dgram.dest.ipv4 = IPC_GET_ARG2(*call);
	dgram.tos = IPC_GET_ARG3(*call);
	ttl = IPC_GET_ARG4(*call);
	df = IPC_GET_ARG5(*call);

	rc = async_data_write_accept(&dgram.data, false, 0, 0, 0, &dgram.size);
	if (rc != EOK) {
		async_answer_0(callid, rc);
		return;
	}

	rc = inet_send(client, &dgram, ttl, df);

	free(dgram.data);
	async_answer_0(callid, rc);
}

static void inet_set_proto_srv(inet_client_t *client, ipc_callid_t callid,
    ipc_call_t *call)
{
	sysarg_t proto;

	proto = IPC_GET_ARG1(*call);
	log_msg(LVL_DEBUG, "inet_set_proto_srv(%lu)", (unsigned long) proto);

	if (proto > UINT8_MAX) {
		async_answer_0(callid, EINVAL);
		return;
	}

	client->protocol = proto;
	async_answer_0(callid, EOK);
}

static void inet_client_init(inet_client_t *client)
{
	client->sess = NULL;

	fibril_mutex_lock(&client_list_lock);
	list_append(&client->client_list, &client_list);
	fibril_mutex_unlock(&client_list_lock);
}

static void inet_client_fini(inet_client_t *client)
{
	async_hangup(client->sess);
	client->sess = NULL;

	fibril_mutex_lock(&client_list_lock);
	list_remove(&client->client_list);
	fibril_mutex_unlock(&client_list_lock);
}

static void inet_client_conn(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	inet_client_t client;

	log_msg(LVL_DEBUG, "inet_client_conn()");

	/* Accept the connection */
	async_answer_0(iid, EOK);

	inet_client_init(&client);

	while (true) {
		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);
		sysarg_t method = IPC_GET_IMETHOD(call);

		if (!method) {
			/* The other side has hung up */
			async_answer_0(callid, EOK);
			return;
		}

		switch (method) {
		case INET_CALLBACK_CREATE:
			inet_callback_create_srv(&client, callid, &call);
			break;
		case INET_GET_SRCADDR:
			inet_get_srcaddr_srv(&client, callid, &call);
			break;
		case INET_SEND:
			inet_send_srv(&client, callid, &call);
			break;
		case INET_SET_PROTO:
			inet_set_proto_srv(&client, callid, &call);
			break;
		default:
			async_answer_0(callid, EINVAL);
		}
	}

	inet_client_fini(&client);
}

int inet_ev_recv(inet_client_t *client, inet_dgram_t *dgram)
{
	async_exch_t *exch = async_exchange_begin(client->sess);

	ipc_call_t answer;
	aid_t req = async_send_3(exch, INET_EV_RECV, dgram->src.ipv4,
	    dgram->dest.ipv4, dgram->tos, &answer);
	int rc = async_data_write_start(exch, dgram->data, dgram->size);
	async_exchange_end(exch);

	if (rc != EOK) {
		async_wait_for(req, NULL);
		return rc;
	}

	sysarg_t retval;
	async_wait_for(req, &retval);
	if (retval != EOK)
		return retval;

	return EOK;
}

int main(int argc, char *argv[])
{
	int rc;

	printf(NAME ": HelenOS Internet Protocol service\n");

	if (log_init(NAME, LVL_DEBUG) != EOK) {
		printf(NAME ": Failed to initialize logging.\n");
		return 1;
	}

	rc = inet_init();
	if (rc != EOK)
		return 1;

	printf(NAME ": Accepting connections.\n");
	task_retval(0);
	async_manager();

	/* Not reached */
	return 0;
}

/** @}
 */
