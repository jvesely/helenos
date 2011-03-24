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

/** @addtogroup libusb
 * @{
 */
/** @file
 * USB HID report parser initialization from descriptors.
 */

#ifndef LIBUSB_HIDREPORT_H_
#define LIBUSB_HIDREPORT_H_

#include <usb/devdrv.h>
#include <usb/classes/hidparser.h>

/**
 * Retrieves the Report descriptor from the USB device and initializes the
 * report parser.
 *
 * \param dev USB device representing a HID device.
 * \param parser HID Report parser.
 *
 * \retval EOK if successful.
 * \retval EINVAL if one of the parameters is not given (is NULL).
 * \retval ENOENT if there are some descriptors missing.
 * \retval ENOMEM if an error with allocation occured.
 * \retval EINVAL if the Report descriptor's size does not match the size 
 *         from the interface descriptor.
 * \return Other value inherited from function usb_pipe_start_session(),
 *         usb_pipe_end_session() or usb_request_get_descriptor().
 */
int usb_hid_process_report_descriptor(usb_device_t *dev, 
    usb_hid_report_parser_t *parser);

#endif /* LIBUSB_HIDREPORT_H_ */

/**
 * @}
 */
