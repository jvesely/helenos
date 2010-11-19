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

/** @addtogroup lo
 * @{
 */

/** @file
 * Loopback network interface implementation.
 */

#include <async.h>
#include <errno.h>
#include <stdio.h>
#include <str.h>

#include <ipc/ipc.h>
#include <ipc/services.h>
#include <ipc/nil.h>

#include <net/modules.h>
#include <adt/measured_strings.h>
#include <packet_client.h>
#include <net/device.h>
#include <nil_interface.h>
#include <netif_interface.h>
#include <netif_local.h>

/** Default hardware address. */
#define DEFAULT_ADDR		"\0\0\0\0\0\0"

/** Default address length. */
#define DEFAULT_ADDR_LEN	(sizeof(DEFAULT_ADDR) / sizeof(char))

/** Loopback module name. */
#define NAME  "lo"

/** Network interface global data. */
netif_globals_t	netif_globals;

int netif_specific_message(ipc_callid_t callid, ipc_call_t *call,
    ipc_call_t *answer, int *answer_count)
{
	return ENOTSUP;
}

int netif_get_addr_message(device_id_t device_id, measured_string_t *address)
{
	if (!address)
		return EBADMEM;

	address->value = str_dup(DEFAULT_ADDR);
	address->length = DEFAULT_ADDR_LEN;

	return EOK;
}

int netif_get_device_stats(device_id_t device_id, device_stats_t *stats)
{
	netif_device_t *device;
	int rc;

	if (!stats)
		return EBADMEM;

	rc = find_device(device_id, &device);
	if (rc != EOK)
		return rc;

	memcpy(stats, (device_stats_t *) device->specific,
	    sizeof(device_stats_t));

	return EOK;
}

/** Changes the loopback state.
 *
 * @param[in] device	The device structure.
 * @param[in] state	The new device state.
 * @return		The new state if changed.
 * @return		EOK otherwise.
 */
static int change_state_message(netif_device_t *device, device_state_t state)
{
	if (device->state != state) {
		device->state = state;
		
		printf("%s: State changed to %s\n", NAME,
		    (state == NETIF_ACTIVE) ? "active" : "stopped");
		
		return state;
	}
	
	return EOK;
}

/** Creates and returns the loopback network interface structure.
 *
 * @param[in] device_id	The new devce identifier.
 * @param[out] device	The device structure.
 * @return		EOK on success.
 * @return		EXDEV if one loopback network interface already exists.
 * @return		ENOMEM if there is not enough memory left.
 */
static int create(device_id_t device_id, netif_device_t **device)
{
	int index;

	if (netif_device_map_count(&netif_globals.device_map) > 0)
		return EXDEV;

	*device = (netif_device_t *) malloc(sizeof(netif_device_t));
	if (!*device)
		return ENOMEM;

	(*device)->specific = (device_stats_t *) malloc(sizeof(device_stats_t));
	if (!(*device)->specific) {
		free(*device);
		return ENOMEM;
	}

	null_device_stats((device_stats_t *) (*device)->specific);
	(*device)->device_id = device_id;
	(*device)->nil_phone = -1;
	(*device)->state = NETIF_STOPPED;
	index = netif_device_map_add(&netif_globals.device_map,
	    (*device)->device_id, *device);

	if (index < 0) {
		free(*device);
		free((*device)->specific);
		*device = NULL;
		return index;
	}
	
	return EOK;
}

int netif_initialize(void)
{
	ipcarg_t phonehash;

	return REGISTER_ME(SERVICE_LO, &phonehash);
}

int netif_probe_message(device_id_t device_id, int irq, uintptr_t io)
{
	netif_device_t *device;
	int rc;

	/* Create a new device */
	rc = create(device_id, &device);
	if (rc != EOK)
		return rc;

	/* Print the settings */
	printf("%s: Device created (id: %d)\n", NAME, device->device_id);

	return EOK;
}

int netif_send_message(device_id_t device_id, packet_t packet, services_t sender)
{
	netif_device_t *device;
	size_t length;
	packet_t next;
	int phone;
	int rc;

	rc = find_device(device_id, &device);
	if (rc != EOK)
		return EOK;

	if (device->state != NETIF_ACTIVE) {
		netif_pq_release(packet_get_id(packet));
		return EFORWARD;
	}

	next = packet;
	do {
		((device_stats_t *) device->specific)->send_packets++;
		((device_stats_t *) device->specific)->receive_packets++;
		length = packet_get_data_length(next);
		((device_stats_t *) device->specific)->send_bytes += length;
		((device_stats_t *) device->specific)->receive_bytes += length;
		next = pq_next(next);
	} while(next);

	phone = device->nil_phone;
	fibril_rwlock_write_unlock(&netif_globals.lock);
	nil_received_msg(phone, device_id, packet, sender);
	fibril_rwlock_write_lock(&netif_globals.lock);
	
	return EOK;
}

int netif_start_message(netif_device_t *device)
{
	return change_state_message(device, NETIF_ACTIVE);
}

int netif_stop_message(netif_device_t *device)
{
	return change_state_message(device, NETIF_STOPPED);
}

/** Default thread for new connections.
 *
 * @param[in] iid	The initial message identifier.
 * @param[in] icall	The initial message call structure.
 */
static void netif_client_connection(ipc_callid_t iid, ipc_call_t *icall)
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
		int res = netif_module_message(NAME, callid, &call, &answer,
		    &answer_count);
		
		/*
		 * End if told to either by the message or the processing
		 * result.
		 */
		if ((IPC_GET_METHOD(call) == IPC_M_PHONE_HUNGUP) ||
		    (res == EHANGUP))
			return;
		
		/* Answer the message */
		answer_call(callid, res, &answer, answer_count);
	}
}

int main(int argc, char *argv[])
{
	int rc;
	
	/* Start the module */
	rc = netif_module_start(netif_client_connection);
	return rc;
}

/** @}
 */
