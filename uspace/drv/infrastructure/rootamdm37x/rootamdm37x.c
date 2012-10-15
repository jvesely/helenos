/*
 * Copyright (c) 2012 Jan Vesely
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
 * @defgroup root_amdm37x TI AM/DM37x platform driver.
 * @brief HelenOS TI AM/DM37x platform driver.
 * @{
 */

/** @file
 */
#define _DDF_DATA_IMPLANT

#define DEBUG_CM

#include <ddf/driver.h>
#include <ddf/log.h>
#include <errno.h>
#include <ops/hw_res.h>
#include <stdio.h>
#include <ddi.h>

#include "uhh.h"
#include "usbtll.h"
#include "core_cm.h"
#include "clock_control_cm.h"
#include "usbhost_cm.h"

#define NAME  "rootamdm37x"

typedef struct {
	uhh_regs_t *uhh;
	tll_regs_t *tll;
	struct {
		core_cm_regs_t *core;
		clock_control_cm_regs_t *clocks;
		usbhost_cm_regs_t *usbhost;
	} cm;
} amdm37x_t;

static int amdm37x_hw_access_init(amdm37x_t *device)
{
	assert(device);
	int ret = EOK;

	ret = pio_enable((void*)USBHOST_CM_BASE_ADDRESS, USBHOST_CM_SIZE,
	    (void**)&device->cm.usbhost);
	if (ret != EOK)
		return ret;

	ret = pio_enable((void*)CORE_CM_BASE_ADDRESS, CORE_CM_SIZE,
	    (void**)&device->cm.core);
	if (ret != EOK)
		return ret;

	ret = pio_enable((void*)CLOCK_CONTROL_CM_BASE_ADDRESS,
		    CLOCK_CONTROL_CM_SIZE, (void**)&device->cm.clocks);
	if (ret != EOK)
		return ret;

	ret = pio_enable((void*)AMDM37x_USBTLL_BASE_ADDRESS,
	    AMDM37x_USBTLL_SIZE, (void**)&device->tll);
	if (ret != EOK)
		return ret;

	ret = pio_enable((void*)AMDM37x_UHH_BASE_ADDRESS,
	    AMDM37x_UHH_SIZE, (void**)&device->uhh);
	if (ret != EOK)
		return ret;

	return EOK;
}

