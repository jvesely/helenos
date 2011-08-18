/*
 * Copyright (c) 2010 Lenka Trochtova
 * Copyright (c) 2011 Jiri Svoboda
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

/**
 * @defgroup ns8250 Serial port driver.
 * @brief HelenOS serial port driver.
 * @{
 */

/** @file
 */

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <bool.h>
#include <fibril_synch.h>
#include <stdlib.h>
#include <str.h>
#include <ctype.h>
#include <macros.h>
#include <malloc.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ddi.h>
#include <libarch/ddi.h>

#include <ddf/driver.h>
#include <ddf/interrupt.h>
#include <ddf/log.h>
#include <ops/char_dev.h>

#include <devman.h>
#include <ipc/devman.h>
#include <device/hw_res.h>
#include <ipc/serial_ctl.h>

#include "cyclic_buffer.h"

#define NAME "ns8250"

#define REG_COUNT 7
#define MAX_BAUD_RATE 115200
#define DLAB_MASK (1 << 7)

/** Obtain soft-state structure from function node */
#define NS8250(fnode) ((ns8250_t *) ((fnode)->dev->driver_data))

/** Obtain soft-state structure from device node */
#define NS8250_FROM_DEV(dnode) ((ns8250_t *) ((dnode)->driver_data))

/** The number of bits of one data unit send by the serial port. */
typedef enum {
	WORD_LENGTH_5,
	WORD_LENGTH_6,
	WORD_LENGTH_7,
	WORD_LENGTH_8
} word_length_t;

/** The number of stop bits used by the serial port. */
typedef enum {
	/** Use one stop bit. */
	ONE_STOP_BIT,
	/** 1.5 stop bits for word length 5, 2 stop bits otherwise. */
	TWO_STOP_BITS
} stop_bit_t;

/** The driver data for the serial port devices. */
typedef struct ns8250 {
	/** DDF device node */
	ddf_dev_t *dev;
	/** DDF function node */
	ddf_fun_t *fun;
	/** Is there any client conntected to the device? */
	bool client_connected;
	/** The irq assigned to this device. */
	int irq;
	/** The base i/o address of the devices registers. */
	uint32_t io_addr;
	/** The i/o port used to access the serial ports registers. */
	ioport8_t *port;
	/** The buffer for incomming data. */
	cyclic_buffer_t input_buffer;
	/** The fibril mutex for synchronizing the access to the device. */
	fibril_mutex_t mutex;
} ns8250_t;

/** Create per-device soft-state structure.
 *
 * @return	Pointer to soft-state structure.
 */
static ns8250_t *ns8250_new(void)
{
	ns8250_t *ns;
	
	ns = (ns8250_t *) calloc(1, sizeof(ns8250_t));
	if (ns == NULL)
		return NULL;
	
	fibril_mutex_initialize(&ns->mutex);
	return ns;
}

/** Delete soft-state structure.
 *
 * @param ns	The driver data structure.
 */
static void ns8250_delete(ns8250_t *ns)
{
	assert(ns != NULL);
	free(ns);
}

/** Find out if there is some incomming data available on the serial port.
 *
 * @param port		The base address of the serial port device's ports.
 * @return		True if there are data waiting to be read, false
 *			otherwise.
 */
static bool ns8250_received(ioport8_t *port)
{
	return (pio_read_8(port + 5) & 1) != 0;
}

/** Read one byte from the serial port.
 *
 * @param port		The base address of the serial port device's ports.
 * @return		The data read.
 */
static uint8_t ns8250_read_8(ioport8_t *port)
{
	return pio_read_8(port);
}

/** Find out wheter it is possible to send data.
 *
 * @param port		The base address of the serial port device's ports.
 */
static bool is_transmit_empty(ioport8_t *port)
{
	return (pio_read_8(port + 5) & 0x20) != 0;
}

/** Write one character on the serial port.
 *
 * @param port		The base address of the serial port device's ports.
 * @param c		The character to be written to the serial port device.
 */
