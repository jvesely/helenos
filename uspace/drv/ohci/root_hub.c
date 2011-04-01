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
#include "usb/classes/classes.h"
#include "usb/devdrv.h"
#include <usb/request.h>
#include <usb/classes/hub.h>

/**
 *	standart device descriptor for ohci root hub
 */
static const usb_standard_device_descriptor_t ohci_rh_device_descriptor =
{
		.configuration_count = 1,
		.descriptor_type = USB_DESCTYPE_DEVICE,
		.device_class = USB_CLASS_HUB,
		.device_protocol = 0,
		.device_subclass = 0,
		.device_version = 0,
		.length = sizeof(usb_standard_device_descriptor_t),
		/// \TODO this value is guessed
		.max_packet_size = 8,
		.vendor_id = 0x16db,
		.product_id = 0x0001,
		/// \TODO these values migt be different
		.str_serial_number = 0,
		.usb_spec_version = 0x110,
};

/**
 * standart configuration descriptor with filled common values
 * for ohci root hubs
 */
static const usb_standard_configuration_descriptor_t ohci_rh_conf_descriptor =
{
	/// \TODO some values are default or guessed
	.attributes = 1<<7,
	.configuration_number = 1,
	.descriptor_type = USB_DESCTYPE_CONFIGURATION,
	.interface_count = 1,
	.length = sizeof(usb_standard_configuration_descriptor_t),
	.max_power = 100,
	.str_configuration = 0,
};

/**
 * standart ohci root hub interface descriptor
 */
static const usb_standard_interface_descriptor_t ohci_rh_iface_descriptor =
{
	.alternate_setting = 0,
	.descriptor_type = USB_DESCTYPE_INTERFACE,
	.endpoint_count = 1,
	.interface_class = USB_CLASS_HUB,
	/// \TODO is this correct?
	.interface_number = 1,
	.interface_protocol = 0,
	.interface_subclass = 0,
	.length = sizeof(usb_standard_interface_descriptor_t),
	.str_interface = 0,
};

/**
 * standart ohci root hub endpoint descriptor
 */
static const usb_standard_endpoint_descriptor_t ohci_rh_ep_descriptor =
{
	.attributes = USB_TRANSFER_INTERRUPT,
	.descriptor_type = USB_DESCTYPE_ENDPOINT,
	.endpoint_address = 1 + (1<<7),
	.length = sizeof(usb_standard_endpoint_descriptor_t),
	.max_packet_size = 8,
	.poll_interval = 255,
};

/**
 * Create hub descriptor used in hub-driver <-> hub communication
 *
 * This means creating byt array from data in root hub registers. For more
 * info see usb hub specification.
 *
 * @param instance root hub instance
 * @param@out out_result pointer to resultant serialized descriptor
 * @param@out out_size size of serialized descriptor
 */
static void usb_create_serialized_hub_descriptor(rh_t *instance,
		uint8_t ** out_result,
		size_t * out_size) {
	//base size
	size_t size = 7;
	//variable size according to port count
	size_t var_size = instance->port_count / 8 +
			((instance->port_count % 8 > 0) ? 1 : 0);
	size += 2 * var_size;
	uint8_t * result = (uint8_t*) malloc(size);
	bzero(result,size);
	//size
	result[0] = size;
	//descriptor type
	result[1] = USB_DESCTYPE_HUB;
	result[2] = instance->port_count;
	uint32_t hub_desc_reg = instance->registers->rh_desc_a;
	result[3] =
			((hub_desc_reg >> 8) %2) +
			(((hub_desc_reg >> 9) %2) << 1) +
			(((hub_desc_reg >> 10) %2) << 2) +
			(((hub_desc_reg >> 11) %2) << 3) +
			(((hub_desc_reg >> 12) %2) << 4);
	result[4] = 0;
	result[5] = /*descriptor->pwr_on_2_good_time*/ 50;
	result[6] = 50;

	int port;
	for (port = 1; port <= instance->port_count; ++port) {
		result[7 + port/8] +=
				((instance->registers->rh_desc_b >> port)%2) << (port%8);
	}
	size_t i;
	for (i = 0; i < var_size; ++i) {
		result[7 + var_size + i] = 255;
	}
	(*out_result) = result;
	(*out_size) = size;
}


