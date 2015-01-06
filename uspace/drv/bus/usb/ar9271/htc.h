/*
 * Copyright (c) 2014 Jan Kolarik
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

/** @file htc.h
 *
 * Definitions of the Atheros HTC (Host Target Communication) technology 
 * for communication between host (PC) and target (device firmware).
 *
 */

#ifndef ATHEROS_HTC_H
#define	ATHEROS_HTC_H

#include <usb/dev/driver.h>
#include <sys/types.h>
#include <errno.h>

#include "ath.h"

/**
 * HTC message IDs
 */
typedef enum {
	HTC_MESSAGE_READY = 1,
	HTC_MESSAGE_CONNECT_SERVICE,
        HTC_MESSAGE_CONNECT_SERVICE_RESPONSE,
        HTC_MESSAGE_SETUP_COMPLETE,
        HTC_MESSAGE_CONFIG
} htc_message_id_t;

/**
 * HTC response message status codes
 */
typedef enum {
        HTC_SERVICE_SUCCESS = 0,
        HTC_SERVICE_NOT_FOUND,
        HTC_SERVICE_FAILED,
        HTC_SERVICE_NO_RESOURCES,
        HTC_SERVICE_NO_MORE_EP
} htc_response_status_code_t;

/**
 * HTC endpoint numbers
 */
typedef struct {
        int ctrl_endpoint;
	int wmi_endpoint;
	int beacon_endpoint;
	int cab_endpoint;
	int uapsd_endpoint;
	int mgmt_endpoint;
	int data_be_endpoint;
	int data_bk_endpoint;
	int data_video_endpoint;
	int data_voice_endpoint;
} htc_pipes_t;

/**
 * HTC device data
 */
typedef struct {
        /** WMI message sequence number */
        uint16_t sequence_number;
    
	/** HTC endpoints numbers */
	htc_pipes_t endpoints;
	
	/** Lock for receiver */
	fibril_mutex_t rx_lock;
	
	/** Lock for transmitter */
	fibril_mutex_t tx_lock;
	
	/** Pointer to Atheros WiFi device structure */
	ath_t *ath_device;
} htc_device_t;

/** 
 * HTC frame header structure 
 */
typedef struct {
	uint8_t   endpoint_id;
	uint8_t   flags;
	uint16_t  payload_length;	/**< Big Endian value! */
	uint8_t   control_bytes[4];
    
	/* Message payload starts after the header. */
} __attribute__((packed)) htc_frame_header_t;

/** 
 * HTC ready message structure 
 */
typedef struct {
        uint16_t message_id;		/**< Big Endian value! */
        uint16_t credits;		/**< Big Endian value! */
        uint16_t credit_size;		/**< Big Endian value! */
        
        uint8_t max_endpoints;
	uint8_t pad;
} __attribute__((packed)) htc_ready_msg_t;

/** 
 * HTC service message structure 
 */
typedef struct {
	uint16_t message_id;		/**< Big Endian value! */
	uint16_t service_id;		/**< Big Endian value! */
	uint16_t connection_flags;	/**< Big Endian value! */
	
	uint8_t download_pipe_id;
	uint8_t upload_pipe_id;
	uint8_t service_meta_length;
	uint8_t pad;
} __attribute__((packed)) htc_service_msg_t;

/** 
 * HTC service response message structure 
 */
typedef struct {
        uint16_t message_id;            /**< Big Endian value! */
        uint16_t service_id;            /**< Big Endian value! */
        uint8_t status;
        uint8_t endpoint_id;
        uint16_t max_message_length;    /**< Big Endian value! */
        uint8_t service_meta_length;
        uint8_t pad;
} __attribute__((packed)) htc_service_resp_msg_t;

/**
 * HTC credits config message structure
 */
typedef struct {
        uint16_t message_id;            /**< Big Endian value! */
        uint8_t pipe_id;
        uint8_t credits;
} __attribute__((packed)) htc_config_msg_t;

/**
 * HTC setup complete message structure
 */
typedef struct {
        uint16_t message_id;            /**< Big Endian value! */
} __attribute__((packed)) htc_setup_complete_msg_t;

extern int htc_device_init(ath_t *ath_device, htc_device_t *htc_device);
extern int htc_init(htc_device_t* htc_device);
extern int htc_read_message(htc_device_t *htc_device, void *buffer, 
	size_t buffer_size, size_t *transferred_size);
extern int htc_send_message(htc_device_t *htc_device, void *buffer, 
	size_t buffer_size, uint8_t endpoint_id);

#endif	/* HTC_H */

