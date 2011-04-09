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
 * USB pipes representation.
 */
#ifndef LIBUSB_PIPES_H_
#define LIBUSB_PIPES_H_

#include <sys/types.h>
#include <usb/usb.h>
#include <usb/usbdevice.h>
#include <usb/descriptor.h>
#include <ipc/devman.h>
#include <ddf/driver.h>
#include <fibril_synch.h>

/** Abstraction of a physical connection to the device.
 * This type is an abstraction of the USB wire that connects the host and
 * the function (device).
 */
typedef struct {
	/** Handle of the host controller device is connected to. */
	devman_handle_t hc_handle;
	/** Address of the device. */
	usb_address_t address;
} usb_device_connection_t;

/** Abstraction of a logical connection to USB device endpoint.
 * It encapsulates endpoint attributes (transfer type etc.) as well
 * as information about currently running sessions.
 * This endpoint must be bound with existing usb_device_connection_t
 * (i.e. the wire to send data over).
 *
 * Locking order: if you want to lock both mutexes
 * (@c guard and @c hc_phone_mutex), lock @c guard first.
 * It is not necessary to lock @c guard if you want to lock @c hc_phone_mutex
 * only.
 */
typedef struct {
	/** Guard of the whole pipe. */
	fibril_mutex_t guard;

	/** The connection used for sending the data. */
	usb_device_connection_t *wire;

	/** Endpoint number. */
	usb_endpoint_t endpoint_no;

	/** Endpoint transfer type. */
	usb_transfer_type_t transfer_type;

	/** Endpoint direction. */
	usb_direction_t direction;

	/** Maximum packet size for the endpoint. */
	size_t max_packet_size;

	/** Phone to the host controller.
	 * Negative when no session is active.
	 * It is an error to access this member without @c hc_phone_mutex
	 * being locked.
	 * If call over the phone is to be made, it must be preceeded by
	 * call to pipe_add_ref() [internal libusb function].
	 */
	int hc_phone;

	/** Guard for serialization of requests over the phone. */
	fibril_mutex_t hc_phone_mutex;

	/** Number of active transfers over the pipe. */
	int refcount;
} usb_pipe_t;


/** Description of endpoint characteristics. */
typedef struct {
	/** Transfer type (e.g. control or interrupt). */
	usb_transfer_type_t transfer_type;
	/** Transfer direction (to or from a device). */
	usb_direction_t direction;
	/** Interface class this endpoint belongs to (-1 for any). */
	int interface_class;
	/** Interface subclass this endpoint belongs to (-1 for any). */
	int interface_subclass;
	/** Interface protocol this endpoint belongs to (-1 for any). */
	int interface_protocol;
	/** Extra endpoint flags. */
	unsigned int flags;
} usb_endpoint_description_t;

/** Mapping of endpoint pipes and endpoint descriptions. */
typedef struct {
	/** Endpoint pipe. */
	usb_pipe_t *pipe;
	/** Endpoint description. */
	const usb_endpoint_description_t *description;
	/** Interface number the endpoint must belong to (-1 for any). */
	int interface_no;
	/** Alternate interface setting to choose. */
	int interface_setting;
	/** Found descriptor fitting the description. */
	usb_standard_endpoint_descriptor_t *descriptor;
	/** Interface descriptor the endpoint belongs to. */
	usb_standard_interface_descriptor_t *interface;
	/** Whether the endpoint was actually found. */
	bool present;
} usb_endpoint_mapping_t;

int usb_device_connection_initialize_on_default_address(
    usb_device_connection_t *, usb_hc_connection_t *);
int usb_device_connection_initialize_from_device(usb_device_connection_t *,
    ddf_dev_t *);
int usb_device_connection_initialize(usb_device_connection_t *,
    devman_handle_t, usb_address_t);

int usb_device_get_assigned_interface(ddf_dev_t *);
usb_address_t usb_device_get_assigned_address(devman_handle_t);

int usb_pipe_initialize(usb_pipe_t *, usb_device_connection_t *,
    usb_endpoint_t, usb_transfer_type_t, size_t, usb_direction_t);
int usb_pipe_initialize_default_control(usb_pipe_t *,
    usb_device_connection_t *);
int usb_pipe_probe_default_control(usb_pipe_t *);
int usb_pipe_initialize_from_configuration(usb_endpoint_mapping_t *,
    size_t, uint8_t *, size_t, usb_device_connection_t *);
int usb_pipe_register(usb_pipe_t *, unsigned int, usb_hc_connection_t *);
int usb_pipe_unregister(usb_pipe_t *, usb_hc_connection_t *);

int usb_pipe_start_session(usb_pipe_t *);
int usb_pipe_end_session(usb_pipe_t *);
bool usb_pipe_is_session_started(usb_pipe_t *);

int usb_pipe_read(usb_pipe_t *, void *, size_t, size_t *);
int usb_pipe_write(usb_pipe_t *, void *, size_t);

int usb_pipe_control_read(usb_pipe_t *, void *, size_t,
    void *, size_t, size_t *);
int usb_pipe_control_write(usb_pipe_t *, void *, size_t,
    void *, size_t);

#endif
/**
 * @}
 */
