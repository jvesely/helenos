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

/** @addtogroup arp
 * @{
 */

/** @file
 * ARP module implementation.
 * @see arp.h
 */

#include "arp.h"
#include "arp_header.h"
#include "arp_oc.h"
#include "arp_module.h"

#include <async.h>
#include <malloc.h>
#include <mem.h>
#include <fibril_synch.h>
#include <stdio.h>
#include <str.h>
#include <task.h>
#include <adt/measured_strings.h>
#include <ipc/ipc.h>
#include <ipc/services.h>
#include <ipc/net.h>
#include <ipc/arp.h>
#include <ipc/il.h>
#include <byteorder.h>
#include <errno.h>

#include <net/modules.h>
#include <net/device.h>
#include <net/packet.h>

#include <nil_interface.h>
#include <protocol_map.h>
#include <packet_client.h>
#include <packet_remote.h>
#include <il_interface.h>
#include <il_local.h>


/** ARP module name. */
#define NAME  "arp"

/** Number of microseconds to wait for an ARP reply. */
#define ARP_TRANS_WAIT	1000000

/** ARP global data. */
arp_globals_t arp_globals;

DEVICE_MAP_IMPLEMENT(arp_cache, arp_device_t);
INT_MAP_IMPLEMENT(arp_protos, arp_proto_t);
GENERIC_CHAR_MAP_IMPLEMENT(arp_addr, arp_trans_t);

static void arp_clear_trans(arp_trans_t *trans)
{
	if (trans->hw_addr) {
		free(trans->hw_addr);
		trans->hw_addr = NULL;
	}
	fibril_condvar_broadcast(&trans->cv);
}

static void arp_clear_addr(arp_addr_t *addresses)
{
	int count;
	arp_trans_t *trans;

	for (count = arp_addr_count(addresses) - 1; count >= 0; count--) {
		trans = arp_addr_items_get_index(&addresses->values, count);
		if (trans)
			arp_clear_trans(trans);
	}
}


/** Clears the device specific data.
 *
 * @param[in] device	The device specific data.
 */
static void arp_clear_device(arp_device_t *device)
{
	int count;
	arp_proto_t *proto;

	for (count = arp_protos_count(&device->protos) - 1; count >= 0;
	    count--) {
		proto = arp_protos_get_index(&device->protos, count);
		if (proto) {
			if (proto->addr)
				free(proto->addr);
			if (proto->addr_data)
				free(proto->addr_data);
			arp_clear_addr(&proto->addresses);
			arp_addr_destroy(&proto->addresses);
		}
	}
	arp_protos_clear(&device->protos);
}

static int arp_clean_cache_req(int arp_phone)
{
	int count;
	arp_device_t *device;

	fibril_mutex_lock(&arp_globals.lock);
	for (count = arp_cache_count(&arp_globals.cache) - 1; count >= 0;
	    count--) {
		device = arp_cache_get_index(&arp_globals.cache, count);
		if (device) {
			arp_clear_device(device);
			if (device->addr_data)
				free(device->addr_data);
			if (device->broadcast_data)
				free(device->broadcast_data);
		}
	}
	arp_cache_clear(&arp_globals.cache);
	fibril_mutex_unlock(&arp_globals.lock);
	printf("Cache cleaned\n");
	return EOK;
}

static int arp_clear_address_req(int arp_phone, device_id_t device_id,
    services_t protocol, measured_string_t *address)
{
	arp_device_t *device;
	arp_proto_t *proto;
	arp_trans_t *trans;

	fibril_mutex_lock(&arp_globals.lock);
	device = arp_cache_find(&arp_globals.cache, device_id);
	if (!device) {
		fibril_mutex_unlock(&arp_globals.lock);
		return ENOENT;
	}
	proto = arp_protos_find(&device->protos, protocol);
	if (!proto) {
		fibril_mutex_unlock(&arp_globals.lock);
		return ENOENT;
	}
	trans = arp_addr_find(&proto->addresses, address->value, address->length);
	if (trans)
		arp_clear_trans(trans);
	arp_addr_exclude(&proto->addresses, address->value, address->length);
	fibril_mutex_unlock(&arp_globals.lock);
	return EOK;
}


