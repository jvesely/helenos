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

/** @addtogroup usbinfo
 * @{
 */
/**
 * @file
 * Dumping of generic device properties.
 */
#include <stdio.h>
#include <str_error.h>
#include <errno.h>
#include <usb/pipes.h>
#include <usb/recognise.h>
#include <usb/request.h>
#include <usb/classes/classes.h>
#include <usb/classes/hid.h>
#include "usbinfo.h"

void dump_short_device_identification(usbinfo_device_t *dev)
{
	printf("%sDevice 0x%04x by vendor 0x%04x\n", get_indent(0),
	    (int) dev->device_descriptor.product_id,
	    (int) dev->device_descriptor.vendor_id);
}

static void dump_match_ids_from_interface(uint8_t *descriptor, size_t depth,
    void *arg)
{
	if (depth != 1) {
		return;
	}
	size_t descr_size = descriptor[0];
	if (descr_size < sizeof(usb_standard_interface_descriptor_t)) {
		return;
	}
	int descr_type = descriptor[1];
	if (descr_type != USB_DESCTYPE_INTERFACE) {
		return;
	}

	usbinfo_device_t *dev = (usbinfo_device_t *) arg;

	usb_standard_interface_descriptor_t *iface
	    = (usb_standard_interface_descriptor_t *) descriptor;

	printf("%sInterface #%d match ids (%s, 0x%02x, 0x%02x)\n",
	    get_indent(0),
	    (int) iface->interface_number,
	    usb_str_class(iface->interface_class),
	    (int) iface->interface_subclass,
	    (int) iface->interface_protocol);

	match_id_list_t matches;
	init_match_ids(&matches);
	usb_device_create_match_ids_from_interface(&dev->device_descriptor,
	    iface, &matches);
	dump_match_ids(&matches, get_indent(1));
	clean_match_ids(&matches);
}

void dump_device_match_ids(usbinfo_device_t *dev)
{
	match_id_list_t matches;
	init_match_ids(&matches);
	usb_device_create_match_ids_from_device_descriptor(
	    &dev->device_descriptor, &matches);
	printf("%sDevice match ids (0x%04x by 0x%04x, %s)\n", get_indent(0),
	    (int) dev->device_descriptor.product_id,
	    (int) dev->device_descriptor.vendor_id,
	    usb_str_class(dev->device_descriptor.device_class));
	dump_match_ids(&matches, get_indent(1));
	clean_match_ids(&matches);

	usb_dp_walk_simple(dev->full_configuration_descriptor,
	    dev->full_configuration_descriptor_size,
	    usb_dp_standard_descriptor_nesting,
	    dump_match_ids_from_interface,
	    dev);
}

static void dump_descriptor_tree_brief_device(const char *prefix,
    usb_standard_device_descriptor_t *descriptor)
{
	printf("%sDevice (0x%04x by 0x%04x, %s, %zu configurations)\n", prefix,
	    (int) descriptor->product_id,
	    (int) descriptor->vendor_id,
	    usb_str_class(descriptor->device_class),
	    (size_t) descriptor->configuration_count);
}

static void dump_descriptor_tree_brief_configuration(const char *prefix,
    usb_standard_configuration_descriptor_t *descriptor)
{
	printf("%sConfiguration #%d (%zu interfaces)\n", prefix,
	    (int) descriptor->configuration_number,
	    (size_t) descriptor->interface_count);
}

static void dump_descriptor_tree_brief_interface(const char *prefix,
    usb_standard_interface_descriptor_t *descriptor)
{
	printf("%sInterface #%d (%s, 0x%02x, 0x%02x), alternate %d\n", prefix,
	    (int) descriptor->interface_number,
	    usb_str_class(descriptor->interface_class),
	    (int) descriptor->interface_subclass,
	    (int) descriptor->interface_protocol,
	    (int) descriptor->alternate_setting);
}

