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

/** @addtogroup ethip
 * @{
 */
/**
 * @file
 * @brief
 */

#ifndef ETHIP_H_
#define ETHIP_H_

#include <adt/list.h>
#include <async.h>
#include <inet/iplink_srv.h>
#include <loc.h>
#include <sys/types.h>

typedef struct ethip_nic {
	link_t nic_list;
	service_id_t svc_id;
	char *svc_name;
	async_sess_t *sess;

	iplink_srv_t iplink;
	service_id_t iplink_sid;
} ethip_nic_t;

/** IEEE MAC-48 identifier */
typedef struct {
	/** MAC Address (in lowest 48 bits) */
	uint64_t addr;
} mac48_addr_t;

/** Ethernet frame */
typedef struct {
	/** Destination Address */
	mac48_addr_t dest;
	/** Source Address */
	mac48_addr_t src;
	/** Ethertype or Length */
	uint16_t etype_len;
	/** Payload */
	void *data;
	/** Payload size */
	size_t size;
} eth_frame_t;

extern int ethip_iplink_init(ethip_nic_t *);
extern int ethip_received(iplink_srv_t *, void *, size_t);


#endif

/** @}
 */
