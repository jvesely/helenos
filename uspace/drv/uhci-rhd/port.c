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
/** @addtogroup drvusbuhcirh
 * @{
 */
/** @file
 * @brief UHCI root hub port routines
 */
#include <libarch/ddi.h>  /* pio_read and pio_write */
#include <fibril_synch.h> /* async_usleep */
#include <errno.h>
#include <str_error.h>
#include <time.h>
#include <async.h>

#include <usb/usb.h>    /* usb_address_t */
#include <usb/dev/hub.h>    /* usb_hc_new_device_wrapper */
#include <usb/debug.h>

#include "port.h"

static int uhci_port_check(void *port);
static int uhci_port_reset_enable(int portno, void *arg);
static int uhci_port_new_device(uhci_port_t *port, usb_speed_t speed);
static int uhci_port_remove_device(uhci_port_t *port);
static int uhci_port_set_enabled(uhci_port_t *port, bool enabled);
static void uhci_port_print_status(
    uhci_port_t *port, const port_status_t value);

/** Register reading helper function.
 *
 * @param[in] port Structure to use.
 * @return Error code. (Always EOK)
 */
static inline port_status_t uhci_port_read_status(uhci_port_t *port)
{
	assert(port);
	return pio_read_16(port->address);
}
/*----------------------------------------------------------------------------*/
/** Register writing helper function.
 *
 * @param[in] port Structure to use.
 * @param[in] val New register value.
 * @return Error code. (Always EOK)
 */
static inline void uhci_port_write_status(uhci_port_t *port, port_status_t val)
{
	assert(port);
	pio_write_16(port->address, val);
}
/*----------------------------------------------------------------------------*/
/** Initialize UHCI root hub port instance.
 *
 * @param[in] port Memory structure to use.
 * @param[in] address Address of I/O register.
 * @param[in] number Port number.
 * @param[in] usec Polling interval.
 * @param[in] rh Pointer to ddf instance fo the root hub driver.
 * @return Error code.
 *
 * Creates and starts the polling fibril.
 */
int uhci_port_init(uhci_port_t *port,
    port_status_t *address, unsigned number, unsigned usec, ddf_dev_t *rh)
{
	assert(port);
	asprintf(&port->id_string, "Port (%p - %u)", port, number);
	if (port->id_string == NULL) {
		return ENOMEM;
	}

	port->address = address;
	port->number = number;
	port->wait_period_usec = usec;
	port->attached_device = 0;
	port->rh = rh;

	int ret =
	    usb_hc_connection_initialize_from_device(&port->hc_connection, rh);
	if (ret != EOK) {
		usb_log_error("Failed to initialize connection to HC.");
		return ret;
	}

	port->checker = fibril_create(uhci_port_check, port);
	if (port->checker == 0) {
		usb_log_error("%s: failed to create polling fibril.",
		    port->id_string);
		return ENOMEM;
	}

	fibril_add_ready(port->checker);
	usb_log_debug("%s: Started polling fibril (%" PRIun ").\n",
	    port->id_string, port->checker);
	return EOK;
}
/*----------------------------------------------------------------------------*/
/** Cleanup UHCI root hub port instance.
 *
 * @param[in] port Memory structure to use.
 *
 * Stops the polling fibril.
 */
void uhci_port_fini(uhci_port_t *port)
{
	assert(port);
	free(port->id_string);
	/* TODO: Kill fibril here */
	return;
}
/*----------------------------------------------------------------------------*/
/** Periodically checks port status and reports new devices.
 *
 * @param[in] port Port structure to use.
 * @return Error code.
 */
int uhci_port_check(void *port)
{
	uhci_port_t *instance = port;
	assert(instance);

	while (1) {
		async_usleep(instance->wait_period_usec);

		/* Read register value */
		port_status_t port_status = uhci_port_read_status(instance);

		/* Print the value if it's interesting */
		if (port_status & ~STATUS_ALWAYS_ONE)
			uhci_port_print_status(instance, port_status);

		if ((port_status & STATUS_CONNECTED_CHANGED) == 0)
			continue;

		usb_log_debug("%s: Connected change detected: %x.\n",
		    instance->id_string, port_status);

		int rc =
		    usb_hc_connection_open(&instance->hc_connection);
		if (rc != EOK) {
			usb_log_error("%s: Failed to connect to HC.",
			    instance->id_string);
			continue;
		}

		/* Remove any old device */
		if (instance->attached_device) {
			usb_log_debug2("%s: Removing device.\n",
			    instance->id_string);
			uhci_port_remove_device(instance);
		}

		if ((port_status & STATUS_CONNECTED) != 0) {
			/* New device */
			const usb_speed_t speed =
			    ((port_status & STATUS_LOW_SPEED) != 0) ?
			    USB_SPEED_LOW : USB_SPEED_FULL;
			uhci_port_new_device(instance, speed);
		} else {
			/* Write one to WC bits, to ack changes */
			uhci_port_write_status(instance, port_status);
			usb_log_debug("%s: status change ACK.\n",
			    instance->id_string);
		}

		rc = usb_hc_connection_close(&instance->hc_connection);
		if (rc != EOK) {
			usb_log_error("%s: Failed to disconnect.",
			    instance->id_string);
		}
	}
	return EOK;
}
/*----------------------------------------------------------------------------*/
/** Callback for enabling port during adding a new device.
 *
 * @param portno Port number (unused).
 * @param arg Pointer to uhci_port_t of port with the new device.
 * @return Error code.
 *
 * Resets and enables the ub port.
 */