static int arp_clear_device_req(int arp_phone, device_id_t device_id)
{
	arp_device_t *device;

	fibril_mutex_lock(&arp_globals.lock);
	device = arp_cache_find(&arp_globals.cache, device_id);
	if (!device) {
		fibril_mutex_unlock(&arp_globals.lock);
		return ENOENT;
	}
	arp_clear_device(device);
	printf("Device %d cleared\n", device_id);
	fibril_mutex_unlock(&arp_globals.lock);
	return EOK;
}

/** Creates new protocol specific data.
 *
 * Allocates and returns the needed memory block as the proto parameter.
 *
 * @param[out] proto	The allocated protocol specific data.
 * @param[in] service	The protocol module service.
 * @param[in] address	The actual protocol device address.
 * @return		EOK on success.
 * @return		ENOMEM if there is not enough memory left.
 */
static int arp_proto_create(arp_proto_t **proto, services_t service,
    measured_string_t *address)
{
	int rc;

	*proto = (arp_proto_t *) malloc(sizeof(arp_proto_t));
	if (!*proto)
		return ENOMEM;
	
	(*proto)->service = service;
	(*proto)->addr = address;
	(*proto)->addr_data = address->value;
	
	rc = arp_addr_initialize(&(*proto)->addresses);
	if (rc != EOK) {
		free(*proto);
		return rc;
	}
	
	return EOK;
}

/** Registers the device.
 *
 * Creates new device entry in the cache or updates the protocol address if the
 * device with the device identifier and the driver service exists.
 *
 * @param[in] device_id	The device identifier.
 * @param[in] service	The device driver service.
 * @param[in] protocol	The protocol service.
 * @param[in] address	The actual device protocol address.
 * @return		EOK on success.
 * @return		EEXIST if another device with the same device identifier
 *			and different driver service exists.
 * @return		ENOMEM if there is not enough memory left.
 * @return		Other error codes as defined for the
 *			measured_strings_return() function.
 */
