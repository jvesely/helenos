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
 * USB device driver framework.
 */
#ifndef LIBUSB_DEVDRV_H_
#define LIBUSB_DEVDRV_H_

#include <usb/pipes.h>

/** Descriptors for USB device. */
typedef struct {
	/** Standard device descriptor. */
	usb_standard_device_descriptor_t device;
	/** Full configuration descriptor of current configuration. */
	uint8_t *configuration;
	size_t configuration_size;
} usb_device_descriptors_t;

/** Wrapper for data related to alternate interface setting.
 * The pointers will typically point inside configuration descriptor and
 * thus you shall not deallocate them.
 */
typedef struct {
	/** Interface descriptor. */
	usb_standard_interface_descriptor_t *interface;
	/** Pointer to start of descriptor tree bound with this interface. */
	uint8_t *nested_descriptors;
	/** Size of data pointed by nested_descriptors in bytes. */
	size_t nested_descriptors_size;
} usb_alternate_interface_descriptors_t;

/** Alternate interface settings. */
typedef struct {
	/** Array of alternate interfaces descriptions. */
	usb_alternate_interface_descriptors_t *alternatives;
	/** Size of @c alternatives array. */
	size_t alternative_count;
	/** Index of currently selected one. */
	size_t current;
} usb_alternate_interfaces_t;

/** USB device structure. */
typedef struct {
	/** The default control pipe. */
	usb_pipe_t ctrl_pipe;
	/** Other endpoint pipes.
	 * This is an array of other endpoint pipes in the same order as
	 * in usb_driver_t.
	 */
	usb_endpoint_mapping_t *pipes;
	/** Number of other endpoint pipes. */
	size_t pipes_count;
	/** Current interface.
	 * Usually, drivers operate on single interface only.
	 * This item contains the value of the interface or -1 for any.
	 */
	int interface_no;

	/** Alternative interfaces.
	 * Set to NULL when the driver controls whole device
	 * (i.e. more (or any) interfaces).
	 */
	usb_alternate_interfaces_t *alternate_interfaces;

	/** Some useful descriptors. */
	usb_device_descriptors_t descriptors;

	/** Generic DDF device backing this one. */
	ddf_dev_t *ddf_dev;
	/** Custom driver data.
	 * Do not use the entry in generic device, that is already used
	 * by the framework.
	 */
	void *driver_data;

	/** Connection backing the pipes.
	 * Typically, you will not need to use this attribute at all.
	 */
	usb_device_connection_t wire;
} usb_device_t;

/** USB driver ops. */
typedef struct {
	/** Callback when new device is about to be controlled by the driver. */
	int (*add_device)(usb_device_t *);
} usb_driver_ops_t;

/** USB driver structure. */
typedef struct {
	/** Driver name.
	 * This name is copied to the generic driver name and must be exactly
	 * the same as the directory name where the driver executable resides.
	 */
	const char *name;
	/** Expected endpoints description.
	 * This description shall exclude default control endpoint (pipe zero)
	 * and must be NULL terminated.
	 * When only control endpoint is expected, you may set NULL directly
	 * without creating one item array containing NULL.
	 *
	 * When the driver expect single interrupt in endpoint,
	 * the initialization may look like this:
\code
static usb_endpoint_description_t poll_endpoint_description = {
	.transfer_type = USB_TRANSFER_INTERRUPT,
	.direction = USB_DIRECTION_IN,
	.interface_class = USB_CLASS_HUB,
	.interface_subclass = 0,
	.interface_protocol = 0,
	.flags = 0
};

static usb_endpoint_description_t *hub_endpoints[] = {
	&poll_endpoint_description,
	NULL
};

static usb_driver_t hub_driver = {
	.endpoints = hub_endpoints,
	...
};
\endcode
	 */
	usb_endpoint_description_t **endpoints;
	/** Driver ops. */
	usb_driver_ops_t *ops;
} usb_driver_t;

int usb_driver_main(usb_driver_t *);

int usb_device_select_interface(usb_device_t *, uint8_t,
    usb_endpoint_description_t **);

int usb_device_retrieve_descriptors(usb_pipe_t *, usb_device_descriptors_t *);
int usb_device_create_pipes(ddf_dev_t *, usb_device_connection_t *,
    usb_endpoint_description_t **, uint8_t *, size_t, int, int,
    usb_endpoint_mapping_t **, size_t *);
int usb_device_destroy_pipes(ddf_dev_t *, usb_endpoint_mapping_t *, size_t);
int usb_device_create(ddf_dev_t *, usb_endpoint_description_t **, usb_device_t **, const char **);

size_t usb_interface_count_alternates(uint8_t *, size_t, uint8_t);
int usb_alternate_interfaces_create(uint8_t *, size_t, int,
    usb_alternate_interfaces_t **);

#endif
/**
 * @}
 */
