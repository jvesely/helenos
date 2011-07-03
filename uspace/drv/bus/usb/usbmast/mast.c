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

/** @addtogroup drvusbmast
 * @{
 */
/**
 * @file
 * Generic functions for USB mass storage (implementation).
 */
#include "mast.h"
#include "cmds.h"
#include <bool.h>
#include <errno.h>
#include <str_error.h>
#include <usb/debug.h>
#include <usb/dev/request.h>

bool usb_mast_verbose = true;

#define MASTLOG(format, ...) \
	do { \
		if (usb_mast_verbose) { \
			usb_log_debug("USB cl08: " format, ##__VA_ARGS__); \
		} \
	} while (false)

/** Request data from mass storage device.
 *
 * @param tag Command block wrapper tag (automatically compared with answer).
 * @param lun LUN index.
 * @param cmd SCSI command buffer (in SCSI endianness).
 * @param cmd_size Length of SCSI command @p cmd in bytes.
 * @param in_buffer Buffer where to store the answer (CSW is not returned).
 * @param in_buffer_size Size of the buffer (size of the request to the device).
 * @param received_size Number of actually received bytes.
 * @return Error code.
 */
int usb_massstor_data_in(usb_device_t *dev,
    uint32_t tag, uint8_t lun, void *cmd, size_t cmd_size,
    void *in_buffer, size_t in_buffer_size, size_t *received_size)
{
	int rc;
	size_t act_size;
	usb_pipe_t *bulk_in_pipe = dev->pipes[BULK_IN_EP].pipe;
	usb_pipe_t *bulk_out_pipe = dev->pipes[BULK_OUT_EP].pipe;

	/* Prepare CBW - command block wrapper */
	usb_massstor_cbw_t cbw;
	usb_massstor_cbw_prepare(&cbw, tag, in_buffer_size,
	    USB_DIRECTION_IN, lun, cmd_size, cmd);

	/* First, send the CBW. */
	rc = usb_pipe_write(bulk_out_pipe, &cbw, sizeof(cbw));
	MASTLOG("CBW '%s' sent: %s.\n",
	    usb_debug_str_buffer((uint8_t *) &cbw, sizeof(cbw), 0),
	    str_error(rc));
	if (rc != EOK) {
		return rc;
	}

	/* Try to retrieve the data from the device. */
	act_size = 0;
	rc = usb_pipe_read(bulk_in_pipe, in_buffer, in_buffer_size, &act_size);
	MASTLOG("Received %zuB (%s): %s.\n", act_size,
	    usb_debug_str_buffer((uint8_t *) in_buffer, act_size, 0),
	    str_error(rc));
	if (rc != EOK) {
		return rc;
	}

	/* Read CSW. */
	usb_massstor_csw_t csw;
	size_t csw_size;
	rc = usb_pipe_read(bulk_in_pipe, &csw, sizeof(csw), &csw_size);
	MASTLOG("CSW '%s' received (%zuB): %s.\n",
	    usb_debug_str_buffer((uint8_t *) &csw, csw_size, 0), csw_size,
	    str_error(rc));
	if (rc != EOK) {
		return rc;
	}
	if (csw_size != sizeof(csw)) {
		return ERANGE;
	}

	if (csw.dCSWTag != tag) {
		return EBADCHECKSUM;
	}

	/*
	 * Determine the actual return value from the CSW.
	 */
	if (csw.dCSWStatus != 0) {
		// FIXME: better error code
		// FIXME: distinguish 0x01 and 0x02
		return EXDEV;
	}

	size_t residue = (size_t) uint32_usb2host(csw.dCSWDataResidue);
	if (residue > in_buffer_size) {
		return ERANGE;
	}
	if (act_size != in_buffer_size - residue) {
		return ERANGE;
	}
	if (received_size != NULL) {
		*received_size = in_buffer_size - residue;
	}

	return EOK;
}

/** Perform bulk-only mass storage reset.
 *
 * @param dev Device to be reseted.
 * @return Error code.
 */
int usb_massstor_reset(usb_device_t *dev)
{
	return usb_control_request_set(&dev->ctrl_pipe,
	    USB_REQUEST_TYPE_CLASS, USB_REQUEST_RECIPIENT_INTERFACE,
	    0xFF, 0, dev->interface_no, NULL, 0);
}

/** Perform complete reset recovery of bulk-only mass storage.
 *
 * Notice that no error is reported because if this fails, the error
 * would reappear on next transaction somehow.
 *
 * @param dev Device to be reseted.
 */
void usb_massstor_reset_recovery(usb_device_t *dev)
{
	/* We would ignore errors here because if this fails
	 * we are doomed anyway and any following transaction would fail.
	 */
	usb_massstor_reset(dev);
	usb_pipe_clear_halt(&dev->ctrl_pipe, dev->pipes[BULK_IN_EP].pipe);
	usb_pipe_clear_halt(&dev->ctrl_pipe, dev->pipes[BULK_OUT_EP].pipe);
}

/** Get max LUN of a mass storage device.
 *
 * @see usb_masstor_get_lun_count
 *
 * @warning Error from this command does not necessarily indicate malfunction
 * of the device. Device does not need to support this request.
 * You shall rather use usb_masstor_get_lun_count.
 *
 * @param dev Mass storage device.
 * @return Error code of maximum LUN (index, not count).
 */
int usb_massstor_get_max_lun(usb_device_t *dev)
{
	uint8_t max_lun;
	size_t data_recv_len;
	int rc = usb_control_request_get(&dev->ctrl_pipe,
	    USB_REQUEST_TYPE_CLASS, USB_REQUEST_RECIPIENT_INTERFACE,
	    0xFE, 0, dev->interface_no, &max_lun, 1, &data_recv_len);
	if (rc != EOK) {
		return rc;
	}
	if (data_recv_len != 1) {
		return EEMPTY;
	}
	return (int) max_lun;
}

/** Get number of LUNs supported by mass storage device.
 *
 * @warning This function hides any error during the request
 * (typically that shall not be a problem).
 *
 * @param dev Mass storage device.
 * @return Number of LUNs.
 */
size_t usb_masstor_get_lun_count(usb_device_t *dev)
{
	int max_lun = usb_massstor_get_max_lun(dev);
	if (max_lun < 0) {
		max_lun = 1;
	} else {
		max_lun++;
	}

	return (size_t) max_lun;
}

/**
 * @}
 */