static void ns8250_write_8(ioport8_t *port, uint8_t c)
{
	while (!is_transmit_empty(port))
		;
	
	pio_write_8(port, c);
}

/** Read data from the serial port device.
 *
 * @param fun		The serial port function
 * @param buf		The ouput buffer for read data.
 * @param count		The number of bytes to be read.
 *
 * @return		The number of bytes actually read on success, negative
 *			error number otherwise.
 */
static int ns8250_read(ddf_fun_t *fun, char *buf, size_t count)
{
	ns8250_t *ns = NS8250(fun);
	int ret = EOK;
	
	fibril_mutex_lock(&ns->mutex);
	while (!buf_is_empty(&ns->input_buffer) && (size_t)ret < count) {
		buf[ret] = (char)buf_pop_front(&ns->input_buffer);
		ret++;
	}
	fibril_mutex_unlock(&ns->mutex);
	
	return ret;
}

/** Write a character to the serial port.
 *
 * @param ns		Serial port device
 * @param c		The character to be written
 */
static inline void ns8250_putchar(ns8250_t *ns, uint8_t c)
{
	fibril_mutex_lock(&ns->mutex);
	ns8250_write_8(ns->port, c);
	fibril_mutex_unlock(&ns->mutex);
}

/** Write data to the serial port.
 *
 * @param fun		The serial port function
 * @param buf		The data to be written
 * @param count		The number of bytes to be written
 * @return		Zero on success
 */
static int ns8250_write(ddf_fun_t *fun, char *buf, size_t count)
{
	ns8250_t *ns = NS8250(fun);
	size_t idx;
	
	for (idx = 0; idx < count; idx++)
		ns8250_putchar(ns, (uint8_t) buf[idx]);
	
	return 0;
}

static ddf_dev_ops_t ns8250_dev_ops;

/** The character interface's callbacks. */
static char_dev_ops_t ns8250_char_dev_ops = {
	.read = &ns8250_read,
	.write = &ns8250_write
};

static int ns8250_add_device(ddf_dev_t *dev);

/** The serial port device driver's standard operations. */
static driver_ops_t ns8250_ops = {
	.add_device = &ns8250_add_device
};

/** The serial port device driver structure. */
static driver_t ns8250_driver = {
	.name = NAME,
	.driver_ops = &ns8250_ops
};

/** Clean up the serial port soft-state
 *
 * @param ns		Serial port device
 */
static void ns8250_dev_cleanup(ns8250_t *ns)
{
	if (ns->dev->parent_sess) {
		async_hangup(ns->dev->parent_sess);
		ns->dev->parent_sess = NULL;
	}
}

/** Enable the i/o ports of the device.
 *
 * @param ns		Serial port device
 * @return		True on success, false otherwise
 */
static bool ns8250_pio_enable(ns8250_t *ns)
{
	ddf_msg(LVL_DEBUG, "ns8250_pio_enable %s", ns->dev->name);
	
	/* Gain control over port's registers. */
	if (pio_enable((void *)(uintptr_t) ns->io_addr, REG_COUNT,
	    (void **) &ns->port)) {
		ddf_msg(LVL_ERROR, "Cannot map the port %#" PRIx32
		    " for device %s.", ns->io_addr, ns->dev->name);
		return false;
	}
	
	return true;
}

/** Probe the serial port device for its presence.
 *
 * @param ns		Serial port device
 * @return		True if the device is present, false otherwise
 */
static bool ns8250_dev_probe(ns8250_t *ns)
{
	ddf_msg(LVL_DEBUG, "ns8250_dev_probe %s", ns->dev->name);
	
	ioport8_t *port_addr = ns->port;
	bool res = true;
	uint8_t olddata;
	
	olddata = pio_read_8(port_addr + 4);
	
	pio_write_8(port_addr + 4, 0x10);
	if (pio_read_8(port_addr + 6) & 0xf0)
		res = false;
	
	pio_write_8(port_addr + 4, 0x1f);
	if ((pio_read_8(port_addr + 6) & 0xf0) != 0xf0)
		res = false;
	
	pio_write_8(port_addr + 4, olddata);
	
	if (!res) {
		ddf_msg(LVL_DEBUG, "Device %s is not present.",
		    ns->dev->name);
	}
	
	return res;
}

