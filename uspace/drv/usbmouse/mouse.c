/*
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

/** @addtogroup drvusbmouse
 * @{
 */
/**
 * @file
 * Actual handling of USB mouse protocol.
 */
#include "mouse.h"
#include <usb/debug.h>
#include <errno.h>

int usb_mouse_polling_fibril(void *arg)
{
	assert(arg != NULL);
	ddf_dev_t *dev = (ddf_dev_t *) arg;
	usb_mouse_t *mouse = (usb_mouse_t *) dev->driver_data;

	assert(mouse);

	while (true) {
		async_usleep(10 * 1000);

		uint8_t buffer[8];
		size_t actual_size;

		/* FIXME: check for errors. */
		usb_endpoint_pipe_start_session(&mouse->poll_pipe);

		usb_endpoint_pipe_read(&mouse->poll_pipe, buffer, 8, &actual_size);

		usb_endpoint_pipe_end_session(&mouse->poll_pipe);

		uint8_t butt = buffer[0];
		char str_buttons[4] = {
			butt & 1 ? '#' : '.',
			butt & 2 ? '#' : '.',
			butt & 4 ? '#' : '.',
			0
		};

		int shift_x = ((int) buffer[1]) - 127;
		int shift_y = ((int) buffer[2]) - 127;
		int wheel = ((int) buffer[3]) - 127;

		if (buffer[1] == 0) {
			shift_x = 0;
		}
		if (buffer[2] == 0) {
			shift_y = 0;
		}
		if (buffer[3] == 0) {
			wheel = 0;
		}

		usb_log_debug("buttons=%s  dX=%+3d  dY=%+3d  wheel=%+3d\n",
		   str_buttons, shift_x, shift_y, wheel);
	}

	return EOK;
}


/**
 * @}
 */