static int usb_clocks(amdm37x_t *device, bool on)
{
	uint32_t reg;

	/* Set DPLL3 and DPLL4 to automatic */
	reg = device->cm.clocks->autoidle_pll;
	reg &= ~(CLOCK_CONTROL_CM_AUTOIDLE_PLL_AUTO_CORE_DPLL_MASK <<
	    CLOCK_CONTROL_CM_AUTOIDLE_PLL_AUTO_CORE_DPLL_SHIFT);
	reg &= ~(CLOCK_CONTROL_CM_AUTOIDLE_PLL_AUTO_PERIPH_DPLL_MASK <<
	    CLOCK_CONTROL_CM_AUTOIDLE_PLL_AUTO_PERIPH_DPLL_SHIFT);
	reg |= (CLOCK_CONTROL_CM_AUTOIDLE_PLL_AUTO_CORE_DPLL_AUTOMATIC <<
	    CLOCK_CONTROL_CM_AUTOIDLE_PLL_AUTO_CORE_DPLL_SHIFT);
	reg |= (CLOCK_CONTROL_CM_AUTOIDLE_PLL_AUTO_PERIPH_DPLL_AUTOMATIC <<
	    CLOCK_CONTROL_CM_AUTOIDLE_PLL_AUTO_PERIPH_DPLL_SHIFT);
	device->cm.clocks->autoidle_pll = reg;

	/* Set DPLL5 to automatic */
	reg = device->cm.clocks->autoidle2_pll;
	reg &= ~(CLOCK_CONTROL_CM_AUTOIDLE2_PLL_AUTO_PERIPH2_DPLL_MASK <<
	    CLOCK_CONTROL_CM_AUTOIDLE2_PLL_AUTO_PERIPH2_DPLL_SHIFT);
	reg |= (CLOCK_CONTROL_CM_AUTOIDLE2_PLL_AUTO_PERIPH2_DPLL_AUTOMATIC <<
	    CLOCK_CONTROL_CM_AUTOIDLE2_PLL_AUTO_PERIPH2_DPLL_SHIFT);
	device->cm.clocks->autoidle2_pll = reg;


#ifdef DEBUG_CM
	printf("DPLL5 could be on: %x %x.\n",
	    device->cm.clocks->idlest_ckgen, device->cm.clocks->idlest2_ckgen);
#endif

	if (on) {
		/* Enable interface and function clock for USB TLL */
		device->cm.core->iclken3 |= CORE_CM_ICLKEN3_EN_USBTLL_FLAG;
		device->cm.core->fclken3 |= CORE_CM_FCLKEN3_EN_USBTLL_FLAG;

		/* Enable interface and function clock for USB hosts */
		device->cm.usbhost->iclken |= USBHOST_CM_ICLKEN_EN_USBHOST;
		device->cm.usbhost->fclken |= USBHOST_CM_FCLKEN_EN_USBHOST1_FLAG;
		device->cm.usbhost->fclken |= USBHOST_CM_FCLKEN_EN_USBHOST2_FLAG;
#ifdef DEBUG_CM
	printf("DPLL5 (and everything else) should be on: %x %x.\n",
	    device->cm.clocks->idlest_ckgen, device->cm.clocks->idlest2_ckgen);
#endif
	} else {
		/* Disable interface and function clock for USB hosts */
		device->cm.usbhost->fclken &= ~USBHOST_CM_FCLKEN_EN_USBHOST2_FLAG;
		device->cm.usbhost->fclken &= ~USBHOST_CM_FCLKEN_EN_USBHOST1_FLAG;
		device->cm.usbhost->iclken &= ~USBHOST_CM_ICLKEN_EN_USBHOST;

		/* Disable interface and function clock for USB TLL */
		device->cm.core->fclken3 &= ~CORE_CM_FCLKEN3_EN_USBTLL_FLAG;
		device->cm.core->iclken3 &= ~CORE_CM_ICLKEN3_EN_USBTLL_FLAG;
	}

	return EOK;
}

/** Initialize USB TLL port connections.
 *
 * Different modes are on page 3312 of the Manual Figure 22-34.
 * Select mode than can operate in FS/LS.
 */
