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
 * USB HID driver API.
 */

#ifndef USB_HID_USBHID_H_
#define USB_HID_USBHID_H_

#include <stdint.h>

#include <usb/hid/hidparser.h>
#include <ddf/driver.h>
#include <usb/dev/pipes.h>
#include <usb/dev/driver.h>
#include <usb/hid/hid.h>
#include <bool.h>

struct usb_hid_dev;

typedef int (*usb_hid_driver_init_t)(struct usb_hid_dev *, void **data);
typedef void (*usb_hid_driver_deinit_t)(struct usb_hid_dev *, void *data);
typedef bool (*usb_hid_driver_poll)(struct usb_hid_dev *, void *data, uint8_t *,
                                    size_t);
typedef int (*usb_hid_driver_poll_ended)(struct usb_hid_dev *, void *data, 
                                         bool reason);

typedef struct usb_hid_subdriver {	
	/** Function to be called when initializing HID device. */
	usb_hid_driver_init_t init;
	/** Function to be called when destroying the HID device structure. */
	usb_hid_driver_deinit_t deinit;
	/** Function to be called when data arrives from the device. */
	usb_hid_driver_poll poll;
	/** Function to be called when polling ends. */
	usb_hid_driver_poll_ended poll_end;
	/** Arbitrary data needed by the subdriver. */
	void *data;
} usb_hid_subdriver_t;

/*----------------------------------------------------------------------------*/
/**
 * Structure for holding general HID device data.
 */
typedef struct usb_hid_dev {
	/** Structure holding generic USB device information. */
	usb_device_t *usb_dev;
	
	/** Index of the polling pipe in usb_hid_endpoints array. */
	int poll_pipe_index;
	
	/** Subdrivers. */
	usb_hid_subdriver_t *subdrivers;
	
	/** Number of subdrivers. */
	int subdriver_count;
	
	/** Report descriptor. */
	uint8_t *report_desc;

	/** Report descriptor size. */
	size_t report_desc_size;
	
	/** HID Report parser. */
	usb_hid_report_t *report;
	
	uint8_t *input_report;
	
	size_t input_report_size;
	size_t max_input_report_size;
	
	int report_nr;
} usb_hid_dev_t;

/*----------------------------------------------------------------------------*/

enum {
	USB_HID_KBD_POLL_EP_NO = 0,
	USB_HID_MOUSE_POLL_EP_NO = 1,
	USB_HID_GENERIC_POLL_EP_NO = 2,
	USB_HID_POLL_EP_COUNT = 3
};

usb_endpoint_description_t *usb_hid_endpoints[USB_HID_POLL_EP_COUNT + 1];

/*----------------------------------------------------------------------------*/

usb_hid_dev_t *usb_hid_new(void);

int usb_hid_init(usb_hid_dev_t *hid_dev, usb_device_t *dev);

bool usb_hid_polling_callback(usb_device_t *dev, uint8_t *buffer, 
    size_t buffer_size, void *arg);

void usb_hid_polling_ended_callback(usb_device_t *dev, bool reason, 
     void *arg);

void usb_hid_new_report(usb_hid_dev_t *hid_dev);

int usb_hid_report_number(usb_hid_dev_t *hid_dev);

void usb_hid_free(usb_hid_dev_t **hid_dev);

#endif /* USB_HID_USBHID_H_ */

/**
 * @}
 */
