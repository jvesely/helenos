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

/** @addtogroup packet
 *  @{
 */

/** @file
 *  Packet client interface implementation for remote modules.
 *  @see packet_client.h
 */

#include <async.h>
#include <errno.h>
#include <ipc/ipc.h>
#include <sys/mman.h>

#include <net_err.h>
#include <net_messages.h>
#include <packet/packet.h>
#include <packet/packet_client.h>
#include <packet/packet_header.h>
#include <packet/packet_messages.h>
#include <packet_remote.h>

/** Obtain the packet from the packet server as the shared memory block.
 *
 * Create the local packet mapping as well.
 *
 * @param[in]  phone     The packet server module phone.
 * @param[out] packet    The packet reference pointer to store the received
 *                       packet reference.
 * @param[in]  packet_id The packet identifier.
 * @param[in]  size      The packet total size in bytes.
 *
 * @return EOK on success.
 * @return Other error codes as defined for the pm_add() function.
 * @return Other error codes as defined for the async_share_in_start() function.
 *
 */
static int
packet_return(int phone, packet_ref packet, packet_id_t packet_id, size_t size)
{
	ERROR_DECLARE;
	
	ipc_call_t answer;
	aid_t message = async_send_1(phone, NET_PACKET_GET, packet_id, &answer);

	*packet = (packet_t) as_get_mappable_page(size);
	if (ERROR_OCCURRED(async_share_in_start_0_0(phone, *packet, size)) ||
	    ERROR_OCCURRED(pm_add(*packet))) {
		munmap(*packet, size);
		async_wait_for(message, NULL);
		return ERROR_CODE;
	}
	
	ipcarg_t result;
	async_wait_for(message, &result);
	
	return result;
}

int packet_translate_remote(int phone, packet_ref packet, packet_id_t packet_id)
{
	ERROR_DECLARE;
	
	if (!packet)
		return EINVAL;
	
	*packet = pm_find(packet_id);
	if (!*packet) {
		ipcarg_t size;
		
		ERROR_PROPAGATE(async_req_1_1(phone, NET_PACKET_GET_SIZE,
		    packet_id, &size));
		ERROR_PROPAGATE(packet_return(phone, packet, packet_id, size));
	}
	if ((*packet)->next) {
		packet_t next;
		
		return packet_translate_remote(phone, &next, (*packet)->next);
	}
	
	return EOK;
}

packet_t packet_get_4_remote(int phone, size_t max_content, size_t addr_len,
    size_t max_prefix, size_t max_suffix)
{
	ERROR_DECLARE;
	
	ipcarg_t packet_id;
	ipcarg_t size;
	
	if (ERROR_OCCURRED(async_req_4_2(phone, NET_PACKET_CREATE_4,
	    max_content, addr_len, max_prefix, max_suffix, &packet_id, &size)))
		return NULL;
	
	
	packet_t packet = pm_find(packet_id);
	if (!packet) {
		if (ERROR_OCCURRED(packet_return(phone, &packet, packet_id,
		    size)))
			return NULL;
	}
	
	return packet;
}

packet_t packet_get_1_remote(int phone, size_t content)
{
	ERROR_DECLARE;
	
	ipcarg_t packet_id;
	ipcarg_t size;
	
	if (ERROR_OCCURRED(async_req_1_2(phone, NET_PACKET_CREATE_1, content,
	    &packet_id, &size)))
		return NULL;
	
	packet_t packet = pm_find(packet_id);
	if (!packet) {
		if (ERROR_OCCURRED(packet_return(phone, &packet, packet_id,
		    size)))
			return NULL;
	}
	
	return packet;
}

void pq_release_remote(int phone, packet_id_t packet_id)
{
	async_msg_1(phone, NET_PACKET_RELEASE, packet_id);
}

/** @}
 */
