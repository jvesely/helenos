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
/** @addtogroup drvusbohci
 * @{
 */
/** @file
 * @brief OHCI driver
 */
#include <assert.h>
#include <errno.h>
#include <str_error.h>

#include <usb/debug.h>

#include "root_hub.h"
#include <usb/classes/classes.h>
#include <usb/classes/hub.h>
#include <usb/dev/driver.h>
#include "ohci_regs.h"

#include <usb/dev/request.h>
#include <usb/classes/hub.h>

/**
 * standart device descriptor for ohci root hub
 */
static const usb_standard_device_descriptor_t ohci_rh_device_descriptor = {
	.configuration_count = 1,
	.descriptor_type = USB_DESCTYPE_DEVICE,
	.device_class = USB_CLASS_HUB,
	.device_protocol = 0,
	.device_subclass = 0,
	.device_version = 0,
	.length = sizeof (usb_standard_device_descriptor_t),
	.max_packet_size = 64,
	.vendor_id = 0x16db,
	.product_id = 0x0001,
	.str_serial_number = 0,
	.usb_spec_version = 0x110,
};

/**
 * standart configuration descriptor with filled common values
 * for ohci root hubs
 */
static const usb_standard_configuration_descriptor_t ohci_rh_conf_descriptor = {
	.attributes = 1 << 7,
	.configuration_number = 1,
	.descriptor_type = USB_DESCTYPE_CONFIGURATION,
	.interface_count = 1,
	.length = sizeof (usb_standard_configuration_descriptor_t),
	.max_power = 0, /* root hubs don't need no power */
	.str_configuration = 0,
};

/**
 * standart ohci root hub interface descriptor
 */
static const usb_standard_interface_descriptor_t ohci_rh_iface_descriptor = {
	.alternate_setting = 0,
	.descriptor_type = USB_DESCTYPE_INTERFACE,
	.endpoint_count = 1,
	.interface_class = USB_CLASS_HUB,
	.interface_number = 1,
	.interface_protocol = 0,
	.interface_subclass = 0,
	.length = sizeof (usb_standard_interface_descriptor_t),
	.str_interface = 0,
};

/**
 * standart ohci root hub endpoint descriptor
 */
static const usb_standard_endpoint_descriptor_t ohci_rh_ep_descriptor = {
	.attributes = USB_TRANSFER_INTERRUPT,
	.descriptor_type = USB_DESCTYPE_ENDPOINT,
	.endpoint_address = 1 + (1 << 7),
	.length = sizeof(usb_standard_endpoint_descriptor_t),
	.max_packet_size = 2,
	.poll_interval = 255,
};

/**
 * bitmask of port features that are valid to be set
 */
static const uint32_t port_set_feature_valid_mask =
    RHPS_SET_PORT_ENABLE |
    RHPS_SET_PORT_SUSPEND |
    RHPS_SET_PORT_RESET |
    RHPS_SET_PORT_POWER;

/**
 * bitmask of port features that can be cleared
 */
static const uint32_t port_clear_feature_valid_mask =
    RHPS_CCS_FLAG |
    RHPS_SET_PORT_SUSPEND |
    RHPS_POCI_FLAG |
    RHPS_SET_PORT_POWER |
    RHPS_CSC_FLAG |
    RHPS_PESC_FLAG |
    RHPS_PSSC_FLAG |
    RHPS_OCIC_FLAG |
    RHPS_PRSC_FLAG;

//note that USB_HUB_FEATURE_PORT_POWER bit is translated into
//USB_HUB_FEATURE_PORT_LOW_SPEED for port set feature request

static void create_serialized_hub_descriptor(rh_t *instance);
static void rh_init_descriptors(rh_t *instance);
static uint16_t create_interrupt_mask(rh_t *instance);
static int get_status_request(rh_t *instance, usb_transfer_batch_t *request);
static int get_descriptor_request(
    rh_t *instance, usb_transfer_batch_t *request);
