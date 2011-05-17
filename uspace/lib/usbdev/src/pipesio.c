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

/** @addtogroup libusb
 * @{
 */
/** @file
 * Input and output functions (reads and writes) on endpoint pipes.
 *
 * Note on synchronousness of the operations: there is ABSOLUTELY NO
 * guarantee that a call to particular function will not trigger a fibril
 * switch.
 *
 * Note about the implementation: the transfer requests are always divided
 * into two functions.
 * The outer one does checking of input parameters (e.g. that session was
 * already started, buffers are not NULL etc), while the inner one
 * (with _no_checks suffix) does the actual IPC (it checks for IPC errors,
 * obviously).
 */
#include <usb/usb.h>
#include <usb/dev/pipes.h>
#include <errno.h>
#include <assert.h>
#include <usbhc_iface.h>
#include <usb/dev/request.h>
#include "pipepriv.h"

/** Request an in transfer, no checking of input parameters.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[out] buffer Buffer where to store the data.
 * @param[in] size Size of the buffer (in bytes).
 * @param[out] size_transfered Number of bytes that were actually transfered.
 * @return Error code.
 */
static int usb_pipe_read_no_checks(usb_pipe_t *pipe,
    void *buffer, size_t size, size_t *size_transfered)
{
	/*
	 * Get corresponding IPC method.
	 * In future, replace with static array of mappings
	 * transfer type -> method.
	 */
	usbhc_iface_funcs_t ipc_method;
	switch (pipe->transfer_type) {
		case USB_TRANSFER_INTERRUPT:
			ipc_method = IPC_M_USBHC_INTERRUPT_IN;
			break;
		case USB_TRANSFER_BULK:
			ipc_method = IPC_M_USBHC_BULK_IN;
			break;
		default:
			return ENOTSUP;
	}

	/* Ensure serialization over the phone. */
	pipe_start_transaction(pipe);

	/*
	 * Make call identifying target USB device and type of transfer.
	 */
	aid_t opening_request = async_send_3(pipe->hc_phone,
	    DEV_IFACE_ID(USBHC_DEV_IFACE), ipc_method,
	    pipe->wire->address, pipe->endpoint_no,
	    NULL);
	if (opening_request == 0) {
		pipe_end_transaction(pipe);
		return ENOMEM;
	}

	/*
	 * Retrieve the data.
	 */
	ipc_call_t data_request_call;
	aid_t data_request = async_data_read(pipe->hc_phone, buffer, size,
	    &data_request_call);

	/*
	 * Since now on, someone else might access the backing phone
	 * without breaking the transfer IPC protocol.
	 */
	pipe_end_transaction(pipe);

	if (data_request == 0) {
		/*
		 * FIXME:
		 * How to let the other side know that we want to abort?
		 */
		async_wait_for(opening_request, NULL);
		return ENOMEM;
	}

	/*
	 * Wait for the answer.
	 */
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

	*size_transfered = IPC_GET_ARG2(data_request_call);

	return EOK;
}


/** Request a read (in) transfer on an endpoint pipe.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[out] buffer Buffer where to store the data.
 * @param[in] size Size of the buffer (in bytes).
 * @param[out] size_transfered Number of bytes that were actually transfered.
 * @return Error code.
 */
int usb_pipe_read(usb_pipe_t *pipe,
    void *buffer, size_t size, size_t *size_transfered)
{
	assert(pipe);

	if (buffer == NULL) {
		return EINVAL;
	}

	if (size == 0) {
		return EINVAL;
	}

	if (pipe->direction != USB_DIRECTION_IN) {
		return EBADF;
	}

	if (pipe->transfer_type == USB_TRANSFER_CONTROL) {
		return EBADF;
	}

	int rc;
	rc = pipe_add_ref(pipe, false);
	if (rc != EOK) {
		return rc;
	}


	size_t act_size = 0;

	rc = usb_pipe_read_no_checks(pipe, buffer, size, &act_size);

	pipe_drop_ref(pipe);

	if (rc != EOK) {
		return rc;
	}

	if (size_transfered != NULL) {
		*size_transfered = act_size;
	}

	return EOK;
}




/** Request an out transfer, no checking of input parameters.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[in] buffer Buffer with data to transfer.
 * @param[in] size Size of the buffer (in bytes).
 * @return Error code.
 */
