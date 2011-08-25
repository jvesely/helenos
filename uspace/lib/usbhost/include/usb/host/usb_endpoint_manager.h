/*
 * Copyright (c) 2011 Jan Vesely
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
/** @addtogroup libusbhost
 * @{
 */
/** @file
 * Device keeper structure and functions.
 *
 * Typical USB host controller needs to keep track of various settings for
 * each device that is connected to it.
 * State of toggle bit, device speed etc. etc.
 * This structure shall simplify the management.
 */
#ifndef LIBUSBHOST_HOST_USB_ENDPOINT_MANAGER_H
#define LIBUSBHOST_HOST_USB_ENDPOINT_MANAGER_H

#include <stdlib.h>
#include <adt/hash_table.h>
#include <fibril_synch.h>
#include <usb/usb.h>
#include <usb/host/endpoint.h>

#define BANDWIDTH_TOTAL_USB11 12000000
#define BANDWIDTH_AVAILABLE_USB11 ((BANDWIDTH_TOTAL_USB11 / 10) * 9)

typedef struct usb_endpoint_manager {
	hash_table_t ep_table;
	fibril_mutex_t guard;
	fibril_condvar_t change;
	size_t free_bw;
} usb_endpoint_manager_t;

size_t bandwidth_count_usb11(usb_speed_t speed, usb_transfer_type_t type,
    size_t size, size_t max_packet_size);

int usb_endpoint_manager_init(usb_endpoint_manager_t *instance,
    size_t available_bandwidth);

void usb_endpoint_manager_destroy(usb_endpoint_manager_t *instance);

int usb_endpoint_manager_register_ep(usb_endpoint_manager_t *instance,
    endpoint_t *ep, size_t data_size);

int usb_endpoint_manager_unregister_ep(usb_endpoint_manager_t *instance,
    usb_address_t address, usb_endpoint_t ep, usb_direction_t direction);

endpoint_t * usb_endpoint_manager_get_ep(usb_endpoint_manager_t *instance,
    usb_address_t address, usb_endpoint_t ep, usb_direction_t direction,
    size_t *bw);

void usb_endpoint_manager_reset_if_need(
    usb_endpoint_manager_t *instance, usb_target_t target, const uint8_t *data);

/** Wrapper combining allocation and insertion */
static inline int usb_endpoint_manager_add_ep(usb_endpoint_manager_t *instance,
    usb_address_t address, usb_endpoint_t endpoint, usb_direction_t direction,
    usb_transfer_type_t type, usb_speed_t speed, size_t max_packet_size,
    size_t data_size)
{
	endpoint_t *ep = endpoint_get(
	    address, endpoint, direction, type, speed, max_packet_size);
	if (!ep)
		return ENOMEM;

	const int ret =
	    usb_endpoint_manager_register_ep(instance, ep, data_size);
	if (ret != EOK) {
		endpoint_destroy(ep);
	}
	return ret;
}
#endif
/**
 * @}
 */