static int port_feature_set_request(
    rh_t *instance, uint16_t feature, uint16_t port);
static int port_feature_clear_request(
    rh_t *instance, uint16_t feature, uint16_t port);
static int request_with_output(rh_t *instance, usb_transfer_batch_t *request);
static int request_without_data(rh_t *instance, usb_transfer_batch_t *request);
static int ctrl_request(rh_t *instance, usb_transfer_batch_t *request);

#define TRANSFER_OK(bytes) \
do { \
	request->transfered_size = bytes; \
	return EOK; \
} while (0)

#define OHCI_POWER 2

/** Root hub initialization
 * @return Error code.
 */
void rh_init(rh_t *instance, ohci_regs_t *regs)
{
	assert(instance);
	assert(regs);

	instance->registers = regs;
	instance->port_count =
	    (instance->registers->rh_desc_a >> RHDA_NDS_SHIFT) & RHDA_NDS_MASK;
	if (instance->port_count > 15) {
		usb_log_error("OHCI specification does not allow more than 15"
		    " ports. Max 15 ports will be used");
		instance->port_count = 15;
	}

	/* Don't forget the hub status bit and round up */
	instance->interrupt_mask_size = (instance->port_count + 1 + 8) / 8;
	instance->unfinished_interrupt_transfer = NULL;
#if OHCI_POWER == 0
	/* Set port power mode to no power-switching. (always on) */
	instance->registers->rh_desc_a |= RHDA_NPS_FLAG;
#elif OHCI_POWER == 1
	/* Set port power mode to no ganged power-switching. */
	instance->registers->rh_desc_a &= ~RHDA_NPS_FLAG;
	instance->registers->rh_desc_a &= ~RHDA_PSM_FLAG;
#else
	/* Set port power mode to no per port power-switching. */
	instance->registers->rh_desc_a &= ~RHDA_NPS_FLAG;
	instance->registers->rh_desc_a |= RHDA_PSM_FLAG;

#endif

	rh_init_descriptors(instance);

	usb_log_info("Root hub (%zu ports) initialized.\n",
	    instance->port_count);
}
/*----------------------------------------------------------------------------*/
/**
 * Process root hub request.
 *
 * @param instance Root hub instance
 * @param request Structure containing both request and response information
 * @return Error code
 */
int rh_request(rh_t *instance, usb_transfer_batch_t *request)
{
	assert(instance);
	assert(request);

	int opResult;
	switch (request->ep->transfer_type)
	{
	case USB_TRANSFER_CONTROL:
		usb_log_debug("Root hub got CONTROL packet\n");
		opResult = ctrl_request(instance, request);
		usb_transfer_batch_finish_error(request, opResult);
		break;
	case USB_TRANSFER_INTERRUPT:
		usb_log_debug("Root hub got INTERRUPT packet\n");
		const uint16_t mask = create_interrupt_mask(instance);
		if (mask == 0) {
			usb_log_debug("No changes..\n");
			instance->unfinished_interrupt_transfer = request;
			//will be finished later
		} else {
			usb_log_debug("Processing changes..\n");
			memcpy(request->data_buffer, &mask,
			    instance->interrupt_mask_size);
			request->transfered_size = instance->interrupt_mask_size;
			instance->unfinished_interrupt_transfer = NULL;
			usb_transfer_batch_finish_error(request, EOK);
		}
		break;
	default:
		usb_log_error("Root hub got unsupported request.\n");
		usb_transfer_batch_finish_error(request, EINVAL);
	}
	return EOK;
}
/*----------------------------------------------------------------------------*/
/**
 * process interrupt on a hub
 *
 * If there is no pending interrupt transfer, nothing happens.
 * @param instance
 */