static int usb_pipe_write_no_check(usb_pipe_t *pipe,
    void *buffer, size_t size)
{
	/*
	 * Get corresponding IPC method.
	 * In future, replace with static array of mappings
	 * transfer type -> method.
	 */
	usbhc_iface_funcs_t ipc_method;
	switch (pipe->transfer_type) {
		case USB_TRANSFER_INTERRUPT:
			ipc_method = IPC_M_USBHC_INTERRUPT_OUT;
			break;
		case USB_TRANSFER_BULK:
			ipc_method = IPC_M_USBHC_BULK_OUT;
			break;
		default:
			return ENOTSUP;
	}

	/* Ensure serialization over the phone. */
	pipe_start_transaction(pipe);

	/*
	 * Make call identifying target USB device and type of transfer.
	 */
	aid_t opening_request = async_send_3(pipe->hc_phone,
	    DEV_IFACE_ID(USBHC_DEV_IFACE), ipc_method,
	    pipe->wire->address, pipe->endpoint_no,
	    NULL);
	if (opening_request == 0) {
		pipe_end_transaction(pipe);
		return ENOMEM;
	}

	/*
	 * Send the data.
	 */
	int rc = async_data_write_start(pipe->hc_phone, buffer, size);

	/*
	 * Since now on, someone else might access the backing phone
	 * without breaking the transfer IPC protocol.
	 */
	pipe_end_transaction(pipe);

	if (rc != EOK) {
		async_wait_for(opening_request, NULL);
		return rc;
	}

	/*
	 * Wait for the answer.
	 */
	sysarg_t opening_request_rc;
	async_wait_for(opening_request, &opening_request_rc);

	return (int) opening_request_rc;
}

/** Request a write (out) transfer on an endpoint pipe.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[in] buffer Buffer with data to transfer.
 * @param[in] size Size of the buffer (in bytes).
 * @return Error code.
 */
int usb_pipe_write(usb_pipe_t *pipe,
    void *buffer, size_t size)
{
	assert(pipe);

	if (buffer == NULL) {
		return EINVAL;
	}

	if (size == 0) {
		return EINVAL;
	}

	if (pipe->direction != USB_DIRECTION_OUT) {
		return EBADF;
	}

	if (pipe->transfer_type == USB_TRANSFER_CONTROL) {
		return EBADF;
	}

	int rc;

	rc = pipe_add_ref(pipe, false);
	if (rc != EOK) {
		return rc;
	}

	rc = usb_pipe_write_no_check(pipe, buffer, size);

	pipe_drop_ref(pipe);

	return rc;
}

/** Try to clear endpoint halt of default control pipe.
 *
 * @param pipe Pipe for control endpoint zero.
 */
static void clear_self_endpoint_halt(usb_pipe_t *pipe)
{
	assert(pipe != NULL);

	if (!pipe->auto_reset_halt || (pipe->endpoint_no != 0)) {
		return;
	}


	/* Prevent indefinite recursion. */
	pipe->auto_reset_halt = false;
	usb_request_clear_endpoint_halt(pipe, 0);
	pipe->auto_reset_halt = true;
}


/** Request a control read transfer, no checking of input parameters.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[in] setup_buffer Buffer with the setup packet.
 * @param[in] setup_buffer_size Size of the setup packet (in bytes).
 * @param[out] data_buffer Buffer for incoming data.
 * @param[in] data_buffer_size Size of the buffer for incoming data (in bytes).
 * @param[out] data_transfered_size Number of bytes that were actually
 *                                  transfered during the DATA stage.
 * @return Error code.
 */
