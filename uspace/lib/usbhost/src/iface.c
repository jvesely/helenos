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
 * @brief HCD DDF interface implementation
 */
#include <ddf/driver.h>
#include <errno.h>

#include <usb/debug.h>
#include <usb/host/endpoint.h>
#include <usb/host/hcd.h>

static inline int send_batch(
    ddf_fun_t *fun, usb_target_t target, usb_direction_t direction,
    void *data, size_t size, uint64_t setup_data,
    usbhc_iface_transfer_in_callback_t in,
    usbhc_iface_transfer_out_callback_t out, void *arg, const char* name)
{
	assert(fun);
	hcd_t *hcd = fun_to_hcd(fun);
	assert(hcd);

	endpoint_t *ep = usb_endpoint_manager_find_ep(&hcd->ep_manager,
	    target.address, target.endpoint, direction);
	if (ep == NULL) {
		usb_log_error("Endpoint(%d:%d) not registered for %s.\n",
		    target.address, target.endpoint, name);
		return ENOENT;
	}

	usb_log_debug2("%s %d:%d %zu(%zu).\n",
	    name, target.address, target.endpoint, size, ep->max_packet_size);

	const size_t bw = bandwidth_count_usb11(
	    ep->speed, ep->transfer_type, size, ep->max_packet_size);
	/* Check if we have enough bandwidth reserved */
	if (ep->bandwidth < bw) {
		usb_log_error("Endpoint(%d:%d) %s needs %zu bw "
		    "but only %zu is reserved.\n",
		    ep->address, ep->endpoint, name, bw, ep->bandwidth);
		return ENOSPC;
	}
	if (!hcd->schedule) {
		usb_log_error("HCD does not implement scheduler.\n");
		return ENOTSUP;
	}

	/* No private data and no private data dtor */
	usb_transfer_batch_t *batch =
	    usb_transfer_batch_create(ep, data, size, setup_data,
	    in, out, arg, fun, NULL, NULL);
	if (!batch) {
		return ENOMEM;
	}

	const int ret = hcd->schedule(hcd, batch);
	if (ret != EOK)
		usb_transfer_batch_destroy(batch);

	return ret;
}
/*----------------------------------------------------------------------------*/
/** Request address interface function
 *
 * @param[in] fun DDF function that was called.
 * @param[in] speed Speed to associate with the new default address.
 * @param[out] address Place to write a new address.
 * @return Error code.
 */
static int request_address(
    ddf_fun_t *fun, usb_address_t *address, bool strict, usb_speed_t speed)
{
	assert(fun);
	hcd_t *hcd = fun_to_hcd(fun);
	assert(hcd);
	assert(address);

	usb_log_debug("Address request speed: %s.\n", usb_str_speed(speed));
	return usb_device_manager_request_address(
	    &hcd->dev_manager, address, strict, speed);
}
/*----------------------------------------------------------------------------*/
/** Bind address interface function
 *
 * @param[in] fun DDF function that was called.
 * @param[in] address Address of the device
 * @param[in] handle Devman handle of the device driver.
 * @return Error code.
 */
static int bind_address(
  ddf_fun_t *fun, usb_address_t address, devman_handle_t handle)
{
	assert(fun);
	hcd_t *hcd = fun_to_hcd(fun);
	assert(hcd);

	usb_log_debug("Address bind %d-%" PRIun ".\n", address, handle);
	return usb_device_manager_bind_address(
	    &hcd->dev_manager, address, handle);
}
/*----------------------------------------------------------------------------*/
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
	assert(fun);
	hcd_t *hcd = fun_to_hcd(fun);
	assert(hcd);
	return usb_device_manager_get_info_by_address(
	    &hcd->dev_manager, address, handle, NULL);
}
/*----------------------------------------------------------------------------*/
/** Release address interface function
 *
 * @param[in] fun DDF function that was called.
 * @param[in] address USB address to be released.
 * @return Error code.
 */
