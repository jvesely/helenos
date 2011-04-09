/*
 * Copyright (c) 2010 Matus Dekanek
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
/** @addtogroup drvusbhub
 * @{
 */
/** @file
 * @brief usb hub main functionality
 */

#include <ddf/driver.h>
#include <bool.h>
#include <errno.h>
#include <str_error.h>
#include <inttypes.h>

#include <usb_iface.h>
#include <usb/ddfiface.h>
#include <usb/descriptor.h>
#include <usb/recognise.h>
#include <usb/request.h>
#include <usb/classes/hub.h>
#include <stdio.h>

#include "usbhub.h"
#include "usbhub_private.h"
#include "port_status.h"
#include "usb/usb.h"
#include "usb/pipes.h"
#include "usb/classes/classes.h"


/** Information for fibril for device discovery. */
struct add_device_phase1 {
	usb_hub_info_t *hub;
	size_t port;
	usb_speed_t speed;
};


static usb_hub_info_t * usb_hub_info_create(usb_device_t * usb_dev);

static int usb_hub_process_hub_specific_info(usb_hub_info_t * hub_info);

static int usb_hub_set_configuration(usb_hub_info_t * hub_info);

static int usb_hub_release_default_address(usb_hub_info_t * hub);

//static int usb_hub_init_add_device(usb_hub_info_t * hub, uint16_t port,
//	usb_speed_t speed);

//static void usb_hub_finalize_add_device(usb_hub_info_t * hub,
//	uint16_t port, usb_speed_t speed);

static void usb_hub_removed_device(
	usb_hub_info_t * hub, uint16_t port);

static void usb_hub_port_over_current(usb_hub_info_t * hub,
	uint16_t port, uint32_t status);

static int get_port_status(usb_pipe_t *ctrl_pipe, size_t port,
    usb_port_status_t *status);

static int enable_port_callback(int port_no, void *arg);

static int add_device_phase1_worker_fibril(void *arg);

static int add_device_phase1_new_fibril(usb_hub_info_t *hub, size_t port,
    usb_speed_t speed);

static void usb_hub_process_interrupt(usb_hub_info_t * hub,
	uint16_t port);

static int usb_process_hub_over_current(usb_hub_info_t * hub_info,
	usb_hub_status_t status);

static int usb_process_hub_power_change(usb_hub_info_t * hub_info,
	usb_hub_status_t status);

static void usb_hub_process_global_interrupt(usb_hub_info_t * hub_info);

//static int initialize_non_removable(usb_hub_info_t * hub_info,
//	unsigned int port);

//static int usb_hub_trigger_connecting_non_removable_devices(
//	usb_hub_info_t * hub, usb_hub_descriptor_t * descriptor);


/**
 * control loop running in hub`s fibril
 *
 * Hub`s fibril periodically asks for changes on hub and if needded calls
 * change handling routine.
 * @warning currently hub driver asks for changes once a second
 * @param hub_info_param hub representation pointer
 * @return zero
 */
/*
int usb_hub_control_loop(void * hub_info_param) {
	usb_hub_info_t * hub_info = (usb_hub_info_t*) hub_info_param;
	int errorCode = EOK;

	while (errorCode == EOK) {
		async_usleep(1000 * 1000 * 10); /// \TODO proper number once
		errorCode = usb_hub_check_hub_changes(hub_info);
	}
	usb_log_error("something in ctrl loop went wrong, errno %d\n",
		errorCode);

	return 0;
}
 */
/// \TODO malloc checking

//*********************************************
//
//  hub driver code, initialization
//
//*********************************************



/**
 * Initialize hub device driver fibril
 *
 * Creates hub representation and fibril that periodically checks hub`s status.
 * Hub representation is passed to the fibril.
 * @param usb_dev generic usb device information
 * @return error code
 */
