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
 * @defgroup pciintel pci bus driver for intel method 1.
 * @brief HelenOS root pci bus driver for intel method 1.
 * @{
 */

/** @file
 */

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <bool.h>
#include <fibril_synch.h>
#include <str.h>
#include <ctype.h>
#include <macros.h>
#include <str_error.h>

#include <ddf/driver.h>
#include <ddf/log.h>
#include <devman.h>
#include <ipc/devman.h>
#include <ipc/dev_iface.h>
#include <ipc/irc.h>
#include <ns.h>
#include <ipc/services.h>
#include <sysinfo.h>
#include <ops/hw_res.h>
#include <device/hw_res.h>
#include <ddi.h>
#include <libarch/ddi.h>
#include <pci_dev_iface.h>

#include "pci.h"

#define NAME "pciintel"

#define CONF_ADDR(bus, dev, fn, reg) \
	((1 << 31) | (bus << 16) | (dev << 11) | (fn << 8) | (reg & ~3))

/** Obtain PCI function soft-state from DDF function node */
#define PCI_FUN(fnode) ((pci_fun_t *) (fnode)->driver_data)

/** Obtain PCI bus soft-state from DDF device node */
#define PCI_BUS(dnode) ((pci_bus_t *) (dnode)->driver_data)

/** Obtain PCI bus soft-state from function soft-state */
#define PCI_BUS_FROM_FUN(fun) ((fun)->busptr)

static hw_resource_list_t *pciintel_get_resources(ddf_fun_t *fnode)
{
	pci_fun_t *fun = PCI_FUN(fnode);
	
	if (fun == NULL)
		return NULL;
	return &fun->hw_resources;
}

static bool pciintel_enable_interrupt(ddf_fun_t *fnode)
{
	/* This is an old ugly way, copied from ne2000 driver */
	assert(fnode);
	pci_fun_t *dev_data = (pci_fun_t *) fnode->driver_data;
	
	sysarg_t apic;
	sysarg_t i8259;
	
	async_sess_t *irc_sess = NULL;
	
	if (((sysinfo_get_value("apic", &apic) == EOK) && (apic))
	    || ((sysinfo_get_value("i8259", &i8259) == EOK) && (i8259))) {
		irc_sess = service_connect_blocking(EXCHANGE_SERIALIZE,
		    SERVICE_IRC, 0, 0);
	}
	
	if (!irc_sess)
		return false;
	
	size_t i = 0;
	hw_resource_list_t *res = &dev_data->hw_resources;
	for (; i < res->count; i++) {
		if (res->resources[i].type == INTERRUPT) {
			const int irq = res->resources[i].res.interrupt.irq;
			
			async_exch_t *exch = async_exchange_begin(irc_sess);
			const int rc =
			    async_req_1_0(exch, IRC_ENABLE_INTERRUPT, irq);
			async_exchange_end(exch);
			
			if (rc != EOK) {
				async_hangup(irc_sess);
				return false;
			}
		}
	}
	
	async_hangup(irc_sess);
	return true;
}

static int pci_config_space_write_32(ddf_fun_t *fun, uint32_t address,
    uint32_t data)
{
	if (address > 252)
		return EINVAL;
	pci_conf_write_32(PCI_FUN(fun), address, data);
	return EOK;
}

static int pci_config_space_write_16(
    ddf_fun_t *fun, uint32_t address, uint16_t data)
{
	if (address > 254)
		return EINVAL;
	pci_conf_write_16(PCI_FUN(fun), address, data);
	return EOK;
}

static int pci_config_space_write_8(
    ddf_fun_t *fun, uint32_t address, uint8_t data)
{
	if (address > 255)
		return EINVAL;
	pci_conf_write_8(PCI_FUN(fun), address, data);
	return EOK;
}

static int pci_config_space_read_32(
    ddf_fun_t *fun, uint32_t address, uint32_t *data)
{
	if (address > 252)
		return EINVAL;
	*data = pci_conf_read_32(PCI_FUN(fun), address);
	return EOK;
}

static int pci_config_space_read_16(
    ddf_fun_t *fun, uint32_t address, uint16_t *data)
{
	if (address > 254)
		return EINVAL;
	*data = pci_conf_read_16(PCI_FUN(fun), address);
	return EOK;
}

static int pci_config_space_read_8(
    ddf_fun_t *fun, uint32_t address, uint8_t *data)
{
	if (address > 255)
		return EINVAL;
	*data = pci_conf_read_8(PCI_FUN(fun), address);
	return EOK;
}

static hw_res_ops_t pciintel_hw_res_ops = {
	&pciintel_get_resources,
	&pciintel_enable_interrupt
};

static pci_dev_iface_t pci_dev_ops = {
	.config_space_read_8 = &pci_config_space_read_8,
	.config_space_read_16 = &pci_config_space_read_16,
	.config_space_read_32 = &pci_config_space_read_32,
	.config_space_write_8 = &pci_config_space_write_8,
	.config_space_write_16 = &pci_config_space_write_16,
	.config_space_write_32 = &pci_config_space_write_32
};

static ddf_dev_ops_t pci_fun_ops = {
	.interfaces[HW_RES_DEV_IFACE] = &pciintel_hw_res_ops,
	.interfaces[PCI_DEV_IFACE] = &pci_dev_ops
};

static int pci_add_device(ddf_dev_t *);

/** PCI bus driver standard operations */
static driver_ops_t pci_ops = {
	.add_device = &pci_add_device
};

/** PCI bus driver structure */
static driver_t pci_driver = {
	.name = NAME,
	.driver_ops = &pci_ops
};

static pci_bus_t *pci_bus_new(void)
{
	pci_bus_t *bus;
	
	bus = (pci_bus_t *) calloc(1, sizeof(pci_bus_t));
	if (bus == NULL)
		return NULL;
	
	fibril_mutex_initialize(&bus->conf_mutex);
	return bus;
}

static void pci_bus_delete(pci_bus_t *bus)
{
	assert(bus != NULL);
	free(bus);
}

static void pci_conf_read(pci_fun_t *fun, int reg, uint8_t *buf, size_t len)
{
	pci_bus_t *bus = PCI_BUS_FROM_FUN(fun);
	
	fibril_mutex_lock(&bus->conf_mutex);
	
	uint32_t conf_addr;
	conf_addr = CONF_ADDR(fun->bus, fun->dev, fun->fn, reg);
	void *addr = bus->conf_data_port + (reg & 3);
	
	pio_write_32(bus->conf_addr_port, conf_addr);
	
	switch (len) {
	case 1:
		buf[0] = pio_read_8(addr);
		break;
	case 2:
		((uint16_t *) buf)[0] = pio_read_16(addr);
		break;
	case 4:
		((uint32_t *) buf)[0] = pio_read_32(addr);
		break;
	}
	
	fibril_mutex_unlock(&bus->conf_mutex);
}

static void pci_conf_write(pci_fun_t *fun, int reg, uint8_t *buf, size_t len)
{
	pci_bus_t *bus = PCI_BUS_FROM_FUN(fun);
	
	fibril_mutex_lock(&bus->conf_mutex);
	
	uint32_t conf_addr;
	conf_addr = CONF_ADDR(fun->bus, fun->dev, fun->fn, reg);
	void *addr = bus->conf_data_port + (reg & 3);
	
	pio_write_32(bus->conf_addr_port, conf_addr);
	
	switch (len) {
	case 1:
		pio_write_8(addr, buf[0]);
		break;
	case 2:
		pio_write_16(addr, ((uint16_t *) buf)[0]);
		break;
	case 4:
		pio_write_32(addr, ((uint32_t *) buf)[0]);
		break;
	}
	
	fibril_mutex_unlock(&bus->conf_mutex);
}

uint8_t pci_conf_read_8(pci_fun_t *fun, int reg)
{
	uint8_t res;
	pci_conf_read(fun, reg, &res, 1);
	return res;
}

uint16_t pci_conf_read_16(pci_fun_t *fun, int reg)
{
	uint16_t res;
	pci_conf_read(fun, reg, (uint8_t *) &res, 2);
	return res;
}

uint32_t pci_conf_read_32(pci_fun_t *fun, int reg)
{
	uint32_t res;
	pci_conf_read(fun, reg, (uint8_t *) &res, 4);
	return res;
}

void pci_conf_write_8(pci_fun_t *fun, int reg, uint8_t val)
{
	pci_conf_write(fun, reg, (uint8_t *) &val, 1);
}

void pci_conf_write_16(pci_fun_t *fun, int reg, uint16_t val)
{
	pci_conf_write(fun, reg, (uint8_t *) &val, 2);
}

void pci_conf_write_32(pci_fun_t *fun, int reg, uint32_t val)
{
	pci_conf_write(fun, reg, (uint8_t *) &val, 4);
}

void pci_fun_create_match_ids(pci_fun_t *fun)
{
	char *match_id_str;
	int rc;
	
	asprintf(&match_id_str, "pci/ven=%04x&dev=%04x",
	    fun->vendor_id, fun->device_id);

	if (match_id_str == NULL) {
		ddf_msg(LVL_ERROR, "Out of memory creating match ID.");
		return;
	}

	rc = ddf_fun_add_match_id(fun->fnode, match_id_str, 90);
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "Failed adding match ID: %s",
		    str_error(rc));
	}
	
	free(match_id_str);
	
	/* TODO add more ids (with subsys ids, using class id etc.) */
}