/** Initialize serial port device.
 *
 * @param ns		Serial port device
 * @return		Zero on success, negative error number otherwise
 */
static int ns8250_dev_initialize(ns8250_t *ns)
{
	ddf_msg(LVL_DEBUG, "ns8250_dev_initialize %s", ns->dev->name);
	
	int ret = EOK;
	
	hw_resource_list_t hw_resources;
	memset(&hw_resources, 0, sizeof(hw_resource_list_t));
	
	/* Connect to the parent's driver. */
	ns->dev->parent_sess = devman_parent_device_connect(EXCHANGE_SERIALIZE,
	    ns->dev->handle, IPC_FLAG_BLOCKING);
	if (!ns->dev->parent_sess) {
		ddf_msg(LVL_ERROR, "Failed to connect to parent driver of "
		    "device %s.", ns->dev->name);
		ret = ENOENT;
		goto failed;
	}
	
	/* Get hw resources. */
	ret = hw_res_get_resource_list(ns->dev->parent_sess, &hw_resources);
	if (ret != EOK) {
		ddf_msg(LVL_ERROR, "Failed to get HW resources for device "
		    "%s.", ns->dev->name);
		goto failed;
	}
	
	size_t i;
	hw_resource_t *res;
	bool irq = false;
	bool ioport = false;
	
	for (i = 0; i < hw_resources.count; i++) {
		res = &hw_resources.resources[i];
		switch (res->type) {
		case INTERRUPT:
			ns->irq = res->res.interrupt.irq;
			irq = true;
			ddf_msg(LVL_NOTE, "Device %s was asigned irq = 0x%x.",
			    ns->dev->name, ns->irq);
			break;
			
		case IO_RANGE:
			ns->io_addr = res->res.io_range.address;
			if (res->res.io_range.size < REG_COUNT) {
				ddf_msg(LVL_ERROR, "I/O range assigned to "
				    "device %s is too small.", ns->dev->name);
				ret = ELIMIT;
				goto failed;
			}
			ioport = true;
			ddf_msg(LVL_NOTE, "Device %s was asigned I/O address = "
			    "0x%x.", ns->dev->name, ns->io_addr);
    			break;
			
		default:
			break;
		}
	}
	
	if (!irq || !ioport) {
		ddf_msg(LVL_ERROR, "Missing HW resource(s) for device %s.",
		    ns->dev->name);
		ret = ENOENT;
		goto failed;
	}
	
	hw_res_clean_resource_list(&hw_resources);
	return ret;
	
failed:
	ns8250_dev_cleanup(ns);
	hw_res_clean_resource_list(&hw_resources);
	return ret;
}

/** Enable interrupts on the serial port device.
 *
 * Interrupt when data is received
 *
 * @param port		The base address of the serial port device's ports.
 */
static inline void ns8250_port_interrupts_enable(ioport8_t *port)
{
	pio_write_8(port + 1, 0x1);	/* Interrupt when data received. */
	pio_write_8(port + 4, 0xB);
}

/** Disable interrupts on the serial port device.
 *
 * @param port		The base address of the serial port device's ports
 */
static inline void ns8250_port_interrupts_disable(ioport8_t *port)
{
	pio_write_8(port + 1, 0x0);	/* Disable all interrupts. */
}

/** Enable interrupts for the serial port device.
 *
 * @param ns		Serial port device
 * @return		Zero on success, negative error number otherwise
 */
static int ns8250_interrupt_enable(ns8250_t *ns)
{
	/* Enable interrupt on the serial port. */
	ns8250_port_interrupts_enable(ns->port);
	
	return EOK;
}