void rh_interrupt(rh_t *instance)
{
	if (!instance->unfinished_interrupt_transfer)
		return;

	usb_log_debug("Finalizing interrupt transfer\n");
	const uint16_t mask = create_interrupt_mask(instance);
	memcpy(instance->unfinished_interrupt_transfer->data_buffer,
	    &mask, instance->interrupt_mask_size);
	instance->unfinished_interrupt_transfer->transfered_size =
	   instance->interrupt_mask_size;
	usb_transfer_batch_finish(instance->unfinished_interrupt_transfer);

	instance->unfinished_interrupt_transfer = NULL;
}
/*----------------------------------------------------------------------------*/
/**
 * Create hub descriptor.
 *
 * For descriptor format see USB hub specification (chapter 11.15.2.1, pg. 263)
 *
 * @param instance Root hub instance
 * @return Error code
 */
void create_serialized_hub_descriptor(rh_t *instance)
{
	assert(instance);

	const size_t bit_field_size = (instance->port_count + 1 + 7) / 8;
	assert(bit_field_size == 2 || bit_field_size == 1);
	/* 7 bytes + 2 port bit fields (port count + global bit) */
	const size_t size = 7 + (bit_field_size * 2);
	assert(size <= HUB_DESCRIPTOR_MAX_SIZE);
	instance->hub_descriptor_size = size;

	const uint32_t hub_desc = instance->registers->rh_desc_a;
	const uint32_t port_desc = instance->registers->rh_desc_b;

	/* bDescLength */
	instance->descriptors.hub[0] = size;
	/* bDescriptorType */
	instance->descriptors.hub[1] = USB_DESCTYPE_HUB;
	/* bNmbrPorts */
	instance->descriptors.hub[2] = instance->port_count;
	/* wHubCharacteristics */
	instance->descriptors.hub[3] = 0 |
	    /* The lowest 2 bits indicate power switching mode */
	    (((hub_desc & RHDA_PSM_FLAG)  ? 1 : 0) << 0) |
	    (((hub_desc & RHDA_NPS_FLAG)  ? 1 : 0) << 1) |
	    /* Bit 3 indicates device type (compound device) */
	    (((hub_desc & RHDA_DT_FLAG)   ? 1 : 0) << 2) |
	    /* Bits 4,5 indicate over-current protection mode */
	    (((hub_desc & RHDA_OCPM_FLAG) ? 1 : 0) << 3) |
	    (((hub_desc & RHDA_NOCP_FLAG) ? 1 : 0) << 4);

	/* Reserved */
	instance->descriptors.hub[4] = 0;
	/* bPwrOn2PwrGood */
	instance->descriptors.hub[5] =
	    (hub_desc >> RHDA_POTPGT_SHIFT) & RHDA_POTPGT_MASK;
	/* bHubContrCurrent, root hubs don't need no power. */
	instance->descriptors.hub[6] = 0;

	/* Device Removable and some legacy 1.0 stuff*/
	instance->descriptors.hub[7] =
	    (port_desc >> RHDB_DR_SHIFT) & RHDB_DR_MASK & 0xff;
	instance->descriptors.hub[8] = 0xff;
	if (bit_field_size == 2) {
		instance->descriptors.hub[8] =
		    (port_desc >> RHDB_DR_SHIFT) & RHDB_DR_MASK >> 8;
		instance->descriptors.hub[9]  = 0xff;
		instance->descriptors.hub[10] = 0xff;
	}
}
/*----------------------------------------------------------------------------*/
/** Initialize hub descriptors.
 *
 * Device and full configuration descriptor are created. These need to
 * be initialized only once per hub.
 * @param instance Root hub instance
 * @return Error code
 */