/** initialize hub descriptors
 *
 * Initialized are device and full configuration descriptor. These need to
 * be initialized only once per hub.
 * @instance root hub instance
 */
static void rh_init_descriptors(rh_t *instance){
	memcpy(&instance->descriptors.device, &ohci_rh_device_descriptor,
		sizeof(ohci_rh_device_descriptor)
	);
	usb_standard_configuration_descriptor_t descriptor;
	memcpy(&descriptor,&ohci_rh_conf_descriptor,
			sizeof(ohci_rh_conf_descriptor));
	uint8_t * hub_descriptor;
	size_t hub_desc_size;
	usb_create_serialized_hub_descriptor(instance, &hub_descriptor,
			&hub_desc_size);

	descriptor.total_length =
			sizeof(usb_standard_configuration_descriptor_t)+
			sizeof(usb_standard_endpoint_descriptor_t)+
			sizeof(usb_standard_interface_descriptor_t)+
			hub_desc_size;
	
	uint8_t * full_config_descriptor =
			(uint8_t*) malloc(descriptor.total_length);
	memcpy(full_config_descriptor, &descriptor, sizeof(descriptor));
	memcpy(full_config_descriptor + sizeof(descriptor),
			&ohci_rh_iface_descriptor, sizeof(ohci_rh_iface_descriptor));
	memcpy(full_config_descriptor + sizeof(descriptor) +
				sizeof(ohci_rh_iface_descriptor),
			&ohci_rh_ep_descriptor, sizeof(ohci_rh_ep_descriptor));
	memcpy(full_config_descriptor + sizeof(descriptor) +
				sizeof(ohci_rh_iface_descriptor) +
				sizeof(ohci_rh_ep_descriptor),
			hub_descriptor, hub_desc_size);
	
	instance->descriptors.configuration = full_config_descriptor;
	instance->descriptors.configuration_size = descriptor.total_length;
}

/** Root hub initialization
 * @return Error code.
 */
int rh_init(rh_t *instance, ddf_dev_t *dev, ohci_regs_t *regs)
{
	assert(instance);
	instance->address = -1;
	instance->registers = regs;
	instance->device = dev;
	rh_init_descriptors(instance);


	usb_log_info("OHCI root hub with %d ports.\n", regs->rh_desc_a & 0xff);

	//start generic usb hub driver
	
	/* TODO: implement */
	return EOK;
}
/*----------------------------------------------------------------------------*/

/**
 * create answer to port status_request
 *
 * Copy content of corresponding port status register to answer buffer.
 *
 * @param instance root hub instance
 * @param port port number, counted from 1
 * @param request structure containing both request and response information
 * @return error code
 */
static int process_get_port_status_request(rh_t *instance, uint16_t port,
		usb_transfer_batch_t * request){
	if(port<1 || port>instance->port_count)
		return EINVAL;
	uint32_t * uint32_buffer = (uint32_t*)request->buffer;
	request->transfered_size = 4;
	uint32_buffer[0] = instance->registers->rh_port_status[port -1];
	return EOK;
}

/**
 * create answer to port status_request
 *
 * Copy content of hub status register to answer buffer.
 *
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
static int process_get_hub_status_request(rh_t *instance,
		usb_transfer_batch_t * request){
	uint32_t * uint32_buffer = (uint32_t*)request->buffer;
	//bits, 0,1,16,17
	request->transfered_size = 4;
	uint32_t mask = 1 & (1<<1) & (1<<16) & (1<<17);
	uint32_buffer[0] = mask & instance->registers->rh_status;
	return EOK;

}



/**
 * create answer to status request
 *
 * This might be either hub status or port status request. If neither,
 * ENOTSUP is returned.
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
static int process_get_status_request(rh_t *instance,
		usb_transfer_batch_t * request)
{
	size_t buffer_size = request->buffer_size;
	usb_device_request_setup_packet_t * request_packet =
			(usb_device_request_setup_packet_t*)
			request->setup_buffer;

	usb_hub_bm_request_type_t request_type = request_packet->request_type;
	if(buffer_size<4/*request_packet->length*/){///\TODO
		usb_log_warning("requested more data than buffer size\n");
		return EINVAL;
	}

	if(request_type == USB_HUB_REQ_TYPE_GET_HUB_STATUS)
		return process_get_hub_status_request(instance, request);
	if(request_type == USB_HUB_REQ_TYPE_GET_PORT_STATUS)
		return process_get_port_status_request(instance, request_packet->index,
				request);
	return ENOTSUP;
}