/** Set Divisor Latch Access Bit.
 *
 * When the Divisor Latch Access Bit is set, it is possible to set baud rate of
 * the serial port device.
 *
 * @param port		The base address of the serial port device's ports.
 */
static inline void enable_dlab(ioport8_t *port)
{
	uint8_t val = pio_read_8(port + 3);
	pio_write_8(port + 3, val | DLAB_MASK);
}

/** Clear Divisor Latch Access Bit.
 *
 * @param port		The base address of the serial port device's ports.
 */
static inline void clear_dlab(ioport8_t *port)
{
	uint8_t val = pio_read_8(port + 3);
	pio_write_8(port + 3, val & (~DLAB_MASK));
}

/** Set baud rate of the serial communication on the serial device.
 *
 * @param port		The base address of the serial port device's ports.
 * @param baud_rate	The baud rate to be used by the device.
 * @return		Zero on success, negative error number otherwise (EINVAL
 *			if the specified baud_rate is not valid).
 */
static int ns8250_port_set_baud_rate(ioport8_t *port, unsigned int baud_rate)
{
	uint16_t divisor;
	uint8_t div_low, div_high;
	
	if (baud_rate < 50 || MAX_BAUD_RATE % baud_rate != 0) {
		ddf_msg(LVL_ERROR, "Invalid baud rate %d requested.",
		    baud_rate);
		return EINVAL;
	}
	
	divisor = MAX_BAUD_RATE / baud_rate;
	div_low = (uint8_t)divisor;
	div_high = (uint8_t)(divisor >> 8);
	
	/* Enable DLAB to be able to access baud rate divisor. */
	enable_dlab(port);
	
	/* Set divisor low byte. */
	pio_write_8(port + 0, div_low);
	/* Set divisor high byte. */
	pio_write_8(port + 1, div_high);
	
	clear_dlab(port);
	
	return EOK;
}

/** Get baud rate used by the serial port device.
 *
 * @param port		The base address of the serial port device's ports.
 * @param baud_rate	The ouput parameter to which the baud rate is stored.
 */
static unsigned int ns8250_port_get_baud_rate(ioport8_t *port)
{
	uint16_t divisor;
	uint8_t div_low, div_high;
	
	/* Enable DLAB to be able to access baud rate divisor. */
	enable_dlab(port);
	
	/* Get divisor low byte. */
	div_low = pio_read_8(port + 0);
	/* Get divisor high byte. */
	div_high = pio_read_8(port + 1);
	
	clear_dlab(port);
	
	divisor = (div_high << 8) | div_low;
	return MAX_BAUD_RATE / divisor;
}

/** Get the parameters of the serial communication set on the serial port
 * device.
 *
 * @param parity	The parity used.
 * @param word_length	The length of one data unit in bits.
 * @param stop_bits	The number of stop bits used (one or two).
 */
static void ns8250_port_get_com_props(ioport8_t *port, unsigned int *parity,
    unsigned int *word_length, unsigned int *stop_bits)
{
	uint8_t val;
	
	val = pio_read_8(port + 3);
	*parity = ((val >> 3) & 7);
	
	switch (val & 3) {
	case WORD_LENGTH_5:
		*word_length = 5;
		break;
	case WORD_LENGTH_6:
		*word_length = 6;
		break;
	case WORD_LENGTH_7:
		*word_length = 7;
		break;
	case WORD_LENGTH_8:
		*word_length = 8;
		break;
	}
	
	if ((val >> 2) & 1)
		*stop_bits = 2;
	else
		*stop_bits = 1;
}

/** Set the parameters of the serial communication on the serial port device.
 *
 * @param parity	The parity to be used.
 * @param word_length	The length of one data unit in bits.
 * @param stop_bits	The number of stop bits used (one or two).
 * @return		Zero on success, EINVAL if some of the specified values
 *			is invalid.
 */