void pci_add_range(pci_fun_t *fun, uint64_t range_addr, size_t range_size,
    bool io)
{
	hw_resource_list_t *hw_res_list = &fun->hw_resources;
	hw_resource_t *hw_resources =  hw_res_list->resources;
	size_t count = hw_res_list->count;
	
	assert(hw_resources != NULL);
	assert(count < PCI_MAX_HW_RES);
	
	if (io) {
		hw_resources[count].type = IO_RANGE;
		hw_resources[count].res.io_range.address = range_addr;
		hw_resources[count].res.io_range.size = range_size;
		hw_resources[count].res.io_range.endianness = LITTLE_ENDIAN;
	} else {
		hw_resources[count].type = MEM_RANGE;
		hw_resources[count].res.mem_range.address = range_addr;
		hw_resources[count].res.mem_range.size = range_size;
		hw_resources[count].res.mem_range.endianness = LITTLE_ENDIAN;
	}
	
	hw_res_list->count++;
}

/** Read the base address register (BAR) of the device and if it contains valid
 * address add it to the devices hw resource list.
 *
 * @param fun	PCI function
 * @param addr	The address of the BAR in the PCI configuration address space of
 *		the device
 * @return	The addr the address of the BAR which should be read next
 */
int pci_read_bar(pci_fun_t *fun, int addr)
{
	/* Value of the BAR */
	uint32_t val, mask;
	/* IO space address */
	bool io;
	/* 64-bit wide address */
	bool addrw64;
	
	/* Size of the io or memory range specified by the BAR */
	size_t range_size;
	/* Beginning of the io or memory range specified by the BAR */
	uint64_t range_addr;
	
	/* Get the value of the BAR. */
	val = pci_conf_read_32(fun, addr);

#define IO_MASK  (~0x3)
#define MEM_MASK (~0xf)
	
	io = (bool) (val & 1);
	if (io) {
		addrw64 = false;
		mask = IO_MASK;
	} else {
		mask = MEM_MASK;
		switch ((val >> 1) & 3) {
		case 0:
			addrw64 = false;
			break;
		case 2:
			addrw64 = true;
			break;
		default:
			/* reserved, go to the next BAR */
			return addr + 4;
		}
	}
	
	/* Get the address mask. */
	pci_conf_write_32(fun, addr, 0xffffffff);
	mask &= pci_conf_read_32(fun, addr);
	
	/* Restore the original value. */
	pci_conf_write_32(fun, addr, val);
	val = pci_conf_read_32(fun, addr);
	
	range_size = pci_bar_mask_to_size(mask);
	
	if (addrw64) {
		range_addr = ((uint64_t)pci_conf_read_32(fun, addr + 4) << 32) |
		    (val & 0xfffffff0);
	} else {
		range_addr = (val & 0xfffffff0);
	}
	
	if (range_addr != 0) {
		ddf_msg(LVL_DEBUG, "Function %s : address = %" PRIx64
		    ", size = %x", fun->fnode->name, range_addr,
		    (unsigned int) range_size);
	}
	
	pci_add_range(fun, range_addr, range_size, io);
	
	if (addrw64)
		return addr + 8;
	
	return addr + 4;
}

