/*
 * Copyright (c) 2011 Jan Vesely
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

/** @addtogroup libusbhost
 * @{
 */
/** @file
 * Device keeper structure and functions (implementation).
 */
#include <assert.h>
#include <errno.h>
#include <usb/debug.h>
#include <usb/host/device_keeper.h>

/*----------------------------------------------------------------------------*/
/** Initialize device keeper structure.
 *
 * @param[in] instance Memory place to initialize.
 *
 * Set all values to false/0.
 */
void usb_device_keeper_init(usb_device_keeper_t *instance)
{
	assert(instance);
	unsigned i = 0;
	for (; i < USB_ADDRESS_COUNT; ++i) {
		instance->devices[i].occupied = false;
		instance->devices[i].handle = 0;
		instance->devices[i].speed = USB_SPEED_MAX;
	}
	// TODO: is this hack enough?
	// (it is needed to allow smooth registration at default address)
	instance->devices[0].occupied = true;
	instance->last_address = 0;
	fibril_mutex_initialize(&instance->guard);
}
/*----------------------------------------------------------------------------*/
/** Get a free USB address
 *
 * @param[in] instance Device keeper structure to use.
 * @param[in] speed Speed of the device requiring address.
 * @return Free address, or error code.
 */
usb_address_t device_keeper_get_free_address(
    usb_device_keeper_t *instance, usb_speed_t speed)
{
	assert(instance);
	fibril_mutex_lock(&instance->guard);

	usb_address_t new_address = instance->last_address;
	do {
		++new_address;
		if (new_address > USB11_ADDRESS_MAX)
			new_address = 1;
		if (new_address == instance->last_address) {
			fibril_mutex_unlock(&instance->guard);
			return ENOSPC;
		}
	} while (instance->devices[new_address].occupied);

	assert(new_address != USB_ADDRESS_DEFAULT);
	assert(instance->devices[new_address].occupied == false);

	instance->devices[new_address].occupied = true;
	instance->devices[new_address].speed = speed;
	instance->last_address = new_address;

	fibril_mutex_unlock(&instance->guard);
	return new_address;
}
/*----------------------------------------------------------------------------*/
/** Bind USB address to devman handle.
 *
 * @param[in] instance Device keeper structure to use.
 * @param[in] address Device address
 * @param[in] handle Devman handle of the device.
 */
void usb_device_keeper_bind(usb_device_keeper_t *instance,
    usb_address_t address, devman_handle_t handle)
{
	assert(instance);
	fibril_mutex_lock(&instance->guard);

	assert(address > 0);
	assert(address <= USB11_ADDRESS_MAX);
	assert(instance->devices[address].occupied);

	instance->devices[address].handle = handle;
	fibril_mutex_unlock(&instance->guard);
}
/*----------------------------------------------------------------------------*/
/** Release used USB address.
 *
 * @param[in] instance Device keeper structure to use.
 * @param[in] address Device address
 */
void usb_device_keeper_release(
    usb_device_keeper_t *instance, usb_address_t address)
{
	assert(instance);
	assert(address > 0);
	assert(address <= USB11_ADDRESS_MAX);

	fibril_mutex_lock(&instance->guard);
	assert(instance->devices[address].occupied);

	instance->devices[address].occupied = false;
	fibril_mutex_unlock(&instance->guard);
}
/*----------------------------------------------------------------------------*/
/** Find USB address associated with the device
 *
 * @param[in] instance Device keeper structure to use.
 * @param[in] handle Devman handle of the device seeking its address.
 * @return USB Address, or error code.
 */
usb_address_t usb_device_keeper_find(
    usb_device_keeper_t *instance, devman_handle_t handle)
{
	assert(instance);
	fibril_mutex_lock(&instance->guard);
	usb_address_t address = 1;
	while (address <= USB11_ADDRESS_MAX) {
		if (instance->devices[address].handle == handle) {
			assert(instance->devices[address].occupied);
			fibril_mutex_unlock(&instance->guard);
			return address;
		}
		++address;
	}
	fibril_mutex_unlock(&instance->guard);
	return ENOENT;
}

/** Find devman handled assigned to USB address.
 *
 * @param[in] instance Device keeper structure to use.
 * @param[in] address Address the caller wants to find.
 * @param[out] handle Where to store found handle.
 * @return Whether such address is currently occupied.
 */
bool usb_device_keeper_find_by_address(usb_device_keeper_t *instance,
    usb_address_t address, devman_handle_t *handle)
{
	assert(instance);
	fibril_mutex_lock(&instance->guard);
	if ((address < 0) || (address >= USB_ADDRESS_COUNT)) {
		fibril_mutex_unlock(&instance->guard);
		return false;
	}
	if (!instance->devices[address].occupied) {
		fibril_mutex_unlock(&instance->guard);
		return false;
	}

	if (handle != NULL) {
		*handle = instance->devices[address].handle;
	}

	fibril_mutex_unlock(&instance->guard);
	return true;
}

/*----------------------------------------------------------------------------*/
/** Get speed associated with the address
 *
 * @param[in] instance Device keeper structure to use.
 * @param[in] address Address of the device.
 * @return USB speed.
 */
usb_speed_t usb_device_keeper_get_speed(
    usb_device_keeper_t *instance, usb_address_t address)
{
	assert(instance);
	assert(address >= 0);
	assert(address <= USB11_ADDRESS_MAX);

	return instance->devices[address].speed;
}
/**
 * @}
 */
