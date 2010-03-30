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
 *  @{
 */

/** @file
 *  IP module messages.
 *  @see ip_interface.h
 */

#ifndef __NET_IP_MESSAGES_H__
#define __NET_IP_MESSAGES_H__

#include <ipc/ipc.h>

#include <in.h>
#include <ip_codes.h>

/** IP module messages.
 */
typedef enum{
	/** Adds the routing entry.
	 *  @see ip_add_route()
	 */
	NET_IP_ADD_ROUTE = NET_IP_FIRST,
	/** Gets the actual route information.
	 *  @see ip_get_route()
	 */
	NET_IP_GET_ROUTE,
	/** Processes the received error notification.
	 *  @see ip_received_error_msg()
	 */
	NET_IP_RECEIVED_ERROR,
	/** Sets the default gateway.
	 *  @see ip_set_default_gateway()
	 */
	NET_IP_SET_GATEWAY
} ip_messages;

/** @name IP specific message parameters definitions
 */
/*@{*/

/** Returns the address message parameter.
 *  @param[in] call The message call structure.
 */
#define IP_GET_ADDRESS(call) \
	({in_addr_t addr; addr.s_addr = IPC_GET_ARG3(*call); addr;})

/** Returns the gateway message parameter.
 *  @param[in] call The message call structure.
 */
#define IP_GET_GATEWAY(call) \
	({in_addr_t addr; addr.s_addr = IPC_GET_ARG2(*call); addr;})

/** Sets the header length in the message answer.
 *  @param[out] answer The message answer structure.
 */
#define IP_SET_HEADERLEN(answer, value) \
	{ipcarg_t argument = (ipcarg_t) (value); IPC_SET_ARG2(*answer, argument);}

/** Returns the network mask message parameter.
 *  @param[in] call The message call structure.
 */
#define IP_GET_NETMASK(call) \
	({in_addr_t addr; addr.s_addr = IPC_GET_ARG4(*call); addr;})

/** Returns the protocol message parameter.
 *  @param[in] call The message call structure.
 */
#define IP_GET_PROTOCOL(call) \
	({ip_protocol_t protocol = (ip_protocol_t) IPC_GET_ARG1(*call); protocol;})

/*@}*/

#endif

/** @}
 */
