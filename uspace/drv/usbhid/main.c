/*
 * Copyright (c) 2010 Vojtech Horky
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
 * Main routines of USB HID driver.
 */

#include <ddf/driver.h>
#include <usb/debug.h>
#include <errno.h>
#include <str_error.h>

#include <usb/devdrv.h>

#include "usbhid.h"

/*----------------------------------------------------------------------------*/

#define NAME "usbhid"

/**
 * Function for adding a new device of type USB/HID/keyboard.
 *
 * This functions initializes required structures from the device's descriptors
 * and starts new fibril for polling the keyboard for events and another one for
 * handling auto-repeat of keys.
 *
 * During initialization, the keyboard is switched into boot protocol, the idle
 * rate is set to 0 (infinity), resulting in the keyboard only reporting event
 * when a key is pressed or released. Finally, the LED lights are turned on 
 * according to the default setup of lock keys.
 *
 * @note By default, the keyboards is initialized with Num Lock turned on and 
 *       other locks turned off.
 * @note Currently supports only boot-protocol keyboards.
 *
 * @param dev Device to add.
 *
 * @retval EOK if successful.
 * @retval ENOMEM if there
 * @return Other error code inherited from one of functions usb_kbd_init(),
 *         ddf_fun_bind() and ddf_fun_add_to_class().
 */
static int usb_hid_try_add_device(usb_device_t *dev)
{
	/* 
	 * Initialize device (get and process descriptors, get address, etc.)
	 */
	usb_log_debug("Initializing USB/HID device...\n");
	
	usb_hid_dev_t *hid_dev = usb_hid_new();
	if (hid_dev == NULL) {
		usb_log_error("Error while creating USB/HID device "
		    "structure.\n");
		return ENOMEM;
	}
	
	int rc = usb_hid_init(hid_dev, dev);
	
	if (rc != EOK) {
		usb_log_error("Failed to initialize USB/HID device.\n");
		usb_hid_free(&hid_dev);
		return rc;
	}	
	
	usb_log_debug("USB/HID device structure initialized.\n");
	
	/* Create the function exposed under /dev/devices. */
	ddf_fun_t *hid_fun = ddf_fun_create(dev->ddf_dev, fun_exposed, 
	    usb_hid_get_function_name(hid_dev));
	if (hid_fun == NULL) {
		usb_log_error("Could not create DDF function node.\n");
		usb_hid_free(&hid_dev);
		return ENOMEM;
	}
	
	/*
	 * Store the initialized HID device and HID ops
	 * to the DDF function.
	 */
	hid_fun->ops = &hid_dev->ops;
	hid_fun->driver_data = hid_dev;   // TODO: maybe change to hid_dev->data

	rc = ddf_fun_bind(hid_fun);
	if (rc != EOK) {
		usb_log_error("Could not bind DDF function: %s.\n",
		    str_error(rc));
		// TODO: Can / should I destroy the DDF function?
		ddf_fun_destroy(hid_fun);
		usb_hid_free(&hid_dev);
		return rc;
	}
	
	rc = ddf_fun_add_to_class(hid_fun, usb_hid_get_class_name(hid_dev));
	if (rc != EOK) {
		usb_log_error(
		    "Could not add DDF function to class 'hid': %s.\n",
		    str_error(rc));
		// TODO: Can / should I destroy the DDF function?
		ddf_fun_destroy(hid_fun);
		usb_hid_free(&hid_dev);
		return rc;
	}
	
	/* Start automated polling function.
	 * This will create a separate fibril that will query the device
	 * for the data continuously 
	 */
       rc = usb_device_auto_poll(dev,
	   /* Index of the polling pipe. */
	   hid_dev->poll_pipe_index,
	   /* Callback when data arrives. */
	   usb_hid_polling_callback,
	   /* How much data to request. */
	   dev->pipes[hid_dev->poll_pipe_index].pipe->max_packet_size,
	   /* Callback when the polling ends. */
	   usb_hid_polling_ended_callback,
	   /* Custom argument. */
	   hid_dev);
	
	
	if (rc != EOK) {
		usb_log_error("Failed to start polling fibril for `%s'.\n",
		    dev->ddf_dev->name);
		return rc;
	}

	/*
	 * Hurrah, device is initialized.
	 */
	return EOK;
}

/*----------------------------------------------------------------------------*/
/**
 * Callback for passing a new device to the driver.
 *
 * @note Currently, only boot-protocol keyboards are supported by this driver.
 *
 * @param dev Structure representing the new device.
 *
 * @retval EOK if successful. 
 * @retval EREFUSED if the device is not supported.
 */
static int usb_hid_add_device(usb_device_t *dev)
{
	usb_log_debug("usb_hid_add_device()\n");
	
	if (dev->interface_no < 0) {
		usb_log_warning("Device is not a supported HID device.\n");
		usb_log_error("Failed to add HID device: endpoints not found."
		    "\n");
		return ENOTSUP;
	}
	
	int rc = usb_hid_try_add_device(dev);
	
	if (rc != EOK) {
		usb_log_warning("Device is not a supported HID device.\n");
		usb_log_error("Failed to add HID device: %s.\n",
		    str_error(rc));
		return rc;
	}
	
	usb_log_info("HID device `%s' ready to use.\n", dev->ddf_dev->name);

	return EOK;
}

/*----------------------------------------------------------------------------*/

/* Currently, the framework supports only device adding. Once the framework
 * supports unplug, more callbacks will be added. */
static usb_driver_ops_t usb_hid_driver_ops = {
        .add_device = usb_hid_add_device,
};


/* The driver itself. */
static usb_driver_t usb_hid_driver = {
        .name = NAME,
        .ops = &usb_hid_driver_ops,
        .endpoints = usb_hid_endpoints
};

/*----------------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
	printf(NAME ": HelenOS USB HID driver.\n");

	usb_log_enable(USB_LOG_LEVEL_DEBUG, NAME);

	return usb_driver_main(&usb_hid_driver);
}

/**
 * @}
 */
