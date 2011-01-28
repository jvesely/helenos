/*
 * Copyright (c) 2010 Jan Vesely
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
/** @addtogroup usb
 * @{
 */
/** @file
 * @brief UHCI driver
 */
#ifndef DRV_UHCI_CALLBACK_H
#define DRV_UHCI_CALLBACK_H

#include <usbhc_iface.h>
#include <usb/usb.h>

#include "debug.h"
#include "translating_malloc.h"

typedef struct callback
{
	usbhc_iface_transfer_in_callback_t callback_in;
	usbhc_iface_transfer_out_callback_t callback_out;
	void *old_buffer;
	void *new_buffer;
	void *arg;
	size_t buffer_size;
	size_t actual_size;
	device_t *dev;
} callback_t;


int callback_init(callback_t *instance, device_t *dev,
  void *buffer, size_t size, usbhc_iface_transfer_in_callback_t func_in,
  usbhc_iface_transfer_out_callback_t func_out, void *arg);

#define callback_in_init(instance, dev, buffer, size, func, arg) \
	callback_init(instance, dev, buffer, size, func, NULL, arg)

#define callback_out_init(instance, dev, buffer, size, func, arg) \
	callback_init(instance, dev, buffer, size, func, NULL, arg)

static inline callback_t *callback_get(device_t *dev,
  void *buffer, size_t size, usbhc_iface_transfer_in_callback_t func_in,
  usbhc_iface_transfer_out_callback_t func_out, void *arg)
{
	callback_t *instance = malloc(sizeof(callback_t));
	if (callback_init(instance, dev, buffer, size, func_in, func_out, arg)) {
		free(instance);
		instance = NULL;
	}
	return instance;
}

static inline void callback_fini(callback_t *instance)
{
	assert(instance);
	if (instance->new_buffer)
		trans_free(instance->new_buffer);
}

static inline void callback_dispose(callback_t *instance)
{
	callback_fini(instance);
	free(instance);
}

void callback_run(
  callback_t *instance, usb_transaction_outcome_t outcome, size_t act_size);
#endif
/**
 * @}
 */
