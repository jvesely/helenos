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

#include <stdbool.h>
#include <errno.h>
#include <fibril_synch.h>
#include <inet/iplink.h>
#include <io/log.h>
#include <loc.h>
#include <stdlib.h>
#include <str.h>
#include <net/socket_codes.h>
#include "addrobj.h"
#include "inetsrv.h"
#include "inet_link.h"
#include "pdu.h"

static bool first_link = true;
static bool first_link6 = true;

static FIBRIL_MUTEX_INITIALIZE(ip_ident_lock);
static uint16_t ip_ident = 0;

static int inet_link_open(service_id_t);
static int inet_iplink_recv(iplink_t *, iplink_recv_sdu_t *, uint16_t);

static iplink_ev_ops_t inet_iplink_ev_ops = {
	.recv = inet_iplink_recv
};

static LIST_INITIALIZE(inet_link_list);
static FIBRIL_MUTEX_INITIALIZE(inet_discovery_lock);

static addr128_t link_local_node_ip =
    {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xfe, 0, 0, 0};

static void inet_link_local_node_ip(addr48_t mac_addr,
    addr128_t ip_addr)
{
	memcpy(ip_addr, link_local_node_ip, 16);
	
	ip_addr[8] = mac_addr[0] ^ 0x02;
	ip_addr[9] = mac_addr[1];
	ip_addr[10] = mac_addr[2];
	ip_addr[13] = mac_addr[3];
	ip_addr[14] = mac_addr[4];
	ip_addr[15] = mac_addr[5];
}

static int inet_iplink_recv(iplink_t *iplink, iplink_recv_sdu_t *sdu, uint16_t af)
{
	log_msg(LOG_DEFAULT, LVL_DEBUG, "inet_iplink_recv()");
	
	int rc;
	inet_packet_t packet;
	
	switch (af) {
	case AF_INET:
		rc = inet_pdu_decode(sdu->data, sdu->size, &packet);
		break;
	case AF_INET6:
		rc = inet_pdu_decode6(sdu->data, sdu->size, &packet);
		break;
	default:
		log_msg(LOG_DEFAULT, LVL_DEBUG, "invalid address family");
		return EINVAL;
	}
	
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_DEBUG, "failed decoding PDU");
		return rc;
	}
	
	log_msg(LOG_DEFAULT, LVL_DEBUG, "call inet_recv_packet()");
	rc = inet_recv_packet(&packet);
	log_msg(LOG_DEFAULT, LVL_DEBUG, "call inet_recv_packet -> %d", rc);
	free(packet.data);
	
	return rc;
}

static int inet_link_check_new(void)
{
	bool already_known;
	category_id_t iplink_cat;
	service_id_t *svcs;
	size_t count, i;
	int rc;

	fibril_mutex_lock(&inet_discovery_lock);

	rc = loc_category_get_id("iplink", &iplink_cat, IPC_FLAG_BLOCKING);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed resolving category 'iplink'.");
		fibril_mutex_unlock(&inet_discovery_lock);
		return ENOENT;
	}

	rc = loc_category_get_svcs(iplink_cat, &svcs, &count);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed getting list of IP links.");
		fibril_mutex_unlock(&inet_discovery_lock);
		return EIO;
	}

	for (i = 0; i < count; i++) {
		already_known = false;

		list_foreach(inet_link_list, ilink_link) {
			inet_link_t *ilink = list_get_instance(ilink_link,
			    inet_link_t, link_list);
			if (ilink->svc_id == svcs[i]) {
				already_known = true;
				break;
			}
		}

		if (!already_known) {
			log_msg(LOG_DEFAULT, LVL_DEBUG, "Found IP link '%lu'",
			    (unsigned long) svcs[i]);
			rc = inet_link_open(svcs[i]);
			if (rc != EOK)
				log_msg(LOG_DEFAULT, LVL_ERROR, "Could not open IP link.");
		}
	}

	fibril_mutex_unlock(&inet_discovery_lock);
	return EOK;
}

static inet_link_t *inet_link_new(void)
{
	inet_link_t *ilink = calloc(1, sizeof(inet_link_t));

	if (ilink == NULL) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed allocating link structure. "
		    "Out of memory.");
		return NULL;
	}

	link_initialize(&ilink->link_list);

	return ilink;
}

static void inet_link_delete(inet_link_t *ilink)
{
	if (ilink->svc_name != NULL)
		free(ilink->svc_name);
	
	free(ilink);
}

