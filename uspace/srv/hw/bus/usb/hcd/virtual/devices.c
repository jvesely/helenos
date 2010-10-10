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

/** @addtogroup usb
 * @{
 */
/** @file
 * @brief
 */

#include <ipc/ipc.h>
#include <adt/list.h>
#include <bool.h>
#include <async.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <str_error.h>

#include <usb/virtdev.h>

#include "devices.h"

#define list_foreach(pos, head) \
	for (pos = (head)->next; pos != (head); \
        	pos = pos->next)

LIST_INITIALIZE(devices);

virtdev_connection_t *virtdev_find_by_address(usb_address_t address)
{
	link_t *pos;
	list_foreach(pos, &devices) {
		virtdev_connection_t *dev
		    = list_get_instance(pos, virtdev_connection_t, link);
		if (dev->address == address) {
			return dev;
		}
	}
	
	return NULL;
}

virtdev_connection_t *virtdev_add_device(usb_address_t address, int phone)
{
	virtdev_connection_t *dev = (virtdev_connection_t *)
	    malloc(sizeof(virtdev_connection_t));
	dev->phone = phone;
	dev->address = address;
	list_append(&dev->link, &devices);
	
	return dev;
}

 void virtdev_destroy_device(virtdev_connection_t *dev)
{
	list_remove(&dev->link);
	free(dev);
}

/**
 * @}
 */
