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

/** @addtogroup drvusbvhc
 * @{
 */
/** @file
 * Host controller interface implementation.
 */
#include <assert.h>
#include <errno.h>
#include <usb/usb.h>
#include <usb/addrkeep.h>
#include <usb/ddfiface.h>
#include <usb/debug.h>
#include <usbhc_iface.h>
#include "vhcd.h"

#define GET_VHC_DATA(fun) \
	((vhc_data_t *)fun->dev->driver_data)
#define VHC_DATA(vhc, fun) \
	vhc_data_t *vhc = GET_VHC_DATA(fun); assert(vhc->magic == 0xdeadbeef)

#define UNSUPPORTED(methodname) \
	usb_log_warning("Unsupported interface method `%s()' in %s:%d.\n", \
	    methodname, __FILE__, __LINE__)

/** Found free USB address.
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] speed Speed of the device that will get this address.
 * @param[out] address Non-null pointer where to store the free address.
 * @return Error code.
 */
static int request_address(ddf_fun_t *fun, usb_speed_t speed,
    usb_address_t *address)
{
	VHC_DATA(vhc, fun);

	usb_address_t addr = device_keeper_get_free_address(&vhc->dev_keeper,
	    USB_SPEED_HIGH);
	if (addr < 0) {
		return addr;
	}

	if (address != NULL) {
		*address = addr;
	}

	return EOK;
}

/** Bind USB address with device devman handle.
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] address USB address of the device.
 * @param[in] handle Devman handle of the device.
 * @return Error code.
 */
static int bind_address(ddf_fun_t *fun,
    usb_address_t address, devman_handle_t handle)
{
	VHC_DATA(vhc, fun);
	usb_log_debug("Binding handle %" PRIun " to address %d.\n",
	    handle, address);
	usb_device_keeper_bind(&vhc->dev_keeper, address, handle);

	return EOK;
}

/** Find device handle by address interface function.
 *
 * @param[in] fun DDF function that was called.
 * @param[in] address Address in question.
 * @param[out] handle Where to store device handle if found.
 * @return Error code.
 */
static int find_by_address(ddf_fun_t *fun, usb_address_t address,
    devman_handle_t *handle)
{
	VHC_DATA(vhc, fun);
	bool found =
	    usb_device_keeper_find_by_address(&vhc->dev_keeper, address, handle);
	return found ? EOK : ENOENT;
}

/** Release previously requested address.
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] address USB address to be released.
 * @return Error code.
 */
static int release_address(ddf_fun_t *fun, usb_address_t address)
{
	VHC_DATA(vhc, fun);
	usb_log_debug("Releasing address %d...\n", address);
	usb_device_keeper_release(&vhc->dev_keeper, address);

	return ENOTSUP;
}

/** Register endpoint for bandwidth reservation.
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] address USB address of the device.
 * @param[in] speed Endpoint speed (invalid means to use device one).
 * @param[in] endpoint Endpoint number.
 * @param[in] transfer_type USB transfer type.
 * @param[in] direction Endpoint data direction.
 * @param[in] max_packet_size Max packet size of the endpoint.
 * @param[in] interval Polling interval.
 * @return Error code.
 */
static int register_endpoint(ddf_fun_t *fun,
    usb_address_t address, usb_speed_t speed, usb_endpoint_t endpoint,
    usb_transfer_type_t transfer_type, usb_direction_t direction,
    size_t max_packet_size, unsigned int interval)
{
	VHC_DATA(vhc, fun);

	endpoint_t *ep = malloc(sizeof(endpoint_t));
	if (ep == NULL) {
		return ENOMEM;
	}

	int rc = endpoint_init(ep, address, endpoint, direction, transfer_type,
	    USB_SPEED_FULL, 1);
	if (rc != EOK) {
		free(ep);
		return rc;
	}

	rc = usb_endpoint_manager_register_ep(&vhc->ep_manager, ep, 1);
	if (rc != EOK) {
		endpoint_destroy(ep);
		return rc;
	}

	return EOK;
}

/** Unregister endpoint (free some bandwidth reservation).
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] address USB address of the device.
 * @param[in] endpoint Endpoint number.
 * @param[in] direction Endpoint data direction.
 * @return Error code.
 */
static int unregister_endpoint(ddf_fun_t *fun, usb_address_t address,
    usb_endpoint_t endpoint, usb_direction_t direction)
{
	VHC_DATA(vhc, fun);

	endpoint_t *ep = usb_endpoint_manager_get_ep(&vhc->ep_manager,
	    address, endpoint, direction, NULL);
	if (ep == NULL) {
		return ENOENT;
	}

	int rc = usb_endpoint_manager_unregister_ep(&vhc->ep_manager,
	    address, endpoint, direction);

	return rc;
}