static int arp_device_message(device_id_t device_id, services_t service,
    services_t protocol, measured_string_t *address)
{
	arp_device_t *device;
	arp_proto_t *proto;
	hw_type_t hardware;
	int index;
	int rc;

	fibril_mutex_lock(&arp_globals.lock);

	/* An existing device? */
	device = arp_cache_find(&arp_globals.cache, device_id);

	if (device) {
		if (device->service != service) {
			printf("Device %d already exists\n", device->device_id);
			fibril_mutex_unlock(&arp_globals.lock);
			return EEXIST;
		}
		proto = arp_protos_find(&device->protos, protocol);
		if (proto) {
			free(proto->addr);
			free(proto->addr_data);
			proto->addr = address;
			proto->addr_data = address->value;
		} else {
			rc = arp_proto_create(&proto, protocol, address);
			if (rc != EOK) {
				fibril_mutex_unlock(&arp_globals.lock);
				return rc;
			}
			index = arp_protos_add(&device->protos, proto->service,
			    proto);
			if (index < 0) {
				fibril_mutex_unlock(&arp_globals.lock);
				free(proto);
				return index;
			}
			printf("New protocol added:\n\tdevice id\t= "
			    "%d\n\tproto\t= %d", device_id, protocol);
		}
	} else {
		hardware = hardware_map(service);
		if (!hardware)
			return ENOENT;
		
		/* Create a new device */
		device = (arp_device_t *) malloc(sizeof(arp_device_t));
		if (!device) {
			fibril_mutex_unlock(&arp_globals.lock);
			return ENOMEM;
		}
		device->hardware = hardware;
		device->device_id = device_id;
		rc = arp_protos_initialize(&device->protos);
		if (rc != EOK) {
			fibril_mutex_unlock(&arp_globals.lock);
			free(device);
			return rc;
		}
		rc = arp_proto_create(&proto, protocol, address);
		if (rc != EOK) {
			fibril_mutex_unlock(&arp_globals.lock);
			free(device);
			return rc;
		}
		index = arp_protos_add(&device->protos, proto->service, proto);
		if (index < 0) {
			fibril_mutex_unlock(&arp_globals.lock);
			arp_protos_destroy(&device->protos);
			free(device);
			return index;
		}
		device->service = service;
		
		/* Bind the new one */
		device->phone = nil_bind_service(device->service,
		    (sysarg_t) device->device_id, SERVICE_ARP,
		    arp_globals.client_connection);
		if (device->phone < 0) {
			fibril_mutex_unlock(&arp_globals.lock);
			arp_protos_destroy(&device->protos);
			free(device);
			return EREFUSED;
		}
		
		/* Get packet dimensions */
		rc = nil_packet_size_req(device->phone, device_id,
		    &device->packet_dimension);
		if (rc != EOK) {
			fibril_mutex_unlock(&arp_globals.lock);
			arp_protos_destroy(&device->protos);
			free(device);
			return rc;
		}
		
		/* Get hardware address */
		rc = nil_get_addr_req(device->phone, device_id, &device->addr,
		    &device->addr_data);
		if (rc != EOK) {
			fibril_mutex_unlock(&arp_globals.lock);
			arp_protos_destroy(&device->protos);
			free(device);
			return rc;
		}
		
		/* Get broadcast address */
		rc = nil_get_broadcast_addr_req(device->phone, device_id,
		    &device->broadcast_addr, &device->broadcast_data);
		if (rc != EOK) {
			fibril_mutex_unlock(&arp_globals.lock);
			free(device->addr);
			free(device->addr_data);
			arp_protos_destroy(&device->protos);
			free(device);
			return rc;
		}
		
		rc = arp_cache_add(&arp_globals.cache, device->device_id,
		    device);
		if (rc != EOK) {
			fibril_mutex_unlock(&arp_globals.lock);
			free(device->addr);
			free(device->addr_data);
			free(device->broadcast_addr);
			free(device->broadcast_data);
			arp_protos_destroy(&device->protos);
			free(device);
			return rc;
		}
		printf("%s: Device registered (id: %d, type: 0x%x, service: %d,"
		    " proto: %d)\n", NAME, device->device_id, device->hardware,
		    device->service, protocol);
	}
	fibril_mutex_unlock(&arp_globals.lock);
	
	return EOK;
}

/** Initializes the ARP module.
 *
 *  @param[in] client_connection The client connection processing function.
 *			The module skeleton propagates its own one.
 *  @return		EOK on success.
 *  @return		ENOMEM if there is not enough memory left.
 */
int arp_initialize(async_client_conn_t client_connection)
{
	int rc;

	fibril_mutex_initialize(&arp_globals.lock);
	fibril_mutex_lock(&arp_globals.lock);
	arp_globals.client_connection = client_connection;
	rc = arp_cache_initialize(&arp_globals.cache);
	fibril_mutex_unlock(&arp_globals.lock);
	
	return rc;
}

/** Updates the device content length according to the new MTU value.
 *
 * @param[in] device_id	The device identifier.
 * @param[in] mtu	The new mtu value.
 * @return		ENOENT if device is not found.
 * @return		EOK on success.
 */
static int arp_mtu_changed_message(device_id_t device_id, size_t mtu)
{
	arp_device_t *device;

	fibril_mutex_lock(&arp_globals.lock);
	device = arp_cache_find(&arp_globals.cache, device_id);
	if (!device) {
		fibril_mutex_unlock(&arp_globals.lock);
		return ENOENT;
	}
	device->packet_dimension.content = mtu;
	fibril_mutex_unlock(&arp_globals.lock);
	printf("arp - device %d changed mtu to %zu\n\n", device_id, mtu);
	return EOK;
}

/** Processes the received ARP packet.
 *
 * Updates the source hardware address if the source entry exists or the packet
 * is targeted to my protocol address.
 * Responses to the ARP request if the packet is the ARP request and is
 * targeted to my address.
 *
 * @param[in] device_id	The source device identifier.
 * @param[in,out] packet The received packet.
 * @return		EOK on success and the packet is no longer needed.
 * @return		One on success and the packet has been reused.
 * @return		EINVAL if the packet is too small to carry an ARP
 *			packet.
 * @return		EINVAL if the received address lengths differs from
 *			the registered values.
 * @return		ENOENT if the device is not found in the cache.
 * @return		ENOENT if the protocol for the device is not found in
 *			the cache.
 * @return		ENOMEM if there is not enough memory left.
 */
