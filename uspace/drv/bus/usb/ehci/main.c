/*
 * Copyright (c) 2011 Jan Vesely
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
/** @addtogroup drvusbehci
 * @{
 */
/** @file
 * Main routines of EHCI driver.
 */
#include <ddf/driver.h>
#include <ddf/interrupt.h>
#include <device/hw_res.h>
#include <errno.h>
#include <str_error.h>

#include <usb_iface.h>
#include <usb/debug.h>
#include <usb/host/ddf_helpers.h>

#include "res.h"

#define NAME "ehci"

static int ehci_dev_add(ddf_dev_t *device);

static driver_ops_t ehci_driver_ops = {
	.dev_add = ehci_dev_add,
};

static driver_t ehci_driver = {
	.name = NAME,
	.driver_ops = &ehci_driver_ops
};


/** Initializes a new ddf driver instance of EHCI hcd.
 *
 * @param[in] device DDF instance of the device to initialize.
 * @return Error code.
 */
static int ehci_dev_add(ddf_dev_t *device)
{
	assert(device);
#define CHECK_RET_RETURN(ret, message...) \
if (ret != EOK) { \
	usb_log_error(message); \
	return ret; \
}

	uintptr_t reg_base = 0;
	size_t reg_size = 0;
	int irq = 0;

	int ret = get_my_registers(device, &reg_base, &reg_size, &irq);
	CHECK_RET_RETURN(ret,
	    "Failed to get memory addresses for %" PRIun ": %s.\n",
	    ddf_dev_get_handle(device), str_error(ret));
	usb_log_info("Memory mapped regs at 0x%" PRIxn " (size %zu), IRQ %d.\n",
	    reg_base, reg_size, irq);

	ret = disable_legacy(device, reg_base, reg_size);
	CHECK_RET_RETURN(ret,
	    "Failed to disable legacy USB: %s.\n", str_error(ret));

	/* High Speed, no bandwidth */
	ret = hcd_ddf_setup_device(device, NULL, USB_SPEED_HIGH, 0, NULL);	
	CHECK_RET_RETURN(ret,
	    "Failed to init generci hcd driver: %s\n", str_error(ret));

	usb_log_info("Controlling new EHCI device `%s' (handle %" PRIun ").\n",
	    ddf_dev_get_name(device), ddf_dev_get_handle(device));

	return EOK;
#undef CHECK_RET_RETURN
}

/** Initializes global driver structures (NONE).
 *
 * @param[in] argc Nmber of arguments in argv vector (ignored).
 * @param[in] argv Cmdline argument vector (ignored).
 * @return Error code.
 *
 * Driver debug level is set here.
 */
int main(int argc, char *argv[])
{
	log_init(NAME);
	return ddf_driver_main(&ehci_driver);
}
/**
 * @}
 */