/**
 * create answer to status interrupt consisting of change bitmap
 *
 * Result contains bitmap where bit 0 indicates change on hub and
 * bit i indicates change on i`th port (i>0). For more info see
 * Hub and Port status bitmap specification in USB specification.
 * @param instance root hub instance
 * @param@out buffer pointer to created interrupt mas
 * @param@out buffer_size size of created interrupt mask
 */
static void create_interrupt_mask(rh_t *instance, void ** buffer,
		size_t * buffer_size){
	int bit_count = instance->port_count + 1;
	(*buffer_size) = (bit_count / 8) + (bit_count%8==0)?0:1;
	(*buffer) = malloc(*buffer_size);
	uint8_t * bitmap = (uint8_t*)(*buffer);
	uint32_t mask = (1<<16) + (1<<17);
	bzero(bitmap,(*buffer_size));
	if(instance->registers->rh_status & mask){
		bitmap[0] = 1;
	}
	int port;
	mask = 0;
	int i;
	for(i=16;i<=20;++i)
		mask += 1<<i;
	for(port = 1; port<=instance->port_count;++port){
		if(mask & instance->registers->rh_port_status[port-1]){
			bitmap[(port+1)/8] += 1<<(port%8);
		}
	}
}
 
/**
 * create answer to a descriptor request
 *
 * This might be a request for standard (configuration, device, endpoint or
 * interface) or device specific (hub) descriptor.
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
static int process_get_descriptor_request(rh_t *instance,
		usb_transfer_batch_t *request){
	usb_device_request_setup_packet_t * setup_request =
			(usb_device_request_setup_packet_t*)request->setup_buffer;
	size_t size;
	const void * result_descriptor = NULL;
	const uint16_t setup_request_value = setup_request->value_high;
			//(setup_request->value_low << 8);
	bool del = false;
	switch (setup_request_value)
	{
		case USB_DESCTYPE_HUB: {
			uint8_t * descriptor;
			usb_create_serialized_hub_descriptor(
				instance, &descriptor, &size);
			result_descriptor = descriptor;
			if(result_descriptor) del = true;
			break;
		}
		case USB_DESCTYPE_DEVICE: {
			usb_log_debug("USB_DESCTYPE_DEVICE\n");
			result_descriptor = &ohci_rh_device_descriptor;
			size = sizeof(ohci_rh_device_descriptor);
			break;
		}
		case USB_DESCTYPE_CONFIGURATION: {
			usb_log_debug("USB_DESCTYPE_CONFIGURATION\n");
			result_descriptor = instance->descriptors.configuration;
			size = instance->descriptors.configuration_size;
			break;
		}
		case USB_DESCTYPE_INTERFACE: {
			usb_log_debug("USB_DESCTYPE_INTERFACE\n");
			result_descriptor = &ohci_rh_iface_descriptor;
			size = sizeof(ohci_rh_iface_descriptor);
			break;
		}
		case USB_DESCTYPE_ENDPOINT: {
			usb_log_debug("USB_DESCTYPE_ENDPOINT\n");
			result_descriptor = &ohci_rh_ep_descriptor;
			size = sizeof(ohci_rh_ep_descriptor);
			break;
		}
		default: {
			usb_log_debug("USB_DESCTYPE_EINVAL %d \n",setup_request->value);
			usb_log_debug("\ttype %d\n\trequest %d\n\tvalue %d\n\tindex %d\n\tlen %d\n ",
					setup_request->request_type,
					setup_request->request,
					setup_request_value,
					setup_request->index,
					setup_request->length
					);
			return EINVAL;
		}
	}
	if(request->buffer_size < size){
		size = request->buffer_size;
	}
	request->transfered_size = size;
	memcpy(request->transport_buffer,result_descriptor,size);
	usb_log_debug("sent desctiptor: %s\n",
			usb_debug_str_buffer((uint8_t*)request->transport_buffer,size,size));
	if (del)
		free(result_descriptor);
	return EOK;
}

/**
 * answer to get configuration request
 *
 * Root hub works independently on the configuration.
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
static int process_get_configuration_request(rh_t *instance, 
		usb_transfer_batch_t *request){
	//set and get configuration requests do not have any meaning, only dummy
	//values are returned
	if(request->buffer_size != 1)
		return EINVAL;
	request->buffer[0] = 1;
	request->transfered_size = 1;
	return EOK;
}

/**
 * process feature-enabling/disabling request on hub
 * 
 * @param instance root hub instance
 * @param feature feature selector
 * @param enable enable or disable specified feature
 * @return error code
 */
