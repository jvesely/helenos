/*
 * Copyright (c) 2010 Vojtech Horky
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

/** @addtogroup libdrv
 * @addtogroup usb
 * @{
 */
/** @file
 * @brief USB interface definition.
 */

#ifndef LIBDRV_USB_IFACE_H_
#define LIBDRV_USB_IFACE_H_

#include "ddf/driver.h"
#include <async.h>
#include <usb/usb.h>

typedef async_sess_t usb_dev_session_t;

extern usb_dev_session_t *usb_dev_connect(devman_handle_t);
extern usb_dev_session_t *usb_dev_connect_to_self(ddf_dev_t *);
extern void usb_dev_disconnect(usb_dev_session_t *);

extern int usb_get_my_interface(async_exch_t *, int *);
extern int usb_get_my_device_handle(async_exch_t *, devman_handle_t *);

extern int usb_reserve_default_address(async_exch_t *, usb_speed_t);
extern int usb_release_default_address(async_exch_t *);

extern int usb_device_enumerate(async_exch_t *, unsigned port);
extern int usb_device_remove(async_exch_t *, unsigned port);

extern int usb_register_endpoint(async_exch_t *, usb_endpoint_t,
    usb_transfer_type_t, usb_direction_t, size_t, unsigned, unsigned);
extern int usb_unregister_endpoint(async_exch_t *, usb_endpoint_t,
    usb_direction_t);
extern int usb_read(async_exch_t *, usb_endpoint_t, uint64_t, void *, size_t,
    size_t *);
extern int usb_write(async_exch_t *, usb_endpoint_t, uint64_t, const void *,
    size_t);

/** Callback for outgoing transfer. */
typedef void (*usb_iface_transfer_out_callback_t)(int, void *);

/** Callback for incoming transfer. */
typedef void (*usb_iface_transfer_in_callback_t)(int, size_t, void *);

/** USB device communication interface. */
typedef struct {
	int (*get_my_interface)(ddf_fun_t *, int *);
	int (*get_my_device_handle)(ddf_fun_t *, devman_handle_t *);

	int (*reserve_default_address)(ddf_fun_t *, usb_speed_t);
	int (*release_default_address)(ddf_fun_t *);

	int (*device_enumerate)(ddf_fun_t *, unsigned);
	int (*device_remove)(ddf_fun_t *, unsigned);

	int (*register_endpoint)(ddf_fun_t *, usb_endpoint_t,
	    usb_transfer_type_t, usb_direction_t, size_t, unsigned, unsigned);
	int (*unregister_endpoint)(ddf_fun_t *, usb_endpoint_t,
	    usb_direction_t);

	int (*read)(ddf_fun_t *, usb_endpoint_t, uint64_t, uint8_t *, size_t,
	    usb_iface_transfer_in_callback_t, void *);
	int (*write)(ddf_fun_t *, usb_endpoint_t, uint64_t, const uint8_t *,
	    size_t, usb_iface_transfer_out_callback_t, void *);
} usb_iface_t;

#endif
/**
 * @}
 */
