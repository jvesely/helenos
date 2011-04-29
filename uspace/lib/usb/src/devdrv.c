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

/** @addtogroup libusb
 * @{
 */
/** @file
 * USB device driver framework.
 */
#include <usb/devdrv.h>
#include <usb/request.h>
#include <usb/debug.h>
#include <usb/dp.h>
#include <errno.h>
#include <str_error.h>
#include <assert.h>

static int generic_add_device(ddf_dev_t *);

static driver_ops_t generic_driver_ops = {
	.add_device = generic_add_device
};
static driver_t generic_driver = {
	.driver_ops = &generic_driver_ops
};

static usb_driver_t *driver = NULL;


/** Main routine of USB device driver.
 *
 * Under normal conditions, this function never returns.
 *
 * @param drv USB device driver structure.
 * @return Task exit status.
 */
int usb_driver_main(usb_driver_t *drv)
{
	assert(drv != NULL);

	/* Prepare the generic driver. */
	generic_driver.name = drv->name;

	driver = drv;

	return ddf_driver_main(&generic_driver);
}

/** Count number of pipes the driver expects.
 *
 * @param drv USB driver.
 * @return Number of pipes (excluding default control pipe).
 */
static size_t count_other_pipes(usb_endpoint_description_t **endpoints)
{
	size_t count = 0;
	if (endpoints == NULL) {
		return 0;
	}

	while (endpoints[count] != NULL) {
		count++;
	}

	return count;
}

/** Initialize endpoint pipes, excluding default control one.
 *
 * @param drv The device driver.
 * @param dev Device to be initialized.
 * @return Error code.
 */
static int initialize_other_pipes(usb_endpoint_description_t **endpoints,
    usb_device_t *dev, int alternate_setting)
{
	if (endpoints == NULL) {
		dev->pipes = NULL;
		dev->pipes_count = 0;
		return EOK;
	}

	usb_endpoint_mapping_t *pipes;
	size_t pipes_count;

	int rc = usb_device_create_pipes(dev->ddf_dev, &dev->wire, endpoints,
	    dev->descriptors.configuration, dev->descriptors.configuration_size,
	    dev->interface_no, alternate_setting,
	    &pipes, &pipes_count);

	if (rc != EOK) {
		return rc;
	}

	dev->pipes = pipes;
	dev->pipes_count = pipes_count;

	return EOK;
}

/** Callback when new device is supposed to be controlled by this driver.
 *
 * This callback is a wrapper for USB specific version of @c add_device.
 *
 * @param gen_dev Device structure as prepared by DDF.
 * @return Error code.
 */
int generic_add_device(ddf_dev_t *gen_dev)
{
	assert(driver);
	assert(driver->ops);
	assert(driver->ops->add_device);

	int rc;

	usb_device_t *dev = NULL;
	const char *err_msg = NULL;
	rc = usb_device_create(gen_dev, driver->endpoints, &dev, &err_msg);
	if (rc != EOK) {
		usb_log_error("USB device `%s' creation failed (%s): %s.\n",
		    gen_dev->name, err_msg, str_error(rc));
		return rc;
	}

	return driver->ops->add_device(dev);
}

/** Destroy existing pipes of a USB device.
 *
 * @param dev Device where to destroy the pipes.
 * @return Error code.
 */
static int destroy_current_pipes(usb_device_t *dev)
{
	int rc = usb_device_destroy_pipes(dev->ddf_dev,
	    dev->pipes, dev->pipes_count);
	if (rc != EOK) {
		return rc;
	}

	dev->pipes = NULL;
	dev->pipes_count = 0;

	return EOK;
}

/** Change interface setting of a device.
 * This function selects new alternate setting of an interface by issuing
 * proper USB command to the device and also creates new USB pipes
 * under @c dev->pipes.
 *
 * @warning This function is intended for drivers working at interface level.
 * For drivers controlling the whole device, you need to change interface
 * manually using usb_request_set_interface() and creating new pipes
 * with usb_pipe_initialize_from_configuration().
 *
 * @warning This is a wrapper function that does several operations that
 * can fail and that cannot be rollbacked easily. That means that a failure
 * during the SET_INTERFACE request would result in having a device with
 * no pipes at all (except the default control one). That is because the old
 * pipes needs to be unregistered at HC first and the new ones could not
 * be created.
 *
 * @param dev USB device.
 * @param alternate_setting Alternate setting to choose.
 * @param endpoints New endpoint descriptions.
 * @return Error code.
 */
int usb_device_select_interface(usb_device_t *dev, uint8_t alternate_setting,
    usb_endpoint_description_t **endpoints)
{
	if (dev->interface_no < 0) {
		return EINVAL;
	}

	int rc;

	/* Destroy existing pipes. */
	rc = destroy_current_pipes(dev);
	if (rc != EOK) {
		return rc;
	}

	/* Change the interface itself. */
	rc = usb_request_set_interface(&dev->ctrl_pipe, dev->interface_no,
	    alternate_setting);
	if (rc != EOK) {
		return rc;
	}

	/* Create new pipes. */
	rc = initialize_other_pipes(endpoints, dev, (int) alternate_setting);

	return rc;
}