void pci_add_interrupt(pci_fun_t *fun, int irq)
{
	hw_resource_list_t *hw_res_list = &fun->hw_resources;
	hw_resource_t *hw_resources = hw_res_list->resources;
	size_t count = hw_res_list->count;
	
	assert(NULL != hw_resources);
	assert(count < PCI_MAX_HW_RES);
	
	hw_resources[count].type = INTERRUPT;
	hw_resources[count].res.interrupt.irq = irq;
	
	hw_res_list->count++;
	
	ddf_msg(LVL_NOTE, "Function %s uses irq %x.", fun->fnode->name, irq);
}

void pci_read_interrupt(pci_fun_t *fun)
{
	uint8_t irq = pci_conf_read_8(fun, PCI_BRIDGE_INT_LINE);
	if (irq != 0xff)
		pci_add_interrupt(fun, irq);
}

/** Enumerate (recursively) and register the devices connected to a pci bus.
 *
 * @param bus		Host-to-PCI bridge
 * @param bus_num	Bus number
 */
void pci_bus_scan(pci_bus_t *bus, int bus_num) 
{
	ddf_fun_t *fnode;
	pci_fun_t *fun;
	
	int child_bus = 0;
	int dnum, fnum;
	bool multi;
	uint8_t header_type;
	
	fun = pci_fun_new(bus);
	
	for (dnum = 0; dnum < 32; dnum++) {
		multi = true;
		for (fnum = 0; multi && fnum < 8; fnum++) {
			pci_fun_init(fun, bus_num, dnum, fnum);
			fun->vendor_id = pci_conf_read_16(fun,
			    PCI_VENDOR_ID);
			fun->device_id = pci_conf_read_16(fun,
			    PCI_DEVICE_ID);
			if (fun->vendor_id == 0xffff) {
				/*
				 * The device is not present, go on scanning the
				 * bus.
				 */
				if (fnum == 0)
					break;
				else
					continue;
			}
			
			header_type = pci_conf_read_8(fun, PCI_HEADER_TYPE);
			if (fnum == 0) {
				/* Is the device multifunction? */
				multi = header_type >> 7;
			}
			/* Clear the multifunction bit. */
			header_type = header_type & 0x7F;
			
			char *fun_name = pci_fun_create_name(fun);
			if (fun_name == NULL) {
				ddf_msg(LVL_ERROR, "Out of memory.");
				return;
			}
			
			fnode = ddf_fun_create(bus->dnode, fun_inner, fun_name);
			if (fnode == NULL) {
				ddf_msg(LVL_ERROR, "Failed creating function.");
				return;
			}
			
			free(fun_name);
			fun->fnode = fnode;
			
			pci_alloc_resource_list(fun);
			pci_read_bars(fun);
			pci_read_interrupt(fun);
			
			fnode->ops = &pci_fun_ops;
			fnode->driver_data = fun;
			
			ddf_msg(LVL_DEBUG, "Adding new function %s.",
			    fnode->name);
			
			pci_fun_create_match_ids(fun);
			
			if (ddf_fun_bind(fnode) != EOK) {
				pci_clean_resource_list(fun);
				clean_match_ids(&fnode->match_ids);
				free((char *) fnode->name);
				fnode->name = NULL;
				continue;
			}
			
			if (header_type == PCI_HEADER_TYPE_BRIDGE ||
			    header_type == PCI_HEADER_TYPE_CARDBUS) {
				child_bus = pci_conf_read_8(fun,
				    PCI_BRIDGE_SEC_BUS_NUM);
				ddf_msg(LVL_DEBUG, "Device is pci-to-pci "
				    "bridge, secondary bus number = %d.",
				    bus_num);
				if (child_bus > bus_num)
					pci_bus_scan(bus, child_bus);
			}
			
			fun = pci_fun_new(bus);
		}
	}
	
	if (fun->vendor_id == 0xffff) {
		/* Free the auxiliary function structure. */
		pci_fun_delete(fun);
	}
}