void rh_init_descriptors(rh_t *instance)
{
	assert(instance);

	instance->descriptors.configuration = ohci_rh_conf_descriptor;
	instance->descriptors.interface = ohci_rh_iface_descriptor;
	instance->descriptors.endpoint = ohci_rh_ep_descriptor;
	create_serialized_hub_descriptor(instance);

	instance->descriptors.configuration.total_length =
	    sizeof(usb_standard_configuration_descriptor_t) +
	    sizeof(usb_standard_endpoint_descriptor_t) +
	    sizeof(usb_standard_interface_descriptor_t) +
	    instance->hub_descriptor_size;
}
/*----------------------------------------------------------------------------*/
/**
 * Create answer to status request.
 *
 * This might be either hub status or port status request. If neither,
 * ENOTSUP is returned.
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
int get_status_request(rh_t *instance, usb_transfer_batch_t *request)
{
	assert(instance);
	assert(request);

	const usb_device_request_setup_packet_t *request_packet =
	    (usb_device_request_setup_packet_t*)request->setup_buffer;

	if (request->buffer_size < 4) {
		usb_log_error("Buffer too small for get status request.\n");
		return EOVERFLOW;
	}

	/* Hub status: just filter relevant info from rh_status reg */
	if (request_packet->request_type == USB_HUB_REQ_TYPE_GET_HUB_STATUS) {
		const uint32_t data = instance->registers->rh_status &
		    (RHS_LPS_FLAG | RHS_LPSC_FLAG | RHS_OCI_FLAG | RHS_OCIC_FLAG);
		memcpy(request->data_buffer, &data, 4);
		TRANSFER_OK(4);
	}

	/* Copy appropriate rh_port_status register, OHCI designers were
	 * kind enough to make those bit values match USB specification */
	if (request_packet->request_type == USB_HUB_REQ_TYPE_GET_PORT_STATUS) {
		const unsigned port = request_packet->index;
		if (port < 1 || port > instance->port_count)
			return EINVAL;

		const uint32_t data =
		    instance->registers->rh_port_status[port - 1];
		memcpy(request->data_buffer, &data, 4);
		TRANSFER_OK(4);
	}

	return ENOTSUP;
}
/*----------------------------------------------------------------------------*/
/**
 * Create bitmap of changes to answer status interrupt.
 *
 * Result contains bitmap where bit 0 indicates change on hub and
 * bit i indicates change on i`th port (i>0). For more info see
 * Hub and Port status bitmap specification in USB specification
 * (chapter 11.13.4).
 * Uses instance`s interrupt buffer to store the interrupt information.
 * @param instance root hub instance
 */
uint16_t create_interrupt_mask(rh_t *instance)
{
	assert(instance);
	uint16_t mask = 0;

	/* Only local power source change and over-current change can happen */
	if (instance->registers->rh_status & (RHS_LPSC_FLAG | RHS_OCIC_FLAG)) {
		mask |= 1;
	}
	size_t port = 1;
	for (; port <= instance->port_count; ++port) {
		/* Write-clean bits are those that indicate change */
		if (RHPS_CHANGE_WC_MASK
		    & instance->registers->rh_port_status[port - 1]) {

			mask |= (1 << port);
		}
	}
	return host2uint32_t_le(mask);
}
/*----------------------------------------------------------------------------*/
/**
 * Create answer to a descriptor request.
 *
 * This might be a request for standard (configuration, device, endpoint or
 * interface) or device specific (hub) descriptor.
 * @param instance Root hub instance
 * @param request Structure containing both request and response information
 * @return Error code
 */