static int ns8250_port_set_com_props(ioport8_t *port, unsigned int parity,
    unsigned int word_length, unsigned int stop_bits)
{
	uint8_t val;
	
	switch (word_length) {
	case 5:
		val = WORD_LENGTH_5;
		break;
	case 6:
		val = WORD_LENGTH_6;
		break;
	case 7:
		val = WORD_LENGTH_7;
		break;
	case 8:
		val = WORD_LENGTH_8;
		break;
	default:
		return EINVAL;
	}
	
	switch (stop_bits) {
	case 1:
		val |= ONE_STOP_BIT << 2;
		break;
	case 2:
		val |= TWO_STOP_BITS << 2;
		break;
	default:
		return EINVAL;
	}
	
	switch (parity) {
	case SERIAL_NO_PARITY:
	case SERIAL_ODD_PARITY:
	case SERIAL_EVEN_PARITY:
	case SERIAL_MARK_PARITY:
	case SERIAL_SPACE_PARITY:
		val |= parity << 3;
		break;
	default:
		return EINVAL;
	}
	
	pio_write_8(port + 3, val);
	
	return EOK;
}

/** Initialize the serial port device.
 *
 * Set the default parameters of the serial communication.
 *
 * @param ns		Serial port device
 */
static void ns8250_initialize_port(ns8250_t *ns)
{
	ioport8_t *port = ns->port;
	
	/* Disable interrupts. */
	ns8250_port_interrupts_disable(port);
	/* Set baud rate. */
	ns8250_port_set_baud_rate(port, 38400);
	/* 8 bits, no parity, two stop bits. */
	ns8250_port_set_com_props(port, SERIAL_NO_PARITY, 8, 2);
	/* Enable FIFO, clear them, with 14-byte threshold. */
	pio_write_8(port + 2, 0xC7);
	/*
	 * RTS/DSR set (Request to Send and Data Terminal Ready lines enabled),
	 * Aux Output2 set - needed for interrupts.
	 */
	pio_write_8(port + 4, 0x0B);
}

/** Read the data from the serial port device and store them to the input
 * buffer.
 *
 * @param ns		Serial port device
 */
static void ns8250_read_from_device(ns8250_t *ns)
{
	ioport8_t *port = ns->port;
	bool cont = true;
	
	while (cont) {
		fibril_mutex_lock(&ns->mutex);
		
		cont = ns8250_received(port);
		if (cont) {
			uint8_t val = ns8250_read_8(port);
			
			if (ns->client_connected) {
				if (!buf_push_back(&ns->input_buffer, val)) {
					ddf_msg(LVL_WARN, "Buffer overflow on "
					    "%s.", ns->dev->name);
				} else {
					ddf_msg(LVL_DEBUG2, "Character %c saved "
					    "to the buffer of %s.",
					    val, ns->dev->name);
				}
			}
		}
		
		fibril_mutex_unlock(&ns->mutex);
		fibril_yield();
	}
}

/** The interrupt handler.
 *
 * The serial port is initialized to interrupt when some data come, so the
 * interrupt is handled by reading the incomming data.
 *
 * @param dev		The serial port device.
 */
static inline void ns8250_interrupt_handler(ddf_dev_t *dev, ipc_callid_t iid,
    ipc_call_t *icall)
{
	ns8250_read_from_device(NS8250_FROM_DEV(dev));
}

/** Register the interrupt handler for the device.
 *
 * @param ns		Serial port device
 */
static inline int ns8250_register_interrupt_handler(ns8250_t *ns)
{
	return register_interrupt_handler(ns->dev, ns->irq,
	    ns8250_interrupt_handler, NULL);
}

/** Unregister the interrupt handler for the device.
 *
 * @param ns		Serial port device
 */
static inline int ns8250_unregister_interrupt_handler(ns8250_t *ns)
{
	return unregister_interrupt_handler(ns->dev, ns->irq);
}

/** The add_device callback method of the serial port driver.
 *
 * Probe and initialize the newly added device.
 *
 * @param dev		The serial port device.
 */