int usb_hub_add_device(usb_device_t * usb_dev) {
	if (!usb_dev) return EINVAL;
	usb_hub_info_t * hub_info = usb_hub_info_create(usb_dev);
	//create hc connection
	usb_log_debug("Initializing USB wire abstraction.\n");
	int opResult = usb_hc_connection_initialize_from_device(
		&hub_info->connection,
		hub_info->usb_device->ddf_dev);
	if (opResult != EOK) {
		usb_log_error("could not initialize connection to device, "
			"errno %d\n",
			opResult);
		free(hub_info);
		return opResult;
	}

	usb_pipe_start_session(hub_info->control_pipe);
	//set hub configuration
	opResult = usb_hub_set_configuration(hub_info);
	if (opResult != EOK) {
		usb_log_error("could not set hub configuration, errno %d\n",
			opResult);
		free(hub_info);
		return opResult;
	}
	//get port count and create attached_devs
	opResult = usb_hub_process_hub_specific_info(hub_info);
	if (opResult != EOK) {
		usb_log_error("could not set hub configuration, errno %d\n",
			opResult);
		free(hub_info);
		return opResult;
	}
	usb_pipe_end_session(hub_info->control_pipe);


	/// \TODO what is this?
	usb_log_debug("Creating `hub' function.\n");
	ddf_fun_t *hub_fun = ddf_fun_create(hub_info->usb_device->ddf_dev,
		fun_exposed, "hub");
	assert(hub_fun != NULL);
	hub_fun->ops = NULL;

	int rc = ddf_fun_bind(hub_fun);
	assert(rc == EOK);
	rc = ddf_fun_add_to_class(hub_fun, "hub");
	assert(rc == EOK);

	/*
	 * The processing will require opened control pipe and connection
	 * to the host controller.
	 * It is waste of resources but let's hope there will be less
	 * hubs than the phone limit.
	 * FIXME: with some proper locking over pipes and session
	 * auto destruction, this could work better.
	 */
	rc = usb_pipe_start_session(&usb_dev->ctrl_pipe);
	if (rc != EOK) {
		usb_log_error("Failed to start session on control pipe: %s.\n",
		    str_error(rc));
		goto leave;
	}
	rc = usb_hc_connection_open(&hub_info->connection);
	if (rc != EOK) {
		usb_pipe_end_session(&usb_dev->ctrl_pipe);
		usb_log_error("Failed to open connection to HC: %s.\n",
		    str_error(rc));
		goto leave;
	}

	rc = usb_device_auto_poll(hub_info->usb_device, 0,
	    hub_port_changes_callback, ((hub_info->port_count+1) / 8) + 1,
	    NULL, hub_info);
	if (rc != EOK) {
		usb_log_error("Failed to create polling fibril: %s.\n",
		    str_error(rc));
		free(hub_info);
		return rc;
	}

	usb_log_info("Controlling hub `%s' (%d ports).\n",
		hub_info->usb_device->ddf_dev->name, hub_info->port_count);
	return EOK;
leave:
	free(hub_info);

	return rc;
}


//*********************************************
//
//  hub driver code, main loop and port handling
//
//*********************************************


/** Callback for polling hub for port changes.
 *
 * @param dev Device where the change occured.
 * @param change_bitmap Bitmap of changed ports.
 * @param change_bitmap_size Size of the bitmap in bytes.
 * @param arg Custom argument, points to @c usb_hub_info_t.
 * @return Whether to continue polling.
 */
bool hub_port_changes_callback(usb_device_t *dev,
    uint8_t *change_bitmap, size_t change_bitmap_size, void *arg)
{
	usb_hub_info_t *hub = (usb_hub_info_t *) arg;

	/* FIXME: check that we received enough bytes. */
	if (change_bitmap_size == 0) {
		goto leave;
	}

	size_t port;
	for (port = 1; port < hub->port_count + 1; port++) {
		bool change = (change_bitmap[port / 8] >> (port % 8)) % 2;
		if (change) {
			usb_hub_process_interrupt(hub, port);
		}
	}


leave:
	/* FIXME: proper interval. */
	async_usleep(1000 * 1000 * 10 );

	return true;
}


/**
 * check changes on hub
 *
 * Handles changes on each port with a status change.
 * @param hub_info hub representation
 * @return error code
 */
