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

#include <align.h>
#include <bitops.h>
#include <byteorder.h>
#include <errno.h>
#include <io/log.h>
#include <mem.h>
#include <stdlib.h>

#include "inet.h"
#include "inet_std.h"
#include "pdu.h"

/** Encode Internet PDU.
 *
 * XXX We should be encoding from packet, not from datagram.
 */
int inet_pdu_encode(inet_dgram_t *dgram, uint8_t ttl, int df, void **rdata,
    size_t *rsize)
{
	void *data;
	size_t size;
	ip_header_t *hdr;
	size_t hdr_size;
	size_t data_offs;

	hdr_size = sizeof(ip_header_t);
	size = hdr_size + dgram->size;
	data_offs = ROUND_UP(hdr_size, 4);

	data = calloc(size, 1);
	if (data == NULL)
		return ENOMEM;

	hdr = (ip_header_t *)data;
	hdr->ver_ihl = (4 << VI_VERSION_l) | (hdr_size / sizeof(uint32_t));
	hdr->tos = dgram->tos;
	hdr->tot_len = host2uint16_t_be(size);
	hdr->id = host2uint16_t_be(42);
	hdr->flags_foff = host2uint16_t_be(df ? BIT_V(uint16_t, FF_FLAG_DF) : 0);
	hdr->ttl = ttl;
	hdr->proto = 0;
	hdr->chksum = 0;
	hdr->src_addr = host2uint32_t_be(dgram->src.ipv4);
	hdr->dest_addr = host2uint32_t_be(dgram->dest.ipv4);

	memcpy((uint8_t *)data + data_offs, dgram->data, dgram->size);

	*rdata = data;
	*rsize = size;
	return EOK;
}

int inet_pdu_decode(void *data, size_t size, inet_dgram_t *dgram, uint8_t *ttl,
    int *df)
{
	ip_header_t *hdr;
	size_t tot_len;
	size_t data_offs;
	uint8_t version;
	uint16_t ident;

	log_msg(LVL_DEBUG, "inet_pdu_decode()");

	if (size < sizeof(ip_header_t)) {
		log_msg(LVL_DEBUG, "PDU too short (%zu)", size);
		return EINVAL;
	}

	hdr = (ip_header_t *)data;

	version = BIT_RANGE_EXTRACT(uint8_t, VI_VERSION_h, VI_VERSION_l,
	    hdr->ver_ihl);
	if (version != 4) {
		log_msg(LVL_DEBUG, "Version (%d) != 4", version);
		return EINVAL;
	}

	tot_len = uint16_t_be2host(hdr->tot_len);
	if (tot_len < sizeof(ip_header_t)) {
		log_msg(LVL_DEBUG, "Total Length too small (%zu)", tot_len);
		return EINVAL;
	}

	if (tot_len > size) {
		log_msg(LVL_DEBUG, "Total Length = %zu > PDU size = %zu",
			tot_len, size);
		return EINVAL;
	}

	ident = uint16_t_be2host(hdr->id);
	(void)ident;
	/* XXX Flags */
	/* XXX Fragment offset */
	/* XXX Protocol */
	/* XXX Checksum */

	dgram->src.ipv4 = uint32_t_be2host(hdr->src_addr);
	dgram->dest.ipv4 = uint32_t_be2host(hdr->dest_addr);
	dgram->tos = hdr->tos;
	*ttl = hdr->ttl;
	*df = (uint16_t_be2host(hdr->tos) & BIT_V(uint16_t, FF_FLAG_DF)) ?
	    1 : 0;

	/* XXX IP options */
	data_offs = sizeof(uint32_t) * BIT_RANGE_EXTRACT(uint8_t, VI_IHL_h,
	    VI_IHL_l, hdr->ver_ihl);

	dgram->size = tot_len - data_offs;
	dgram->data = calloc(dgram->size, 1);
	if (dgram->data == NULL) {
		log_msg(LVL_WARN, "Out of memory.");
		return ENOMEM;
	}

	memcpy(dgram->data, (uint8_t *)data + data_offs, dgram->size);

	return EOK;
}

/** @}
 */