static int process_hub_feature_set_request(rh_t *instance,
		uint16_t feature, bool enable){
	if(feature > USB_HUB_FEATURE_C_HUB_OVER_CURRENT)
		return EINVAL;
	instance->registers->rh_status =
			enable ?
			(instance->registers->rh_status | (1<<feature))
			:
			(instance->registers->rh_status & (~(1<<feature)));
	/// \TODO any error?
	return EOK;
}

/**
 * process feature-enabling/disabling request on hub
 * 
 * @param instance root hub instance
 * @param feature feature selector
 * @param port port number, counted from 1
 * @param enable enable or disable the specified feature
 * @return error code
 */
static int process_port_feature_set_request(rh_t *instance,
		uint16_t feature, uint16_t port, bool enable){
	if(feature > USB_HUB_FEATURE_C_PORT_RESET)
		return EINVAL;
	if(port<1 || port>instance->port_count)
		return EINVAL;
	instance->registers->rh_port_status[port - 1] =
			enable ?
			(instance->registers->rh_port_status[port - 1] | (1<<feature))
			:
			(instance->registers->rh_port_status[port - 1] & (~(1<<feature)));
	/// \TODO any error?
	return EOK;
}

/**
 * register address to this device
 * 
 * @param instance root hub instance
 * @param address new address
 * @return error code
 */
static int process_address_set_request(rh_t *instance,
		uint16_t address){
	instance->address = address;
	return EOK;
}