int usb_hub_check_hub_changes(usb_hub_info_t * hub_info) {
	int opResult;
	opResult = usb_pipe_start_session(
		hub_info->status_change_pipe);
	//this might not be necessary - if all non-removables are ok, it is
	//not needed here
	opResult = usb_pipe_start_session(hub_info->control_pipe);
	if (opResult != EOK) {
		usb_log_error("could not initialize communication for hub; %d\n",
			opResult);
		return opResult;
	}

	size_t port_count = hub_info->port_count;
	//first check non-removable devices
	/*
	{
		unsigned int port;
		for (port = 0; port < port_count; ++port) {
			bool is_non_removable =
				hub_info->not_initialized_non_removables[port/8]
				& (1 << (port-1 % 8));
			if (is_non_removable) {
				opResult = initialize_non_removable(hub_info,
					port+1);
			}
		}
	}
	*/

	/// FIXME: count properly
	size_t byte_length = ((port_count + 1) / 8) + 1;
	void *change_bitmap = malloc(byte_length);
	size_t actual_size;

	/*
	 * Send the request.
	 */
	opResult = usb_pipe_read(
		hub_info->status_change_pipe,
		change_bitmap, byte_length, &actual_size
		);

	if (opResult != EOK) {
		free(change_bitmap);
		usb_log_warning("something went wrong while getting the"
			"status of hub\n");
		usb_pipe_end_session(hub_info->status_change_pipe);
		return opResult;
	}
	unsigned int port;

	if (opResult != EOK) {
		usb_log_error("could not start control pipe session %d\n",
			opResult);
		usb_pipe_end_session(hub_info->status_change_pipe);
		return opResult;
	}
	opResult = usb_hc_connection_open(&hub_info->connection);
	if (opResult != EOK) {
		usb_log_error("could not start host controller session %d\n",
			opResult);
		usb_pipe_end_session(hub_info->control_pipe);
		usb_pipe_end_session(hub_info->status_change_pipe);
		return opResult;
	}

	///todo, opresult check, pre obe konekce
	bool interrupt;
	interrupt = ((uint8_t*)change_bitmap)[0] & 1;
	if(interrupt){
		usb_hub_process_global_interrupt(hub_info);
	}
	for (port = 1; port < port_count + 1; ++port) {
		interrupt =
			((uint8_t*) change_bitmap)[port / 8] & (1<<(port % 8));
		if (interrupt) {
			usb_hub_process_interrupt(
				hub_info, port);
		}
	}
	/// \todo check hub status
	usb_hc_connection_close(&hub_info->connection);
	usb_pipe_end_session(hub_info->control_pipe);
	usb_pipe_end_session(hub_info->status_change_pipe);
	free(change_bitmap);
	return EOK;
}

//*********************************************
//
//  support functions
//
//*********************************************

/**
 * create usb_hub_info_t structure
 *
 * Does only basic copying of known information into new structure.
 * @param usb_dev usb device structure
 * @return basic usb_hub_info_t structure
 */
static usb_hub_info_t * usb_hub_info_create(usb_device_t * usb_dev) {
	usb_hub_info_t * result = usb_new(usb_hub_info_t);
	if (!result) return NULL;
	result->usb_device = usb_dev;
	result->status_change_pipe = usb_dev->pipes[0].pipe;
	result->control_pipe = &usb_dev->ctrl_pipe;
	result->is_default_address_used = false;
	return result;
}


/**
 * Load hub-specific information into hub_info structure and process if needed
 *
 * Particularly read port count and initialize structure holding port
 * information. If there are non-removable devices, start initializing them.
 * This function is hub-specific and should be run only after the hub is
 * configured using usb_hub_set_configuration function.
 * @param hub_info hub representation
 * @return error code
 */