static int pci_add_device(ddf_dev_t *dnode)
{
	pci_bus_t *bus = NULL;
	ddf_fun_t *ctl = NULL;
	bool got_res = false;
	int rc;
	
	ddf_msg(LVL_DEBUG, "pci_add_device");
	dnode->parent_sess = NULL;
	
	bus = pci_bus_new();
	if (bus == NULL) {
		ddf_msg(LVL_ERROR, "pci_add_device allocation failed.");
		rc = ENOMEM;
		goto fail;
	}
	bus->dnode = dnode;
	dnode->driver_data = bus;
	
	dnode->parent_sess = devman_parent_device_connect(EXCHANGE_SERIALIZE,
	    dnode->handle, IPC_FLAG_BLOCKING);
	if (!dnode->parent_sess) {
		ddf_msg(LVL_ERROR, "pci_add_device failed to connect to the "
		    "parent driver.");
		rc = ENOENT;
		goto fail;
	}
	
	hw_resource_list_t hw_resources;
	
	rc = hw_res_get_resource_list(dnode->parent_sess, &hw_resources);
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "pci_add_device failed to get hw resources "
		    "for the device.");
		goto fail;
	}
	got_res = true;
	
	ddf_msg(LVL_DEBUG, "conf_addr = %" PRIx64 ".",
	    hw_resources.resources[0].res.io_range.address);
	
	assert(hw_resources.count > 0);
	assert(hw_resources.resources[0].type == IO_RANGE);
	assert(hw_resources.resources[0].res.io_range.size == 8);
	
	bus->conf_io_addr =
	    (uint32_t) hw_resources.resources[0].res.io_range.address;
	
	if (pio_enable((void *)(uintptr_t)bus->conf_io_addr, 8,
	    &bus->conf_addr_port)) {
		ddf_msg(LVL_ERROR, "Failed to enable configuration ports.");
		rc = EADDRNOTAVAIL;
		goto fail;
	}
	bus->conf_data_port = (char *) bus->conf_addr_port + 4;
	
	/* Make the bus device more visible. It has no use yet. */
	ddf_msg(LVL_DEBUG, "Adding a 'ctl' function");
	
	ctl = ddf_fun_create(bus->dnode, fun_exposed, "ctl");
	if (ctl == NULL) {
		ddf_msg(LVL_ERROR, "Failed creating control function.");
		rc = ENOMEM;
		goto fail;
	}
	
	rc = ddf_fun_bind(ctl);
	if (rc != EOK) {
		ddf_msg(LVL_ERROR, "Failed binding control function.");
		goto fail;
	}
	
	/* Enumerate functions. */
	ddf_msg(LVL_DEBUG, "Scanning the bus");
	pci_bus_scan(bus, 0);
	
	hw_res_clean_resource_list(&hw_resources);
	
	return EOK;
	
