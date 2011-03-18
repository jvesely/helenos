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
 * USB endpoint pipes miscellaneous functions.
 */
#include <usb/usb.h>
#include <usb/pipes.h>
#include <usb/debug.h>
#include <usbhc_iface.h>
#include <usb_iface.h>
#include <devman.h>
#include <errno.h>
#include <assert.h>

/** Tell USB address assigned to given device.
 *
 * @param phone Phone to parent device.
 * @param dev Device in question.
 * @return USB address or error code.
 */
static usb_address_t get_my_address(int phone, ddf_dev_t *dev)
{
	sysarg_t address;

	/*
	 * We are sending special value as a handle - zero - to get
	 * handle of the parent function (that handle was used
	 * when registering our device @p dev.
	 */
	int rc = async_req_2_1(phone, DEV_IFACE_ID(USB_DEV_IFACE),
	    IPC_M_USB_GET_ADDRESS,
	    0, &address);

	if (rc != EOK) {
		return rc;
	}

	return (usb_address_t) address;
}

/** Tell USB interface assigned to given device.
 *
 * @param device Device in question.
 * @return Interface number (negative code means any).
 */
int usb_device_get_assigned_interface(ddf_dev_t *device)
{
	int parent_phone = devman_parent_device_connect(device->handle,
	    IPC_FLAG_BLOCKING);
	if (parent_phone < 0) {
		return -1;
	}

	sysarg_t iface_no;
	int rc = async_req_2_1(parent_phone, DEV_IFACE_ID(USB_DEV_IFACE),
	    IPC_M_USB_GET_INTERFACE,
	    device->handle, &iface_no);

	async_hangup(parent_phone);

	if (rc != EOK) {
		return -1;
	}

	return (int) iface_no;
}

/** Tell USB address assigned to given device.
 *
 * @param dev_handle Devman handle of the USB device in question.
 * @return USB address or negative error code.
 */
usb_address_t usb_device_get_assigned_address(devman_handle_t dev_handle)
{
	int parent_phone = devman_parent_device_connect(dev_handle,
	    IPC_FLAG_BLOCKING);
	if (parent_phone < 0) {
		return parent_phone;
	}

	sysarg_t address;

	int rc = async_req_2_1(parent_phone, DEV_IFACE_ID(USB_DEV_IFACE),
	    IPC_M_USB_GET_ADDRESS,
	    dev_handle, &address);

	if (rc != EOK) {
		return rc;
	}

	async_hangup(parent_phone);

	return (usb_address_t) address;
}

/** Initialize connection to USB device.
 *
 * @param connection Connection structure to be initialized.
 * @param dev Generic device backing the USB device.
 * @return Error code.
 */
int usb_device_connection_initialize_from_device(
    usb_device_connection_t *connection, ddf_dev_t *dev)
{
	assert(connection);
	assert(dev);

	int rc;
	devman_handle_t hc_handle;
	usb_address_t my_address;

	rc = usb_hc_find(dev->handle, &hc_handle);
	if (rc != EOK) {
		return rc;
	}

	int parent_phone = devman_parent_device_connect(dev->handle,
	    IPC_FLAG_BLOCKING);
	if (parent_phone < 0) {
		return parent_phone;
	}

	my_address = get_my_address(parent_phone, dev);
	if (my_address < 0) {
		rc = my_address;
		goto leave;
	}

	rc = usb_device_connection_initialize(connection,
	    hc_handle, my_address);

leave:
	async_hangup(parent_phone);
	return rc;
}

/** Initialize connection to USB device.
 *
 * @param connection Connection structure to be initialized.
 * @param host_controller_handle Devman handle of host controller device is
 * 	connected to.
 * @param device_address Device USB address.
 * @return Error code.
 */
int usb_device_connection_initialize(usb_device_connection_t *connection,
    devman_handle_t host_controller_handle, usb_address_t device_address)
{
	assert(connection);

	if ((device_address < 0) || (device_address >= USB11_ADDRESS_MAX)) {
		return EINVAL;
	}

	connection->hc_handle = host_controller_handle;
	connection->address = device_address;

	return EOK;
}

/** Initialize connection to USB device on default address.
 *
 * @param dev_connection Device connection structure to be initialized.
 * @param hc_connection Initialized connection to host controller.
 * @return Error code.
 */
int usb_device_connection_initialize_on_default_address(
    usb_device_connection_t *dev_connection,
    usb_hc_connection_t *hc_connection)
{
	assert(dev_connection);

	if (hc_connection == NULL) {
		return EBADMEM;
	}

	return usb_device_connection_initialize(dev_connection,
	    hc_connection->hc_handle, (usb_address_t) 0);
}


/** Start a session on the endpoint pipe.
 *
 * A session is something inside what any communication occurs.
 * It is expected that sessions would be started right before the transfer
 * and ended - see usb_endpoint_pipe_end_session() - after the last
 * transfer.
 * The reason for this is that session actually opens some communication
 * channel to the host controller (or to the physical hardware if you
 * wish) and thus it involves acquiring kernel resources.
 * Since they are limited, sessions shall not be longer than strictly
 * necessary.
 *
 * @param pipe Endpoint pipe to start the session on.
 * @return Error code.
 */
int usb_endpoint_pipe_start_session(usb_endpoint_pipe_t *pipe)
{
	assert(pipe);

	if (usb_endpoint_pipe_is_session_started(pipe)) {
		return EBUSY;
	}

	int phone = devman_device_connect(pipe->wire->hc_handle, 0);
	if (phone < 0) {
		return phone;
	}

	pipe->hc_phone = phone;

	return EOK;
}


/** Ends a session on the endpoint pipe.
 *
 * @see usb_endpoint_pipe_start_session
 *
 * @param pipe Endpoint pipe to end the session on.
 * @return Error code.
 */
int usb_endpoint_pipe_end_session(usb_endpoint_pipe_t *pipe)
{
	assert(pipe);

	if (!usb_endpoint_pipe_is_session_started(pipe)) {
		return ENOENT;
	}

	int rc = async_hangup(pipe->hc_phone);
	if (rc != EOK) {
		return rc;
	}

	pipe->hc_phone = -1;

	return EOK;
}

/** Tell whether a session is started (open) on the endpoint pipe.
 *
 * The expected usage of this function is in assertions for some
 * nested functions.
 *
 * @param pipe Endpoint pipe in question.
 * @return Whether @p pipe has opened a session.
 */
bool usb_endpoint_pipe_is_session_started(usb_endpoint_pipe_t *pipe)
{
	return (pipe->hc_phone >= 0);
}

/**
 * @}
 */