static int usb_hub_process_hub_specific_info(usb_hub_info_t * hub_info) {
	// get hub descriptor
	usb_log_debug("creating serialized descriptor\n");
	void * serialized_descriptor = malloc(USB_HUB_MAX_DESCRIPTOR_SIZE);
	usb_hub_descriptor_t * descriptor;
	int opResult;

	/* this was one fix of some bug, should not be needed anymore
	 * these lines allow to reset hub once more, it can be used as
	 * brute-force initialization for non-removable devices
	 *
	opResult = usb_request_set_configuration(hub_info->control_pipe,
		1);
	if (opResult != EOK) {
		usb_log_error("could not set default configuration, errno %d",
			opResult);
		return opResult;
	}*/


	size_t received_size;
	opResult = usb_request_get_descriptor(hub_info->control_pipe,
		USB_REQUEST_TYPE_CLASS, USB_REQUEST_RECIPIENT_DEVICE,
		USB_DESCTYPE_HUB,
		0, 0, serialized_descriptor,
		USB_HUB_MAX_DESCRIPTOR_SIZE, &received_size);

	if (opResult != EOK) {
		usb_log_error("failed when receiving hub descriptor, "
			"badcode = %d\n",
			opResult);
		free(serialized_descriptor);
		return opResult;
	}
	usb_log_debug2("deserializing descriptor\n");
	descriptor = usb_deserialize_hub_desriptor(serialized_descriptor);
	if (descriptor == NULL) {
		usb_log_warning("could not deserialize descriptor \n");
		return opResult;
	}
	usb_log_debug("setting port count to %d\n", descriptor->ports_count);
	hub_info->port_count = descriptor->ports_count;
	/// \TODO check attached_devices array: this is not semantically correct
	//hub_info->attached_devs = (usb_hc_attached_device_t*)
//		malloc((hub_info->port_count + 1) *
//			sizeof (usb_hc_attached_device_t)
//		);
	hub_info->ports = malloc(sizeof(usb_hub_port_t) * (hub_info->port_count+1));
	size_t port;
	for (port = 0; port < hub_info->port_count + 1; port++) {
		usb_hub_port_init(&hub_info->ports[port]);
	}
	//handle non-removable devices
	//usb_hub_trigger_connecting_non_removable_devices(hub_info, descriptor);
	usb_log_debug2("freeing data\n");
	free(serialized_descriptor);
	free(descriptor->devices_removable);
	free(descriptor);
	return EOK;
}

/**
 * Set configuration of hub
 *
 * Check whether there is at least one configuration and sets the first one.
 * This function should be run prior to running any hub-specific action.
 * @param hub_info hub representation
 * @return error code
 */
static int usb_hub_set_configuration(usb_hub_info_t * hub_info) {
	//device descriptor
	usb_standard_device_descriptor_t *std_descriptor
		= &hub_info->usb_device->descriptors.device;
	usb_log_debug("hub has %d configurations\n",
		std_descriptor->configuration_count);
	if (std_descriptor->configuration_count < 1) {
		usb_log_error("there are no configurations available\n");
		return EINVAL;
	}

	usb_standard_configuration_descriptor_t *config_descriptor
		= (usb_standard_configuration_descriptor_t *)
		hub_info->usb_device->descriptors.configuration;

	/* Set configuration. */
	int opResult = usb_request_set_configuration(
		&hub_info->usb_device->ctrl_pipe,
		config_descriptor->configuration_number);

	if (opResult != EOK) {
		usb_log_error("Failed to set hub configuration: %s.\n",
			str_error(opResult));
		return opResult;
	}
	usb_log_debug("\tused configuration %d\n",
		config_descriptor->configuration_number);

	return EOK;
}

/**
 * release default address used by given hub
 *
 * Also unsets hub->is_default_address_used. Convenience wrapper function.
 * @note hub->connection MUST be open for communication
 * @param hub hub representation
 * @return error code
 */
static int usb_hub_release_default_address(usb_hub_info_t * hub) {
	int opResult = usb_hc_release_default_address(&hub->connection);
	if (opResult != EOK) {
		usb_log_error("could not release default address, errno %d\n",
			opResult);
		return opResult;
	}
	hub->is_default_address_used = false;
	return EOK;
}

#if 0
/**
 * Reset the port with new device and reserve the default address.
 * @param hub hub representation
 * @param port port number, starting from 1
 * @param speed transfer speed of attached device, one of low, full or high
 * @return error code
 */
static int usb_hub_init_add_device(usb_hub_info_t * hub, uint16_t port,
	usb_speed_t speed) {
	//if this hub already uses default address, it cannot request it once more
	if (hub->is_default_address_used) {
		usb_log_info("default address used, another time\n");
		return EREFUSED;
	}
	usb_log_debug("some connection changed\n");
	assert(hub->control_pipe->hc_phone);
	int opResult = usb_hub_clear_port_feature(hub->control_pipe,
		port, USB_HUB_FEATURE_C_PORT_CONNECTION);
	if (opResult != EOK) {
		usb_log_warning("could not clear port-change-connection flag\n");
	}
	usb_device_request_setup_packet_t request;

	//get default address
	opResult = usb_hc_reserve_default_address(&hub->connection, speed);

	if (opResult != EOK) {
		usb_log_warning("cannot assign default address, it is probably "
			"used %d\n",
			opResult);
		return opResult;
	}
	hub->is_default_address_used = true;
	//reset port
	usb_hub_set_reset_port_request(&request, port);
	opResult = usb_pipe_control_write(
		hub->control_pipe,
		&request, sizeof (usb_device_request_setup_packet_t),
		NULL, 0
		);
	if (opResult != EOK) {
		usb_log_error("something went wrong when reseting a port %d\n",
			opResult);
		usb_hub_release_default_address(hub);
	}
	return opResult;
}
#endif

