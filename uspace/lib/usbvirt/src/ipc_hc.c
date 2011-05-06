/*
 * Copyright (c) 2011 Vojtech Horky
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

/** @addtogroup libusbvirt
 * @{
 */
/** @file
 * IPC wrappers, host controller side.
 */
#include <errno.h>
#include <str.h>
#include <stdio.h>
#include <assert.h>
#include <async.h>
#include <devman.h>
#include <usbvirt/device.h>
#include <usbvirt/ipc.h>
#include <usb/debug.h>

int usbvirt_ipc_send_control_read(int phone, usb_endpoint_t ep,
    void *setup_buffer, size_t setup_buffer_size,
    void *data_buffer, size_t data_buffer_size, size_t *data_transfered_size)
{
	aid_t opening_request = async_send_1(phone,
	    IPC_M_USBVIRT_CONTROL_READ, ep, NULL);
	if (opening_request == 0) {
		return ENOMEM;
	}

	int rc = async_data_write_start(phone,
	    setup_buffer, setup_buffer_size);
	if (rc != EOK) {
		async_wait_for(opening_request, NULL);
		return rc;
	}

	ipc_call_t data_request_call;
	aid_t data_request = async_data_read(phone,
	    data_buffer, data_buffer_size,
	    &data_request_call);

	if (data_request == 0) {
		async_wait_for(opening_request, NULL);
		return ENOMEM;
	}

	sysarg_t data_request_rc;
	sysarg_t opening_request_rc;
	async_wait_for(data_request, &data_request_rc);
	async_wait_for(opening_request, &opening_request_rc);

	if (data_request_rc != EOK) {
		/* Prefer the return code of the opening request. */
		if (opening_request_rc != EOK) {
			return (int) opening_request_rc;
		} else {
			return (int) data_request_rc;
		}
	}
	if (opening_request_rc != EOK) {
		return (int) opening_request_rc;
	}

	*data_transfered_size = IPC_GET_ARG2(data_request_call);

	return EOK;
}

int usbvirt_ipc_send_control_write(int phone, usb_endpoint_t ep,
    void *setup_buffer, size_t setup_buffer_size,
    void *data_buffer, size_t data_buffer_size)
{
	aid_t opening_request = async_send_2(phone,
	    IPC_M_USBVIRT_CONTROL_WRITE, ep, data_buffer_size,  NULL);
	if (opening_request == 0) {
		return ENOMEM;
	}

	int rc = async_data_write_start(phone,
	    setup_buffer, setup_buffer_size);
	if (rc != EOK) {
		async_wait_for(opening_request, NULL);
		return rc;
	}

	if (data_buffer_size > 0) {
		rc = async_data_write_start(phone,
		    data_buffer, data_buffer_size);

		if (rc != EOK) {
			async_wait_for(opening_request, NULL);
			return rc;
		}
	}

	sysarg_t opening_request_rc;
	async_wait_for(opening_request, &opening_request_rc);

	return (int) opening_request_rc;
}

int usbvirt_ipc_send_data_in(int phone, usb_endpoint_t ep,
    usb_transfer_type_t tr_type, void *data, size_t data_size, size_t *act_size)
{
	aid_t opening_request = async_send_2(phone,
	    IPC_M_USBVIRT_INTERRUPT_IN, ep, tr_type, NULL);
	if (opening_request == 0) {
		return ENOMEM;
	}

	ipc_call_t data_request_call;
	aid_t data_request = async_data_read(phone,
	    data, data_size,  &data_request_call);

	if (data_request == 0) {
		async_wait_for(opening_request, NULL);
		return ENOMEM;
	}

	sysarg_t data_request_rc;
	sysarg_t opening_request_rc;
	async_wait_for(data_request, &data_request_rc);
	async_wait_for(opening_request, &opening_request_rc);

	if (data_request_rc != EOK) {
		/* Prefer the return code of the opening request. */
		if (opening_request_rc != EOK) {
			return (int) opening_request_rc;
		} else {
			return (int) data_request_rc;
		}
	}
	if (opening_request_rc != EOK) {
		return (int) opening_request_rc;
	}

	if (act_size != NULL) {
		*act_size = IPC_GET_ARG2(data_request_call);
	}

	return EOK;
}

int usbvirt_ipc_send_data_out(int phone, usb_endpoint_t ep,
    usb_transfer_type_t tr_type, void *data, size_t data_size)
{
	aid_t opening_request = async_send_2(phone,
	    IPC_M_USBVIRT_INTERRUPT_OUT, ep, tr_type, NULL);
	if (opening_request == 0) {
		return ENOMEM;
	}

	int rc = async_data_write_start(phone,
	    data, data_size);
	if (rc != EOK) {
		async_wait_for(opening_request, NULL);
		return rc;
	}

	sysarg_t opening_request_rc;
	async_wait_for(opening_request, &opening_request_rc);

	return (int) opening_request_rc;
}


/**
 * @}
 */
