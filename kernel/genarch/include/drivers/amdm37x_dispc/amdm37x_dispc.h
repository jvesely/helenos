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
/** @addtogroup genarch
 * @{
 */
/**
 * @file
 * @brief Texas Instruments AM/DM37x SDRAM Memory Scheduler.
 */

#ifndef KERN_AMDM37x_DISPC_H_
#define KERN_AMDM37x_DISPC_H_

/* AMDM37x TRM p. 1813 */
#define AMDM37x_DISPC_BASE_ADDRESS 0x48050400
#define AMDM37x_DISPC_SIZE 1024

#define __paddname(line) PADD32_ ## line
#define _paddname(line) __paddname(line)
#define PADD32(count) uint32_t _paddname(__LINE__)[count]

#include <typedefs.h>

typedef struct {
	const ioport32_t revision;
#define AMDM37X_DISPC_REVISION_MASK  0xff

	PADD32(3);
	ioport32_t sysconfig;
#define AMDM37X_DISPC_SYSCONFIG_AUTOIDLE_FLAG  (1 << 0)
#define AMDM37X_DISPC_SYSCONFIG_SOFTRESET_FLAG  (1 << 1)
#define AMDM37X_DISPC_SYSCONFIG_ENWAKEUP_FLAG  (1 << 2)
#define AMDM37X_DISPC_SYSCONFIG_SIDLEMODE_MASK  0x3
#define AMDM37X_DISPC_SYSCONFIG_SIDLEMODE_SHIFT  3
#define AMDM37X_DISPC_SYSCONFIG_CLOCKACTIVITY_MASK  0x3
#define AMDM37X_DISPC_SYSCONFIG_CLOCKACTIVITY_SHIFT  8
#define AMDM37X_DISPC_SYSCONFIG_MIDLEMODE_MASK  0x3
#define AMDM37X_DISPC_SYSCONFIG_MIDLEMODE_SHIFT  12

	const ioport32_t sysstatus;
#define AMDM37X_DISPC_SYSSTATUS_RESETDONE_FLAG  (1 << 0)

	ioport32_t irqstatus;
	ioport32_t irqenable;
#define AMDM37X_DISPC_IRQ_FRAMEDONE_FLAG  (1 << 0)
#define AMDM37X_DISPC_IRQ_VSYNC_FLAG  (1 << 1)
#define AMDM37X_DISPC_IRQ_EVSYNCEVEN_FLAG  (1 << 2)
#define AMDM37X_DISPC_IRQ_EVSYNCODD_FLAG  (1 << 3)
#define AMDM37X_DISPC_IRQ_ACBIASCOUNTSTATUS_FLAG  (1 << 4)
#define AMDM37X_DISPC_IRQ_PROGRAMMEDLINENUMBER_FLAG  (1 << 5)
#define AMDM37X_DISPC_IRQ_GFXFIFOUNDERFLOW_FLAG  (1 << 6)
#define AMDM37X_DISPC_IRQ_GFXENDWINDOW_FLAG  (1 << 7)
#define AMDM37X_DISPC_IRQ_PALETTEGAMMALOADING_FLAG  (1 << 8)
#define AMDM37X_DISPC_IRQ_OPCERROR_FLAG  (1 << 9)
#define AMDM37X_DISPC_IRQ_VID1FIFOUNDERFLOW_FLAG  (1 << 10)
#define AMDM37X_DISPC_IRQ_VID1ENDWINDOW_FLAG  (1 << 11)
#define AMDM37X_DISPC_IRQ_VID2FIFOUNDERFLOW_FLAG  (1 << 12)
#define AMDM37X_DISPC_IRQ_VID2ENDWINDOW_FLAG  (1 << 13)
#define AMDM37X_DISPC_IRQ_SYNCLOST_FLAG  (1 << 14)
#define AMDM37X_DISPC_IRQ_SYNCLOSTDIGITAL_FLAG  (1 << 15)
#define AMDM37X_DISPC_IRQ_WAKEUP_FLAG  (1 << 16)

	PADD32(8);
	ioport32_t control;
#define AMDM37X_DISPC_CONTROL_LCD_ENABLE_FLAG  (1 << 0)
#define AMDM37X_DISPC_CONTROL_DIGITAL_ENABLE_FLAG  (1 << 1)
#define AMDM37X_DISPC_CONTROL_MONOCOLOR_FLAG  (1 << 2)
#define AMDM37X_DISPC_CONTROL_STNTFT_FLAG  (1 << 3)
#define AMDM37X_DISPC_CONTROL_M8B_FLAG  (1 << 4)
#define AMDM37X_DISPC_CONTROL_GOLCD_FLAG  (1 << 5)
#define AMDM37X_DISPC_CONTROL_GODIGITAL_FLAG  (1 << 6)
#define AMDM37X_DISPC_CONTROL_STDITHERENABLE_FLAG  (1 << 7)
#define AMDM37X_DISPC_CONTROL_TFTDATALINES_MASK  0x3
#define AMDM37X_DISPC_CONTROL_TFTDATALINES_SHIFT  8
#define AMDM37X_DISPC_CONTROL_TFTDATALINES_12B  0
#define AMDM37X_DISPC_CONTROL_TFTDATALINES_16B  1
#define AMDM37X_DISPC_CONTROL_TFTDATALINES_18B  2
#define AMDM37X_DISPC_CONTROL_TFTDATALINES_24B  3
#define AMDM37X_DISPC_CONTROL_STALLMODE_FLAG  (1 << 11)
#define AMDM37X_DISPC_CONTROL_OVERLAYOPTIMIZATION_FLAG  (1 << 12)
#define AMDM37X_DISPC_CONTROL_GPIN0_FLAG  (1 << 13)
#define AMDM37X_DISPC_CONTROL_GPIN1_FLAG  (1 << 14)
#define AMDM37X_DISPC_CONTROL_GPOUT0_FLAG  (1 << 15)
#define AMDM37X_DISPC_CONTROL_GPOUT1_FLAG  (1 << 16)
#define AMDM37X_DISPC_CONTROL_HT_MASK  0x7
#define AMDM37X_DISPC_CONTROL_HT_SHIFT  17
#define AMDM37X_DISPC_CONTROL_TDMENABLE_FLAG  (1 << 20)
#define AMDM37X_DISPC_CONTROL_TDMPARALLELMODE_MASK  0x3
#define AMDM37X_DISPC_CONTROL_TDMPARELLELMODE_SHIFT  21
#define AMDM37X_DISPC_CONTROL_TDMCYCLEFORMAT_MASK  0x3
#define AMDM37X_DISPC_CONTROL_TDMCYCLEFORMAT_SHIFT  23
#define AMDM37X_DISPC_CONTROL_TDMUNUSEDBITS_MASK  0x3
#define AMDM37X_DISPC_CONTROL_TDMUNUSEDBITS_SHIFT  25
#define AMDM37X_DISPC_CONTROL_PCKFREEENABLE_FLAG  (1 << 27)
#define AMDM37X_DISPC_CONTROL_LCDENABLESIGNAL_FLAG  (1 << 28)
#define AMDM37X_DISPC_CONTROL_KCDENABLEPOL_FLAG  (1 << 29)
#define AMDM37X_DISPC_CONTROL_SPATIALTEMPORALDITHERINGFRAMES_MASK  0x3
#define AMDM37X_DISPC_CONTROL_SPATIALTEMPORALDITHERINGFRAMES_SHIFT  30

	ioport32_t config;
#define AMDM37X_DISPC_CONFIG_PIXELGATED_FLAG  (1 << 0)
#define AMDM37X_DISPC_CONFIG_LOADMODE_MASK  0x3
#define AMDM37X_DISPC_CONFIG_LOADMODE_SHIFT  1
#define AMDM37X_DISPC_CONFIG_LOADMODE_PGDATAEVERYFRAME  0x0
#define AMDM37X_DISPC_CONFIG_LOADMODE_PGUSER  0x1
#define AMDM37X_DISPC_CONFIG_LOADMODE_DATAEVERYFRAME  0x2
#define AMDM37X_DISPC_CONFIG_LOADMODE_PGDFIRSTFRAME  0x3
#define AMDM37X_DISPC_CONFIG_PALETTEGAMMATABLE_FLAG  (1 << 3)
#define AMDM37X_DISPC_CONFIG_PIXELDATAGATED_FLAG  (1 << 4)
#define AMDM37X_DISPC_CONFIG_PIXELCLOCKGATED_FLAG  (1 << 5)
#define AMDM37X_DISPC_CONFIG_HSYNCGATED_FLAG  (1 << 6)
#define AMDM37X_DISPC_CONFIG_VSYNCGATED_FLAG  (1 << 7)
#define AMDM37X_DISPC_CONFIG_ACBIASGATED_FLAG  (1 << 8)
#define AMDM37X_DISPC_CONFIG_FUNCGATED_FLAG  (1 << 9)
#define AMDM37X_DISPC_CONFIG_TCKLCDENABLE_FLAG  (1 << 10)
#define AMDM37X_DISPC_CONFIG_TCKLCDSELECTION_FLAG  (1 << 11)
#define AMDM37X_DISPC_CONFIG_TCKDIGENABLE_FLAG  (1 << 12)
#define AMDM37X_DISPC_CONFIG_TCKDIGSELECTION_FLAG  (1 << 13)
#define AMDM37X_DISPC_CONFIG_FIFOMERGE_FLAG  (1 << 14)
#define AMDM37X_DISPC_CONFIG_CPR_FLAG  (1 << 15)
#define AMDM37X_DISPC_CONFIG_FIFOHANDCHECK_FLAG  (1 << 16)
#define AMDM37X_DISPC_CONFIG_FIFOFILLING_FLAG  (1 << 17)
#define AMDM37X_DISPC_CONFIG_LCDPALPHABLENDERENABLDE_FLAG  (1 << 18)
#define AMDM37X_DISPC_CONFIG_TVALPHABLENDERENABLE_FLAG  (1 << 19)

	PADD32(1);
	ioport32_t default_color[2];
	ioport32_t trans_color[2];
#define AMDM37X_DISPC_COLOR_MASK 0xffffff

	const ioport32_t line_status;
	ioport32_t line_number;
#define AMDM37X_DISPC_LINE_NUMBER_MASK 0x3ff

	ioport32_t timing_h;
#define AMDM37X_DISPC_TIMING_H_HSW_MASK 0xff
#define AMDM37X_DISPC_TIMING_H_HSW_SHIFT 0
#define AMDM37X_DISPC_TIMING_H_HFP_MASK 0xfff
#define AMDM37X_DISPC_TIMING_H_HFP_SHIFT 8
#define AMDM37X_DISPC_TIMING_H_HBP_MASK 0xfff
#define AMDM37X_DISPC_TIMING_H_HBP_SHIFT 20

	ioport32_t timing_v;
#define AMDM37X_DISPC_TIMING_V_VSW_MASK 0xff
#define AMDM37X_DISPC_TIMING_V_VSW_SHIFT 0
#define AMDM37X_DISPC_TIMING_V_VFP_MASK 0xfff
#define AMDM37X_DISPC_TIMING_V_VFP_SHIFT 8
#define AMDM37X_DISPC_TIMING_V_VBP_MASK 0xfff
#define AMDM37X_DISPC_TIMING_V_VBP_SHIFT 20

	ioport32_t pol_freq;
#define AMDM37X_DISPC_POL_FREQ_ACB_MASK  0xff
#define AMDM37X_DISPC_POL_FREQ_ACB_SHIFT 0
#define AMDM37X_DISPC_POL_FREQ_ACBI_MASK  0xf
#define AMDM37X_DISPC_POL_FREQ_ACBI_SHIFT 8
#define AMDM37X_DISPC_POL_FREQ_IVS_FLAG  (1 << 12)
#define AMDM37X_DISPC_POL_FREQ_IHS_FLAG  (1 << 13)
#define AMDM37X_DISPC_POL_FREQ_IPC_FLAG  (1 << 14)
#define AMDM37X_DISPC_POL_FREQ_IEO_FLAG  (1 << 15)
#define AMDM37X_DISPC_POL_FREQ_RF_FLAG  (1 << 16)
#define AMDM37X_DISPC_POL_FREQ_ONOFF_FLAG  (1 << 17)

	ioport32_t divisor;
#define AMDM37X_DISPC_DIVISOR_PCD_MASK  0xff
#define AMDM37X_DISPC_DIVISOR_PCD_SHIFT  0
#define AMDM37X_DISPC_DIVISOR_LCD_MASK  0xff
#define AMDM37X_DISPC_DIVISOR_LCD_SHIFT  16

	ioport32_t global_alpha;
#define AMDM37X_DISPC_GLOBAL_ALPHA_GFXGLOBALALPHA_MASK  0xff
#define AMDM37X_DISPC_GLOBAL_ALPHA_GFXGLOBALALPHA_SHIFT  0
#define AMDM37X_DISPC_GLOBAL_ALPHA_VID2GLOBALALPHA_MASK  0xff
#define AMDM37X_DISPC_GLOBAL_ALPHA_VID2GLOBALALPHA_SHIFT  16

	ioport32_t size_dig;
	ioport32_t size_lcd;

	struct {
		ioport32_t ba[2];
		ioport32_t position;
#define AMDM37X_DISPC_GFX_POSITION_GFXPOSX_MASK  0x7ff
#define AMDM37X_DISPC_GFX_POSITION_GFXPOSX_SHIFT  0
#define AMDM37X_DISPC_GFX_POSITION_GFXPOSY_MASK  0x7ff
#define AMDM37X_DISPC_GFX_POSITION_GFXPOSY_SHIFT  16

		ioport32_t size;
#define AMDM37X_DISPC_SIZE_WIDTH_MASK  0x7ff
#define AMDM37X_DISPC_SIZE_WIDTH_SHIFT  0
#define AMDM37X_DISPC_SIZE_HEIGHT_MASK  0x7ff
#define AMDM37X_DISPC_SIZE_HEIGHT_SHIFT  16

		PADD32(4);
		ioport32_t attributes;
#define AMDM37X_DISPC_GFX_ATTRIBUTES_ENABLE_FLAG  (1 << 0)
#define AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_MASK  0xf
#define AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_SHIFT  1
#define AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_ARGB16  0x5
#define AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_RGB16  0x6
#define AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_RGB24_32  0x8
#define AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_RGB24  0x9
#define AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_ARGB  0xc
#define AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_RGBA  0xd
#define AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_RGBX  0xe
#define AMDM37X_DISPC_GFX_ATTRIBUTES_REPLICATIONENABLE_FLAG  (1 << 5)
#define AMDM37X_DISPC_GFX_ATTRIBUTES_GFXBURSTSIZE_MASK  0x3
#define AMDM37X_DISPC_GFX_ATTRIBUTES_GFXBURSTSIZE_SHIFT  6
#define AMDM37X_DISPC_GFX_ATTRIBUTES_GFXCHANNELOUT_FLAG  (1 << 8)
#define AMDM37X_DISPC_GFX_ATTRIBUTES_GFXNIBBLEMODE_FLAG  (1 << 9)
#define AMDM37X_DISPC_GFX_ATTRIBUTES_GFXENDIANNES_FLAG  (1 << 10)
#define AMDM37X_DISPC_GFX_ATTRIBUTES_GFXFIFOPRELOAD_FLAG  (1 << 11)
#define AMDM37X_DISPC_GFX_ATTRIBUTES_GFXROTATION_MASK  0x3
#define AMDM37X_DISPC_GFX_ATTRIBUTES_GFXROTATION_SHIFT  12
#define AMDM37X_DISPC_GFX_ATTRIBUTES_GFXARBITRATION_FLAG  (1 << 14)
#define AMDM37X_DISPC_GFX_ATTRIBUTES_GFXSELFREFRESH_FLAG  (1 << 15)
#define AMDM37X_DISPC_GFX_ATTRIBUTES_PREMULTIALPHA_FLAG  (1 << 28)


		ioport32_t fifo_threshold;
		const ioport32_t fifo_size_status;
		ioport32_t row_inc;
		ioport32_t pixel_inc;
		ioport32_t window_skip;
		ioport32_t table_ba;
	} gfx;

	struct {
		ioport32_t ba[2];
		ioport32_t position;
		ioport32_t size;
		ioport32_t attributes;
		ioport32_t fifo_threshold;
		const ioport32_t fifo_size_status;
		ioport32_t row_inc;
		ioport32_t pixel_inc;
		ioport32_t fir;
		ioport32_t picture_size;
		ioport32_t accui[2];
		struct {
			ioport32_t hi;
			ioport32_t hvi;
		} fir_coef[8];
		ioport32_t conv_coef[5];
		PADD32(2);
	} vid[2];
	/* 0x1d4 */
	ioport32_t data_cycle[3];
	/* 0x1e0 */
	ioport32_t vid_fir_coef_v[8];
	/* 0x200 */
	PADD32(8);
	/* 0x220 */
	ioport32_t cpr_coef_r;
	ioport32_t cpr_coef_g;
	ioport32_t cpr_coef_b;
	ioport32_t gfx_preload;

	/* 0x230 */
	ioport32_t vid_preload[2];

} __attribute__((packed)) amdm37x_dispc_regs_t;


