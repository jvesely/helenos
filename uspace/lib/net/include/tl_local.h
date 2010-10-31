/*
 * Copyright (c) 2010 Martin Decky
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

/** @addtogroup libnet
 * @{
 */

#ifndef LIBNET_TL_LOCAL_H_
#define LIBNET_TL_LOCAL_H_

#include <ipc/ipc.h>
#include <async.h>

/** Starts the TL module.
 *
 * Initializes the client connection serving function, initializes the module,
 * registers the module service and starts the async manager, processing IPC
 * messages in an infinite loop.
 *
 * @param[in] client_connection The client connection processing function. The
 *			module skeleton propagates its own one.
 * @returns		EOK on successful module termination.
 * @returns		Other error codes as defined for the module initialize
 *			function.
 * @returns		Other error codes as defined for the REGISTER_ME() macro
 *			function.
 */
extern int tl_module_message_standalone(ipc_callid_t, ipc_call_t *,
    ipc_call_t *, int *);


/** Processes the TL module message.
 *
 * @param[in] callid	The message identifier.
 * @param[in] call	The message parameters.
 * @param[out] answer	The message answer parameters.
 * @param[out] answer_count The last parameter for the actual answer in the
 *			answer parameter.
 * @returns		EOK on success.
 * @returns		Other error codes as defined for the module's message
 *			standalone function.
 */
extern int tl_module_start_standalone(async_client_conn_t);

#endif

/** @}
 */