/** Retrieve basic descriptors from the device.
 *
 * @param[in] ctrl_pipe Control endpoint pipe.
 * @param[out] descriptors Where to store the descriptors.
 * @return Error code.
 */
int usb_device_retrieve_descriptors(usb_pipe_t *ctrl_pipe,
    usb_device_descriptors_t *descriptors)
{
	assert(descriptors != NULL);

	descriptors->configuration = NULL;

	int rc;

	/* It is worth to start a long transfer. */
	usb_pipe_start_long_transfer(ctrl_pipe);

	/* Get the device descriptor. */
	rc = usb_request_get_device_descriptor(ctrl_pipe, &descriptors->device);
	if (rc != EOK) {
		goto leave;
	}

	/* Get the full configuration descriptor. */
	rc = usb_request_get_full_configuration_descriptor_alloc(
	    ctrl_pipe, 0, (void **) &descriptors->configuration,
	    &descriptors->configuration_size);

leave:
	usb_pipe_end_long_transfer(ctrl_pipe);

	return rc;
}

/** Create pipes for a device.
 *
 * This is more or less a wrapper that does following actions:
 * - allocate and initialize pipes
 * - map endpoints to the pipes based on the descriptions
 * - registers endpoints with the host controller
 *
 * @param[in] dev Generic DDF device backing the USB one.
 * @param[in] wire Initialized backing connection to the host controller.
 * @param[in] endpoints Endpoints description, NULL terminated.
 * @param[in] config_descr Configuration descriptor of active configuration.
 * @param[in] config_descr_size Size of @p config_descr in bytes.
 * @param[in] interface_no Interface to map from.
 * @param[in] interface_setting Interface setting (default is usually 0).
 * @param[out] pipes_ptr Where to store array of created pipes
 *	(not NULL terminated).
 * @param[out] pipes_count_ptr Where to store number of pipes
 *	(set to if you wish to ignore the count).
 * @return Error code.
 */
int usb_device_create_pipes(ddf_dev_t *dev, usb_device_connection_t *wire,
    usb_endpoint_description_t **endpoints,
    uint8_t *config_descr, size_t config_descr_size,
    int interface_no, int interface_setting,
    usb_endpoint_mapping_t **pipes_ptr, size_t *pipes_count_ptr)
{
	assert(dev != NULL);
	assert(wire != NULL);
	assert(endpoints != NULL);
	assert(config_descr != NULL);
	assert(config_descr_size > 0);
	assert(pipes_ptr != NULL);

	size_t i;
	int rc;

	size_t pipe_count = count_other_pipes(endpoints);
	if (pipe_count == 0) {
		*pipes_ptr = NULL;
		return EOK;
	}

	usb_endpoint_mapping_t *pipes
	    = malloc(sizeof(usb_endpoint_mapping_t) * pipe_count);
	if (pipes == NULL) {
		return ENOMEM;
	}

	/* Initialize to NULL to allow smooth rollback. */
	for (i = 0; i < pipe_count; i++) {
		pipes[i].pipe = NULL;
	}

	/* Now allocate and fully initialize. */
	for (i = 0; i < pipe_count; i++) {
		pipes[i].pipe = malloc(sizeof(usb_pipe_t));
		if (pipes[i].pipe == NULL) {
			rc = ENOMEM;
			goto rollback_free_only;
		}
		pipes[i].description = endpoints[i];
		pipes[i].interface_no = interface_no;
		pipes[i].interface_setting = interface_setting;
	}

	/* Find the mapping from configuration descriptor. */
	rc = usb_pipe_initialize_from_configuration(pipes, pipe_count,
	    config_descr, config_descr_size, wire);
	if (rc != EOK) {
		goto rollback_free_only;
	}

	/* Register the endpoints with HC. */
	usb_hc_connection_t hc_conn;
	rc = usb_hc_connection_initialize_from_device(&hc_conn, dev);
	if (rc != EOK) {
		goto rollback_free_only;
	}

	rc = usb_hc_connection_open(&hc_conn);
	if (rc != EOK) {
		goto rollback_free_only;
	}

	for (i = 0; i < pipe_count; i++) {
		if (pipes[i].present) {
			rc = usb_pipe_register(pipes[i].pipe,
			    pipes[i].descriptor->poll_interval, &hc_conn);
			if (rc != EOK) {
				goto rollback_unregister_endpoints;
			}
		}
	}

	usb_hc_connection_close(&hc_conn);

	*pipes_ptr = pipes;
	if (pipes_count_ptr != NULL) {
		*pipes_count_ptr = pipe_count;
	}

	return EOK;

	/*
	 * Jump here if something went wrong after endpoints have
	 * been registered.
	 * This is also the target when the registration of
	 * endpoints fails.
	 */
rollback_unregister_endpoints:
	for (i = 0; i < pipe_count; i++) {
		if (pipes[i].present) {
			usb_pipe_unregister(pipes[i].pipe, &hc_conn);
		}
	}

	usb_hc_connection_close(&hc_conn);

	/*
	 * Jump here if something went wrong before some actual communication
	 * with HC. Then the only thing that needs to be done is to free
	 * allocated memory.
	 */
rollback_free_only:
	for (i = 0; i < pipe_count; i++) {
		if (pipes[i].pipe != NULL) {
			free(pipes[i].pipe);
		}
	}
	free(pipes);

	return rc;
}

