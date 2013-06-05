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
 * @brief Texas Instruments AMDM37x on-chip interrupt controller driver.
 */

#ifndef KERN_AMDM37x_UART_H_
#define KERN_AMDM37x_UART_H_

#include <typedefs.h>
#include <console/chardev.h>
#include <ddi/irq.h>

/* AMDM37x TRM p. 2950 */
#define AMDM37x_UART1_BASE_ADDRESS   0x4806a000
#define AMDM37x_UART1_SIZE   1024
#define AMDM37x_UART1_IRQ   72 /* AMDM37x TRM p. 2418 */

#define AMDM37x_UART2_BASE_ADDRESS   0x4806b000
#define AMDM37x_UART2_SIZE   1024
#define AMDM37x_UART2_IRQ   73 /* AMDM37x TRM p. 2418 */

#define AMDM37x_UART3_BASE_ADDRESS   0x49020000
#define AMDM37x_UART3_SIZE   1024
#define AMDM37x_UART3_IRQ   74 /* AMDM37x TRM p. 2418 */

#define AMDM37x_UART4_BASE_ADDRESS   0x49042000
#define AMDM37x_UART4_SIZE   1024
#define AMDM37x_UART4_IRQ   80 /* AMDM37x TRM p. 2418 */

typedef struct {
	union {
		/** Stores lower part of the 14-bit baud divisor */
		ioport32_t dll;
#define AMDM37x_UART_DLL_MASK   (0xff)

		/** Receive holding register */
		const ioport32_t rhr;
#define AMDM37x_UART_RHR_MASK   (0xff)

		/** Transmit holding register */
		ioport32_t thr;
#define AMDM37x_UART_THR_MASK   (0xff)
	};

	union {
		/** Stores higher part of the 14-bit baud divisor */
		ioport32_t dlh;
#define AMDM37x_UART_DLH_MASK   (0x1f)

		/** Interrupt enable registers */
		ioport32_t ier;
#define AMDM37x_UART_IER_RHR_IRQ_FLAG   (1 << 0)
#define AMDM37x_UART_IER_THR_IRQ_FLAG   (1 << 1)
#define AMDM37x_UART_IER_LINE_STS_IRQ_FLAG   (1 << 2)
#define AMDM37x_UART_IER_MODEM_STS_IRQ_FLAG   (1 << 3)
#define AMDM37x_UART_IER_SLEEP_MODE_FLAG   (1 << 4)
#define AMDM37x_UART_IER_XOFF_IRQ_FLAG   (1 << 5)
#define AMDM37x_UART_IER_RTS_IRQ_FLAG   (1 << 6)
#define AMDM37x_UART_IER_CTS_IRQ_FLAG   (1 << 7)

#define AMDM37x_CIR_IER_RHR_IRQ_FLAG   (1 << 0)
#define AMDM37x_CIR_IER_THR_IRQ_FLAG   (1 << 1)
#define AMDM37x_CIR_IER_RX_STOP_IRQ_FLAG   (1 << 2)
#define AMDM37x_CIR_IER_RX_OVERRUN_IRQ_FLAG   (1 << 3)
#define AMDM37x_CIR_IER_TX_STS_IRQ_FLAG   (1 << 5)

#define AMDM37x_IRDA_IER_RHR_IRQ_FLAG   (1 << 0)
#define AMDM37x_IRDA_IER_THR_IRQ_FLAG   (1 << 1)
#define AMDM37x_IRDA_IER_LAST_RX_BYTE_IRQ_FLAG   (1 << 2)
#define AMDM37x_IRDA_IER_RX_OVERRUN_IRQ_FLAG   (1 << 3)
#define AMDM37x_IRDA_IER_STS_FIFO_TRIG_IRQ_FLAG   (1 << 4)
#define AMDM37x_IRDA_IER_TX_STS_IRQ_FLAG   (1 << 5)
#define AMDM37x_IRDA_IER_LINE_STS_IRQ_FLAG   (1 << 6)
#define AMDM37x_IRDA_IER_EOF_IRQ_FLAG   (1 << 7)
	};

	union {
		/** Interrupt identification register */
		const ioport32_t iir;
#define AMDM37x_UART_IIR_IRQ_PENDING_FLAG   (1 << 0)
#define AMDM37x_UART_IIR_TYPE_MASK   (0x1f)
#define AMDM37x_UART_IIR_TYPE_SHIFT   (1)
#define AMDM37x_UART_IIR_FCR_MASK   (0x3)
#define AMDM37x_UART_IIR_FCR_SHIFT   (6)

#define AMDM37x_CIR_IIR_RHR_IRQ_FLAG   (1 << 0)
#define AMDM37x_CIR_IIR_THR_IRQ_FLAG   (1 << 1)
#define AMDM37x_CIR_IIR_RX_STOP_IRQ_FLAG   (1 << 2)
#define AMDM37x_CIR_IIR_RX_OE_IRQ_FLAG   (1 << 3)
#define AMDM37x_CIR_IIR_TX_STS_IRQ_FLAG   (1 << 5)

#define AMDM37x_IRDA_IIR_RHR_IRQ_FLAG   (1 << 0)
#define AMDM37x_IRDA_IIR_THR_IRQ_FLAG   (1 << 1)
#define AMDM37x_IRDA_IIR_RX_FIFO_LB_IRQ_FLAG   (1 << 2)
#define AMDM37x_IRDA_IIR_RX_OE_IRQ_FLAG   (1 << 3)
#define AMDM37x_IRDA_IIR_STS_FIFO_IRQ_FLAG   (1 << 4)
#define AMDM37x_IRDA_IIR_TX_STS_IRQ_FLAG   (1 << 5)
#define AMDM37x_IRDA_IIR_LINE_STS_IRQ_FLAG   (1 << 6)
#define AMDM37x_IRDA_IIR_EOF_IRQ_FLAG   (1 << 7)

		/** FIFO control register */
		ioport32_t fcr;
#define AMDM37x_UART_FCR_FIFO_EN_FLAG   (1 << 0)
#define AMDM37x_UART_FCR_RX_FIFO_CLR_FLAG   (1 << 1)
#define AMDM37x_UART_FCR_TX_FIFO_CLR_FLAG   (1 << 3)
#define AMDM37x_UART_FCR_DMA_MODE_FLAG   (1 << 4)

#define AMDM37x_UART_FCR_TX_FIFO_TRIG_MASK   (0x3)
#define AMDM37x_UART_FCR_TX_FIFO_TRIG_SHIFT   (4)

#define AMDM37x_UART_FCR_RX_FIFO_TRIG_MASK   (0x3)
#define AMDM37x_UART_FCR_RX_FIFO_TRIG_SHIFT   (6)

		/** Enhanced feature register */
		ioport32_t efr;
#define AMDM37x_UART_EFR_SW_FLOW_CTRL_RX_MASK   (0x3)
#define AMDM37x_UART_EFR_SW_FLOW_CTRL_RX_SHIFT   (0)
#define AMDM37x_UART_EFR_SW_FLOW_CTRL_TX_MASK   (0x3)
#define AMDM37x_UART_EFR_SW_FLOW_CTRL_TX_SHIFT   (2)

#define AMDM37x_UART_EFR_SW_FLOW_CTRL_NONE   (0x0)
#define AMDM37x_UART_EFR_SW_FLOW_CTRL_X2   (0x1)
#define AMDM37x_UART_EFR_SW_FLOW_CTRL_X1   (0x2)
#define AMDM37x_UART_EFR_SW_FLOW_CTRL_XBOTH   (0x3)

#define AMDM37x_UART_EFR_ENH_FLAG   (1 << 4)
#define AMDM37x_UART_EFR_SPEC_CHAR_FLAG   (1 << 5)
#define AMDM37x_UART_EFR_AUTO_RTS_EN_FLAG   (1 << 6)
#define AMDM37x_UART_EFR_AUTO_CTS_EN_FLAG   (1 << 7)
	};

	/** Line control register */
	ioport32_t lcr;
#define AMDM37x_UART_LCR_CHAR_LENGTH_MASK   (0x3)
#define AMDM37x_UART_LCR_CHAR_LENGTH_SHIFT   (0)
#define AMDM37x_UART_LCR_CHAR_LENGTH_5BITS   (0x0)
#define AMDM37x_UART_LCR_CHAR_LENGTH_6BITS   (0x1)
#define AMDM37x_UART_LCR_CHAR_LENGTH_7BITS   (0x2)
#define AMDM37x_UART_LCR_CHAR_LENGTH_8BITS   (0x3)
#define AMDM37x_UART_LCR_NB_STOP_FLAG   (1 << 2)
#define AMDM37x_UART_LCR_PARITY_EN_FLAG   (1 << 3)
#define AMDM37x_UART_LCR_PARITY_TYPE1_FLAG   (1 << 4)
#define AMDM37x_UART_LCR_PARITY_TYPE2_FLAG   (1 << 5)
#define AMDM37x_UART_LCR_BREAK_EN_FLAG   (1 << 6)
#define AMDM37x_UART_LCR_DIV_EN_FLAG   (1 << 7)


	union {
		/** Modem control register */
		ioport32_t mcr;
#define AMDM37x_UART_MCR_DTR_FLAG   (1 << 0)
#define AMDM37x_UART_MCR_RTS_FLAG   (1 << 1)
#define AMDM37x_UART_MCR_RI_STS_CH_FLAG   (1 << 2)
#define AMDM37x_UART_MCR_CD_STS_CH_FLAG   (1 << 3)
#define AMDM37x_UART_MCR_LOOPBACK_EN_FLAG   (1 << 4)
#define AMDM37x_UART_MCR_XON_EN_FLAG   (1 << 5)
#define AMDM37x_UART_MCR_TCR_TLR_FLAG   (1 << 6)

		/** UART: XON1 char, IRDA: ADDR1 address */
		ioport32_t xon1_addr1;
#define AMDM37x_UART_XON1_ADDR1_MASK   (0xff)
	};

	union {
		/** Line status register */
		const ioport32_t lsr;
#define AMDM37x_UART_LSR_RX_FIFO_E_FLAG   (1 << 0)
#define AMDM37x_UART_LSR_RX_OE_FLAG   (1 << 1)
#define AMDM37x_UART_LSR_RX_PE_FLAG   (1 << 2)
#define AMDM37x_UART_LSR_RX_FE_FLAG   (1 << 3)
#define AMDM37x_UART_LSR_RX_BI_FLAG   (1 << 4)
#define AMDM37x_UART_LSR_TX_FIFO_E_FLAG   (1 << 5)
#define AMDM37x_UART_LSR_TX_SR_E_FLAG   (1 << 6)
#define AMDM37x_UART_LSR_RX_FIFO_STS_FLAG   (1 << 7)

#define AMDM37x_CIR_LSR_RX_FIFO_E_FLAG   (1 << 0)
#define AMDM37x_CIR_LSR_RX_STOP_FLAG   (1 << 5)
#define AMDM37x_CIR_LSR_THR_EMPTY_FLAG   (1 << 7)

#define AMDM37x_IRDA_LSR_RX_FIFO_E_FLAG   (1 << 0)
#define AMDM37x_IRDA_LSR_STS_FIFO_E_FLAG   (1 << 1)
#define AMDM37x_IRDA_LSR_CRC_FLAG   (1 << 2)
#define AMDM37x_IRDA_LSR_ABORT_FLAG   (1 << 3)
#define AMDM37x_IRDA_LSR_FTL_FLAG   (1 << 4)
#define AMDM37x_IRDA_LSR_RX_LAST_FLAG   (1 << 5)
#define AMDM37x_IRDA_LSR_STS_FIFO_FULL_FLAG   (1 << 6)
#define AMDM37x_IRDA_LSR_THR_EMPTY_FLAG   (1 << 7)

		/** UART: XON2 char, IRDA: ADDR2 address */
		ioport32_t xon2_addr2;
	};

	union {
		/** Modem status register */
		const ioport32_t msr;
#define AMDM37x_UART_MSR_CTS_STS_FLAG   (1 << 0)
#define AMDM37x_UART_MSR_DSR_STS_FLAG   (1 << 1)
#define AMDM37x_UART_MSR_RI_STS_FLAG   (1 << 2)
#define AMDM37x_UART_MSR_DCD_STS_FLAG   (1 << 3)
#define AMDM37x_UART_MSR_NCTS_STS_FLAG   (1 << 4)
#define AMDM37x_UART_MSR_NDSR_STS_FLAG   (1 << 5)
#define AMDM37x_UART_MSR_NRI_STS_FLAG   (1 << 6)
#define AMDM37x_UART_MSR_NCD_STS_FLAG   (1 << 7)

		/** Transmission control register */
		ioport32_t tcr;
#define AMDM37x_UART_TCR_FIFO_TRIG_MASK   (0xf)
#define AMDM37x_UART_TCR_FIFO_TRIG_HALT_SHIFT   (0)
#define AMDM37x_UART_TCR_FIFO_TRIG_START_SHIFT   (4)

		/** UART: XOFF1 char */
		ioport32_t xoff1;
#define AMDM37x_UART_XOFF1_MASK   (0xff)
	};

	union {
		/* Scratchpad register, does nothing */
		ioport32_t spr;
#define AMDM37x_UART_SPR_MASK   (0xff)

		/* Trigger level register */
		ioport32_t tlr;
#define AMDM37x_UART_TLR_LEVEL_MASK   (0xf)
#define AMDM37x_UART_TLR_TX_FIFO_TRIG_SHIFT   (0)
#define AMDM37x_UART_TLR_RX_FIFO_TRIG_SHIFT   (4)

		/** UART: XOFF2 char */
		ioport32_t xoff2;
#define AMDM37x_UART_XOFF2_MASK   (0xff)
	};

	/** Mode definition register. */
	ioport32_t mdr1;
#define AMDM37x_UART_MDR_MS_MASK   (0x7)
#define AMDM37x_UART_MDR_MS_SHIFT   (0)
#define AMDM37x_UART_MDR_MS_UART16   (0x0)
#define AMDM37x_UART_MDR_MS_SIR   (0x1)
#define AMDM37x_UART_MDR_MS_UART16_AUTO   (0x2)
#define AMDM37x_UART_MDR_MS_UART13   (0x3)
#define AMDM37x_UART_MDR_MS_MIR   (0x4)
#define AMDM37x_UART_MDR_MS_FIR   (0x5)
#define AMDM37x_UART_MDR_MS_CIR   (0x6)
#define AMDM37x_UART_MDR_MS_DISABLE   (0x7)

#define AMDM37x_UART_MDR_IR_SLEEP_FLAG   (1 << 3)
#define AMDM37x_UART_MDR_SET_TXIR_FLAG   (1 << 4)
#define AMDM37x_UART_MDR_SCT_FLAG   (1 << 5)
#define AMDM37x_UART_MDR_SIP_FLAG   (1 << 6)
#define AMDM37x_UART_MDR_FRAME_END_MODE_FLAG   (1 << 7)

	/** Mode definition register */
	ioport32_t mdr2;
#define AMDM37x_UART_MDR_IRTX_UNDERRUN_FLAG   (1 << 0)
#define AMDM37x_UART_MDR_STS_FIFO_TRIG_MASK   (0x3)
#define AMDM37x_UART_MDR_STS_FIFO_TRIG_SHIFT   (1)
#define AMDM37x_UART_MDR_PULSE_SHAPING_FLAG   (1 << 3)
#define AMDM37x_UART_MDR_CIR_PULSE_MODE_MASK   (0x3)
#define AMDM37x_UART_MDR_CIR_PULSE_MODE_SHIFT   (4)
#define AMDM37x_UART_MDR_IRRXINVERT_FLAG   (1 << 6)


	/* UART3 specific */
	union {
		/** Status FIFO line status register (IrDA only) */
		const ioport32_t sflsr;
#define AMDM37x_IRDA_SFLSR_CRC_ERROR_FLAG   (1 << 1)
#define AMDM37x_IRDA_SFLSR_ABORT_FLAG   (1 << 2)
#define AMDM37x_IRDA_SFLSR_FTL_FLAG   (1 << 3)
#define AMDM37x_IRDA_SFLSR_OE_FLAG   (1 << 4)

		/** Transmit frame length low (IrDA only) */
		ioport32_t txfll;
#define AMDM37x_UART_TXFLL_MASK   (0xff)
	};

	/* UART3 specific */
	union {
		/** Dummy register to restart TX or RX (IrDA only) */
		const ioport32_t resume;
		/** Transmit frame length high (IrDA only) */
		ioport32_t txflh;
#define AMDM37x_UART_TXFLH_MASK   (0xff)
	};

	/* UART3 specific */
	union {
		/** Status FIFO register low (IrDA only) */
		const ioport32_t sfregl;
#define AMDM37x_UART_SFREGL_MASK   (0xff)
		/** Received frame length low (IrDA only) */
		ioport32_t rxfll;
#define AMDM37x_UART_RXFLL_MASK   (0xff)
	};

	/* UART3 specific */
	union {
		/** Status FIFO register high (IrDA only) */
		const ioport32_t sfregh;
#define AMDM37x_UART_SFREGH_MASK   (0xf)
		/** Received frame length high (IrDA only) */
		ioport32_t rxflh;
#define AMDM37x_UART_RXFLH_MASK   (0xf)
	};

	union {
		/** UART autobauding status register */
		const ioport32_t uasr;
#define AMDM37x_UART_UASR_SPEED_MASK   (0x1f)
#define AMDM37x_UART_UASR_SPEED_SHIFT   (0)
#define AMDM37x_UART_UASR_8BIT_CHAR_FLAG   (1 << 5)
#define AMDM37x_UART_UASR_PARITY_MASK   (0x3)
#define AMDM37x_UART_UASR_PARITY_SHIFT   (6)

		/** BOF control register (IrDA only) */
		ioport32_t blr; /* UART3 sepcific */
#define AMDM37x_IRDA_BLR_XBOF_TYPE_FLAG   (1 << 6)
#define AMDM37x_IRDA_BLR_STS_FIFO_RESET   (1 << 7)
	};

	/** Auxiliary control register (IrDA only) */
	ioport32_t acreg; /* UART3 specific */
#define AMDM37x_IRDA_ACREG_EOT_EN_FLAG   (1 << 0)
#define AMDM37x_IRDA_ACREG_ABORT_EN_FLAG   (1 << 1)
#define AMDM37x_IRDA_ACREG_SCTX_EN_FLAG   (1 << 2)
#define AMDM37x_IRDA_ACREG_SEND_SIP_FLAG   (1 << 3)
#define AMDM37x_IRDA_ACREG_DIS_TX_UNDERRUN_FLAG   (1 << 4)
#define AMDM37x_IRDA_ACREG_DIS_IR_RX_FLAG   (1 << 5)
#define AMDM37x_IRDA_ACREG_SD_MOD_FLAG   (1 << 6)
#define AMDM37x_IRDA_ACREG_PULSE_TYPE_FLAG   (1 << 7)

	/** Supplementary control register */
	ioport32_t scr;
#define AMDM37x_UART_SCR_DMA_MODE_CTL_FLAG   (1 << 0)
#define AMDM37x_UART_SCR_DMA_MODE_MASK   (0x3)
#define AMDM37x_UART_SCR_DMA_MODE_SHIFT   (1)
#define AMDM37x_UART_SCR_TX_EMPTY_CTL_IRQ_FLAG   (1 << 3)
#define AMDM37x_UART_SCR_RX_CTS_WU_EN_FLAG   (1 << 4)
#define AMDM37x_UART_SCR_TX_TRIG_GRANU1_FLAG   (1 << 6)
#define AMDM37x_UART_SCR_RX_TRIG_GRANU1_FLAG   (1 << 7)

	/** Supplementary status register */
	const ioport32_t ssr;
#define AMDM37x_UART_SSR_TX_FIFO_FULL_FLAG   (1 << 0)
#define AMDM37x_UART_SSR_RX_CTS_WU_STS_FLAG   (1 << 1)
#define AMDM37x_UART_SSR_DMA_COUNTER_RESET_FLAG   (1 << 2)

	/** BOF Length register (IrDA only)*/
	ioport32_t eblr; /* UART3 specific */
#define AMDM37x_IRDA_EBLR_DISABLED   (0x00)
#define AMDM37x_IRDA_EBLR_RX_STOP_BITS(bits)   (bits & 0xff)

	uint32_t padd0_;

	/** Module version register */
	const ioport32_t mvr;
#define AMDM37x_UART_MVR_MINOR_MASK   (0xf)
#define AMDM37x_UART_MVR_MINOR_SHIFT   (0)
#define AMDM37x_UART_MVR_MAJOR_MASK   (0xf)
#define AMDM37x_UART_MVR_MAJOR_SHIFT   (4)

	/** System configuration register */
	ioport32_t sysc;
#define AMDM37x_UART_SYSC_AUTOIDLE_FLAG   (1 << 0)
#define AMDM37x_UART_SYSC_SOFTRESET_FLAG   (1 << 1)
#define AMDM37x_UART_SYSC_ENWAKEUP_FLAG   (1 << 2)
#define AMDM37x_UART_SYSC_IDLE_MODE_MASK   (0x3)
#define AMDM37x_UART_SYSC_IDLE_MODE_SHIFT   (3)
#define AMDM37x_UART_SYSC_IDLE_MODE_FORCE   (0x0)
#define AMDM37x_UART_SYSC_IDLE_MODE_NO   (0x1)
#define AMDM37x_UART_SYSC_IDLE_MODE_SMART   (0x2)

	/** System status register */
	const ioport32_t syss;
#define AMDM37x_UART_SYSS_RESETDONE_FLAG   (1 << 0)

	/** Wake-up enable register */
	ioport32_t wer;
#define AMDM37x_UART_WER_CTS_ACTIVITY_FLAG  (1 << 0)
#define AMDM37x_UART_WER_RI_ACTIVITY_FLAG  (1 << 2)
#define AMDM37x_UART_WER_RX_ACTIVITY_FLAG  (1 << 4)
#define AMDM37x_UART_WER_RHR_IRQ_FLAG  (1 << 5)
#define AMDM37x_UART_WER_RLS_IRQ_FLAG  (1 << 6)
#define AMDM37x_UART_WER_TX_WAKEUP_EN_FLAG  (1 << 7)

	/** Carrier frequency prescaler */
	ioport32_t cfps;	/* UART3 specific */
#define AMDM37x_UART_CFPS_MASK   (0xff)

	/** Number of bytes in RX fifo */
	const ioport32_t rx_fifo_lvl;
#define AMDM37x_UART_RX_FIFO_LVL_MASK   (0xff)

	/** Number of bytes in TX fifo */
	const ioport32_t tx_fifo_lvl;
#define AMDM37x_UART_TX_FIFO_LVL_MASK   (0xff)

	/** RX/TX empty interrupts */
	ioport32_t ier2;
#define AMDM37x_UART_IER2_RX_FIFO_EMPTY_IRQ_EN_FLAG  (1 << 0)
#define AMDM37x_UART_IER2_TX_FIFO_EMPTY_IRQ_EN_FLAG  (1 << 1)

	/** RX/TX empty status */
	ioport32_t isr2;
#define AMDM37x_UART_ISR2_RX_FIFO_EMPTY_FLAG  (1 << 0)
#define AMDM37x_UART_ISR2_TX_FIFO_EMPTY_FLAG  (1 << 1)

	uint32_t padd2_[3];

	/** Mode definition register 3 */
	ioport32_t mdr3;
#define AMDM37x_UART_MDR3_DIS_CIR_RX_DEMOD_FLAG   (1 << 0)
} amdm37x_uart_regs_t;

typedef struct {
	amdm37x_uart_regs_t *regs;
	indev_t *indev;
	outdev_t outdev;
	irq_t irq;
} amdm37x_uart_t;


bool amdm37x_uart_init(amdm37x_uart_t *, inr_t, uintptr_t, size_t);
void amdm37x_uart_input_wire(amdm37x_uart_t *, indev_t *);

#endif

/**
 * @}
 */