/** Schedule interrupt out transfer.
 *
 * The callback is supposed to be called once the transfer (on the wire) is
 * complete regardless of the outcome.
 * However, the callback could be called only when this function returns
 * with success status (i.e. returns EOK).
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] target Target pipe (address and endpoint number) specification.
 * @param[in] data Data to be sent (in USB endianess, allocated and deallocated
 *	by the caller).
 * @param[in] size Size of the @p data buffer in bytes.
 * @param[in] callback Callback to be issued once the transfer is complete.
 * @param[in] arg Pass-through argument to the callback.
 * @return Error code.
 */
static int interrupt_out(ddf_fun_t *fun, usb_target_t target,
    void *data, size_t size,
    usbhc_iface_transfer_out_callback_t callback, void *arg)
{
	VHC_DATA(vhc, fun);

	vhc_transfer_t *transfer = vhc_transfer_create(target.address,
	    target.endpoint, USB_DIRECTION_OUT, USB_TRANSFER_INTERRUPT,
	    fun, arg);
	if (transfer == NULL) {
		return ENOMEM;
	}

	transfer->data_buffer = data;
	transfer->data_buffer_size = size;
	transfer->callback_out = callback;

	int rc = vhc_virtdev_add_transfer(vhc, transfer);
	if (rc != EOK) {
		free(transfer);
		return rc;
	}

	return EOK;
}

/** Schedule interrupt in transfer.
 *
 * The callback is supposed to be called once the transfer (on the wire) is
 * complete regardless of the outcome.
 * However, the callback could be called only when this function returns
 * with success status (i.e. returns EOK).
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] target Target pipe (address and endpoint number) specification.
 * @param[in] data Buffer where to store the data (in USB endianess,
 *	allocated and deallocated by the caller).
 * @param[in] size Size of the @p data buffer in bytes.
 * @param[in] callback Callback to be issued once the transfer is complete.
 * @param[in] arg Pass-through argument to the callback.
 * @return Error code.
 */
static int interrupt_in(ddf_fun_t *fun, usb_target_t target,
    void *data, size_t size,
    usbhc_iface_transfer_in_callback_t callback, void *arg)
{
	VHC_DATA(vhc, fun);

	vhc_transfer_t *transfer = vhc_transfer_create(target.address,
	    target.endpoint, USB_DIRECTION_IN, USB_TRANSFER_INTERRUPT,
	    fun, arg);
	if (transfer == NULL) {
		return ENOMEM;
	}

	transfer->data_buffer = data;
	transfer->data_buffer_size = size;
	transfer->callback_in = callback;

	int rc = vhc_virtdev_add_transfer(vhc, transfer);
	if (rc != EOK) {
		free(transfer);
		return rc;
	}

	return EOK;
}

/** Schedule bulk out transfer.
 *
 * The callback is supposed to be called once the transfer (on the wire) is
 * complete regardless of the outcome.
 * However, the callback could be called only when this function returns
 * with success status (i.e. returns EOK).
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] target Target pipe (address and endpoint number) specification.
 * @param[in] data Data to be sent (in USB endianess, allocated and deallocated
 *	by the caller).
 * @param[in] size Size of the @p data buffer in bytes.
 * @param[in] callback Callback to be issued once the transfer is complete.
 * @param[in] arg Pass-through argument to the callback.
 * @return Error code.
 */
static int bulk_out(ddf_fun_t *fun, usb_target_t target,
    void *data, size_t size,
    usbhc_iface_transfer_out_callback_t callback, void *arg)
{
	UNSUPPORTED("bulk_out");

	return ENOTSUP;
}

/** Schedule bulk in transfer.
 *
 * The callback is supposed to be called once the transfer (on the wire) is
 * complete regardless of the outcome.
 * However, the callback could be called only when this function returns
 * with success status (i.e. returns EOK).
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] target Target pipe (address and endpoint number) specification.
 * @param[in] data Buffer where to store the data (in USB endianess,
 *	allocated and deallocated by the caller).
 * @param[in] size Size of the @p data buffer in bytes.
 * @param[in] callback Callback to be issued once the transfer is complete.
 * @param[in] arg Pass-through argument to the callback.
 * @return Error code.
 */
static int bulk_in(ddf_fun_t *fun, usb_target_t target,
    void *data, size_t size,
    usbhc_iface_transfer_in_callback_t callback, void *arg)
{
	UNSUPPORTED("bulk_in");

	return ENOTSUP;
}

/** Schedule control write transfer.
 *
 * The callback is supposed to be called once the transfer (on the wire) is
 * complete regardless of the outcome.
 * However, the callback could be called only when this function returns
 * with success status (i.e. returns EOK).
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] target Target pipe (address and endpoint number) specification.
 * @param[in] setup_packet Setup packet buffer (in USB endianess, allocated
 *	and deallocated by the caller).
 * @param[in] setup_packet_size Size of @p setup_packet buffer in bytes.
 * @param[in] data_buffer Data buffer (in USB endianess, allocated and
 *	deallocated by the caller).
 * @param[in] data_buffer_size Size of @p data_buffer buffer in bytes.
 * @param[in] callback Callback to be issued once the transfer is complete.
 * @param[in] arg Pass-through argument to the callback.
 * @return Error code.
 */