int uhci_port_reset_enable(int portno, void *arg)
{
	uhci_port_t *port = (uhci_port_t *) arg;

	usb_log_debug2("%s: new_device_enable_port.\n", port->id_string);

	/*
	 * Resets from root ports should be nominally 50ms (USB spec 7.1.7.3)
	 */
	{
		usb_log_debug("%s: Reset Signal start.\n", port->id_string);
		port_status_t port_status = uhci_port_read_status(port);
		port_status |= STATUS_IN_RESET;
		uhci_port_write_status(port, port_status);
		async_usleep(50000);
		port_status = uhci_port_read_status(port);
		port_status &= ~STATUS_IN_RESET;
		uhci_port_write_status(port, port_status);
		while (uhci_port_read_status(port) & STATUS_IN_RESET);
	}
	udelay(10);
	/* Enable the port. */
	uhci_port_set_enabled(port, true);

	/* Reset recovery period,
	 * devices do not have to respond during this period
	 */
	async_usleep(10000);
	return EOK;
}
/*----------------------------------------------------------------------------*/
/** Initialize and report connected device.
 *
 * @param[in] port Port structure to use.
 * @param[in] speed Detected speed.
 * @return Error code.
 *
 * Uses libUSB function to do the actual work.
 */
int uhci_port_new_device(uhci_port_t *port, usb_speed_t speed)
{
	assert(port);
	assert(usb_hc_connection_is_opened(&port->hc_connection));

	usb_log_debug("%s: Detected new device.\n", port->id_string);

	int ret, count = 0;
	usb_address_t dev_addr;
	do {
		ret = usb_hc_new_device_wrapper(port->rh, &port->hc_connection,
		    speed, uhci_port_reset_enable, port->number, port,
		    &dev_addr, &port->attached_device, NULL, NULL, NULL);
	} while (ret != EOK && ++count < 4);

	if (ret != EOK) {
		usb_log_error("%s: Failed(%d) to add device: %s.\n",
		    port->id_string, ret, str_error(ret));
		uhci_port_set_enabled(port, false);
		return ret;
	}

	usb_log_info("New device at port %u, address %d (handle %" PRIun ").\n",
	    port->number, dev_addr, port->attached_device);
	return EOK;
}
/*----------------------------------------------------------------------------*/
/** Remove device.
 *
 * @param[in] port Memory structure to use.
 * @return Error code.
 *
 * Does not work, DDF does not support device removal.
 * Does not even free used USB address (it would be dangerous if tis driver
 * is still running).
 */
int uhci_port_remove_device(uhci_port_t *port)
{
	usb_log_error("%s: Don't know how to remove device %" PRIun ".\n",
	    port->id_string, port->attached_device);
	return ENOTSUP;
}
/*----------------------------------------------------------------------------*/
/** Enable or disable root hub port.
 *
 * @param[in] port Port structure to use.
 * @param[in] enabled Port status to set.
 * @return Error code. (Always EOK)
 */
int uhci_port_set_enabled(uhci_port_t *port, bool enabled)
{
	assert(port);

	/* Read register value */
	port_status_t port_status = uhci_port_read_status(port);

	/* Set enabled bit */
	if (enabled) {
		port_status |= STATUS_ENABLED;
	} else {
		port_status &= ~STATUS_ENABLED;
	}

	/* Write new value. */
	uhci_port_write_status(port, port_status);

	/* Wait for port to become enabled */
	do {
		port_status = uhci_port_read_status(port);
	} while ((port_status & STATUS_CONNECTED) &&
	    !(port_status & STATUS_ENABLED));

	usb_log_debug("%s: %sabled port.\n",
		port->id_string, enabled ? "En" : "Dis");
	return EOK;
}
/*----------------------------------------------------------------------------*/
/** Print the port status value in a human friendly way
 *
 * @param[in] port Port structure to use.
 * @param[in] value Port register value to print.
 * @return Error code. (Always EOK)
 */
void uhci_port_print_status(uhci_port_t *port, const port_status_t value)
{
	assert(port);
	usb_log_debug2("%s Port status(%#x):%s%s%s%s%s%s%s%s%s%s%s.\n",
	    port->id_string, value,
	    (value & STATUS_SUSPEND) ? " SUSPENDED," : "",
	    (value & STATUS_RESUME) ? " IN RESUME," : "",
	    (value & STATUS_IN_RESET) ? " IN RESET," : "",
	    (value & STATUS_LINE_D_MINUS) ? " VD-," : "",
	    (value & STATUS_LINE_D_PLUS) ? " VD+," : "",
	    (value & STATUS_LOW_SPEED) ? " LOWSPEED," : "",
	    (value & STATUS_ENABLED_CHANGED) ? " ENABLED-CHANGE," : "",
	    (value & STATUS_ENABLED) ? " ENABLED," : "",
	    (value & STATUS_CONNECTED_CHANGED) ? " CONNECTED-CHANGE," : "",
	    (value & STATUS_CONNECTED) ? " CONNECTED," : "",
	    (value & STATUS_ALWAYS_ONE) ? " ALWAYS ONE" : " ERR: NO ALWAYS ONE"
	);
}
/**
 * @}
 */