fail:
	if (bus != NULL)
		pci_bus_delete(bus);
	
	if (dnode->parent_sess)
		async_hangup(dnode->parent_sess);
	
	if (got_res)
		hw_res_clean_resource_list(&hw_resources);
	
	if (ctl != NULL)
		ddf_fun_destroy(ctl);
	
	return rc;
}

static void pciintel_init(void)
{
	ddf_log_init(NAME, LVL_ERROR);
	pci_fun_ops.interfaces[HW_RES_DEV_IFACE] = &pciintel_hw_res_ops;
	pci_fun_ops.interfaces[PCI_DEV_IFACE] = &pci_dev_ops;
}

pci_fun_t *pci_fun_new(pci_bus_t *bus)
{
	pci_fun_t *fun;
	
	fun = (pci_fun_t *) calloc(1, sizeof(pci_fun_t));
	if (fun == NULL)
		return NULL;

	fun->busptr = bus;
	return fun;
}

void pci_fun_init(pci_fun_t *fun, int bus, int dev, int fn)
{
	fun->bus = bus;
	fun->dev = dev;
	fun->fn = fn;
}

void pci_fun_delete(pci_fun_t *fun)
{
	assert(fun != NULL);
	hw_res_clean_resource_list(&fun->hw_resources);
	free(fun);
}

char *pci_fun_create_name(pci_fun_t *fun)
{
	char *name = NULL;
	
	asprintf(&name, "%02x:%02x.%01x", fun->bus, fun->dev,
	    fun->fn);
	return name;
}

bool pci_alloc_resource_list(pci_fun_t *fun)
{
	fun->hw_resources.resources =
	    (hw_resource_t *) malloc(PCI_MAX_HW_RES * sizeof(hw_resource_t));
	return fun->hw_resources.resources != NULL;
}

void pci_clean_resource_list(pci_fun_t *fun)
{
	if (fun->hw_resources.resources != NULL) {
		free(fun->hw_resources.resources);
		fun->hw_resources.resources = NULL;
	}
}

/** Read the base address registers (BARs) of the function and add the addresses
 * to its HW resource list.
 *
 * @param fun	PCI function
 */
void pci_read_bars(pci_fun_t *fun)
{
	/*
	 * Position of the BAR in the PCI configuration address space of the
	 * device.
	 */
	int addr = PCI_BASE_ADDR_0;
	
	while (addr <= PCI_BASE_ADDR_5)
		addr = pci_read_bar(fun, addr);
}

size_t pci_bar_mask_to_size(uint32_t mask)
{
	size_t size = mask & ~(mask - 1);
	return size;
}

int main(int argc, char *argv[])
{
	printf(NAME ": HelenOS PCI bus driver (Intel method 1).\n");
	pciintel_init();
	return ddf_driver_main(&pci_driver);
}

/**
 * @}
 */
