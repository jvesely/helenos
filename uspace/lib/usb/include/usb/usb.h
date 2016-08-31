/*
 * Copyright (c) 2010 Vojtech Horky
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
 * Common USB types and functions.
 */
#ifndef LIBUSB_USB_H_
#define LIBUSB_USB_H_

#include <stdbool.h>
#include <sys/types.h>
#include <byteorder.h>

/** Convert 16bit value from native (host) endianness to USB endianness. */
#define uint16_host2usb(n) host2uint16_t_le((n))

/** Convert 32bit value from native (host) endianness to USB endianness. */
#define uint32_host2usb(n) host2uint32_t_le((n))

/** Convert 16bit value from USB endianness into native (host) one. */
#define uint16_usb2host(n) uint16_t_le2host((n))

/** Convert 32bit value from USB endianness into native (host) one. */
#define uint32_usb2host(n) uint32_t_le2host((n))


/** USB transfer type. */
typedef enum {
	USB_TRANSFER_CONTROL = 0,
	USB_TRANSFER_ISOCHRONOUS = 1,
	USB_TRANSFER_BULK = 2,
	USB_TRANSFER_INTERRUPT = 3
} usb_transfer_type_t;

const char * usb_str_transfer_type(usb_transfer_type_t t);
const char * usb_str_transfer_type_short(usb_transfer_type_t t);

/** USB data transfer direction. */
typedef enum {
	USB_DIRECTION_IN,
	USB_DIRECTION_OUT,
	USB_DIRECTION_BOTH
} usb_direction_t;

const char *usb_str_direction(usb_direction_t);

/** USB speeds. */
typedef enum {
	/** USB 1.1 low speed (1.5Mbits/s). */
	USB_SPEED_LOW,
	/** USB 1.1 full speed (12Mbits/s). */
	USB_SPEED_FULL,
	/** USB 2.0 high speed (480Mbits/s). */
	USB_SPEED_HIGH,
	/** Psuedo-speed serving as a boundary. */
	USB_SPEED_MAX
} usb_speed_t;

static inline bool usb_speed_is_11(const usb_speed_t s)
{
	return (s == USB_SPEED_FULL) || (s == USB_SPEED_LOW);
}

const char *usb_str_speed(usb_speed_t);


/** USB request type target. */
typedef enum {
	USB_REQUEST_TYPE_STANDARD = 0,
	USB_REQUEST_TYPE_CLASS = 1,
	USB_REQUEST_TYPE_VENDOR = 2
} usb_request_type_t;

/** USB request recipient. */
typedef enum {
	USB_REQUEST_RECIPIENT_DEVICE = 0,
	USB_REQUEST_RECIPIENT_INTERFACE = 1,
	USB_REQUEST_RECIPIENT_ENDPOINT = 2,
	USB_REQUEST_RECIPIENT_OTHER = 3
} usb_request_recipient_t;

/** USB address type.
 * Negative values could be used to indicate error.
 */
typedef int16_t usb_address_t;

/** Default USB address. */
#define USB_ADDRESS_DEFAULT 0

/** Maximum address number in USB 1.1. */
#define USB11_ADDRESS_MAX 127
#define USB_ADDRESS_COUNT (USB11_ADDRESS_MAX + 1)

/** Check USB address for allowed values.
 *
 * @param ep USB address.
 *
 * @return True, if value is wihtin limits, false otherwise.
 *
 */
static inline bool usb_address_is_valid(usb_address_t a)
{
	return (a >= USB_ADDRESS_DEFAULT) && (a <= USB11_ADDRESS_MAX);
}

/** USB endpoint number type.
 * Negative values could be used to indicate error.
 */
typedef int16_t usb_endpoint_t;

/** Default control endpoint */
#define USB_ENDPOINT_DEFAULT_CONTROL 0

/** Maximum endpoint number in USB 1.1. */
#define USB11_ENDPOINT_MAX 16

/** Check USB endpoint for allowed values.
 *
 * @param ep USB endpoint number.
 *
 * @return True, if value is wihtin limits, false otherwise.
 *
 */
static inline bool usb_endpoint_is_valid(usb_endpoint_t ep)
{
	return (ep >= USB_ENDPOINT_DEFAULT_CONTROL) &&
	    (ep < USB11_ENDPOINT_MAX);
}


/** USB complete address type. 
 * Pair address + endpoint is identification of transaction recipient.
 */
typedef union {
	struct {
		usb_address_t address;
		usb_endpoint_t endpoint;
	} __attribute__((packed));
	uint32_t packed;
} usb_target_t;

/** Check USB target for allowed values (address and endpoint).
 *
 * @param target.
 * @return True, if values are wihtin limits, false otherwise.
 */
static inline bool usb_target_is_valid(usb_target_t target)
{
	return usb_address_is_valid(target.address) &&
	    usb_endpoint_is_valid(target.endpoint);
}

/** Compare USB targets (addresses and endpoints).
 *
 * @param a First target.
 * @param b Second target.
 * @return Whether @p a and @p b points to the same pipe on the same device.
 */
static inline int usb_target_same(usb_target_t a, usb_target_t b)
{
	return (a.address == b.address)
	    && (a.endpoint == b.endpoint);
}

/** General handle type.
 * Used by various USB functions as opaque handle.
 */
typedef sysarg_t usb_handle_t;

/** USB packet identifier. */
typedef enum {
#define _MAKE_PID_NIBBLE(tag, type) \
	((uint8_t)(((tag) << 2) | (type)))
#define _MAKE_PID(tag, type) \
	( \
	    _MAKE_PID_NIBBLE(tag, type) \
	    | (((~_MAKE_PID_NIBBLE(tag, type)) & 0xf) << 4) \
	)
	USB_PID_OUT = _MAKE_PID(0, 1),
	USB_PID_IN = _MAKE_PID(2, 1),
	USB_PID_SOF = _MAKE_PID(1, 1),
	USB_PID_SETUP = _MAKE_PID(3, 1),

	USB_PID_DATA0 = _MAKE_PID(0 ,3),
	USB_PID_DATA1 = _MAKE_PID(2 ,3),

	USB_PID_ACK = _MAKE_PID(0 ,2),
	USB_PID_NAK = _MAKE_PID(2 ,2),
	USB_PID_STALL = _MAKE_PID(3 ,2),

	USB_PID_PRE = _MAKE_PID(3 ,0),
	/* USB_PID_ = _MAKE_PID( ,), */
#undef _MAKE_PID
#undef _MAKE_PID_NIBBLE
} usb_packet_id;

/** Category for USB host controllers. */
#define USB_HC_CATEGORY "usbhc"

#endif
/**
 * @}
 */
