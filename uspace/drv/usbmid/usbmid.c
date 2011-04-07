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

/** @addtogroup drvusbmid
 * @{
 */
/**
 * @file
 * Helper functions.
 */
#include <errno.h>
#include <str_error.h>
#include <stdlib.h>
#include <usb_iface.h>
#include <usb/ddfiface.h>
#include <usb/pipes.h>
#include <usb/classes/classes.h>
#include <usb/recognise.h>
#include "usbmid.h"

/** Callback for DDF USB interface. */
static int usb_iface_get_address_impl(ddf_fun_t *fun, devman_handle_t handle,
    usb_address_t *address)
{
	return usb_iface_get_address_hub_impl(fun, handle, address);
}

/** Callback for DDF USB interface. */
static int usb_iface_get_interface_impl(ddf_fun_t *fun, devman_handle_t handle,
    int *iface_no)
{
	assert(fun);

	usbmid_interface_t *iface = fun->driver_data;
	assert(iface);

	if (iface_no != NULL) {
		*iface_no = iface->interface_no;
	}

	return EOK;
}

/** DDF interface of the child - interface function. */
static usb_iface_t child_usb_iface = {
	.get_hc_handle = usb_iface_get_hc_handle_hub_child_impl,
	.get_address = usb_iface_get_address_impl,
	.get_interface = usb_iface_get_interface_impl
};

/** Operations for children - interface functions. */
static ddf_dev_ops_t child_device_ops = {
	.interfaces[USB_DEV_IFACE] = &child_usb_iface
};


/** Spawn new child device from one interface.
 *
 * @param parent Parent MID device.
 * @param iface Interface information.
 * @param device_descriptor Device descriptor.
 * @param interface_descriptor Interface descriptor.
 * @return Error code.
 */
int usbmid_spawn_interface_child(usb_device_t *parent,
    usbmid_interface_t *iface,
    const usb_standard_device_descriptor_t *device_descriptor,
    const usb_standard_interface_descriptor_t *interface_descriptor)
{
	ddf_fun_t *child = NULL;
	char *child_name = NULL;
	int rc;

	/*
	 * Name is class name followed by interface number.
	 * The interface number shall provide uniqueness while the
	 * class name something humanly understandable.
	 */
	rc = asprintf(&child_name, "%s%d",
	    usb_str_class(interface_descriptor->interface_class),
	    (int) interface_descriptor->interface_number);
	if (rc < 0) {
		goto error_leave;
	}

	/* Create the device. */
	child = ddf_fun_create(parent->ddf_dev, fun_inner, child_name);
	if (child == NULL) {
		rc = ENOMEM;
		goto error_leave;
	}

	iface->fun = child;

	child->driver_data = iface;
	child->ops = &child_device_ops;

	rc = usb_device_create_match_ids_from_interface(device_descriptor,
	    interface_descriptor,
	    &child->match_ids);
	if (rc != EOK) {
		goto error_leave;
	}

	rc = ddf_fun_bind(child);
	if (rc != EOK) {
		goto error_leave;
	}

	return EOK;

error_leave:
	if (child != NULL) {
		child->name = NULL;
		/* This takes care of match_id deallocation as well. */
		ddf_fun_destroy(child);
	}
	if (child_name != NULL) {
		free(child_name);
	}

	return rc;
}

/**
 * @}
 */