static int usb_tll_init(amdm37x_t *device)
{

	/* Reset USB TLL */
	device->tll->sysconfig |= TLL_SYSCONFIG_SOFTRESET_FLAG;
	ddf_msg(LVL_DEBUG2, "Waiting for USB TLL reset");
	while (!(device->tll->sysstatus & TLL_SYSSTATUS_RESET_DONE_FLAG));
	ddf_msg(LVL_DEBUG, "USB TLL Reset done.");

	{
	/* Setup idle mode (smart idle) */
	uint32_t sysc = device->tll->sysconfig;
	sysc |= TLL_SYSCONFIG_CLOCKACTIVITY_FLAG | TLL_SYSCONFIG_AUTOIDLE_FLAG;
	sysc = (sysc
	    & ~(TLL_SYSCONFIG_SIDLE_MODE_MASK << TLL_SYSCONFIG_SIDLE_MODE_SHIFT)
	    ) | (0x2 << TLL_SYSCONFIG_SIDLE_MODE_SHIFT);
	device->tll->sysconfig = sysc;
	ddf_msg(LVL_DEBUG2, "Set TLL->sysconfig (%p) to %x:%x.",
	    &device->tll->sysconfig, device->tll->sysconfig, sysc);
	}

	{
	/* Smart idle for UHH */
	uint32_t sysc = device->uhh->sysconfig;
	sysc |= UHH_SYSCONFIG_CLOCKACTIVITY_FLAG | UHH_SYSCONFIG_AUTOIDLE_FLAG;
	sysc = (sysc
	    & ~(UHH_SYSCONFIG_SIDLE_MODE_MASK << UHH_SYSCONFIG_SIDLE_MODE_SHIFT)
	    ) | (0x2 << UHH_SYSCONFIG_SIDLE_MODE_SHIFT);
	sysc = (sysc
	    & ~(UHH_SYSCONFIG_MIDLE_MODE_MASK << UHH_SYSCONFIG_MIDLE_MODE_SHIFT)
	    ) | (0x2 << UHH_SYSCONFIG_MIDLE_MODE_SHIFT);
	ddf_msg(LVL_DEBUG2, "Set UHH->sysconfig (%p) to %x.",
	    &device->uhh->sysconfig, device->uhh->sysconfig);
	device->uhh->sysconfig = sysc;

	/* All ports are connected on BBxM */
	device->uhh->hostconfig |= (UHH_HOSTCONFIG_P1_CONNECT_STATUS_FLAG
	    | UHH_HOSTCONFIG_P2_CONNECT_STATUS_FLAG
	    | UHH_HOSTCONFIG_P3_CONNECT_STATUS_FLAG);

	/* Set all ports to go through TLL(UTMI)
	 * Direct connection can only work in HS mode */
	device->uhh->hostconfig |= (UHH_HOSTCONFIG_P1_ULPI_BYPASS_FLAG
	    | UHH_HOSTCONFIG_P2_ULPI_BYPASS_FLAG
	    | UHH_HOSTCONFIG_P3_ULPI_BYPASS_FLAG);
	ddf_msg(LVL_DEBUG2, "Set UHH->hostconfig (%p) to %x.",
	    &device->uhh->hostconfig, device->uhh->hostconfig);
	}

	device->tll->shared_conf |= TLL_SHARED_CONF_FCLK_IS_ON_FLAG;
	ddf_msg(LVL_DEBUG2, "Set shared conf port (%p) to %x.",
	    &device->tll->shared_conf, device->tll->shared_conf);

	for (unsigned i = 0; i < 3; ++i) {
		uint32_t ch = device->tll->channel_conf[i];
		/* Clear Channel mode and FSLS mode */
		ch &= ~(TLL_CHANNEL_CONF_CHANMODE_MASK
		    << TLL_CHANNEL_CONF_CHANMODE_SHIFT)
		    & ~(TLL_CHANNEL_CONF_FSLSMODE_MASK
		    << TLL_CHANNEL_CONF_FSLSMODE_SHIFT);

		/* Serial mode is the only one capable of FS/LS operation. */
		ch |= (TLL_CHANNEL_CONF_CHANMODE_UTMI_SERIAL_MODE
		    << TLL_CHANNEL_CONF_CHANMODE_SHIFT);

		/* Select FS/LS mode, no idea what the difference is
		 * one of bidirectional modes might be good choice
		 * 2 = 3pin bidi phy. */
		ch |= (2 << TLL_CHANNEL_CONF_FSLSMODE_SHIFT);

		/* Write to register */
		ddf_msg(LVL_DEBUG2, "Setting port %u(%p) to %x.",
		    i, &device->tll->channel_conf[i], ch);
		device->tll->channel_conf[i] = ch;
	}
	return EOK;
}

typedef struct {
	hw_resource_list_t hw_resources;
} rootamdm37x_fun_t;

#define OHCI_BASE_ADDRESS  0x48064400
#define OHCI_SIZE  1024
#define EHCI_BASE_ADDRESS  0x48064800
#define EHCI_SIZE  1024

static hw_resource_t ohci_res[] = {
	{
		.type = MEM_RANGE,
		/* See amdm37x TRM page. 3316 for these values */
		.res.io_range = {
			.address = OHCI_BASE_ADDRESS,
			.size = OHCI_SIZE,
			.endianness = LITTLE_ENDIAN
		},
	},
	{
		.type = INTERRUPT,
		.res.interrupt = { .irq = 76 },
	},
};

static const rootamdm37x_fun_t ohci = {
	.hw_resources = {
	    .resources = ohci_res,
	    .count = sizeof(ohci_res)/sizeof(ohci_res[0]),
	}
};

static hw_resource_t ehci_res[] = {
	{
		.type = MEM_RANGE,
		/* See amdm37x TRM page. 3316 for these values */
		.res.io_range = {
			.address = EHCI_BASE_ADDRESS,
			.size = EHCI_SIZE,
			.endianness = LITTLE_ENDIAN
		},
	},
	{
		.type = INTERRUPT,
		.res.interrupt = { .irq = 77 },
	},
};

