/*
 * Copyright (c) 2009 Lukas Mejdrech
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

/** @addtogroup ip
 * @{
 */

/** @file
 *  IP standalone module implementation.
 *  Contains skeleton module functions mapping.
 *  The functions are used by the module skeleton as module specific entry points.
 *  @see module.c
 */

#include <async.h>
#include <stdio.h>
#include <ipc/ipc.h>
#include <ipc/services.h>
#include <err.h>

#include <net_modules.h>
#include <net_interface.h>
#include <packet/packet.h>
#include <il_local.h>

#include "ip.h"
#include "ip_module.h"

/** IP module global data.
 */
extern ip_globals_t ip_globals;

/** Processes the IP message.
 *  @param[in] callid The message identifier.
 *  @param[in] call The message parameters.
 *  @param[out] answer The message answer parameters.
 *  @param[out] answer_count The last parameter for the actual answer in the answer parameter.
 *  @returns EOK on success.
 *  @returns Other error codes as defined for the ip_message() function.
 */
int il_module_message_standalone(ipc_callid_t callid, ipc_call_t * call, ipc_call_t * answer, int * answer_count){
	return ip_message_standalone(callid, call, answer, answer_count);
}

/** Starts the IP module.
 *  Initializes the client connection serving function, initializes the module, registers the module service and starts the async manager, processing IPC messages in an infinite loop.
 *  @param[in] client_connection The client connection processing function. The module skeleton propagates its own one.
 *  @returns EOK on successful module termination.
 *  @returns Other error codes as defined for the ip_initialize() function.
 *  @returns Other error codes as defined for the REGISTER_ME() macro function.
 */
int il_module_start_standalone(async_client_conn_t client_connection){
	ERROR_DECLARE;
	
	async_set_client_connection(client_connection);
	ip_globals.net_phone = net_connect_module(SERVICE_NETWORKING);
	ERROR_PROPAGATE(pm_init());
	
	ipcarg_t phonehash;
	if (ERROR_OCCURRED(ip_initialize(client_connection))
	    || ERROR_OCCURRED(REGISTER_ME(SERVICE_IP, &phonehash))) {
		pm_destroy();
		return ERROR_CODE;
	}
	
	async_manager();
	
	pm_destroy();
	return EOK;
}

/** @}
 */
