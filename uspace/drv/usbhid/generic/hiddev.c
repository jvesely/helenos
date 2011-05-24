/*
 * Copyright (c) 2011 Lubos Slovak
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

/** @addtogroup drvusbhid
 * @{
 */
/**
 * @file
 * USB HID driver API.
 */

#include <usb/debug.h>
#include <usb/classes/classes.h>
#include <errno.h>
#include <str_error.h>
#include <bool.h>

#include <usbhid_iface.h>

#include "hiddev.h"
#include "usbhid.h"

/*----------------------------------------------------------------------------*/

usb_endpoint_description_t usb_hid_generic_poll_endpoint_description = {
	.transfer_type = USB_TRANSFER_INTERRUPT,
	.direction = USB_DIRECTION_IN,
	.interface_class = USB_CLASS_HID,
	.flags = 0
};

const char *HID_GENERIC_FUN_NAME = "hid";
const char *HID_GENERIC_CLASS_NAME = "hid";

/*----------------------------------------------------------------------------*/

static size_t usb_generic_hid_get_event_length(ddf_fun_t *fun);

static int usb_generic_hid_get_event(ddf_fun_t *fun, uint8_t *buffer, 
    size_t size, size_t *act_size, unsigned int flags);

static int usb_generic_hid_client_connected(ddf_fun_t *fun);

static size_t usb_generic_get_report_descriptor_length(ddf_fun_t *fun);

static int usb_generic_get_report_descriptor(ddf_fun_t *fun, uint8_t *desc, 
    size_t size, size_t *actual_size);

/*----------------------------------------------------------------------------*/

static usbhid_iface_t usb_generic_iface = {
	.get_event = usb_generic_hid_get_event,
	.get_event_length = usb_generic_hid_get_event_length,
	.get_report_descriptor_length = usb_generic_get_report_descriptor_length,
	.get_report_descriptor = usb_generic_get_report_descriptor
};

static ddf_dev_ops_t usb_generic_hid_ops = {
	.interfaces[USBHID_DEV_IFACE] = &usb_generic_iface,
	.open = usb_generic_hid_client_connected
};

/*----------------------------------------------------------------------------*/

static size_t usb_generic_hid_get_event_length(ddf_fun_t *fun)
{
	usb_log_debug("Generic HID: Get event length (fun: %p, "
	    "fun->driver_data: %p.\n", fun, fun->driver_data);
	
	if (fun == NULL || fun->driver_data == NULL) {
		return 0;
	}

	usb_hid_dev_t *hid_dev = (usb_hid_dev_t *)fun->driver_data;
	
	usb_log_debug("hid_dev: %p, Max input report size (%d).\n",
	    hid_dev, hid_dev->max_input_report_size);
	
	return hid_dev->max_input_report_size;
}

/*----------------------------------------------------------------------------*/

static int usb_generic_hid_get_event(ddf_fun_t *fun, uint8_t *buffer, 
    size_t size, size_t *act_size, unsigned int flags)
{
	usb_log_debug("Generic HID: Get event.\n");
	
	if (fun == NULL || fun->driver_data == NULL) {
		usb_log_debug("No function");
		return EINVAL;
	}

	usb_hid_dev_t *hid_dev = (usb_hid_dev_t *)fun->driver_data;
	
	if (hid_dev->input_report_size > size) {
		return EINVAL;	// TODO: other error code
	}
	
	/*! @todo This should probably be atomic. */
	if (usb_hid_report_ready()) {
		memcpy(buffer, hid_dev->input_report, 
		    hid_dev->input_report_size);
		*act_size = hid_dev->input_report_size;
		usb_hid_report_received();
	}
	
	// clear the buffer so that it will not be received twice
	//memset(hid_dev->input_report, 0, hid_dev->input_report_size);
	
	// note that we already received this report
//	report_received = true;
	
	return EOK;
}

/*----------------------------------------------------------------------------*/

static size_t usb_generic_get_report_descriptor_length(ddf_fun_t *fun)
{
	usb_log_debug("Generic HID: Get report descriptor length.\n");
	
	if (fun == NULL || fun->driver_data == NULL) {
		usb_log_debug("No function");
		return EINVAL;
	}
	
	usb_hid_dev_t *hid_dev = (usb_hid_dev_t *)fun->driver_data;
	
	return hid_dev->report_desc_size;
}

/*----------------------------------------------------------------------------*/

static int usb_generic_get_report_descriptor(ddf_fun_t *fun, uint8_t *desc, 
    size_t size, size_t *actual_size)
{
	usb_log_debug("Generic HID: Get report descriptor.\n");
	
	if (fun == NULL || fun->driver_data == NULL) {
		usb_log_debug("No function");
		return EINVAL;
	}
	
	usb_hid_dev_t *hid_dev = (usb_hid_dev_t *)fun->driver_data;
	
	if (hid_dev->report_desc_size > size) {
		return EINVAL;	// TODO: other error code
	}
	
	memcpy(desc, hid_dev->report_desc, hid_dev->report_desc_size);
	*actual_size = hid_dev->report_desc_size;
	
	return EOK;
}

/*----------------------------------------------------------------------------*/

static int usb_generic_hid_client_connected(ddf_fun_t *fun)
{
	usb_log_debug("Generic HID: Client connected.\n");
	usb_hid_report_received();
	return EOK;
}

/*----------------------------------------------------------------------------*/

static int usb_generic_hid_create_function(usb_hid_dev_t *hid_dev)
{	
	/* Create the function exposed under /dev/devices. */
	/** @todo Generate numbers for the devices? */
	usb_log_debug("Creating DDF function %s...\n", HID_GENERIC_FUN_NAME);
	ddf_fun_t *fun = ddf_fun_create(hid_dev->usb_dev->ddf_dev, fun_exposed, 
	    HID_GENERIC_FUN_NAME);
	if (fun == NULL) {
		usb_log_error("Could not create DDF function node.\n");
		return ENOMEM;
	}
	
	fun->ops = &usb_generic_hid_ops;
	fun->driver_data = hid_dev;

	int rc = ddf_fun_bind(fun);
	if (rc != EOK) {
		usb_log_error("Could not bind DDF function: %s.\n",
		    str_error(rc));
		ddf_fun_destroy(fun);
		return rc;
	}
	
	usb_log_debug("HID function created. Handle: %d\n", fun->handle);
	
	return EOK;
}

/*----------------------------------------------------------------------------*/

int usb_generic_hid_init(usb_hid_dev_t *hid_dev, void **data)
{
	if (hid_dev == NULL) {
		return EINVAL;
	}
	
	return usb_generic_hid_create_function(hid_dev);
}

/*----------------------------------------------------------------------------*/

bool usb_generic_hid_polling_callback(usb_hid_dev_t *hid_dev, void *data, 
    uint8_t *buffer, size_t buffer_size)
{
	usb_log_debug("usb_hid_polling_callback(%p, %p, %zu)\n",
	    hid_dev, buffer, buffer_size);
	usb_debug_str_buffer(buffer, buffer_size, 0);
	return true;
}

/**
 * @}
 */