int get_descriptor_request(
    rh_t *instance, usb_transfer_batch_t *request)
{
	assert(instance);
	assert(request);

	const usb_device_request_setup_packet_t *setup_request =
	    (usb_device_request_setup_packet_t *) request->setup_buffer;
	size_t size;
	const void * result_descriptor = NULL;
	const uint16_t setup_request_value = setup_request->value_high;
	//(setup_request->value_low << 8);
	switch (setup_request_value)
	{
	case USB_DESCTYPE_HUB:
		usb_log_debug2("USB_DESCTYPE_HUB\n");
		result_descriptor = instance->descriptors.hub;
		size = instance->hub_descriptor_size;
		break;

	case USB_DESCTYPE_DEVICE:
		usb_log_debug2("USB_DESCTYPE_DEVICE\n");
		result_descriptor = &ohci_rh_device_descriptor;
		size = sizeof(ohci_rh_device_descriptor);
		break;

	case USB_DESCTYPE_CONFIGURATION:
		usb_log_debug2("USB_DESCTYPE_CONFIGURATION\n");
		result_descriptor = &instance->descriptors;
		size = instance->descriptors.configuration.total_length;
		break;

	case USB_DESCTYPE_INTERFACE:
		usb_log_debug2("USB_DESCTYPE_INTERFACE\n");
		result_descriptor = &ohci_rh_iface_descriptor;
		size = sizeof(ohci_rh_iface_descriptor);
		break;

	case USB_DESCTYPE_ENDPOINT:
		usb_log_debug2("USB_DESCTYPE_ENDPOINT\n");
		result_descriptor = &ohci_rh_ep_descriptor;
		size = sizeof(ohci_rh_ep_descriptor);
		break;

	default:
		usb_log_debug2("USB_DESCTYPE_EINVAL %d \n"
		    "\ttype %d\n\trequest %d\n\tvalue "
		    "%d\n\tindex %d\n\tlen %d\n ",
		    setup_request->value,
		    setup_request->request_type, setup_request->request,
		    setup_request_value, setup_request->index,
		    setup_request->length);
		return EINVAL;
	}
	if (request->buffer_size < size) {
		size = request->buffer_size;
	}

	memcpy(request->data_buffer, result_descriptor, size);
	TRANSFER_OK(size);
}
/*----------------------------------------------------------------------------*/
/**
 * process feature-enabling request on hub
 *
 * @param instance root hub instance
 * @param feature feature selector
 * @param port port number, counted from 1
 * @param enable enable or disable the specified feature
 * @return error code
 */
int port_feature_set_request(rh_t *instance, uint16_t feature, uint16_t port)
{
	assert(instance);

	if (port < 1 || port > instance->port_count)
		return EINVAL;

	switch (feature)
	{
	case USB_HUB_FEATURE_PORT_POWER:   //8
		/* No power switching */
		if (instance->registers->rh_desc_a & RHDA_NPS_FLAG)
			return EOK;
		/* Ganged power switching */
		if (!(instance->registers->rh_desc_a & RHDA_PSM_FLAG)) {
			instance->registers->rh_status = RHS_SET_GLOBAL_POWER;
			return EOK;
		}
	case USB_HUB_FEATURE_PORT_ENABLE:  //1
	case USB_HUB_FEATURE_PORT_SUSPEND: //2
	case USB_HUB_FEATURE_PORT_RESET:   //4
		/* Nice thing is that these shifts correspond to the position
		 * of control bits in register */
		instance->registers->rh_port_status[port - 1] = (1 << feature);
		return EOK;
	default:
		return ENOTSUP;
	}
}
/*----------------------------------------------------------------------------*/
/**
 * process feature-disabling request on hub
 *
 * @param instance root hub instance
 * @param feature feature selector
 * @param port port number, counted from 1
 * @param enable enable or disable the specified feature
 * @return error code
 */