#if 0
/**
 * Finalize adding new device after port reset
 *
 * Set device`s address and start it`s driver.
 * @param hub hub representation
 * @param port port number, starting from 1
 * @param speed transfer speed of attached device, one of low, full or high
 */
static void usb_hub_finalize_add_device(usb_hub_info_t * hub,
	uint16_t port, usb_speed_t speed) {

	int opResult;
	usb_log_debug("finalizing add device\n");
	opResult = usb_hub_clear_port_feature(hub->control_pipe,
		port, USB_HUB_FEATURE_C_PORT_RESET);

	if (opResult != EOK) {
		usb_log_error("failed to clear port reset feature\n");
		usb_hub_release_default_address(hub);
		return;
	}
	//create connection to device
	usb_pipe_t new_device_pipe;
	usb_device_connection_t new_device_connection;
	usb_device_connection_initialize_on_default_address(
		&new_device_connection,
		&hub->connection
		);
	usb_pipe_initialize_default_control(
		&new_device_pipe,
		&new_device_connection);
	usb_pipe_probe_default_control(&new_device_pipe);

	/* Request address from host controller. */
	usb_address_t new_device_address = usb_hc_request_address(
		&hub->connection,
		speed
		);
	if (new_device_address < 0) {
		usb_log_error("failed to get free USB address\n");
		opResult = new_device_address;
		usb_hub_release_default_address(hub);
		return;
	}
	usb_log_debug("setting new address %d\n", new_device_address);
	//opResult = usb_drv_req_set_address(hc, USB_ADDRESS_DEFAULT,
	//    new_device_address);
	usb_pipe_start_session(&new_device_pipe);
	opResult = usb_request_set_address(&new_device_pipe,
		new_device_address);
	usb_pipe_end_session(&new_device_pipe);
	if (opResult != EOK) {
		usb_log_error("could not set address for new device %d\n",
			opResult);
		usb_hub_release_default_address(hub);
		return;
	}

	//opResult = usb_hub_release_default_address(hc);
	opResult = usb_hub_release_default_address(hub);
	if (opResult != EOK) {
		return;
	}

	devman_handle_t child_handle;
	//??
	opResult = usb_device_register_child_in_devman(new_device_address,
		hub->connection.hc_handle, hub->usb_device->ddf_dev,
		&child_handle,
		NULL, NULL, NULL);

	if (opResult != EOK) {
		usb_log_error("could not start driver for new device %d\n",
			opResult);
		return;
	}
	hub->attached_devs[port].handle = child_handle;
	hub->attached_devs[port].address = new_device_address;

	//opResult = usb_drv_bind_address(hc, new_device_address, child_handle);
	opResult = usb_hc_register_device(
		&hub->connection,
		&hub->attached_devs[port]);
	if (opResult != EOK) {
		usb_log_error("could not assign address of device in hcd %d\n",
			opResult);
		return;
	}
	usb_log_info("Detected new device on `%s' (port %d), " \
	    "address %d (handle %llu).\n",
		hub->usb_device->ddf_dev->name, (int) port,
		new_device_address, child_handle);
}
#endif

/**
 * routine called when a device on port has been removed
 *
 * If the device on port had default address, it releases default address.
 * Otherwise does not do anything, because DDF does not allow to remove device
 * from it`s device tree.
 * @param hub hub representation
 * @param port port number, starting from 1
 */
