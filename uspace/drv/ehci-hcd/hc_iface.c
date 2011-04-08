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
/** @addtogroup drvusbehci
 * @{
 */
/** @file
 * USB-HC interface implementation.
 */
#include <ddf/driver.h>
#include <ddf/interrupt.h>
#include <device/hw_res.h>
#include <errno.h>
#include <str_error.h>

#include <usb_iface.h>
#include <usb/ddfiface.h>
#include <usb/debug.h>

#include "ehci.h"

#define UNSUPPORTED(methodname) \
	usb_log_warning("Unsupported interface method `%s()' in %s:%d.\n", \
	    methodname, __FILE__, __LINE__)

/** Reserve default address.
 *
 * This function may block the caller.
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] speed Speed of the device for which the default address is
 *	reserved.
 * @return Error code.
 */
static int reserve_default_address(ddf_fun_t *fun, usb_speed_t speed)
{
	UNSUPPORTED("reserve_default_address");

	return ENOTSUP;
}

/** Release default address.
 *
 * @param[in] fun Device function the action was invoked on.
 * @return Error code.
 */
static int release_default_address(ddf_fun_t *fun)
{
	UNSUPPORTED("release_default_address");

	return ENOTSUP;
}

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
	UNSUPPORTED("request_address");

	return ENOTSUP;
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
	UNSUPPORTED("bind_address");

	return ENOTSUP;
}

/** Release previously requested address.
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] address USB address to be released.
 * @return Error code.
 */
static int release_address(ddf_fun_t *fun, usb_address_t address)
{
	UNSUPPORTED("release_address");

	return ENOTSUP;
}

/** Register endpoint for bandwidth reservation.
 *
 * @param[in] fun Device function the action was invoked on.
 * @param[in] address USB address of the device.
 * @param[in] endpoint Endpoint number.
 * @param[in] transfer_type USB transfer type.
 * @param[in] direction Endpoint data direction.
 * @param[in] max_packet_size Max packet size of the endpoint.
 * @param[in] interval Polling interval.
 * @return Error code.
 */
static int register_endpoint(ddf_fun_t *fun,
    usb_address_t address, usb_endpoint_t endpoint,
    usb_transfer_type_t transfer_type, usb_direction_t direction,
    size_t max_packet_size, unsigned int interval)
{
	UNSUPPORTED("register_endpoint");

	return ENOTSUP;
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
	UNSUPPORTED("unregister_endpoint");

	return ENOTSUP;
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
	UNSUPPORTED("interrupt_out");

	return ENOTSUP;
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
	UNSUPPORTED("interrupt_in");

	return ENOTSUP;
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
	UNSUPPORTED("control_write");

	return ENOTSUP;
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
	UNSUPPORTED("control_read");

	return ENOTSUP;
}

/** Host controller interface implementation for EHCI. */
usbhc_iface_t ehci_hc_iface = {
	.reserve_default_address = reserve_default_address,
	.release_default_address = release_default_address,
	.request_address = request_address,
	.bind_address = bind_address,
	.release_address = release_address,

	.register_endpoint = register_endpoint,
	.unregister_endpoint = unregister_endpoint,

	.interrupt_out = interrupt_out,
	.interrupt_in = interrupt_in,

	.bulk_out = bulk_out,
	.bulk_in = bulk_in,

	.control_write = control_write,
	.control_read = control_read
};

/**
 * @}
 */
