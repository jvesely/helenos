/*
 * Copyright (c) 2008 Lukas Mejdrech
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
 *  @{
 */

/** @file
 *  UDP standalone module implementation.
 *  Contains skeleton module functions mapping.
 *  The functions are used by the module skeleton as module specific entry points.
 *  @see module.c
 */

#include <async.h>
#include <stdio.h>
#include <err.h>
#include <ipc/ipc.h>
#include <ipc/services.h>

#include <net/modules.h>
#include <net/packet.h>
#include <net_interface.h>
#include <tl_local.h>

#include "udp.h"
#include "udp_module.h"

/** UDP module global data.
 */
extern udp_globals_t	udp_globals;

/** Starts the UDP module.
 *  Initializes the client connection serving function, initializes the module, registers the module service and starts the async manager, processing IPC messages in an infinite loop.
 *  @param[in] client_connection The client connection processing function. The module skeleton propagates its own one.
 *  @returns EOK on successful module termination.
 *  @returns Other error codes as defined for the udp_initialize() function.
 *  @returns Other error codes as defined for the REGISTER_ME() macro function.
 */
int tl_module_start_standalone(async_client_conn_t client_connection){
	ERROR_DECLARE;

	ipcarg_t phonehash;

	async_set_client_connection(client_connection);
	udp_globals.net_phone = net_connect_module();
	if(udp_globals.net_phone < 0){
		return udp_globals.net_phone;
	}
	ERROR_PROPAGATE(pm_init());
	if(ERROR_OCCURRED(udp_initialize(client_connection))
		|| ERROR_OCCURRED(REGISTER_ME(SERVICE_UDP, &phonehash))){
		pm_destroy();
		return ERROR_CODE;
	}

	async_manager();

	pm_destroy();
	return EOK;
}

/** Processes the UDP message.
 *  @param[in] callid The message identifier.
 *  @param[in] call The message parameters.
 *  @param[out] answer The message answer parameters.
 *  @param[out] answer_count The last parameter for the actual answer in the answer parameter.
 *  @returns EOK on success.
 *  @returns Other error codes as defined for the udp_message() function.
 */
int tl_module_message_standalone(ipc_callid_t callid, ipc_call_t * call, ipc_call_t * answer, int * answer_count){
	return udp_message_standalone(callid, call, answer, answer_count);
}

/** @}
 */
