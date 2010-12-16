/*
 * Copyright (c) 2010 Jiri Svoboda
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

/** @addtogroup driver_serial
 * @{
 */
/**
 * @file
 * @brief Samsung S3C24xx on-chip UART driver.
 *
 * This UART is present on the Samsung S3C24xx CPU (on the gta02 platform).
 */

#include <ddi.h>
#include <libarch/ddi.h>
#include <devmap.h>
#include <ipc/ipc.h>
#include <ipc/char.h>
#include <async.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysinfo.h>
#include <errno.h>
#include <inttypes.h>

#include "s3c24xx_uart.h"

#define NAME "s3c24ser"
#define NAMESPACE "char"

static irq_cmd_t uart_irq_cmds[] = {
	{
		.cmd = CMD_ACCEPT
	}
};

static irq_code_t uart_irq_code = {
	sizeof(uart_irq_cmds) / sizeof(irq_cmd_t),
	uart_irq_cmds
};

/** S3C24xx UART instance structure */
static s3c24xx_uart_t *uart;

static void s3c24xx_uart_connection(ipc_callid_t iid, ipc_call_t *icall);
static void s3c24xx_uart_irq_handler(ipc_callid_t iid, ipc_call_t *call);
static int s3c24xx_uart_init(s3c24xx_uart_t *uart);
static void s3c24xx_uart_sendb(s3c24xx_uart_t *uart, uint8_t byte);

int main(int argc, char *argv[])
{
	int rc;

	printf(NAME ": S3C24xx on-chip UART driver\n");

	rc = devmap_driver_register(NAME, s3c24xx_uart_connection);
	if (rc < 0) {
		printf(NAME ": Unable to register driver.\n");
		return -1;
	}

	uart = malloc(sizeof(s3c24xx_uart_t));
	if (uart == NULL)
		return -1;

	if (s3c24xx_uart_init(uart) != EOK)
		return -1;

	rc = devmap_device_register(NAMESPACE "/" NAME, &uart->devmap_handle);
	if (rc != EOK) {
		devmap_hangup_phone(DEVMAP_DRIVER);
		printf(NAME ": Unable to register device %s.\n",
		    NAMESPACE "/" NAME);
		return -1;
	}

	printf(NAME ": Registered device %s.\n", NAMESPACE "/" NAME);

	printf(NAME ": Accepting connections\n");
	task_retval(0);
	async_manager();

	/* Not reached */
	return 0;
}

/** Character device connection handler. */
static void s3c24xx_uart_connection(ipc_callid_t iid, ipc_call_t *icall)
{
	ipc_callid_t callid;
	ipc_call_t call;
	sysarg_t method;
	int retval;

	/* Answer the IPC_M_CONNECT_ME_TO call. */
	ipc_answer_0(iid, EOK);

	while (1) {
		callid = async_get_call(&call);
		method = IPC_GET_IMETHOD(call);
		switch (method) {
		case IPC_M_PHONE_HUNGUP:
			/* The other side has hung up. */
			ipc_answer_0(callid, EOK);
			return;
		case IPC_M_CONNECT_TO_ME:
			printf(NAME ": creating callback connection\n");
			uart->client_phone = IPC_GET_ARG5(call);
			retval = 0;
			break;
		case CHAR_WRITE_BYTE:
			printf(NAME ": write %" PRIun " to device\n",
			    IPC_GET_ARG1(call));
			s3c24xx_uart_sendb(uart, (uint8_t) IPC_GET_ARG1(call));
			retval = 0;
			break;
		default:
			retval = EINVAL;
			break;
		}
		ipc_answer_0(callid, retval);
	}
}

static void s3c24xx_uart_irq_handler(ipc_callid_t iid, ipc_call_t *call)
{
	(void) iid; (void) call;

	while ((pio_read_32(&uart->io->ufstat) & S3C24XX_UFSTAT_RX_COUNT) != 0) {
		uint32_t data = pio_read_32(&uart->io->urxh) & 0xff;
		uint32_t status = pio_read_32(&uart->io->uerstat);

		if (uart->client_phone != -1) {
			async_msg_1(uart->client_phone, CHAR_NOTIF_BYTE,
			    data);
		}

		if (status != 0)
			printf(NAME ": Error status 0x%x\n", status);
	}
}

/** Initialize S3C24xx on-chip UART. */
static int s3c24xx_uart_init(s3c24xx_uart_t *uart)
{
	void *vaddr;
	sysarg_t inr;

	if (sysinfo_get_value("s3c24xx_uart.address.physical",
	    &uart->paddr) != EOK)
		return -1;

	if (pio_enable((void *) uart->paddr, sizeof(s3c24xx_uart_io_t),
	    &vaddr) != 0)
		return -1;

	if (sysinfo_get_value("s3c24xx_uart.inr", &inr) != EOK)
		return -1;

	uart->io = vaddr;
	uart->client_phone = -1;

	printf(NAME ": device at physical address %p, inr %" PRIun ".\n",
	    (void *) uart->paddr, inr);

	async_set_interrupt_received(s3c24xx_uart_irq_handler);

	ipc_register_irq(inr, device_assign_devno(), 0, &uart_irq_code);

	/* Enable FIFO, Tx trigger level: empty, Rx trigger level: 1 byte. */
	pio_write_32(&uart->io->ufcon, UFCON_FIFO_ENABLE |
	    UFCON_TX_FIFO_TLEVEL_EMPTY | UFCON_RX_FIFO_TLEVEL_1B);

	/* Set RX interrupt to pulse mode */
	pio_write_32(&uart->io->ucon,
	    pio_read_32(&uart->io->ucon) & ~UCON_RX_INT_LEVEL);

	return EOK;
}

/** Send a byte to the UART. */
static void s3c24xx_uart_sendb(s3c24xx_uart_t *uart, uint8_t byte)
{
	/* Wait for space becoming available in Tx FIFO. */
	while ((pio_read_32(&uart->io->ufstat) & S3C24XX_UFSTAT_TX_FULL) != 0)
		;

	pio_write_32(&uart->io->utxh, byte);
}

/** @}
 */