static void usb_hub_removed_device(
	usb_hub_info_t * hub, uint16_t port) {

	int opResult = usb_hub_clear_port_feature(hub->control_pipe,
		port, USB_HUB_FEATURE_C_PORT_CONNECTION);
	if (opResult != EOK) {
		usb_log_warning("could not clear port-change-connection flag\n");
	}
	/** \TODO remove device from device manager - not yet implemented in
	 * devide manager
	 */

	//close address
	//if (hub->attached_devs[port].address != 0) {
	if(hub->ports[port].attached_device.address >= 0){
		/*uncomment this code to use it when DDF allows device removal
		opResult = usb_hc_unregister_device(
			&hub->connection,
			hub->attached_devs[port].address);
		if(opResult != EOK) {
			dprintf(USB_LOG_LEVEL_WARNING, "could not release "
				"address of "
			    "removed device: %d", opResult);
		}
		hub->attached_devs[port].address = 0;
		hub->attached_devs[port].handle = 0;
		 */
	} else {
		usb_log_warning("this is strange, disconnected device had "
			"no address\n");
		//device was disconnected before it`s port was reset -
		//return default address
		usb_hub_release_default_address(hub);
	}
}

/**
 * Process over current condition on port.
 *
 * Turn off the power on the port.
 *
 * @param hub hub representation
 * @param port port number, starting from 1
 */
static void usb_hub_port_over_current(usb_hub_info_t * hub,
	uint16_t port, uint32_t status) {
	int opResult;
	if(usb_port_over_current(&status)){
		opResult = usb_hub_clear_port_feature(hub->control_pipe,
			port, USB_HUB_FEATURE_PORT_POWER);
		if (opResult != EOK) {
			usb_log_error("cannot power off port %d;  %d\n",
				port, opResult);
		}
	}else{
		opResult = usb_hub_set_port_feature(hub->control_pipe,
			port, USB_HUB_FEATURE_PORT_POWER);
		if (opResult != EOK) {
			usb_log_error("cannot power on port %d;  %d\n",
				port, opResult);
		}
	}
}

/** Retrieve port status.
 *
 * @param[in] ctrl_pipe Control pipe to use.
 * @param[in] port Port number (starting at 1).
 * @param[out] status Where to store the port status.
 * @return Error code.
 */
static int get_port_status(usb_pipe_t *ctrl_pipe, size_t port,
    usb_port_status_t *status)
{
	size_t recv_size;
	usb_device_request_setup_packet_t request;
	usb_port_status_t status_tmp;

	usb_hub_set_port_status_request(&request, port);
	int rc = usb_pipe_control_read(ctrl_pipe,
	    &request, sizeof(usb_device_request_setup_packet_t),
	    &status_tmp, sizeof(status_tmp), &recv_size);
	if (rc != EOK) {
		return rc;
	}

	if (recv_size != sizeof (status_tmp)) {
		return ELIMIT;
	}

	if (status != NULL) {
		*status = status_tmp;
	}

	return EOK;
}

/** Callback for enabling a specific port.
 *
 * We wait on a CV until port is reseted.
 * That is announced via change on interrupt pipe.
 *
 * @param port_no Port number (starting at 1).
 * @param arg Custom argument, points to @c usb_hub_info_t.
 * @return Error code.
 */
static int enable_port_callback(int port_no, void *arg)
{
	usb_hub_info_t *hub = (usb_hub_info_t *) arg;
	int rc;
	usb_device_request_setup_packet_t request;
	usb_hub_port_t *my_port = hub->ports + port_no;

	usb_hub_set_reset_port_request(&request, port_no);
	rc = usb_pipe_control_write(hub->control_pipe,
	    &request, sizeof(request), NULL, 0);
	if (rc != EOK) {
		usb_log_warning("Port reset failed: %s.\n", str_error(rc));
		return rc;
	}

	/*
	 * Wait until reset completes.
	 */
	fibril_mutex_lock(&my_port->reset_mutex);
	while (!my_port->reset_completed) {
		fibril_condvar_wait(&my_port->reset_cv, &my_port->reset_mutex);
	}
	fibril_mutex_unlock(&my_port->reset_mutex);

	/* Clear the port reset change. */
	rc = usb_hub_clear_port_feature(hub->control_pipe,
	    port_no, USB_HUB_FEATURE_C_PORT_RESET);
	if (rc != EOK) {
		usb_log_error("Failed to clear port %d reset feature: %s.\n",
		    port_no, str_error(rc));
		return rc;
	}

	return EOK;
}

/** Fibril for adding a new device.
 *
 * Separate fibril is needed because the port reset completion is announced
 * via interrupt pipe and thus we cannot block here.
 *
 * @param arg Pointer to struct add_device_phase1.
 * @return 0 Always.
 */
