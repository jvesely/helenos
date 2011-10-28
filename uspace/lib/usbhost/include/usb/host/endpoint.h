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
 *
 */
#ifndef LIBUSBHOST_HOST_ENDPOINT_H
#define LIBUSBHOST_HOST_ENDPOINT_H

#include <bool.h>
#include <adt/list.h>
#include <fibril_synch.h>
#include <usb/usb.h>

typedef struct endpoint {
	link_t link;
	usb_address_t address;
	usb_endpoint_t endpoint;
	usb_direction_t direction;
	usb_transfer_type_t transfer_type;
	usb_speed_t speed;
	size_t max_packet_size;
	size_t bandwidth;
	unsigned toggle:1;
	fibril_mutex_t guard;
	fibril_condvar_t avail;
	volatile bool active;
	struct {
		void *data;
		int (*toggle_get)(void *);
		void (*toggle_set)(void *, int);
	} hc_data;
} endpoint_t;

endpoint_t * endpoint_create(usb_address_t address, usb_endpoint_t endpoint,
    usb_direction_t direction, usb_transfer_type_t type, usb_speed_t speed,
    size_t max_packet_size, size_t bw);
void endpoint_destroy(endpoint_t *instance);

void endpoint_set_hc_data(endpoint_t *instance,
    void *data, int (*toggle_get)(void *), void (*toggle_set)(void *, int));
void endpoint_clear_hc_data(endpoint_t *instance);

void endpoint_use(endpoint_t *instance);
void endpoint_release(endpoint_t *instance);

int endpoint_toggle_get(endpoint_t *instance);
void endpoint_toggle_set(endpoint_t *instance, int toggle);

static inline endpoint_t * endpoint_get_instance(link_t *item)
{
	return list_get_instance(item, endpoint_t, link);
}
#endif
/**
 * @}
 */
