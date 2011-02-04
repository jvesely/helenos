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
/** @file
 * Common definitions.
 */

#ifndef USBHID_HID_H_
#define USBHID_HID_H_

#include <usb/classes/hid.h>
#include <driver.h>
#include <usb/pipes.h>

/**
 *
 */
typedef struct {
	usb_standard_interface_descriptor_t iface_desc;
	usb_standard_endpoint_descriptor_t *endpoints;
	usb_standard_hid_descriptor_t hid_desc;
	uint8_t *report_desc;
	//usb_standard_hid_class_descriptor_info_t *class_desc_info;
	//uint8_t **class_descs;
} usb_hid_iface_t;

/**
 *
 */
typedef struct {
	usb_standard_configuration_descriptor_t config_descriptor;
	usb_hid_iface_t *interfaces;
} usb_hid_configuration_t;

/**
 * @brief USB/HID keyboard device type.
 *
 * Quite dummy right now.
 */
typedef struct {
	device_t *device;
	usb_hid_configuration_t *conf;
	usb_hid_report_parser_t *parser;

	usb_device_connection_t wire;
	usb_endpoint_pipe_t ctrl_pipe;
	usb_endpoint_pipe_t poll_pipe;
} usb_hid_dev_kbd_t;

// TODO: more configurations!

#endif