static int add_device_phase1_worker_fibril(void *arg)
{
	struct add_device_phase1 *data
	    = (struct add_device_phase1 *) arg;

	usb_address_t new_address;
	devman_handle_t child_handle;

	int rc = usb_hc_new_device_wrapper(data->hub->usb_device->ddf_dev,
	    &data->hub->connection, data->speed,
	    enable_port_callback, (int) data->port, data->hub,
	    &new_address, &child_handle,
	    NULL, NULL, NULL);

	if (rc != EOK) {
		usb_log_error("Failed registering device on port %zu: %s.\n",
		    data->port, str_error(rc));
		goto leave;
	}

	data->hub->ports[data->port].attached_device.handle = child_handle;
	data->hub->ports[data->port].attached_device.address = new_address;

	usb_log_info("Detected new device on `%s' (port %zu), " 
	    "address %d (handle %" PRIun ").\n",
	    data->hub->usb_device->ddf_dev->name, data->port,
	    new_address, child_handle);

leave:
	free(arg);

	return EOK;
}


/** Start device adding when connection change is detected.
 *
 * This fires a new fibril to complete the device addition.
 *
 * @param hub Hub where the change occured.
 * @param port Port index (starting at 1).
 * @param speed Speed of the device.
 * @return Error code.
 */
static int add_device_phase1_new_fibril(usb_hub_info_t *hub, size_t port,
    usb_speed_t speed)
{
	struct add_device_phase1 *data
	    = malloc(sizeof(struct add_device_phase1));
	if (data == NULL) {
		return ENOMEM;
	}
	data->hub = hub;
	data->port = port;
	data->speed = speed;

	usb_hub_port_t *the_port = hub->ports + port;

	fibril_mutex_lock(&the_port->reset_mutex);
	the_port->reset_completed = false;
	fibril_mutex_unlock(&the_port->reset_mutex);

	int rc = usb_hub_clear_port_feature(hub->control_pipe, port,
	    USB_HUB_FEATURE_C_PORT_CONNECTION);
	if (rc != EOK) {
		free(data);
		usb_log_warning("Failed to clear port change flag: %s.\n",
		    str_error(rc));
		return rc;
	}

	fid_t fibril = fibril_create(add_device_phase1_worker_fibril, data);
	if (fibril == 0) {
		free(data);
		return ENOMEM;
	}
	fibril_add_ready(fibril);

	return EOK;
}


/**
 * Process interrupts on given hub port
 *
 * Accepts connection, over current and port reset change.
 * @param hub hub representation
 * @param port port number, starting from 1
 */
static void usb_hub_process_interrupt(usb_hub_info_t * hub,
	uint16_t port) {
	usb_log_debug("interrupt at port %d\n", port);
	//determine type of change
	//usb_pipe_t *pipe = hub->control_pipe;

	int opResult;

	usb_port_status_t status;
	opResult = get_port_status(&hub->usb_device->ctrl_pipe, port, &status);
	if (opResult != EOK) {
		usb_log_error("Failed to get port %zu status: %s.\n",
		    port, str_error(opResult));
		return;
	}

	//something connected/disconnected
	/*
	if (usb_port_connect_change(&status)) {
		usb_log_debug("connection change on port\n");
		if (usb_port_dev_connected(&status)) {
			usb_hub_init_add_device(hub, port,
				usb_port_speed(&status));
		} else {
			usb_hub_removed_device(hub, port);
		}
	}*/
	if (usb_port_connect_change(&status)) {
		bool device_connected = usb_port_dev_connected(&status);
		usb_log_debug("Connection change on port %zu: %s.\n", port,
		    device_connected ? "device attached" : "device removed");

		if (device_connected) {
			opResult = add_device_phase1_new_fibril(hub, port,
			    usb_port_speed(&status));
			if (opResult != EOK) {
				usb_log_error(
				    "Cannot handle change on port %zu: %s.\n",
				    str_error(opResult));
			}
		} else {
			usb_hub_removed_device(hub, port);
		}
	}
	//over current
	if (usb_port_overcurrent_change(&status)) {
		//check if it was not auto-resolved
		usb_log_debug("overcurrent change on port\n");
		usb_hub_port_over_current(hub, port, status);
	}
	//port reset
	if (usb_port_reset_completed(&status)) {
		/*
		usb_log_debug("port reset complete\n");
		if (usb_port_enabled(&status)) {
			usb_hub_finalize_add_device(hub, port,
				usb_port_speed(&status));
		} else {
			usb_log_warning("port reset, but port still not "
				"enabled\n");
		}
		 * */
		usb_log_debug("Port %zu reset complete.\n", port);
		if (usb_port_enabled(&status)) {
			/* Finalize device adding. */
			usb_hub_port_t *the_port = hub->ports + port;
			fibril_mutex_lock(&the_port->reset_mutex);
			the_port->reset_completed = true;
			fibril_condvar_broadcast(&the_port->reset_cv);
			fibril_mutex_unlock(&the_port->reset_mutex);
		} else {
			usb_log_warning(
			    "Port %zu reset complete but port not enabled.\n",
			    port);
		}
	}
	usb_log_debug("status x%x : %d\n ", status, status);

	usb_port_set_connect_change(&status, false);
	usb_port_set_reset(&status, false);
	usb_port_set_reset_completed(&status, false);
	usb_port_set_dev_connected(&status, false);
	usb_port_set_overcurrent_change(&status,false);
	/// \TODO what about port power change?
	if (status >> 16) {
		usb_log_info("there was unsupported change on port %d: %X\n",
			port, status);

	}
}