static int control_write(ddf_fun_t *fun, usb_target_t target,
    void *setup_packet, size_t setup_packet_size,
    void *data_buffer, size_t data_buffer_size,
    usbhc_iface_transfer_out_callback_t callback, void *arg)
{
	VHC_DATA(vhc, fun);

	vhc_transfer_t *transfer = vhc_transfer_create(target.address,
	    target.endpoint, USB_DIRECTION_OUT, USB_TRANSFER_CONTROL,
	    fun, arg);
	if (transfer == NULL) {
		return ENOMEM;
	}

	transfer->setup_buffer = setup_packet;
	transfer->setup_buffer_size = setup_packet_size;
	transfer->data_buffer = data_buffer;
	transfer->data_buffer_size = data_buffer_size;
	transfer->callback_out = callback;

	int rc = vhc_virtdev_add_transfer(vhc, transfer);
	if (rc != EOK) {
		free(transfer);
		return rc;
	}

	return EOK;
}

/** Schedule control read transfer.
 *
 * The callback is supposed to be called once the transfer (on the wire) is
 * complete regardless of the outcome.
 * However, the callback could be called only when this function returns
 * with success status (i.e. returns EOK).
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] target Target pipe (address and endpoint number) specification.
 * @param[in] setup_packet Setup packet buffer (in USB endianess, allocated
 *	and deallocated by the caller).
 * @param[in] setup_packet_size Size of @p setup_packet buffer in bytes.
 * @param[in] data_buffer Buffer where to store the data (in USB endianess,
 *	allocated and deallocated by the caller).
 * @param[in] data_buffer_size Size of @p data_buffer buffer in bytes.
 * @param[in] callback Callback to be issued once the transfer is complete.
 * @param[in] arg Pass-through argument to the callback.
 * @return Error code.
 */
static int control_read(ddf_fun_t *fun, usb_target_t target,
    void *setup_packet, size_t setup_packet_size,
    void *data_buffer, size_t data_buffer_size,
    usbhc_iface_transfer_in_callback_t callback, void *arg)
{
	VHC_DATA(vhc, fun);

	vhc_transfer_t *transfer = vhc_transfer_create(target.address,
	    target.endpoint, USB_DIRECTION_IN, USB_TRANSFER_CONTROL,
	    fun, arg);
	if (transfer == NULL) {
		return ENOMEM;
	}

	transfer->setup_buffer = setup_packet;
	transfer->setup_buffer_size = setup_packet_size;
	transfer->data_buffer = data_buffer;
	transfer->data_buffer_size = data_buffer_size;
	transfer->callback_in = callback;

	int rc = vhc_virtdev_add_transfer(vhc, transfer);
	if (rc != EOK) {
		free(transfer);
		return rc;
	}

	return EOK;
}

static int tell_address(ddf_fun_t *fun, devman_handle_t handle,
    usb_address_t *address)
{
	UNSUPPORTED("tell_address");

	return ENOTSUP;
}

static int usb_iface_get_hc_handle_rh_impl(ddf_fun_t *root_hub_fun,
    devman_handle_t *handle)
{
	VHC_DATA(vhc, root_hub_fun);

	*handle = vhc->hc_fun->handle;

	return EOK;
}

static int tell_address_rh(ddf_fun_t *root_hub_fun, devman_handle_t handle,
    usb_address_t *address)
{
	VHC_DATA(vhc, root_hub_fun);

	if (handle == 0) {
		handle = root_hub_fun->handle;
	}

	usb_log_debug("tell_address_rh(handle=%" PRIun ")\n", handle);
	usb_address_t addr = usb_device_keeper_find(&vhc->dev_keeper, handle);
	if (addr < 0) {
		return addr;
	} else {
		*address = addr;
		return EOK;
	}
}

usbhc_iface_t vhc_iface = {
	.request_address = request_address,
	.bind_address = bind_address,
	.find_by_address = find_by_address,
	.release_address = release_address,

	.register_endpoint = register_endpoint,
	.unregister_endpoint = unregister_endpoint,

	.interrupt_out = interrupt_out,
	.interrupt_in = interrupt_in,

	.bulk_in = bulk_in,
	.bulk_out = bulk_out,

	.control_write = control_write,
	.control_read = control_read
};

usb_iface_t vhc_usb_iface = {
	.get_hc_handle = usb_iface_get_hc_handle_hc_impl,
	.get_address = tell_address
};

usb_iface_t rh_usb_iface = {
	.get_hc_handle = usb_iface_get_hc_handle_rh_impl,
	.get_address = tell_address_rh
};


/**
 * @}
 */