static const rootamdm37x_fun_t ehci = {
	.hw_resources = {
	    .resources = ehci_res,
	    .count = sizeof(ehci_res) / sizeof(ehci_res[0]),
	}
};

static hw_resource_list_t *rootamdm37x_get_resources(ddf_fun_t *fnode);
static bool rootamdm37x_enable_interrupt(ddf_fun_t *fun);

static hw_res_ops_t fun_hw_res_ops = {
	.get_resource_list = &rootamdm37x_get_resources,
	.enable_interrupt = &rootamdm37x_enable_interrupt,
};

static ddf_dev_ops_t rootamdm37x_fun_ops =
{
	.interfaces[HW_RES_DEV_IFACE] = &fun_hw_res_ops
};

static bool rootamdm37x_add_fun(ddf_dev_t *dev, const char *name,
    const char *str_match_id, const rootamdm37x_fun_t *fun)
{
	ddf_msg(LVL_DEBUG, "Adding new function '%s'.", name);
	
	/* Create new device function. */
	ddf_fun_t *fnode = ddf_fun_create(dev, fun_inner, name);
	if (fnode == NULL)
		return ENOMEM;
	
	
	/* Add match id */
	if (ddf_fun_add_match_id(fnode, str_match_id, 100) != EOK) {
		ddf_fun_destroy(fnode);
		return false;
	}
	
	/* Set provided operations to the device. */
	ddf_fun_data_implant(fnode, (void*)fun);
	ddf_fun_set_ops(fnode, &rootamdm37x_fun_ops);
	
	/* Register function. */
	if (ddf_fun_bind(fnode) != EOK) {
		ddf_msg(LVL_ERROR, "Failed binding function %s.", name);
		// TODO This will try to free our data!
		ddf_fun_destroy(fnode);
		return false;
	}
	
	return true;
}

/** Add the root device.
 *
 * @param dev Device which is root of the whole device tree
 *            (both of HW and pseudo devices).
 *
 * @return Zero on success, negative error number otherwise.
 *
 */
static int rootamdm37x_dev_add(ddf_dev_t *dev)
{
	assert(dev);
	amdm37x_t *device = ddf_dev_data_alloc(dev, sizeof(amdm37x_t));
	if (!device)
		return ENOMEM;
	int ret = amdm37x_hw_access_init(device);
	if (ret != EOK) {
		ddf_msg(LVL_FATAL, "Failed to setup hw access!.\n");
		return ret;
	}

	ret = usb_clocks(device, true);
	if (ret != EOK) {
		ddf_msg(LVL_FATAL, "Failed to enable USB HC clocks!.\n");
		return ret;
	}

	ret = usb_tll_init(device);
	if (ret != EOK) {
		ddf_msg(LVL_FATAL, "Failed to init USB TLL!.\n");
		usb_clocks(device, false);
		return ret;
	}

	/* Register functions */
	if (!rootamdm37x_add_fun(dev, "ohci", "usb/host=ohci", &ohci))
		ddf_msg(LVL_ERROR, "Failed to add OHCI function for "
		    "BeagleBoard-xM platform.");
	if (!rootamdm37x_add_fun(dev, "ehci", "usb/host=ehci", &ehci))
		ddf_msg(LVL_ERROR, "Failed to add EHCI function for "
		    "BeagleBoard-xM platform.");

	return EOK;
}

/** The root device driver's standard operations. */
static driver_ops_t rootamdm37x_ops = {
	.dev_add = &rootamdm37x_dev_add
};

/** The root device driver structure. */
static driver_t rootamdm37x_driver = {
	.name = NAME,
	.driver_ops = &rootamdm37x_ops
};

static hw_resource_list_t *rootamdm37x_get_resources(ddf_fun_t *fnode)
{
	rootamdm37x_fun_t *fun = ddf_fun_data_get(fnode);
	assert(fun != NULL);
	return &fun->hw_resources;
}

static bool rootamdm37x_enable_interrupt(ddf_fun_t *fun)
{
	/* TODO */
	return false;
}

int main(int argc, char *argv[])
{
	printf("%s: HelenOS AM/DM37x(OMAP37x) platform driver\n", NAME);
	ddf_log_init(NAME);
	return ddf_driver_main(&rootamdm37x_driver);
}

/**
 * @}
 */
