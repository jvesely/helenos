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

/** @addtogroup libc
 * @{
 */

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <io/logctl.h>
#include <ipc/logger.h>
#include <sysinfo.h>
#include <ns.h>
#include <str.h>

#define SYSINFO_LOGGGER_BOOT_ARGUMENT "init_args.logger"

/** IPC session with the logger service. */
static async_sess_t *logger_session = NULL;

static int start_logger_exchange(async_exch_t **exchange_out)
{
	assert(exchange_out != NULL);

	if (logger_session == NULL) {
		logger_session = service_connect_blocking(EXCHANGE_SERIALIZE,
		    SERVICE_LOGGER, LOGGER_INTERFACE_CONTROL, 0);
		if (logger_session == NULL)
			return ENOMEM;
	}

	assert(logger_session != NULL);

	async_exch_t *exchange = async_exchange_begin(logger_session);
	if (exchange == NULL)
		return ENOMEM;

	*exchange_out = exchange;

	return EOK;
}


int logctl_set_default_level(log_level_t new_level)
{
	async_exch_t *exchange = NULL;
	int rc = start_logger_exchange(&exchange);
	if (rc != EOK)
		return rc;

	rc = (int) async_req_1_0(exchange,
	    LOGGER_CTL_SET_DEFAULT_LEVEL, new_level);

	async_exchange_end(exchange);

	return rc;
}

int logctl_set_namespace_level(const char *namespace, log_level_t new_level)
{
	async_exch_t *exchange = NULL;
	int rc = start_logger_exchange(&exchange);
	if (rc != EOK)
		return rc;

	aid_t reg_msg = async_send_1(exchange, LOGGER_CTL_SET_TOP_LOG_LEVEL,
	    new_level, NULL);
	rc = async_data_write_start(exchange, namespace, str_size(namespace));
	sysarg_t reg_msg_rc;
	async_wait_for(reg_msg, &reg_msg_rc);

	async_exchange_end(exchange);

	if (rc != EOK) {
		return rc;
	}

	return (int) reg_msg_rc;
}

int logctl_set_context_level(const char *namespace, const char *context, log_level_t new_level)
{
	async_exch_t *exchange = NULL;
	int rc = start_logger_exchange(&exchange);
	if (rc != EOK)
		return rc;

	aid_t reg_msg = async_send_1(exchange, LOGGER_CTL_SET_LOG_LEVEL,
	    new_level, NULL);
	rc = async_data_write_start(exchange, namespace, str_size(namespace));
	int rc2 = async_data_write_start(exchange, context, str_size(context));
	sysarg_t reg_msg_rc;
	async_wait_for(reg_msg, &reg_msg_rc);

	async_exchange_end(exchange);

	if (rc != EOK)
		return rc;

	if (rc2 != EOK)
		return rc2;

	return (int) reg_msg_rc;
}


int logctl_get_boot_level(log_level_t *level)
{
	size_t argument_size;
	void *argument = sysinfo_get_data(SYSINFO_LOGGGER_BOOT_ARGUMENT, &argument_size);
	if (argument == NULL)
		return EINVAL;

	char level_str[20];
	str_cpy(level_str, 20, (const char *) argument);

	log_level_t boot_level;
	int rc = log_level_from_str(level_str, &boot_level);
	if (rc != EOK)
		return rc;

	if (level != NULL)
		*level = (log_level_t) boot_level;

	return EOK;
}

/** @}
 */