static inline void amdm37x_dispc_setup_fb(amdm37x_dispc_regs_t *regs,
    unsigned x, unsigned y, unsigned bpp, uintptr_t pa)
{
	ASSERT(regs);
#define WRITE_DUMP(name, value) \
	printf("Writing %s %p: %x. New: %x\n", #name, &regs->name, value, regs->name)
	/* Init sequence for dispc is in chapter 7.6.5.1.4 p. 1810,
	 * no idea what parts of that work. */

	/* Disable all interrupts */
	regs->irqenable = 0;

	/* Pixel format specifics*/
	uint32_t attrib_pixel_format = 0;
	uint32_t control_data_lanes = 0;
	switch (bpp)
	{
	case 32:
		attrib_pixel_format = AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_RGBX;
		control_data_lanes = AMDM37X_DISPC_CONTROL_TFTDATALINES_24B;
		break;
	case 24:
		attrib_pixel_format = AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_RGB24;
		control_data_lanes = AMDM37X_DISPC_CONTROL_TFTDATALINES_24B;
		break;
	case 16:
		attrib_pixel_format = AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_RGB16;
		control_data_lanes = AMDM37X_DISPC_CONTROL_TFTDATALINES_16B;
		break;
	default:
		ASSERT(false);
	}

	/* Prepare sizes */
	const uint32_t size_reg =
	    (((x - 1) & AMDM37X_DISPC_SIZE_WIDTH_MASK)
	        << AMDM37X_DISPC_SIZE_WIDTH_SHIFT) |
	    (((y - 1) & AMDM37X_DISPC_SIZE_HEIGHT_MASK)
	        << AMDM37X_DISPC_SIZE_HEIGHT_SHIFT);

	/* modes taken from u-boot */
	// TODO replace magic values with actual correct values
//	regs->timing_h = 0x1a4024c9;
//	regs->timing_v = 0x02c00509;
//	regs->pol_freq = 0x00007028;
//	regs->divisor  = 0x00010001;

	/* setup output */
	regs->size_lcd = size_reg;
	WRITE_DUMP(size_lcd, size_reg);
	regs->size_dig = size_reg;
	WRITE_DUMP(size_dig, size_reg);

	/* Nice blue default color */
	regs->default_color[0] = 0x0000ff;
	regs->default_color[1] = 0x0000ff;
	WRITE_DUMP(default_color[0], 0xff);
	WRITE_DUMP(default_color[1], 0xff);

	/* Setup control register */
	uint32_t control = 0 |
		AMDM37X_DISPC_CONTROL_PCKFREEENABLE_FLAG |
		(control_data_lanes << AMDM37X_DISPC_CONTROL_TFTDATALINES_SHIFT) |
		AMDM37X_DISPC_CONTROL_GPOUT0_FLAG |
		AMDM37X_DISPC_CONTROL_GPOUT1_FLAG;
	regs->control = control;
	WRITE_DUMP(control, control);

	/* No gamma stuff only data */
	uint32_t config = (AMDM37X_DISPC_CONFIG_LOADMODE_DATAEVERYFRAME
	            << AMDM37X_DISPC_CONFIG_LOADMODE_SHIFT);
	regs->config = config;
	WRITE_DUMP(config, config);


	/* Set framebuffer base address */
	regs->gfx.ba[0] = pa;
	regs->gfx.ba[1] = pa;
	regs->gfx.position = 0;
	WRITE_DUMP(gfx.ba[0], pa);
	WRITE_DUMP(gfx.ba[1], pa);
	WRITE_DUMP(gfx.position, 0);

	/* Setup fb size */
	regs->gfx.size = size_reg;
	WRITE_DUMP(gfx.size, size_reg);


	/* Set pixel format */
	uint32_t attribs = 0 |
	    (attrib_pixel_format << AMDM37X_DISPC_GFX_ATTRIBUTES_FORMAT_SHIFT);
	regs->gfx.attributes = attribs;
	WRITE_DUMP(gfx.attributes, attribs);

	/* 0x03ff03c0 is the default */
	regs->gfx.fifo_threshold = 0x03ff03c0;
	/* This value should be stride - width, 1 means next pixel i.e.
	 * stride == width */
	regs->gfx.row_inc = 1;
	/* number of bytes to next pixel in BPP multiples */
	regs->gfx.pixel_inc = 1;
	/* only used if video is played over fb */
	regs->gfx.window_skip = 0;
	/* Gamma and palette table */
	regs->gfx.table_ba = 0;
	WRITE_DUMP(gfx.fifo_threshold, 0x03ff03c0);
	WRITE_DUMP(gfx.row_inc, 1);
	WRITE_DUMP(gfx.pixel_inc, 1);
	WRITE_DUMP(gfx.window_skip, 0);
	WRITE_DUMP(gfx.table_ba, 0);

	/* enable frame buffer graphics */
	regs->gfx.attributes |= AMDM37X_DISPC_GFX_ATTRIBUTES_ENABLE_FLAG;
	/* Update register values */
	regs->control |= AMDM37X_DISPC_CONTROL_GOLCD_FLAG;
	regs->control |= AMDM37X_DISPC_CONTROL_GODIGITAL_FLAG;
	/* Enable output */
	regs->control |= AMDM37X_DISPC_CONTROL_LCD_ENABLE_FLAG;
	regs->control |= AMDM37X_DISPC_CONTROL_DIGITAL_ENABLE_FLAG;
}


#endif
/**
 * @}
 */