static int ns8250_add_device(ddf_dev_t *dev)
{
	ns8250_t *ns = NULL;
	ddf_fun_t *fun = NULL;
	bool need_cleanup = false;
	int rc;
	
	ddf_msg(LVL_DEBUG, "ns8250_add_device %s (handle = %d)",
	    dev->name, (int) dev->handle);
	
	/* Allocate soft-state for the device */
	ns = ns8250_new();
	if (ns == NULL) {
		rc = ENOMEM;
		goto fail;
	}
	
	ns->dev = dev;
	dev->driver_data = ns;
	
	rc = ns8250_dev_initialize(ns);
	if (rc != EOK)
		goto fail;
	
	need_cleanup = true;
	
	if (!ns8250_pio_enable(ns)) {
		rc = EADDRNOTAVAIL;
		goto fail;
	}
	
	/* Find out whether the device is present. */
	if (!ns8250_dev_probe(ns)) {
		rc = ENOENT;
		goto fail;
	}
	
	/* Serial port initialization (baud rate etc.). */
	ns8250_initialize_port(ns);
	
	/* Register interrupt handler. */
	if (ns8250_register_interrupt_handler(ns) != EOK) {
		ddf_msg(LVL_ERROR, "Failed to register interrupt handler.");
		rc = EADDRNOTAVAIL;
		goto fail;
	}
	
	/* Enable interrupt. */
	rc = ns8250_interrupt_enable(ns);
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "Failed to enable the interrupt. Error code = "
		    "%d.", rc);
		goto fail;
	}
	
	fun = ddf_fun_create(dev, fun_exposed, "a");
	if (fun == NULL) {
		ddf_msg(LVL_ERROR, "Failed creating function.");
		goto fail;
	}
	
	/* Set device operations. */
	fun->ops = &ns8250_dev_ops;
	rc = ddf_fun_bind(fun);
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "Failed binding function.");
		goto fail;
	}

	ns->fun = fun;
	
	ddf_fun_add_to_category(fun, "serial");
	
	ddf_msg(LVL_NOTE, "Device %s successfully initialized.",
	    dev->name);
	
	return EOK;
fail:
	if (fun != NULL)
		ddf_fun_destroy(fun);
	if (need_cleanup)
		ns8250_dev_cleanup(ns);
	if (ns != NULL)
		ns8250_delete(ns);
	return rc;
}

/** Open the device.
 *
 * This is a callback function called when a client tries to connect to the
 * device.
 *
 * @param dev		The device.
 */
static int ns8250_open(ddf_fun_t *fun)
{
	ns8250_t *data = (ns8250_t *) fun->dev->driver_data;
	int res;
	
	fibril_mutex_lock(&data->mutex);
	if (data->client_connected) {
		res = ELIMIT;
	} else {
		res = EOK;
		data->client_connected = true;
	}
	fibril_mutex_unlock(&data->mutex);
	
	return res;
}

/** Close the device.
 *
 * This is a callback function called when a client tries to disconnect from
 * the device.
 *
 * @param dev		The device.
 */
static void ns8250_close(ddf_fun_t *fun)
{
	ns8250_t *data = (ns8250_t *) fun->dev->driver_data;
	
	fibril_mutex_lock(&data->mutex);
	
	assert(data->client_connected);
	
	data->client_connected = false;
	buf_clear(&data->input_buffer);
	
	fibril_mutex_unlock(&data->mutex);
}

/** Get parameters of the serial communication which are set to the specified
 * device.
 *
 * @param dev		The serial port device.
 * @param baud_rate	The baud rate used by the device.
 * @param parity	The type of parity used by the device.
 * @param word_length	The size of one data unit in bits.
 * @param stop_bits	The number of stop bits used.
 */
