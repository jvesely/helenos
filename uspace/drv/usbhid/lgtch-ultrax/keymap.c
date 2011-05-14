/*
 * Copyright (c) 2011 Lubos Slovak
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

/** @addtogroup drvusbhid
 * @{
 */
/** @file
 * UUSB multimedia key to keycode mapping.
 */

#include <io/keycode.h>
#include <stdint.h>
#include <stdio.h>
#include <usb/debug.h>
#include "keymap.h"

/**
 * Mapping between USB HID multimedia usages (from HID Usage Tables) and 
 * corresponding HelenOS key codes.
 *
 * Currently only Usages used by Logitech UltraX keyboard are present. All other
 * should result in 0.
 */
static int usb_hid_keymap_consumer[0x29c] = {
	[0xf] = KC_F1, /* Just for testing purposes */
	[0x5] = KC_F2, /* Just for testing purposes */
	[0x8] = KC_F3, /* Just for testing purposes */
	[0x6] = KC_F4, /* Just for testing purposes */
	[0x7] = KC_F5, /* Just for testing purposes */
	[0xc] = KC_F6, /* Just for testing purposes */
	
	[0xb5] = 0,       /* Scan Next Track */
	[0xb6] = 0,       /* Scan Previous Track */
	[0xb7] = 0,       /* Stop */
	[0xb8] = 0,       /* Eject */
	[0xcd] = KC_F2,   /* Play/Pause */
	[0xe2] = KC_F3,   /* Mute */
	[0xe9] = KC_F5,   /* Volume Increment */
	[0xea] = KC_F4,   /* Volume Decrement */
	[0x183] = 0,      /* AL Consumer Control Configuration */
	[0x18a] = 0,      /* AL Email Reader */
	[0x192] = 0,      /* AL Calculator */
	[0x221] = 0,      /* AC Search */
	[0x223] = 0,      /* AC Home */
	[0x224] = 0,      /* AC Back */
	[0x225] = 0,      /* AC Forward */
	[0x226] = 0,      /* AC Stop */
	[0x227] = KC_F1,  /* AC Refresh */
	[0x22a] = KC_F6   /* AC Bookmarks */
};

/**
 * Translates USB HID Usages from the Consumer Page into HelenOS keycodes.
 *
 * @param usage USB HID Consumer Page Usage number.
 * 
 * @retval HelenOS key code corresponding to the given USB Consumer Page Usage.
 */
unsigned int usb_lgtch_map_usage(int usage)
{
	unsigned int key;
	int *map = usb_hid_keymap_consumer;
	size_t map_length = sizeof(usb_hid_keymap_consumer) / sizeof(int);

	if ((usage < 0) || ((size_t)usage >= map_length))
		return -1;

	/*! @todo What if the usage is not in the table? */
	key = map[usage];
	
	return key;
}

/**
 * @}
 */
