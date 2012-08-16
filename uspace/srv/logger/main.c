/*
 * Copyright (c) 2012 Vojtech Horky
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

/**
 * @defgroup logger Logging service.
 * @brief HelenOS logging service.
 * @{
 */

/** @file
 */

#include <ipc/services.h>
#include <ipc/logger.h>
#include <io/log.h>
#include <io/logctl.h>
#include <ns.h>
#include <async.h>
#include <stdio.h>
#include <errno.h>
#include <str_error.h>
#include <malloc.h>
#include "logger.h"

static int handle_namespace_level_change(sysarg_t new_level)
{
	void *namespace_name;
	int rc = async_data_write_accept(&namespace_name, true, 0, 0, 0, NULL);
	if (rc != EOK) {
		return rc;
	}

	logging_namespace_t *namespace = namespace_writer_attach((const char *) namespace_name);
	free(namespace_name);
	if (namespace == NULL)
		return ENOENT;

	rc = namespace_change_level(namespace, (log_level_t) new_level);
	namespace_writer_detach(namespace);

	return rc;
}

static void connection_handler_control(void)
{
	printf(NAME "/control: new client.\n");

	while (true) {
		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);

		if (!IPC_GET_IMETHOD(call))
			break;

		int rc;

		switch (IPC_GET_IMETHOD(call)) {
		case LOGGER_CTL_GET_DEFAULT_LEVEL:
			async_answer_1(callid, EOK, get_default_logging_level());
			break;
		case LOGGER_CTL_SET_DEFAULT_LEVEL:
			rc = set_default_logging_level(IPC_GET_ARG1(call));
			async_answer_0(callid, rc);
			break;
		case LOGGER_CTL_SET_NAMESPACE_LEVEL:
			rc = handle_namespace_level_change(IPC_GET_ARG1(call));
			async_answer_0(callid, rc);
			break;
		default:
			async_answer_0(callid, EINVAL);
			break;
		}
	}

	printf(NAME "/control: client terminated.\n");
}

static logging_namespace_t *find_namespace_and_attach_writer(void)
{
	ipc_call_t call;
	ipc_callid_t callid = async_get_call(&call);

	if (IPC_GET_IMETHOD(call) != LOGGER_REGISTER) {
		async_answer_0(callid, EINVAL);
		return NULL;
	}

	void *name;
	int rc = async_data_write_accept(&name, true, 1, MAX_NAMESPACE_LENGTH, 0, NULL);
	async_answer_0(callid, rc);

	if (rc != EOK) {
		return NULL;
	}

	logging_namespace_t *result = namespace_writer_attach((const char *) name);

	free(name);

	return result;
}

static int handle_receive_message(logging_namespace_t *namespace, sysarg_t context, int level)
{
	bool skip_message = !namespace_has_reader(namespace, context, level);
	if (skip_message) {
		/* Abort the actual message buffer transfer. */
		ipc_callid_t callid;
		size_t size;
		int rc = ENAK;
		if (!async_data_write_receive(&callid, &size))
			rc = EINVAL;

		async_answer_0(callid, rc);
		return rc;
	}

	void *message;
	int rc = async_data_write_accept(&message, true, 0, 0, 0, NULL);
	if (rc != EOK) {
		return rc;
	}

	namespace_add_message(namespace, message, context, level);

	free(message);

	return EOK;
}

static int handle_create_context(logging_namespace_t *namespace, sysarg_t *idx)
{
	void *name;
	int rc = async_data_write_accept(&name, true, 0, 0, 0, NULL);
	if (rc != EOK) {
		return rc;
	}

	rc = namespace_create_context(namespace, name);

	free(name);

	if (rc < 0)
		return rc;

	*idx = (sysarg_t) rc;
	return EOK;
}

static void connection_handler_sink(logging_namespace_t *namespace)
{
	printf(NAME "/sink: new client %s.\n", namespace_get_name(namespace));

	while (true) {
		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);

		if (!IPC_GET_IMETHOD(call))
			break;

		int rc;
		sysarg_t arg = 0;

		switch (IPC_GET_IMETHOD(call)) {
		case LOGGER_CREATE_CONTEXT:
			rc = handle_create_context(namespace, &arg);
			async_answer_1(callid, rc, arg);
			break;
		case LOGGER_MESSAGE:
			rc = handle_receive_message(namespace, IPC_GET_ARG1(call), IPC_GET_ARG2(call));
			async_answer_0(callid, rc);
			break;
		default:
			async_answer_0(callid, EINVAL);
			break;
		}
	}

	printf(NAME "/sink: client %s terminated.\n", namespace_get_name(namespace));
	namespace_writer_detach(namespace);
}

static void connection_handler(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	logger_interface_t iface = IPC_GET_ARG1(*icall);

	logging_namespace_t *namespace = NULL;

	switch (iface) {
	case LOGGER_INTERFACE_CONTROL:
		async_answer_0(iid, EOK);
		connection_handler_control();
		break;
	case LOGGER_INTERFACE_SINK:
		/* First call has to be the registration. */
		async_answer_0(iid, EOK);
		namespace = find_namespace_and_attach_writer();
		if (namespace == NULL) {
			fprintf(stderr, NAME ": failed to register namespace.\n");
			break;
		}
		connection_handler_sink(namespace);
		break;
	default:
		async_answer_0(iid, EINVAL);
		break;
	}
}

int main(int argc, char *argv[])
{
	printf(NAME ": HelenOS Logging Service\n");
	
	/* Get default logging level from sysinfo (if available). */
	log_level_t boot_logging_level = LVL_NOTE;
	int rc = logctl_get_boot_level(&boot_logging_level);
	if (rc == EOK)
		set_default_logging_level(boot_logging_level);
	else
		printf(NAME ": Warn: failed to get logging level from sysinfo: %s.\n",
		    str_error(rc));

	async_set_client_connection(connection_handler);
	
	rc = service_register(SERVICE_LOGGER);
	if (rc != EOK) {
		printf(NAME ": failed to register: %s.\n", str_error(rc));
		return -1;
	}
	
	printf(NAME ": Accepting connections\n");
	async_manager();
	
	/* Never reached */
	return 0;
}

/**
 * @}
 */