static int arp_receive_message(device_id_t device_id, packet_t *packet)
{
	size_t length;
	arp_header_t *header;
	arp_device_t *device;
	arp_proto_t *proto;
	arp_trans_t *trans;
	uint8_t *src_hw;
	uint8_t *src_proto;
	uint8_t *des_hw;
	uint8_t *des_proto;
	int rc;

	length = packet_get_data_length(packet);
	if (length <= sizeof(arp_header_t))
		return EINVAL;

	device = arp_cache_find(&arp_globals.cache, device_id);
	if (!device)
		return ENOENT;

	header = (arp_header_t *) packet_get_data(packet);
	if ((ntohs(header->hardware) != device->hardware) ||
	    (length < sizeof(arp_header_t) + header->hardware_length * 2U +
	    header->protocol_length * 2U)) {
		return EINVAL;
	}

	proto = arp_protos_find(&device->protos,
	    protocol_unmap(device->service, ntohs(header->protocol)));
	if (!proto)
		return ENOENT;

	src_hw = ((uint8_t *) header) + sizeof(arp_header_t);
	src_proto = src_hw + header->hardware_length;
	des_hw = src_proto + header->protocol_length;
	des_proto = des_hw + header->hardware_length;
	trans = arp_addr_find(&proto->addresses, (char *) src_proto,
	    header->protocol_length);
	/* Exists? */
	if (trans && trans->hw_addr) {
		if (trans->hw_addr->length != header->hardware_length)
			return EINVAL;
		memcpy(trans->hw_addr->value, src_hw, trans->hw_addr->length);
	}
	/* Is my protocol address? */
	if (proto->addr->length != header->protocol_length)
		return EINVAL;
	if (!str_lcmp(proto->addr->value, (char *) des_proto,
	    proto->addr->length)) {
		/* Not already updated? */
		if (!trans) {
			trans = (arp_trans_t *) malloc(sizeof(arp_trans_t));
			if (!trans)
				return ENOMEM;
			trans->hw_addr = NULL;
			fibril_condvar_initialize(&trans->cv);
			rc = arp_addr_add(&proto->addresses, (char *) src_proto,
			    header->protocol_length, trans);
			if (rc != EOK) {
				/* The generic char map has already freed trans! */
				return rc;
			}
		}
		if (!trans->hw_addr) {
			trans->hw_addr = measured_string_create_bulk(
			    (char *) src_hw, header->hardware_length);
			if (!trans->hw_addr)
				return ENOMEM;

			/* Notify the fibrils that wait for the translation. */
			fibril_condvar_broadcast(&trans->cv);
		}
		if (ntohs(header->operation) == ARPOP_REQUEST) {
			header->operation = htons(ARPOP_REPLY);
			memcpy(des_proto, src_proto, header->protocol_length);
			memcpy(src_proto, proto->addr->value,
			    header->protocol_length);
			memcpy(src_hw, device->addr->value,
			    device->packet_dimension.addr_len);
			memcpy(des_hw, trans->hw_addr->value,
			    header->hardware_length);
			
			rc = packet_set_addr(packet, src_hw, des_hw,
			    header->hardware_length);
			if (rc != EOK)
				return rc;
			
			nil_send_msg(device->phone, device_id, packet,
			    SERVICE_ARP);
			return 1;
		}
	}

	return EOK;
}


/** Returns the hardware address for the given protocol address.
 *
 * Sends the ARP request packet if the hardware address is not found in the
 * cache.
 *
 * @param[in] device_id	The device identifier.
 * @param[in] protocol	The protocol service.
 * @param[in] target	The target protocol address.
 * @param[out] translation Where the hardware address of the target is stored.
 * @return		EOK on success.
 * @return		EAGAIN if the caller should try again.
 * @return		Other error codes in case of error.
 */