static int release_address(ddf_fun_t *fun, usb_address_t address)
{
	assert(fun);
	hcd_t *hcd = fun_to_hcd(fun);
	assert(hcd);
	usb_log_debug("Address release %d.\n", address);
	usb_device_manager_release_address(&hcd->dev_manager, address);
	return EOK;
}
/*----------------------------------------------------------------------------*/
static int register_helper(endpoint_t *ep, void *arg)
{
	hcd_t *hcd = arg;
	assert(ep);
	assert(hcd);
	if (hcd->ep_add_hook)
		return hcd->ep_add_hook(hcd, ep);
	return EOK;
}
/*----------------------------------------------------------------------------*/
static void unregister_helper(endpoint_t *ep, void *arg)
{
	hcd_t *hcd = arg;
	assert(ep);
	assert(hcd);
	if (hcd->ep_remove_hook)
		hcd->ep_remove_hook(hcd, ep);
}
/*----------------------------------------------------------------------------*/
static int register_endpoint(
    ddf_fun_t *fun, usb_address_t address, usb_speed_t ep_speed,
    usb_endpoint_t endpoint,
    usb_transfer_type_t transfer_type, usb_direction_t direction,
    size_t max_packet_size, unsigned int interval)
{
	assert(fun);
	hcd_t *hcd = fun_to_hcd(fun);
	assert(hcd);
	const size_t size = max_packet_size;
	/* Default address is not bound or registered,
	 * thus it does not provide speed info. */
	usb_speed_t speed = ep_speed;
	/* NOTE The function will return EINVAL and won't
	 * touch speed variable for default address */
	usb_device_manager_get_info_by_address(
	    &hcd->dev_manager, address, NULL, &speed);

	usb_log_debug("Register endpoint %d:%d %s-%s %s %zuB %ums.\n",
	    address, endpoint, usb_str_transfer_type(transfer_type),
	    usb_str_direction(direction), usb_str_speed(speed),
	    max_packet_size, interval);

	return usb_endpoint_manager_add_ep(&hcd->ep_manager, address, endpoint,
	    direction, transfer_type, speed, max_packet_size, size,
	    register_helper, hcd);
}
/*----------------------------------------------------------------------------*/
static int unregister_endpoint(
    ddf_fun_t *fun, usb_address_t address,
    usb_endpoint_t endpoint, usb_direction_t direction)
{
	assert(fun);
	hcd_t *hcd = fun_to_hcd(fun);
	assert(hcd);
	usb_log_debug("Unregister endpoint %d:%d %s.\n",
	    address, endpoint, usb_str_direction(direction));
	return usb_endpoint_manager_remove_ep(&hcd->ep_manager, address,
	    endpoint, direction, unregister_helper, hcd);
}
/*----------------------------------------------------------------------------*/
static int usb_read(ddf_fun_t *fun, usb_target_t target, uint64_t setup_data,
    uint8_t *data, size_t size, usbhc_iface_transfer_in_callback_t callback,
    void *arg)
{
	return send_batch(fun, target, USB_DIRECTION_IN, data, size,
	    setup_data, callback, NULL, arg, "READ");
}
/*----------------------------------------------------------------------------*/
static int usb_write(ddf_fun_t *fun, usb_target_t target, uint64_t setup_data,
    const uint8_t *data, size_t size,
    usbhc_iface_transfer_out_callback_t callback, void *arg)
{
	return send_batch(fun, target, USB_DIRECTION_OUT, (uint8_t*)data, size,
	    setup_data, NULL, callback, arg, "WRITE");
}
/*----------------------------------------------------------------------------*/
usbhc_iface_t hcd_iface = {
	.request_address = request_address,
	.bind_address = bind_address,
	.find_by_address = find_by_address,
	.release_address = release_address,

	.register_endpoint = register_endpoint,
	.unregister_endpoint = unregister_endpoint,

	.read = usb_read,
	.write = usb_write,
};
/**
 * @}
 */
