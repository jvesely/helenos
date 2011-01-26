/*
 * Copyright (c) 2009 Jiri Svoboda
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

/** @addtogroup kbd_port
 * @ingroup kbd
 * @{
 */ 
/** @file
 * @brief Chardev keyboard port driver.
 */

#include <ipc/ipc.h>
#include <ipc/char.h>
#include <async.h>
#include <kbd_port.h>
#include <kbd.h>
#include <vfs/vfs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

static void kbd_port_events(ipc_callid_t iid, ipc_call_t *icall);

static int dev_phone;

#define NAME "kbd"

/** List of devices to try connecting to. */
static const char *in_devs[] = {
	"/dev/char/ps2a",
	"/dev/char/s3c24ser"
};

static const int num_devs = sizeof(in_devs) / sizeof(in_devs[0]);

int kbd_port_init(void)
{
	int input_fd;
	int i;

	input_fd = -1;
	for (i = 0; i < num_devs; i++) {
		struct stat s;

		if (stat(in_devs[i], &s) == EOK)
			break;
	}

	if (i >= num_devs) {
		printf(NAME ": Could not find any suitable input device.\n");
		return -1;
	}

	input_fd = open(in_devs[i], O_RDONLY);
	if (input_fd < 0) {
		printf(NAME ": failed opening device %s (%d).\n", in_devs[i],
		    input_fd);
		return -1;
	}

	dev_phone = fd_phone(input_fd);
	if (dev_phone < 0) {
		printf(NAME ": Failed connecting to device\n");
		return -1;
	}

	/* NB: The callback connection is slotted for removal */
	sysarg_t taskhash;
	sysarg_t phonehash;
	if (ipc_connect_to_me(dev_phone, 0, 0, 0, &taskhash, &phonehash) != 0) {
		printf(NAME ": Failed to create callback from device\n");
		return -1;
	}

	async_new_connection(taskhash, phonehash, 0, NULL, kbd_port_events);

	return 0;
}

void kbd_port_yield(void)
{
}

void kbd_port_reclaim(void)
{
}

void kbd_port_write(uint8_t data)
{
	async_msg_1(dev_phone, CHAR_WRITE_BYTE, data);
}

static void kbd_port_events(ipc_callid_t iid, ipc_call_t *icall)
{
	/* Ignore parameters, the connection is already opened */
	while (true) {

		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);

		int retval;

		switch (IPC_GET_IMETHOD(call)) {
		case IPC_M_PHONE_HUNGUP:
			/* TODO: Handle hangup */
			return;
		case CHAR_NOTIF_BYTE:
			kbd_push_scancode(IPC_GET_ARG1(call));
			break;
		default:
			retval = ENOENT;
		}
		ipc_answer_0(callid, retval);
	}
}


/**
 * @}
 */ 
