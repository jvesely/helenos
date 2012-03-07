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
 * @brief
 */

#include <adt/list.h>
#include <async.h>
#include <errno.h>
#include <macros.h>
#include <fibril_synch.h>
#include <io/log.h>
#include <ipc/inet.h>
#include <ipc/services.h>
#include <loc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "inet.h"
#include "inetcfg.h"

static int inetcfg_addr_create_static(inet_naddr_t *naddr, sysarg_t *addr_id)
{
	return ENOTSUP;
}

static int inetcfg_addr_delete(sysarg_t addr_id)
{
	return ENOTSUP;
}

static int inetcfg_addr_get(sysarg_t addr_id, inet_addr_info_t *ainfo)
{
	return ENOTSUP;
}

static int inetcfg_get_addr_list(sysarg_t **addrs, size_t *count)
{
	return ENOTSUP;
}

static int inetcfg_get_link_list(sysarg_t **addrs, size_t *count)
{
	return ENOTSUP;
}

static int inetcfg_link_get(sysarg_t addr_id, inet_link_info_t *ainfo)
{
	return ENOTSUP;
}

static void inetcfg_addr_create_static_srv(ipc_callid_t callid,
    ipc_call_t *call)
{
	inet_naddr_t naddr;
	sysarg_t addr_id;
	int rc;

	log_msg(LVL_DEBUG, "inetcfg_addr_create_static_srv()");

	naddr.ipv4 = IPC_GET_ARG1(*call);
	naddr.bits = IPC_GET_ARG2(*call);

	addr_id = 0;
	rc = inetcfg_addr_create_static(&naddr, &addr_id);
	async_answer_1(callid, rc, addr_id);
}

static void inetcfg_addr_delete_srv(ipc_callid_t callid, ipc_call_t *call)
{
	sysarg_t addr_id;
	int rc;

	log_msg(LVL_DEBUG, "inetcfg_addr_delete_srv()");

	addr_id = IPC_GET_ARG1(*call);

	rc = inetcfg_addr_delete(addr_id);
	async_answer_0(callid, rc);
}

static void inetcfg_addr_get_srv(ipc_callid_t callid, ipc_call_t *call)
{
	sysarg_t addr_id;
	inet_addr_info_t ainfo;
	int rc;

	addr_id = IPC_GET_ARG1(*call);
	log_msg(LVL_DEBUG, "inetcfg_addr_get_srv()");

	rc = inetcfg_addr_get(addr_id, &ainfo);
	async_answer_2(callid, rc, ainfo.naddr.ipv4, ainfo.naddr.bits);
}


static void inetcfg_get_addr_list_srv(ipc_callid_t callid, ipc_call_t *call)
{
	ipc_callid_t rcallid;
	size_t max_size;
	size_t act_size;
	size_t size;
	sysarg_t *id_buf;
	int rc;

	log_msg(LVL_DEBUG, "inetcfg_get_addr_list_srv()");

	if (!async_data_read_receive(&rcallid, &max_size)) {
		async_answer_0(rcallid, EREFUSED);
		async_answer_0(callid, EREFUSED);
		return;
	}

	rc = inetcfg_get_addr_list(&id_buf, &act_size);
	if (rc != EOK) {
		async_answer_0(rcallid, rc);
		async_answer_0(callid, rc);
		return;
	}

	size = min(act_size, max_size);

	sysarg_t retval = async_data_read_finalize(rcallid, id_buf, size);
	free(id_buf);

	async_answer_1(callid, retval, act_size);
}

static void inetcfg_link_get_srv(ipc_callid_t callid, ipc_call_t *call)
{
	sysarg_t link_id;
	inet_link_info_t linfo;
	int rc;

	link_id = IPC_GET_ARG1(*call);
	log_msg(LVL_DEBUG, "inetcfg_link_get_srv()");

	rc = inetcfg_link_get(link_id, &linfo);
	async_answer_0(callid, rc);
}

static void inetcfg_get_link_list_srv(ipc_callid_t callid, ipc_call_t *call)
{
	ipc_callid_t rcallid;
	size_t max_size;
	size_t act_size;
	size_t size;
	sysarg_t *id_buf;
	int rc;

	log_msg(LVL_DEBUG, "inetcfg_get_link_list_srv()");

	if (!async_data_read_receive(&rcallid, &max_size)) {
		async_answer_0(rcallid, EREFUSED);
		async_answer_0(callid, EREFUSED);
		return;
	}

	rc = inetcfg_get_link_list(&id_buf, &act_size);
	if (rc != EOK) {
		async_answer_0(rcallid, rc);
		async_answer_0(callid, rc);
		return;
	}

	size = min(act_size, max_size);

	sysarg_t retval = async_data_read_finalize(rcallid, id_buf, size);
	free(id_buf);

	async_answer_1(callid, retval, act_size);
}

void inet_cfg_conn(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	log_msg(LVL_DEBUG, "inet_cfg_conn()");

	/* Accept the connection */
	async_answer_0(iid, EOK);

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
		case INETCFG_ADDR_CREATE_STATIC:
			inetcfg_addr_create_static_srv(callid, &call);
			break;
		case INETCFG_ADDR_DELETE:
			inetcfg_addr_delete_srv(callid, &call);
			break;
		case INETCFG_ADDR_GET:
			inetcfg_addr_get_srv(callid, &call);
			break;
		case INETCFG_GET_ADDR_LIST:
			inetcfg_get_addr_list_srv(callid, &call);
			break;
		case INETCFG_GET_LINK_LIST:
			inetcfg_get_link_list_srv(callid, &call);
			break;
		case INETCFG_LINK_GET:
			inetcfg_link_get_srv(callid, &call);
			break;
		default:
			async_answer_0(callid, EINVAL);
		}
	}
}

/** @}
 */