/**
 * process one of requests that requere output data
 *
 * Request can be one of USB_DEVREQ_GET_STATUS, USB_DEVREQ_GET_DESCRIPTOR or
 * USB_DEVREQ_GET_CONFIGURATION.
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
static int process_request_with_output(rh_t *instance,
		usb_transfer_batch_t *request){
	usb_device_request_setup_packet_t * setup_request =
			(usb_device_request_setup_packet_t*)request->setup_buffer;
	if(setup_request->request == USB_DEVREQ_GET_STATUS){
		usb_log_debug("USB_DEVREQ_GET_STATUS\n");
		return process_get_status_request(instance, request);
	}
	if(setup_request->request == USB_DEVREQ_GET_DESCRIPTOR){
		usb_log_debug("USB_DEVREQ_GET_DESCRIPTOR\n");
		return process_get_descriptor_request(instance, request);
	}
	if(setup_request->request == USB_DEVREQ_GET_CONFIGURATION){
		usb_log_debug("USB_DEVREQ_GET_CONFIGURATION\n");
		return process_get_configuration_request(instance, request);
	}
	return ENOTSUP;
}

/**
 * process one of requests that carry input data
 *
 * Request can be one of USB_DEVREQ_SET_DESCRIPTOR or
 * USB_DEVREQ_SET_CONFIGURATION.
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
static int process_request_with_input(rh_t *instance,
		usb_transfer_batch_t *request){
	usb_device_request_setup_packet_t * setup_request =
			(usb_device_request_setup_packet_t*)request->setup_buffer;
	request->transfered_size = 0;
	if(setup_request->request == USB_DEVREQ_SET_DESCRIPTOR){
		return ENOTSUP;
	}
	if(setup_request->request == USB_DEVREQ_SET_CONFIGURATION){
		//set and get configuration requests do not have any meaning,
		//only dummy values are returned
		return EOK;
	}
	return ENOTSUP;
}

/**
 * process one of requests that do not request nor carry additional data
 *
 * Request can be one of USB_DEVREQ_CLEAR_FEATURE, USB_DEVREQ_SET_FEATURE or
 * USB_DEVREQ_SET_ADDRESS.
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
static int process_request_without_data(rh_t *instance,
		usb_transfer_batch_t *request){
	usb_device_request_setup_packet_t * setup_request =
			(usb_device_request_setup_packet_t*)request->setup_buffer;
	request->transfered_size = 0;
	if(setup_request->request == USB_DEVREQ_CLEAR_FEATURE
				|| setup_request->request == USB_DEVREQ_SET_FEATURE){
		if(setup_request->request_type == USB_HUB_REQ_TYPE_SET_HUB_FEATURE){
			usb_log_debug("USB_HUB_REQ_TYPE_SET_HUB_FEATURE\n");
			return process_hub_feature_set_request(instance, setup_request->value,
					setup_request->request == USB_DEVREQ_SET_FEATURE);
		}
		if(setup_request->request_type == USB_HUB_REQ_TYPE_SET_PORT_FEATURE){
			usb_log_debug("USB_HUB_REQ_TYPE_SET_PORT_FEATURE\n");
			return process_port_feature_set_request(instance, setup_request->value,
					setup_request->index,
					setup_request->request == USB_DEVREQ_SET_FEATURE);
		}
		usb_log_debug("USB_HUB_REQ_TYPE_INVALID %d\n",setup_request->request_type);
		return EINVAL;
	}
	if(setup_request->request == USB_DEVREQ_SET_ADDRESS){
		usb_log_debug("USB_DEVREQ_SET_ADDRESS\n");
		return process_address_set_request(instance, setup_request->value);
	}
	usb_log_debug("USB_DEVREQ_SET_ENOTSUP %d\n",setup_request->request_type);
	return ENOTSUP;
}

/**
 * process hub control request
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
static int process_ctrl_request(rh_t *instance, usb_transfer_batch_t *request){
	int opResult;
	if (request->setup_buffer) {
		if(sizeof(usb_device_request_setup_packet_t)>request->setup_size){
			usb_log_error("setup packet too small\n");
			return EINVAL;
		}
		usb_log_info("CTRL packet: %s.\n",
			usb_debug_str_buffer((const uint8_t *)request->setup_buffer, 8, 8));
		usb_device_request_setup_packet_t * setup_request =
				(usb_device_request_setup_packet_t*)request->setup_buffer;
		if(
			setup_request->request == USB_DEVREQ_GET_STATUS
			|| setup_request->request == USB_DEVREQ_GET_DESCRIPTOR
			|| setup_request->request == USB_DEVREQ_GET_CONFIGURATION
		){
			usb_log_debug("processing request with output\n");
			opResult = process_request_with_output(instance,request);
		}else if(
			setup_request->request == USB_DEVREQ_CLEAR_FEATURE
			|| setup_request->request == USB_DEVREQ_SET_FEATURE
			|| setup_request->request == USB_DEVREQ_SET_ADDRESS
		){
			usb_log_debug("processing request without additional data\n");
			opResult = process_request_without_data(instance,request);
		}else if(setup_request->request == USB_DEVREQ_SET_DESCRIPTOR
				|| setup_request->request == USB_DEVREQ_SET_CONFIGURATION
		){
			usb_log_debug("processing request with input\n");
			opResult = process_request_with_input(instance,request);
		}else{
			usb_log_warning("received unsuported request: %d\n",
					setup_request->request
					);
			opResult = ENOTSUP;
		}
	}else{
		usb_log_error("root hub received empty transaction?");
		opResult = EINVAL;
	}
	return opResult;
}

/**
 * process root hub request
 *
 * @param instance root hub instance
 * @param request structure containing both request and response information
 * @return error code
 */
int rh_request(rh_t *instance, usb_transfer_batch_t *request)
{
	assert(instance);
	assert(request);
	int opResult;
	if(request->transfer_type == USB_TRANSFER_CONTROL){
		usb_log_info("Root hub got CONTROL packet\n");
		opResult = process_ctrl_request(instance,request);
	}else if(request->transfer_type == USB_TRANSFER_INTERRUPT){
		usb_log_info("Root hub got INTERRUPT packet\n");
		void * buffer;
		create_interrupt_mask(instance, &buffer,
			&(request->transfered_size));
		memcpy(request->transport_buffer,buffer, request->transfered_size);
		opResult = EOK;
	}else{
		opResult = EINVAL;
	}
	usb_transfer_batch_finish(request, opResult);
	return EOK;
}
/*----------------------------------------------------------------------------*/


void rh_interrupt(rh_t *instance)
{
	usb_log_info("Whoa whoa wait, I`m not supposed to receive any interrupts, am I?\n");
	/* TODO: implement? */
}
/**
 * @}
 */