static void
ns8250_get_props(ddf_dev_t *dev, unsigned int *baud_rate, unsigned int *parity,
    unsigned int *word_length, unsigned int* stop_bits)
{
	ns8250_t *data = (ns8250_t *) dev->driver_data;
	ioport8_t *port = data->port;
	
	fibril_mutex_lock(&data->mutex);
	ns8250_port_interrupts_disable(port);
	*baud_rate = ns8250_port_get_baud_rate(port);
	ns8250_port_get_com_props(port, parity, word_length, stop_bits);
	ns8250_port_interrupts_enable(port);
	fibril_mutex_unlock(&data->mutex);
	
	ddf_msg(LVL_DEBUG, "ns8250_get_props: baud rate %d, parity 0x%x, word "
	    "length %d, stop bits %d", *baud_rate, *parity, *word_length,
	    *stop_bits);
}

/** Set parameters of the serial communication to the specified serial port
 * device.
 *
 * @param dev		The serial port device.
 * @param baud_rate	The baud rate to be used by the device.
 * @param parity	The type of parity to be used by the device.
 * @param word_length	The size of one data unit in bits.
 * @param stop_bits	The number of stop bits to be used.
 */
static int ns8250_set_props(ddf_dev_t *dev, unsigned int baud_rate,
    unsigned int parity, unsigned int word_length, unsigned int stop_bits)
{
	ddf_msg(LVL_DEBUG, "ns8250_set_props: baud rate %d, parity 0x%x, word "
	    "length %d, stop bits %d", baud_rate, parity, word_length,
	    stop_bits);
	
	ns8250_t *data = (ns8250_t *) dev->driver_data;
	ioport8_t *port = data->port;
	int ret;
	
	fibril_mutex_lock(&data->mutex);
	ns8250_port_interrupts_disable(port);
	ret = ns8250_port_set_baud_rate(port, baud_rate);
	if (ret == EOK)
		ret = ns8250_port_set_com_props(port, parity, word_length, stop_bits);
	ns8250_port_interrupts_enable(port);
	fibril_mutex_unlock(&data->mutex);
	
	return ret;
}

/** Default handler for client requests which are not handled by the standard
 * interfaces.
 *
 * Configure the parameters of the serial communication.
 */
static void ns8250_default_handler(ddf_fun_t *fun, ipc_callid_t callid,
    ipc_call_t *call)
{
	sysarg_t method = IPC_GET_IMETHOD(*call);
	int ret;
	unsigned int baud_rate, parity, word_length, stop_bits;
	
	switch (method) {
	case SERIAL_GET_COM_PROPS:
		ns8250_get_props(fun->dev, &baud_rate, &parity, &word_length,
		    &stop_bits);
		async_answer_4(callid, EOK, baud_rate, parity, word_length,
		    stop_bits);
		break;
		
	case SERIAL_SET_COM_PROPS:
 		baud_rate = IPC_GET_ARG1(*call);
		parity = IPC_GET_ARG2(*call);
		word_length = IPC_GET_ARG3(*call);
		stop_bits = IPC_GET_ARG4(*call);
		ret = ns8250_set_props(fun->dev, baud_rate, parity, word_length,
		    stop_bits);
		async_answer_0(callid, ret);
		break;
		
	default:
		async_answer_0(callid, ENOTSUP);
	}
}

/** Initialize the serial port driver.
 *
 * Initialize device operations structures with callback methods for handling
 * client requests to the serial port devices.
 */
static void ns8250_init(void)
{
	ddf_log_init(NAME, LVL_ERROR);
	
	ns8250_dev_ops.open = &ns8250_open;
	ns8250_dev_ops.close = &ns8250_close;
	
	ns8250_dev_ops.interfaces[CHAR_DEV_IFACE] = &ns8250_char_dev_ops;
	ns8250_dev_ops.default_handler = &ns8250_default_handler;
}

int main(int argc, char *argv[])
{
	printf(NAME ": HelenOS serial port driver\n");
	ns8250_init();
	return ddf_driver_main(&ns8250_driver);
}

/**
 * @}
 */