static int inet_link_open(service_id_t sid)
{
	inet_link_t *ilink;
	inet_addr_t iaddr;
	int rc;

	log_msg(LOG_DEFAULT, LVL_DEBUG, "inet_link_open()");
	ilink = inet_link_new();
	if (ilink == NULL)
		return ENOMEM;

	ilink->svc_id = sid;
	ilink->iplink = NULL;

	rc = loc_service_get_name(sid, &ilink->svc_name);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed getting service name.");
		goto error;
	}

	ilink->sess = loc_service_connect(EXCHANGE_SERIALIZE, sid, 0);
	if (ilink->sess == NULL) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed connecting '%s'", ilink->svc_name);
		goto error;
	}

	rc = iplink_open(ilink->sess, &inet_iplink_ev_ops, &ilink->iplink);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed opening IP link '%s'",
		    ilink->svc_name);
		goto error;
	}

	rc = iplink_get_mtu(ilink->iplink, &ilink->def_mtu);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed determinning MTU of link '%s'",
		    ilink->svc_name);
		goto error;
	}
	
	/*
	 * Get the MAC address of the link. If the link has a MAC
	 * address, we assume that it supports NDP.
	 */
	rc = iplink_get_mac48(ilink->iplink, &ilink->mac);
	ilink->mac_valid = (rc == EOK);

	log_msg(LOG_DEFAULT, LVL_DEBUG, "Opened IP link '%s'", ilink->svc_name);
	list_append(&ilink->link_list, &inet_link_list);

	inet_addrobj_t *addr = NULL;
	
	if (first_link) {
		addr = inet_addrobj_new();
		
		inet_naddr(&addr->naddr, 127, 0, 0, 1, 24);
		first_link = false;
	} else {
		/*
		 * FIXME
		 * Setting static IPv4 address for testing purposes:
		 * 10.0.2.15/24
		 */
		addr = inet_addrobj_new();
		
		inet_naddr(&addr->naddr, 10, 0, 2, 15, 24);
	}
	
	if (addr != NULL) {
		addr->ilink = ilink;
		addr->name = str_dup("v4a");
		
		rc = inet_addrobj_add(addr);
		if (rc == EOK) {
			inet_naddr_addr(&addr->naddr, &iaddr);
			rc = iplink_addr_add(ilink->iplink, &iaddr);
			if (rc != EOK) {
				log_msg(LOG_DEFAULT, LVL_ERROR,
				    "Failed setting IPv4 address on internet link.");
				inet_addrobj_remove(addr);
				inet_addrobj_delete(addr);
			}
		} else {
			log_msg(LOG_DEFAULT, LVL_ERROR, "Failed adding IPv4 address.");
			inet_addrobj_delete(addr);
		}
	}
	
	inet_addrobj_t *addr6 = NULL;
	
	if (first_link6) {
		addr6 = inet_addrobj_new();
		
		inet_naddr6(&addr6->naddr, 0, 0, 0, 0, 0, 0, 0, 1, 128);
		first_link6 = false;
	} else if (ilink->mac_valid) {
		addr6 = inet_addrobj_new();
		
		addr128_t link_local;
		inet_link_local_node_ip(ilink->mac, link_local);
		
		inet_naddr_set6(link_local, 64, &addr6->naddr);
	}
	
	if (addr6 != NULL) {
		addr6->ilink = ilink;
		addr6->name = str_dup("v6a");
		
		rc = inet_addrobj_add(addr6);
		if (rc == EOK) {
			inet_naddr_addr(&addr6->naddr, &iaddr);
			rc = iplink_addr_add(ilink->iplink, &iaddr);
			if (rc != EOK) {
				log_msg(LOG_DEFAULT, LVL_ERROR,
				    "Failed setting IPv6 address on internet link.");
				inet_addrobj_remove(addr6);
				inet_addrobj_delete(addr6);
			}
		} else {
			log_msg(LOG_DEFAULT, LVL_ERROR, "Failed adding IPv6 address.");
			inet_addrobj_delete(addr6);
		}
	}
	
	return EOK;
	
error:
	if (ilink->iplink != NULL)
		iplink_close(ilink->iplink);
	
	inet_link_delete(ilink);
	return rc;
}

static void inet_link_cat_change_cb(void)
{
	(void) inet_link_check_new();
}