static int
arp_translate_message(device_id_t device_id, services_t protocol,
    measured_string_t *target, measured_string_t **translation)
{
	arp_device_t *device;
	arp_proto_t *proto;
	arp_trans_t *trans;
	size_t length;
	packet_t *packet;
	arp_header_t *header;
	bool retry = false;
	int rc;

restart:
	if (!target || !translation)
		return EBADMEM;

	device = arp_cache_find(&arp_globals.cache, device_id);
	if (!device)
		return ENOENT;

	proto = arp_protos_find(&device->protos, protocol);
	if (!proto || (proto->addr->length != target->length))
		return ENOENT;

	trans = arp_addr_find(&proto->addresses, target->value, target->length);
	if (trans) {
		if (trans->hw_addr) {
			*translation = trans->hw_addr;
			return EOK;
		}
		if (retry)
			return EAGAIN;
		rc = fibril_condvar_wait_timeout(&trans->cv, &arp_globals.lock,
		    ARP_TRANS_WAIT);
		if (rc == ETIMEOUT)
			return ENOENT;
		retry = true;
		goto restart;
	}
	if (retry)
		return EAGAIN;

	/* ARP packet content size = header + (address + translation) * 2 */
	length = 8 + 2 * (proto->addr->length + device->addr->length);
	if (length > device->packet_dimension.content)
		return ELIMIT;

	packet = packet_get_4_remote(arp_globals.net_phone,
	    device->packet_dimension.addr_len, device->packet_dimension.prefix,
	    length, device->packet_dimension.suffix);
	if (!packet)
		return ENOMEM;

	header = (arp_header_t *) packet_suffix(packet, length);
	if (!header) {
		pq_release_remote(arp_globals.net_phone, packet_get_id(packet));
		return ENOMEM;
	}

	header->hardware = htons(device->hardware);
	header->hardware_length = (uint8_t) device->addr->length;
	header->protocol = htons(protocol_map(device->service, protocol));
	header->protocol_length = (uint8_t) proto->addr->length;
	header->operation = htons(ARPOP_REQUEST);
	length = sizeof(arp_header_t);
	memcpy(((uint8_t *) header) + length, device->addr->value,
	    device->addr->length);
	length += device->addr->length;
	memcpy(((uint8_t *) header) + length, proto->addr->value,
	    proto->addr->length);
	length += proto->addr->length;
	bzero(((uint8_t *) header) + length, device->addr->length);
	length += device->addr->length;
	memcpy(((uint8_t *) header) + length, target->value, target->length);

	rc = packet_set_addr(packet, (uint8_t *) device->addr->value,
	    (uint8_t *) device->broadcast_addr->value, device->addr->length);
	if (rc != EOK) {
		pq_release_remote(arp_globals.net_phone, packet_get_id(packet));
		return rc;
	}

	nil_send_msg(device->phone, device_id, packet, SERVICE_ARP);

	trans = (arp_trans_t *) malloc(sizeof(arp_trans_t));
	if (!trans)
		return ENOMEM;
	trans->hw_addr = NULL;
	fibril_condvar_initialize(&trans->cv);
	rc = arp_addr_add(&proto->addresses, target->value, target->length,
	    trans);
	if (rc != EOK) {
		/* The generic char map has already freed trans! */
		return rc;
	}
	
	rc = fibril_condvar_wait_timeout(&trans->cv, &arp_globals.lock,
	    ARP_TRANS_WAIT);
	if (rc == ETIMEOUT)
		return ENOENT;
	retry = true;
	goto restart;
}


/** Processes the ARP message.
 *
 * @param[in] callid	The message identifier.
 * @param[in] call	The message parameters.
 * @param[out] answer	The message answer parameters.
 * @param[out] answer_count The last parameter for the actual answer in the
 *			answer parameter.
 * @return		EOK on success.
 * @return		ENOTSUP if the message is not known.
 *
 * @see arp_interface.h
 * @see IS_NET_ARP_MESSAGE()
 */
