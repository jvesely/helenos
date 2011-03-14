/*
 * Copyright (c) 2011 Vojtech Horky, Jan Vesely
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
/** @addtogroup drvusbuhcihc
 * @{
 */
/** @file
 * @brief UHCI driver initialization
 */
#include <ddf/driver.h>
#include <errno.h>
#include <str_error.h>

#include <usb/ddfiface.h>
#include <usb/debug.h>

#include "iface.h"
#include "uhci.h"

#define NAME "uhci-hcd"

static int uhci_add_device(ddf_dev_t *device);
/*----------------------------------------------------------------------------*/
static driver_ops_t uhci_driver_ops = {
	.add_device = uhci_add_device,
};
/*----------------------------------------------------------------------------*/
static driver_t uhci_driver = {
	.name = NAME,
	.driver_ops = &uhci_driver_ops
};
/*----------------------------------------------------------------------------*/
/** Initialize a new ddf driver instance for uhci hc and hub.
 *
 * @param[in] device DDF instance of the device to initialize.
 * @return Error code.
 */
int uhci_add_device(ddf_dev_t *device)
{
	usb_log_info("uhci_add_device() called\n");
	assert(device);
	uhci_t *uhci = malloc(sizeof(uhci_t));
	if (uhci == NULL) {
		usb_log_error("Failed to allocate UHCI driver.\n");
		return ENOMEM;
	}

	int ret = uhci_init(uhci, device);
	if (ret != EOK) {
		usb_log_error("Failed to initialzie UHCI driver.\n");
		return ret;
	}
	device->driver_data = uhci;
	return EOK;
}
/*----------------------------------------------------------------------------*/
/** Initialize global driver structures (NONE).
 *
 * @param[in] argc Nmber of arguments in argv vector (ignored).
 * @param[in] argv Cmdline argument vector (ignored).
 * @return Error code.
 *
 * Driver debug level is set here.
 */
int main(int argc, char *argv[])
{
	sleep(3); /* TODO: remove in final version */
	usb_log_enable(USB_LOG_LEVEL_DEBUG, NAME);

	return ddf_driver_main(&uhci_driver);
}
/**
 * @}
 */
