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
 * @{
 */
/** @file
 */

#include <async.h>
#include <errno.h>

#include "usb_iface.h"
#include "driver.h"


static void remote_usb_get_hc_handle(device_t *, void *, ipc_callid_t, ipc_call_t *);
static void remote_usb_get_address(device_t *, void *, ipc_callid_t, ipc_call_t *);
//static void remote_usb(device_t *, void *, ipc_callid_t, ipc_call_t *);

/** Remote USB interface operations. */
static remote_iface_func_ptr_t remote_usb_iface_ops [] = {
	remote_usb_get_address,
	remote_usb_get_hc_handle
};

/** Remote USB interface structure.
 */
remote_iface_t remote_usb_iface = {
	.method_count = sizeof(remote_usb_iface_ops) /
	    sizeof(remote_usb_iface_ops[0]),
	.methods = remote_usb_iface_ops
};


void remote_usb_get_address(device_t *device, void *iface,
    ipc_callid_t callid, ipc_call_t *call)
{
	usb_iface_t *usb_iface = (usb_iface_t *) iface;

	if (usb_iface->get_address == NULL) {
		async_answer_0(callid, ENOTSUP);
		return;
	}

	devman_handle_t handle = DEV_IPC_GET_ARG1(*call);

	usb_address_t address;
	int rc = usb_iface->get_address(device, handle, &address);
	if (rc != EOK) {
		async_answer_0(callid, rc);
	} else {
		async_answer_1(callid, EOK, address);
	}
}


void remote_usb_get_hc_handle(device_t *device, void *iface,
    ipc_callid_t callid, ipc_call_t *call)
{
	usb_iface_t *usb_iface = (usb_iface_t *) iface;

	if (usb_iface->get_hc_handle == NULL) {
		async_answer_0(callid, ENOTSUP);
		return;
	}

	devman_handle_t handle;
	int rc = usb_iface->get_hc_handle(device, &handle);
	if (rc != EOK) {
		async_answer_0(callid, rc);
	}

	async_answer_1(callid, EOK, (sysarg_t) handle);
}



/**
 * @}
 */
