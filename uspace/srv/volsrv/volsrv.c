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

/** @addtogroup volsrv
 * @{
 */
/**
 * @file Volume service
 */

#include <async.h>
#include <errno.h>
#include <io/log.h>
#include <ipc/services.h>
#include <ipc/vol.h>
#include <loc.h>
#include <macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <task.h>
#include <types/vol.h>

#include "part.h"

#define NAME  "volsrv"

static void vol_client_conn(ipc_callid_t, ipc_call_t *, void *);

static int vol_init(void)
{
	int rc;
	log_msg(LOG_DEFAULT, LVL_DEBUG, "vol_init()");

	rc = vol_part_init();
	if (rc != EOK)
		return rc;

	rc = vol_part_discovery_start();
	if (rc != EOK)
		return rc;

	async_set_client_connection(vol_client_conn);

	rc = loc_server_register(NAME);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed registering server (%d).", rc);
		return EEXIST;
	}

	service_id_t sid;
	rc = loc_service_register(SERVICE_NAME_VOLSRV, &sid);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed registering service (%d).", rc);
		return EEXIST;
	}

	return EOK;
}

static void vol_get_parts_srv(ipc_callid_t iid, ipc_call_t *icall)
{
	ipc_callid_t callid;
	size_t size;
	size_t act_size;
	int rc;

	if (!async_data_read_receive(&callid, &size)) {
		async_answer_0(callid, EREFUSED);
		async_answer_0(iid, EREFUSED);
		return;
	}

	service_id_t *id_buf = (service_id_t *) malloc(size);
	if (id_buf == NULL) {
		async_answer_0(callid, ENOMEM);
		async_answer_0(iid, ENOMEM);
		return;
	}

	rc = vol_part_get_ids(id_buf, size, &act_size);
	if (rc != EOK) {
		async_answer_0(callid, rc);
		async_answer_0(iid, rc);
		return;
	}

	sysarg_t retval = async_data_read_finalize(callid, id_buf, size);
	free(id_buf);

	async_answer_1(iid, retval, act_size);
}

static void vol_part_add_srv(ipc_callid_t iid, ipc_call_t *icall)
{
	service_id_t sid;
	int rc;

	sid = IPC_GET_ARG1(*icall);

	rc = vol_part_add(sid);
	if (rc != EOK) {
		async_answer_0(iid, rc);
		return;
	}

	async_answer_0(iid, EOK);
}


static void vol_part_info_srv(ipc_callid_t iid, ipc_call_t *icall)
{
	service_id_t sid;
	vol_part_t *part;
	vol_part_info_t pinfo;
	int rc;

	sid = IPC_GET_ARG1(*icall);
	log_msg(LOG_DEFAULT, LVL_NOTE, "vol_part_info_srv(%zu)",
	    sid);
	rc = vol_part_find_by_id(sid, &part);
	if (rc != EOK) {
		async_answer_0(iid, ENOENT);
		log_msg(LOG_DEFAULT, LVL_NOTE, "vol_part_info_srv(%zu) - "
		    "not found", sid);
		return;
	}

	rc = vol_part_get_info(part, &pinfo);
	if (rc != EOK) {
		async_answer_0(iid, EIO);
		log_msg(LOG_DEFAULT, LVL_NOTE, "vol_part_info_srv(%zu) - "
		    "get info failed (%d)", sid, rc);
		return;
	}

	ipc_callid_t callid;
	size_t size;
	if (!async_data_read_receive(&callid, &size)) {
		async_answer_0(callid, EREFUSED);
		async_answer_0(iid, EREFUSED);
		log_msg(LOG_DEFAULT, LVL_NOTE, "vol_part_info_srv(%zu) - "
		    "read receive failed", sid);
		return;
	}

	if (size != sizeof(vol_part_info_t)) {
		async_answer_0(callid, EINVAL);
		async_answer_0(iid, EINVAL);
		log_msg(LOG_DEFAULT, LVL_NOTE, "vol_part_info_srv(%zu) - "
		    "incorrect size", sid);
		return;
	}

	rc = async_data_read_finalize(callid, &pinfo,
	    min(size, sizeof(pinfo)));
	if (rc != EOK) {
		async_answer_0(callid, rc);
		async_answer_0(iid, rc);
		log_msg(LOG_DEFAULT, LVL_NOTE, "vol_part_info_srv(%zu) - "
		    "data read failed", sid);
		return;
	}

	log_msg(LOG_DEFAULT, LVL_NOTE, "vol_part_info_srv(%zu) - "
	    "success", sid);
	async_answer_0(iid, EOK);
}

static void vol_part_empty_srv(ipc_callid_t iid, ipc_call_t *icall)
{
	service_id_t sid;
	vol_part_t *part;
	int rc;

	sid = IPC_GET_ARG1(*icall);

	rc = vol_part_find_by_id(sid, &part);
	if (rc != EOK) {
		async_answer_0(iid, ENOENT);
		return;
	}

	rc = vol_part_empty_part(part);
	if (rc != EOK) {
		async_answer_0(iid, EIO);
		return;
	}

	async_answer_0(iid, EOK);
}

static void vol_client_conn(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	log_msg(LOG_DEFAULT, LVL_DEBUG, "vol_client_conn()");

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
		case VOL_GET_PARTS:
			vol_get_parts_srv(callid, &call);
			break;
		case VOL_PART_ADD:
			vol_part_add_srv(callid, &call);
			break;
		case VOL_PART_INFO:
			vol_part_info_srv(callid, &call);
			break;
		case VOL_PART_EMPTY:
			vol_part_empty_srv(callid, &call);
			break;
		default:
			async_answer_0(callid, EINVAL);
		}
	}
}

int main(int argc, char *argv[])
{
	int rc;

	printf("%s: Volume service\n", NAME);

	if (log_init(NAME) != EOK) {
		printf(NAME ": Failed to initialize logging.\n");
		return 1;
	}

	rc = vol_init();
	if (rc != EOK)
		return 1;

	printf(NAME ": Accepting connections.\n");
	task_retval(0);
	async_manager();

	return 0;
}

/** @}
 */