int inet_link_discovery_start(void)
{
	int rc;

	rc = loc_register_cat_change_cb(inet_link_cat_change_cb);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Failed registering callback for IP link "
		    "discovery (%d).", rc);
		return rc;
	}

	return inet_link_check_new();
}

/** Send IPv4 datagram over Internet link */
int inet_link_send_dgram(inet_link_t *ilink, addr32_t lsrc, addr32_t ldest,
    inet_dgram_t *dgram, uint8_t proto, uint8_t ttl, int df)
{
	addr32_t src_v4;
	uint16_t src_af = inet_addr_get(&dgram->src, &src_v4, NULL);
	if (src_af != AF_INET)
		return EINVAL;
	
	addr32_t dest_v4;
	uint16_t dest_af = inet_addr_get(&dgram->dest, &dest_v4, NULL);
	if (dest_af != AF_INET)
		return EINVAL;
	
	/*
	 * Fill packet structure. Fragmentation is performed by
	 * inet_pdu_encode().
	 */
	
	iplink_sdu_t sdu;
	
	sdu.src = lsrc;
	sdu.dest = ldest;
	
	inet_packet_t packet;
	
	packet.src = dgram->src;
	packet.dest = dgram->dest;
	packet.tos = dgram->tos;
	packet.proto = proto;
	packet.ttl = ttl;
	
	/* Allocate identifier */
	fibril_mutex_lock(&ip_ident_lock);
	packet.ident = ++ip_ident;
	fibril_mutex_unlock(&ip_ident_lock);
	
	packet.df = df;
	packet.data = dgram->data;
	packet.size = dgram->size;
	
	int rc;
	size_t offs = 0;
	
	do {
		/* Encode one fragment */
		
		size_t roffs;
		rc = inet_pdu_encode(&packet, src_v4, dest_v4, offs, ilink->def_mtu,
		    &sdu.data, &sdu.size, &roffs);
		if (rc != EOK)
			return rc;
		
		/* Send the PDU */
		rc = iplink_send(ilink->iplink, &sdu);
		
		free(sdu.data);
		offs = roffs;
	} while (offs < packet.size);
	
	return rc;
}

/** Send IPv6 datagram over Internet link */
int inet_link_send_dgram6(inet_link_t *ilink, addr48_t ldest,
    inet_dgram_t *dgram, uint8_t proto, uint8_t ttl, int df)
{
	addr128_t src_v6;
	uint16_t src_af = inet_addr_get(&dgram->src, NULL, &src_v6);
	if (src_af != AF_INET6)
		return EINVAL;
	
	addr128_t dest_v6;
	uint16_t dest_af = inet_addr_get(&dgram->dest, NULL, &dest_v6);
	if (dest_af != AF_INET6)
		return EINVAL;
	
	iplink_sdu6_t sdu6;
	addr48(ldest, sdu6.dest);
	
	/*
	 * Fill packet structure. Fragmentation is performed by
	 * inet_pdu_encode6().
	 */
	
	inet_packet_t packet;
	
	packet.src = dgram->src;
	packet.dest = dgram->dest;
	packet.tos = dgram->tos;
	packet.proto = proto;
	packet.ttl = ttl;
	
	/* Allocate identifier */
	fibril_mutex_lock(&ip_ident_lock);
	packet.ident = ++ip_ident;
	fibril_mutex_unlock(&ip_ident_lock);
	
	packet.df = df;
	packet.data = dgram->data;
	packet.size = dgram->size;
	
	int rc;
	size_t offs = 0;
	
	do {
		/* Encode one fragment */
		
		size_t roffs;
		rc = inet_pdu_encode6(&packet, src_v6, dest_v6, offs, ilink->def_mtu,
		    &sdu6.data, &sdu6.size, &roffs);
		if (rc != EOK)
			return rc;
		
		/* Send the PDU */
		rc = iplink_send6(ilink->iplink, &sdu6);
		
		free(sdu6.data);
		offs = roffs;
	} while (offs < packet.size);
	
	return rc;
}

inet_link_t *inet_link_get_by_id(sysarg_t link_id)
{
	fibril_mutex_lock(&inet_discovery_lock);

	list_foreach(inet_link_list, elem) {
		inet_link_t *ilink = list_get_instance(elem, inet_link_t,
		    link_list);

		if (ilink->svc_id == link_id) {
			fibril_mutex_unlock(&inet_discovery_lock);
			return ilink;
		}
	}

	fibril_mutex_unlock(&inet_discovery_lock);
	return NULL;
}

/** @}
 */
