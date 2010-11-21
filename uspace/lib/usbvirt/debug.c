/*
 * Copyright (c) 2010 Vojtech Horky
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

/** @addtogroup libusbvirt usb
 * @{
 */
/** @file
 * @brief Debugging support.
 */
#include <stdio.h>
#include <bool.h>

#include "device.h"
#include "private.h"


static void debug_print(int level, uint8_t tag,
    int current_level, uint8_t enabled_tags,
    const char *format, va_list args)
{
	if (level > current_level) {
		return;
	}
	
	if ((tag & enabled_tags) == 0) {
		return;
	}
	
	bool print_prefix = true;
	
	if ((format[0] == '%') && (format[1] == 'M')) {
		format += 2;
		print_prefix = false;
	}
	
	if (print_prefix) {
		printf("[vusb]: ", level);
		while (--level > 0) {
			printf(" ");
		}
	}
	
	vprintf(format, args);
	
	if (print_prefix) {
		printf("\n");
	}
}


void user_debug(usbvirt_device_t *device, int level, uint8_t tag,
    const char *format, ...)
{
	va_list args;
	va_start(args, format);
	
	debug_print(level, tag,
	    device->debug_level, device->debug_enabled_tags,
	    format, args);
	
	va_end(args);
}

void lib_debug(usbvirt_device_t *device, int level, uint8_t tag,
    const char *format, ...)
{
	va_list args;
	va_start(args, format);
	
	debug_print(level, tag,
	    device->lib_debug_level, device->lib_debug_enabled_tags,
	    format, args);
	
	va_end(args);
}

/**
 * @}
 */