int
arp_message_standalone(ipc_callid_t callid, ipc_call_t *call,
    ipc_call_t *answer, int *answer_count)
{
	measured_string_t *address;
	measured_string_t *translation;
	char *data;
	packet_t *packet;
	packet_t *next;
	int rc;
	
	*answer_count = 0;
	switch (IPC_GET_IMETHOD(*call)) {
	case IPC_M_PHONE_HUNGUP:
		return EOK;
	
	case NET_ARP_DEVICE:
		rc = measured_strings_receive(&address, &data, 1);
		if (rc != EOK)
			return rc;
		
		rc = arp_device_message(IPC_GET_DEVICE(call),
		    IPC_GET_SERVICE(call), ARP_GET_NETIF(call), address);
		if (rc != EOK) {
			free(address);
			free(data);
		}
		return rc;
	
	case NET_ARP_TRANSLATE:
		rc = measured_strings_receive(&address, &data, 1);
		if (rc != EOK)
			return rc;
		
		fibril_mutex_lock(&arp_globals.lock);
		rc = arp_translate_message(IPC_GET_DEVICE(call),
		    IPC_GET_SERVICE(call), address, &translation);
		free(address);
		free(data);
		if (rc != EOK) {
			fibril_mutex_unlock(&arp_globals.lock);
			return rc;
		}
		if (!translation) {
			fibril_mutex_unlock(&arp_globals.lock);
			return ENOENT;
		}
		rc = measured_strings_reply(translation, 1);
		fibril_mutex_unlock(&arp_globals.lock);
		return rc;

	case NET_ARP_CLEAR_DEVICE:
		return arp_clear_device_req(0, IPC_GET_DEVICE(call));

	case NET_ARP_CLEAR_ADDRESS:
		rc = measured_strings_receive(&address, &data, 1);
		if (rc != EOK)
			return rc;
		
		arp_clear_address_req(0, IPC_GET_DEVICE(call),
		    IPC_GET_SERVICE(call), address);
		free(address);
		free(data);
		return EOK;
	
	case NET_ARP_CLEAN_CACHE:
		return arp_clean_cache_req(0);
	
	case NET_IL_DEVICE_STATE:
		/* Do nothing - keep the cache */
		return EOK;
	
	case NET_IL_RECEIVED:
		rc = packet_translate_remote(arp_globals.net_phone, &packet,
		    IPC_GET_PACKET(call));
		if (rc != EOK)
			return rc;
		
		fibril_mutex_lock(&arp_globals.lock);
		do {
			next = pq_detach(packet);
			rc = arp_receive_message(IPC_GET_DEVICE(call), packet);
			if (rc != 1) {
				pq_release_remote(arp_globals.net_phone,
				    packet_get_id(packet));
			}
			packet = next;
		} while (packet);
		fibril_mutex_unlock(&arp_globals.lock);
		
		return EOK;
	
	case NET_IL_MTU_CHANGED:
		return arp_mtu_changed_message(IPC_GET_DEVICE(call),
		    IPC_GET_MTU(call));
	}
	
	return ENOTSUP;
}

/** Default thread for new connections.
 *
 * @param[in] iid	The initial message identifier.
 * @param[in] icall	The initial message call structure.
 */
static void il_client_connection(ipc_callid_t iid, ipc_call_t *icall)
{
	/*
	 * Accept the connection
	 *  - Answer the first IPC_M_CONNECT_ME_TO call.
	 */
	ipc_answer_0(iid, EOK);
	
	while (true) {
		ipc_call_t answer;
		int answer_count;
		
		/* Clear the answer structure */
		refresh_answer(&answer, &answer_count);
		
		/* Fetch the next message */
		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);
		
		/* Process the message */
		int res = il_module_message_standalone(callid, &call, &answer,
		    &answer_count);
		
		/*
		 * End if told to either by the message or the processing
		 * result.
		 */
		if ((IPC_GET_IMETHOD(call) == IPC_M_PHONE_HUNGUP) ||
		    (res == EHANGUP))
			return;
		
		/* Answer the message */
		answer_call(callid, res, &answer, answer_count);
	}
}

/** Starts the module.
 *
 * @return		EOK on success.
 * @return		Other error codes as defined for each specific module
 *			start function.
 */
int main(int argc, char *argv[])
{
	int rc;
	
	/* Start the module */
	rc = il_module_start_standalone(il_client_connection);
	return rc;
}

/** @}
 */