/** Destroy pipes previously created by usb_device_create_pipes.
 *
 * @param[in] dev Generic DDF device backing the USB one.
 * @param[in] pipes Endpoint mapping to be destroyed.
 * @param[in] pipes_count Number of endpoints.
 */
int usb_device_destroy_pipes(ddf_dev_t *dev,
    usb_endpoint_mapping_t *pipes, size_t pipes_count)
{
	assert(dev != NULL);
	assert(((pipes != NULL) && (pipes_count > 0))
	    || ((pipes == NULL) && (pipes_count == 0)));

	if (pipes_count == 0) {
		return EOK;
	}

	int rc;

	/* Prepare connection to HC to allow endpoint unregistering. */
	usb_hc_connection_t hc_conn;
	rc = usb_hc_connection_initialize_from_device(&hc_conn, dev);
	if (rc != EOK) {
		return rc;
	}
	rc = usb_hc_connection_open(&hc_conn);
	if (rc != EOK) {
		return rc;
	}

	/* Destroy the pipes. */
	size_t i;
	for (i = 0; i < pipes_count; i++) {
		usb_pipe_unregister(pipes[i].pipe, &hc_conn);
		free(pipes[i].pipe);
	}

	usb_hc_connection_close(&hc_conn);

	free(pipes);

	return EOK;
}

/** Initialize control pipe in a device.
 *
 * @param dev USB device in question.
 * @param errmsg Where to store error context.
 * @return
 */
static int init_wire_and_ctrl_pipe(usb_device_t *dev, const char **errmsg)
{
	int rc;

	rc = usb_device_connection_initialize_from_device(&dev->wire,
	    dev->ddf_dev);
	if (rc != EOK) {
		*errmsg = "device connection initialization";
		return rc;
	}

	rc = usb_pipe_initialize_default_control(&dev->ctrl_pipe,
	    &dev->wire);
	if (rc != EOK) {
		*errmsg = "default control pipe initialization";
		return rc;
	}

	return EOK;
}


/** Create new instance of USB device.
 *
 * @param[in] ddf_dev Generic DDF device backing the USB one.
 * @param[in] endpoints NULL terminated array of endpoints (NULL for none).
 * @param[out] dev_ptr Where to store pointer to the new device.
 * @param[out] errstr_ptr Where to store description of context
 *	(in case error occurs).
 * @return Error code.
 */
int usb_device_create(ddf_dev_t *ddf_dev,
    usb_endpoint_description_t **endpoints,
    usb_device_t **dev_ptr, const char **errstr_ptr)
{
	assert(dev_ptr != NULL);
	assert(ddf_dev != NULL);

	int rc;

	usb_device_t *dev = malloc(sizeof(usb_device_t));
	if (dev == NULL) {
		*errstr_ptr = "structure allocation";
		return ENOMEM;
	}

	// FIXME: proper deallocation in case of errors

	dev->ddf_dev = ddf_dev;
	dev->driver_data = NULL;
	dev->descriptors.configuration = NULL;
	dev->alternate_interfaces = NULL;

	dev->pipes_count = 0;
	dev->pipes = NULL;

	/* Initialize backing wire and control pipe. */
	rc = init_wire_and_ctrl_pipe(dev, errstr_ptr);
	if (rc != EOK) {
		return rc;
	}

	/* Get our interface. */
	dev->interface_no = usb_device_get_assigned_interface(dev->ddf_dev);

	/* Retrieve standard descriptors. */
	rc = usb_device_retrieve_descriptors(&dev->ctrl_pipe,
	    &dev->descriptors);
	if (rc != EOK) {
		*errstr_ptr = "descriptor retrieval";
		return rc;
	}

	/* Create alternate interfaces. */
	rc = usb_alternate_interfaces_create(dev->descriptors.configuration,
	    dev->descriptors.configuration_size, dev->interface_no,
	    &dev->alternate_interfaces);
	if (rc != EOK) {
		/* We will try to silently ignore this. */
		dev->alternate_interfaces = NULL;
	}

	rc = initialize_other_pipes(endpoints, dev, 0);
	if (rc != EOK) {
		*errstr_ptr = "pipes initialization";
		return rc;
	}

	*errstr_ptr = NULL;
	*dev_ptr = dev;

	return EOK;
}

/**
 * @}
 */