/**
 * process hub over current change
 *
 * This means either to power off the hub or power it on.
 * @param hub_info hub instance
 * @param status hub status bitmask
 * @return error code
 */
static int usb_process_hub_over_current(usb_hub_info_t * hub_info,
	usb_hub_status_t status)
{
	int opResult;
	if(usb_hub_over_current(&status)){
		opResult = usb_hub_clear_feature(hub_info->control_pipe,
			USB_HUB_FEATURE_PORT_POWER);
		if (opResult != EOK) {
			usb_log_error("cannot power off hub: %d\n",
				opResult);
		}
	}else{
		opResult = usb_hub_set_feature(hub_info->control_pipe,
			USB_HUB_FEATURE_PORT_POWER);
		if (opResult != EOK) {
			usb_log_error("cannot power on hub: %d\n",
				opResult);
		}
	}
	return opResult;
}

/**
 * process hub power change
 *
 * If the power has been lost, reestablish it.
 * If it was reestablished, re-power all ports.
 * @param hub_info hub instance
 * @param status hub status bitmask
 * @return error code
 */
static int usb_process_hub_power_change(usb_hub_info_t * hub_info,
	usb_hub_status_t status)
{
	int opResult;
	if(usb_hub_local_power_lost(&status)){
		//restart power on hub
		opResult = usb_hub_set_feature(hub_info->control_pipe,
			USB_HUB_FEATURE_PORT_POWER);
		if (opResult != EOK) {
			usb_log_error("cannot power on hub: %d\n",
				opResult);
		}
	}else{//power reestablished on hub- restart ports
		size_t port;
		for(port=0;port<hub_info->port_count;++port){
			opResult = usb_hub_set_port_feature(
				hub_info->control_pipe,
				port, USB_HUB_FEATURE_PORT_POWER);
			if (opResult != EOK) {
				usb_log_error("cannot power on port %d;  %d\n",
					port, opResult);
			}
		}
	}
	return opResult;
}

/**
 * process hub interrupts
 *
 * The change can be either in the over-current condition or
 * local-power lost condition.
 * @param hub_info hub instance
 */
static void usb_hub_process_global_interrupt(usb_hub_info_t * hub_info){
	usb_log_debug("global interrupt on a hub\n");
	usb_pipe_t *pipe = hub_info->control_pipe;
	int opResult;

	usb_port_status_t status;
	size_t rcvd_size;
	usb_device_request_setup_packet_t request;
	//int opResult;
	usb_hub_set_hub_status_request(&request);
	//endpoint 0

	opResult = usb_pipe_control_read(
		pipe,
		&request, sizeof (usb_device_request_setup_packet_t),
		&status, 4, &rcvd_size
		);
	if (opResult != EOK) {
		usb_log_error("could not get hub status\n");
		return;
	}
	if (rcvd_size != sizeof (usb_port_status_t)) {
		usb_log_error("received status has incorrect size\n");
		return;
	}
	//port reset
	if (usb_hub_over_current_change(&status)) {
		usb_process_hub_over_current(hub_info,status);
	}
	if (usb_hub_local_power_change(&status)) {
		usb_process_hub_power_change(hub_info,status);
	}
}



/**
 * @}
 */