static int usb_pipe_control_read_no_check(usb_pipe_t *pipe,
    void *setup_buffer, size_t setup_buffer_size,
    void *data_buffer, size_t data_buffer_size, size_t *data_transfered_size)
{
	/* Ensure serialization over the phone. */
	pipe_start_transaction(pipe);

	/*
	 * Make call identifying target USB device and control transfer type.
	 */
	aid_t opening_request = async_send_3(pipe->hc_phone,
	    DEV_IFACE_ID(USBHC_DEV_IFACE), IPC_M_USBHC_CONTROL_READ,
	    pipe->wire->address, pipe->endpoint_no,
	    NULL);
	if (opening_request == 0) {
		return ENOMEM;
	}

	/*
	 * Send the setup packet.
	 */
	int rc = async_data_write_start(pipe->hc_phone,
	    setup_buffer, setup_buffer_size);
	if (rc != EOK) {
		pipe_end_transaction(pipe);
		async_wait_for(opening_request, NULL);
		return rc;
	}

	/*
	 * Retrieve the data.
	 */
	ipc_call_t data_request_call;
	aid_t data_request = async_data_read(pipe->hc_phone,
	    data_buffer, data_buffer_size,
	    &data_request_call);

	/*
	 * Since now on, someone else might access the backing phone
	 * without breaking the transfer IPC protocol.
	 */
	pipe_end_transaction(pipe);


	if (data_request == 0) {
		async_wait_for(opening_request, NULL);
		return ENOMEM;
	}

	/*
	 * Wait for the answer.
	 */
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

/** Request a control read transfer on an endpoint pipe.
 *
 * This function encapsulates all three stages of a control transfer.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[in] setup_buffer Buffer with the setup packet.
 * @param[in] setup_buffer_size Size of the setup packet (in bytes).
 * @param[out] data_buffer Buffer for incoming data.
 * @param[in] data_buffer_size Size of the buffer for incoming data (in bytes).
 * @param[out] data_transfered_size Number of bytes that were actually
 *                                  transfered during the DATA stage.
 * @return Error code.
 */
int usb_pipe_control_read(usb_pipe_t *pipe,
    void *setup_buffer, size_t setup_buffer_size,
    void *data_buffer, size_t data_buffer_size, size_t *data_transfered_size)
{
	assert(pipe);

	if ((setup_buffer == NULL) || (setup_buffer_size == 0)) {
		return EINVAL;
	}

	if ((data_buffer == NULL) || (data_buffer_size == 0)) {
		return EINVAL;
	}

	if ((pipe->direction != USB_DIRECTION_BOTH)
	    || (pipe->transfer_type != USB_TRANSFER_CONTROL)) {
		return EBADF;
	}

	int rc;

	rc = pipe_add_ref(pipe, false);
	if (rc != EOK) {
		return rc;
	}

	size_t act_size = 0;
	rc = usb_pipe_control_read_no_check(pipe,
	    setup_buffer, setup_buffer_size,
	    data_buffer, data_buffer_size, &act_size);

	if (rc == ESTALL) {
		clear_self_endpoint_halt(pipe);
	}

	pipe_drop_ref(pipe);

	if (rc != EOK) {
		return rc;
	}

	if (data_transfered_size != NULL) {
		*data_transfered_size = act_size;
	}

	return EOK;
}


/** Request a control write transfer, no checking of input parameters.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[in] setup_buffer Buffer with the setup packet.
 * @param[in] setup_buffer_size Size of the setup packet (in bytes).
 * @param[in] data_buffer Buffer with data to be sent.
 * @param[in] data_buffer_size Size of the buffer with outgoing data (in bytes).
 * @return Error code.
 */
static int usb_pipe_control_write_no_check(usb_pipe_t *pipe,
    void *setup_buffer, size_t setup_buffer_size,
    void *data_buffer, size_t data_buffer_size)
{
	/* Ensure serialization over the phone. */
	pipe_start_transaction(pipe);

	/*
	 * Make call identifying target USB device and control transfer type.
	 */
	aid_t opening_request = async_send_4(pipe->hc_phone,
	    DEV_IFACE_ID(USBHC_DEV_IFACE), IPC_M_USBHC_CONTROL_WRITE,
	    pipe->wire->address, pipe->endpoint_no,
	    data_buffer_size,
	    NULL);
	if (opening_request == 0) {
		pipe_end_transaction(pipe);
		return ENOMEM;
	}

	/*
	 * Send the setup packet.
	 */
	int rc = async_data_write_start(pipe->hc_phone,
	    setup_buffer, setup_buffer_size);
	if (rc != EOK) {
		pipe_end_transaction(pipe);
		async_wait_for(opening_request, NULL);
		return rc;
	}

	/*
	 * Send the data (if any).
	 */
	if (data_buffer_size > 0) {
		rc = async_data_write_start(pipe->hc_phone,
		    data_buffer, data_buffer_size);

		/* All data sent, pipe can be released. */
		pipe_end_transaction(pipe);

		if (rc != EOK) {
			async_wait_for(opening_request, NULL);
			return rc;
		}
	} else {
		/* No data to send, we can release the pipe for others. */
		pipe_end_transaction(pipe);
	}

	/*
	 * Wait for the answer.
	 */
	sysarg_t opening_request_rc;
	async_wait_for(opening_request, &opening_request_rc);

	return (int) opening_request_rc;
}

/** Request a control write transfer on an endpoint pipe.
 *
 * This function encapsulates all three stages of a control transfer.
 *
 * @param[in] pipe Pipe used for the transfer.
 * @param[in] setup_buffer Buffer with the setup packet.
 * @param[in] setup_buffer_size Size of the setup packet (in bytes).
 * @param[in] data_buffer Buffer with data to be sent.
 * @param[in] data_buffer_size Size of the buffer with outgoing data (in bytes).
 * @return Error code.
 */
int usb_pipe_control_write(usb_pipe_t *pipe,
    void *setup_buffer, size_t setup_buffer_size,
    void *data_buffer, size_t data_buffer_size)
{
	assert(pipe);

	if ((setup_buffer == NULL) || (setup_buffer_size == 0)) {
		return EINVAL;
	}

	if ((data_buffer == NULL) && (data_buffer_size > 0)) {
		return EINVAL;
	}

	if ((data_buffer != NULL) && (data_buffer_size == 0)) {
		return EINVAL;
	}

	if ((pipe->direction != USB_DIRECTION_BOTH)
	    || (pipe->transfer_type != USB_TRANSFER_CONTROL)) {
		return EBADF;
	}

	int rc;

	rc = pipe_add_ref(pipe, false);
	if (rc != EOK) {
		return rc;
	}

	rc = usb_pipe_control_write_no_check(pipe,
	    setup_buffer, setup_buffer_size, data_buffer, data_buffer_size);

	if (rc == ESTALL) {
		clear_self_endpoint_halt(pipe);
	}

	pipe_drop_ref(pipe);

	return rc;
}


/**
 * @}
 */