static void dump_descriptor_tree_brief_endpoint(const char *prefix,
    usb_standard_endpoint_descriptor_t *descriptor)
{
	usb_endpoint_t endpoint_no = descriptor->endpoint_address & 0xF;
	usb_transfer_type_t transfer = descriptor->attributes & 0x3;
	usb_direction_t direction = descriptor->endpoint_address & 0x80
	    ? USB_DIRECTION_IN : USB_DIRECTION_OUT;
	printf("%sEndpoint #%d (%s %s, %zu)\n", prefix,
	    endpoint_no, usb_str_transfer_type(transfer),
	    direction == USB_DIRECTION_IN ? "in" : "out",
	    (size_t) descriptor->max_packet_size);
}

static void dump_descriptor_tree_brief_hid(const char *prefix,
    usb_standard_hid_descriptor_t *descriptor)
{
	printf("%sHID (country %d, %d descriptors)\n", prefix,
	    (int) descriptor->country_code,
	    (int) descriptor->class_desc_count);
}


static void dump_descriptor_tree_brief_callback(uint8_t *descriptor,
    size_t depth, void *arg)
{
	const char *indent = get_indent(depth + 1);

	int descr_type = -1;
	size_t descr_size = descriptor[0];
	if (descr_size > 0) {
		descr_type = descriptor[1];
	}

	switch (descr_type) {

#define _BRANCH(type_enum, descriptor_type, callback) \
	case type_enum: \
		if (descr_size >= sizeof(descriptor_type)) { \
			callback(indent, (descriptor_type *) descriptor); \
		} else { \
			descr_type = -1; \
		} \
		break;

		_BRANCH(USB_DESCTYPE_DEVICE,
		    usb_standard_device_descriptor_t,
		    dump_descriptor_tree_brief_device);
		_BRANCH(USB_DESCTYPE_CONFIGURATION,
		    usb_standard_configuration_descriptor_t,
		    dump_descriptor_tree_brief_configuration);
		_BRANCH(USB_DESCTYPE_INTERFACE,
		    usb_standard_interface_descriptor_t,
		    dump_descriptor_tree_brief_interface);
		_BRANCH(USB_DESCTYPE_ENDPOINT,
		    usb_standard_endpoint_descriptor_t,
		    dump_descriptor_tree_brief_endpoint);
		_BRANCH(USB_DESCTYPE_HID,
		    usb_standard_hid_descriptor_t,
		    dump_descriptor_tree_brief_hid);

		default:
			break;
	}

	if (descr_type == -1) {
		printf("%sInvalid descriptor.\n", indent);
	}
}

void dump_descriptor_tree_brief(usbinfo_device_t *dev)
{
	dump_descriptor_tree_brief_callback((uint8_t *)&dev->device_descriptor,
	    (size_t) -1, NULL);
	usb_dp_walk_simple(dev->full_configuration_descriptor,
	    dev->full_configuration_descriptor_size,
	    usb_dp_standard_descriptor_nesting,
	    dump_descriptor_tree_brief_callback,
	    NULL);
}

void dump_strings(usbinfo_device_t *dev)
{
	/* Get supported languages. */
	l18_win_locales_t *langs;
	size_t langs_count;
	int rc = usb_request_get_supported_languages(&dev->ctrl_pipe,
	    &langs, &langs_count);
	if (rc != EOK) {
		fprintf(stderr,
		    NAME ": failed to get list of supported languages: %s.\n",
		    str_error(rc));
		return;
	}

	printf("%sString languages (%zu):", get_indent(0), langs_count);
	size_t i;
	for (i = 0; i < langs_count; i++) {
		printf(" 0x%04x", (int) langs[i]);
	}
	printf(".\n");

	/* Get all strings and dump them. */
	for (i = 0; i < langs_count; i++) {
		l18_win_locales_t lang = langs[i];

		printf("%sStrings in %s:\n", get_indent(0),
		    str_l18_win_locale(lang));
		/*
		 * Try only the first 15 strings
		 * (typically, device will not have much more anyway).
		 */
		size_t idx;
		for (idx = 1; idx < 0x0F; idx++) {
			char *string;
			rc = usb_request_get_string(&dev->ctrl_pipe, idx, lang,
			    &string);
			if (rc != EOK) {
				continue;
			}
			printf("%sString #%zu: \"%s\"\n", get_indent(1),
			    idx, string);
			free(string);
		}
	}
}

/** @}
 */