int port_feature_clear_request(rh_t *instance, uint16_t feature, uint16_t port)
{
	assert(instance);

	if (port < 1 || port > instance->port_count)
		return EINVAL;

	/* Enabled features to clear: see page 269 of USB specs */
	switch (feature)
	{
	case USB_HUB_FEATURE_PORT_POWER:          //8
		/* No power switching */
		if (instance->registers->rh_desc_a & RHDA_NPS_FLAG)
			return ENOTSUP;
		/* Ganged power switching */
		if (!(instance->registers->rh_desc_a & RHDA_PSM_FLAG)) {
			instance->registers->rh_status = RHS_CLEAR_GLOBAL_POWER;
			return EOK;
		}
		instance->registers->rh_port_status[port - 1] =
			RHPS_CLEAR_PORT_POWER;
		return EOK;

	case USB_HUB_FEATURE_PORT_ENABLE:         //1
		instance->registers->rh_port_status[port - 1] =
			RHPS_CLEAR_PORT_ENABLE;
		return EOK;

	case USB_HUB_FEATURE_PORT_SUSPEND:        //2
		instance->registers->rh_port_status[port - 1] =
			RHPS_CLEAR_PORT_SUSPEND;
		return EOK;

	case USB_HUB_FEATURE_C_PORT_CONNECTION:   //16
	case USB_HUB_FEATURE_C_PORT_ENABLE:       //17
	case USB_HUB_FEATURE_C_PORT_SUSPEND:      //18
	case USB_HUB_FEATURE_C_PORT_OVER_CURRENT: //19
	case USB_HUB_FEATURE_C_PORT_RESET:        //20
		/* Nice thing is that these shifts correspond to the position
		 * of control bits in register */
		instance->registers->rh_port_status[port - 1] = (1 << feature);
		return EOK;
	default:
		return ENOTSUP;
	}
}
/*----------------------------------------------------------------------------*/
/**
 * Process a request that requires output data.
 *
 * Request can be one of USB_DEVREQ_GET_STATUS, USB_DEVREQ_GET_DESCRIPTOR or
 * USB_DEVREQ_GET_CONFIGURATION.
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
int request_with_output(rh_t *instance, usb_transfer_batch_t *request)
{
	assert(instance);
	assert(request);

	const usb_device_request_setup_packet_t *setup_request =
	    (usb_device_request_setup_packet_t *) request->setup_buffer;
	switch (setup_request->request)
	{
	case USB_DEVREQ_GET_STATUS:
		usb_log_debug("USB_DEVREQ_GET_STATUS\n");
		return get_status_request(instance, request);
	case USB_DEVREQ_GET_DESCRIPTOR:
		usb_log_debug("USB_DEVREQ_GET_DESCRIPTOR\n");
		return get_descriptor_request(instance, request);
	case USB_DEVREQ_GET_CONFIGURATION:
		usb_log_debug("USB_DEVREQ_GET_CONFIGURATION\n");
		if (request->buffer_size != 1)
			return EINVAL;
		request->data_buffer[0] = 1;
		TRANSFER_OK(1);
	}
	return ENOTSUP;
}
/*----------------------------------------------------------------------------*/
/**
 * process one of requests that do not request nor carry additional data
 *
 * Request can be one of USB_DEVREQ_CLEAR_FEATURE, USB_DEVREQ_SET_FEATURE or
 * USB_DEVREQ_SET_ADDRESS.
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
int request_without_data(rh_t *instance, usb_transfer_batch_t *request)
{
	assert(instance);
	assert(request);

	const usb_device_request_setup_packet_t *setup_request =
	    (usb_device_request_setup_packet_t *) request->setup_buffer;
	request->transfered_size = 0;
	const int request_type = setup_request->request_type;
	switch (setup_request->request)
	{
	case USB_DEVREQ_CLEAR_FEATURE:
		if (request_type == USB_HUB_REQ_TYPE_CLEAR_PORT_FEATURE) {
			usb_log_debug("USB_HUB_REQ_TYPE_CLEAR_PORT_FEATURE\n");
			return port_feature_clear_request(instance,
			    setup_request->value, setup_request->index);
		}
		if (request_type == USB_HUB_REQ_TYPE_CLEAR_HUB_FEATURE) {
			usb_log_debug("USB_HUB_REQ_TYPE_CLEAR_HUB_FEATURE\n");
	/*
	 * Chapter 11.16.2 specifies that only C_HUB_LOCAL_POWER and
	 * C_HUB_OVER_CURRENT are supported. C_HUB_OVER_CURRENT is represented
	 * by OHCI RHS_OCIC_FLAG. C_HUB_LOCAL_POWER is not supported
	 * as root hubs do not support local power status feature.
	 * (OHCI pg. 127) */
	if (setup_request->value == USB_HUB_FEATURE_C_HUB_OVER_CURRENT) {
		instance->registers->rh_status = RHS_OCIC_FLAG;
		return EOK;
	}
		}
			usb_log_error("Invalid clear feature request type: %d\n",
			    request_type);
			return EINVAL;

	case USB_DEVREQ_SET_FEATURE:
		switch (request_type)
		{
		case USB_HUB_REQ_TYPE_SET_PORT_FEATURE:
			usb_log_debug("USB_HUB_REQ_TYPE_SET_PORT_FEATURE\n");
			return port_feature_set_request(instance,
			    setup_request->value, setup_request->index);

		case USB_HUB_REQ_TYPE_SET_HUB_FEATURE:
		/* Chapter 11.16.2 specifies that hub can be recipient
		 * only for C_HUB_LOCAL_POWER and C_HUB_OVER_CURRENT
		 * features. It makes no sense to SET either. */
			usb_log_error("Invalid HUB set feature request.\n");
			return ENOTSUP;
		default:
			usb_log_error("Invalid set feature request type: %d\n",
			    request_type);
			return EINVAL;
		}

	case USB_DEVREQ_SET_ADDRESS:
		usb_log_debug("USB_DEVREQ_SET_ADDRESS\n");
		instance->address = setup_request->value;
		TRANSFER_OK(0);
	case USB_DEVREQ_SET_CONFIGURATION:
		usb_log_debug("USB_DEVREQ_SET_CONFIGURATION\n");
		/* We don't need to do anything */
		TRANSFER_OK(0);

	default:
		usb_log_error("Invalid HUB request: %d\n",
		    setup_request->request);
		return ENOTSUP;
	}
}
/*----------------------------------------------------------------------------*/
/**
 * Process hub control request.
 *
 * If needed, writes answer into the request structure.
 * Request can be one of
 * USB_DEVREQ_GET_STATUS,
 * USB_DEVREQ_GET_DESCRIPTOR,
 * USB_DEVREQ_GET_CONFIGURATION,
 * USB_DEVREQ_CLEAR_FEATURE,
 * USB_DEVREQ_SET_FEATURE,
 * USB_DEVREQ_SET_ADDRESS,
 * USB_DEVREQ_SET_DESCRIPTOR or
 * USB_DEVREQ_SET_CONFIGURATION.
 *
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
int ctrl_request(rh_t *instance, usb_transfer_batch_t *request)
{
	assert(instance);
	assert(request);

	if (!request->setup_buffer) {
		usb_log_error("Root hub received empty transaction!");
		return EINVAL;
	}
	if (sizeof(usb_device_request_setup_packet_t) > request->setup_size) {
		usb_log_error("Setup packet too small\n");
		return EOVERFLOW;
	}
	usb_log_debug2("CTRL packet: %s.\n",
	    usb_debug_str_buffer((uint8_t *) request->setup_buffer, 8, 8));
	const usb_device_request_setup_packet_t *setup_request =
	    (usb_device_request_setup_packet_t *) request->setup_buffer;
	switch (setup_request->request)
	{
	case USB_DEVREQ_GET_STATUS:
	case USB_DEVREQ_GET_DESCRIPTOR:
	case USB_DEVREQ_GET_CONFIGURATION:
		usb_log_debug2("Processing request with output\n");
		return request_with_output(instance, request);
	case USB_DEVREQ_CLEAR_FEATURE:
	case USB_DEVREQ_SET_FEATURE:
	case USB_DEVREQ_SET_ADDRESS:
	case USB_DEVREQ_SET_CONFIGURATION:
		usb_log_debug2("Processing request without "
		    "additional data\n");
		return request_without_data(instance, request);
	case USB_DEVREQ_SET_DESCRIPTOR: /* Not supported by OHCI RH */
	default:
		usb_log_error("Received unsupported request: %d.\n",
		    setup_request->request);
		return ENOTSUP;
	}
}

/**
 * @}
 */
