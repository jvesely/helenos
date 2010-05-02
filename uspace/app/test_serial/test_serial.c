/*
 * Copyright (c) 2009 Lenka Trochtova
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

/** @addtogroup test_serial
 * @brief	test the serial port driver 
 * @{
 */ 
/**
 * @file
 */

#include <stdio.h>
#include <ipc/ipc.h>
#include <sys/types.h>
#include <atomic.h>
#include <ipc/devmap.h>
#include <async.h>
#include <ipc/services.h>
#include <ipc/serial.h>
#include <as.h>
#include <sysinfo.h>
#include <errno.h>


#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <string.h>

#define NAME 		"test serial"

/*
static int device_get_handle(const char *name, dev_handle_t *handle);
static void print_usage();
static void move_shutters(const char * serial_dev_name, char room, char cmd);
static char getcmd(bool wnd, bool up);
static bool is_com_dev(const char *dev_name);

static int device_get_handle(const char *name, dev_handle_t *handle)
{
	int phone = ipc_connect_me_to(PHONE_NS, SERVICE_DEVMAP, DEVMAP_CLIENT, 0);
	if (phone < 0)
		return phone;
	
	ipc_call_t answer;
	aid_t req = async_send_2(phone, DEVMAP_DEVICE_GET_HANDLE, 0, 0, &answer);
	
	ipcarg_t retval = ipc_data_write_start(phone, name, str_length(name) + 1); 

	if (retval != EOK) {
		async_wait_for(req, NULL);
		ipc_hangup(phone);
		return retval;
	}

	async_wait_for(req, &retval);

	if (handle != NULL)
		*handle = -1;
	
	if (retval == EOK) {
		if (handle != NULL)
			*handle = (dev_handle_t) IPC_GET_ARG1(answer);
	}
	
	ipc_hangup(phone);
	return retval;
}

static void print_usage()
{
	printf("Usage: \n test_serial comN count \n where 'comN' is a serial port and count is a number of characters to be read\n");	
}


// The name of a serial device must be between 'com0' and 'com9'.
static bool is_com_dev(const char *dev_name) 
{
	if (str_length(dev_name) != 4) {
		return false;
	}
	
	if (str_cmp("com0", dev_name) > 0) {
		return false;
	}
	
	if (str_cmp(dev_name, "com9") > 0) {
		return false;
	}
	
	return true;
}


int main(int argc, char *argv[])
{
	if (argc != 3) {
		printf(NAME ": incorrect number of arguments.\n");
		print_usage();
		return 0;		
	}	

	
	const char *serial_dev_name = argv[1];
	long int cnt = strtol(argv[2], NULL, 10);
	
	if (!is_com_dev(serial_dev_name)) {
		printf(NAME ": the first argument is not correct.\n");
		print_usage();
		return 0;	
	}
	
	dev_handle_t serial_dev_handle = -1;
	
	if (device_get_handle(serial_dev_name, &serial_dev_handle) !=  EOK) {
		printf(NAME ": could not get the handle of %s.\n", serial_dev_name);
		return;
	}
	
	printf(NAME ": got the handle of %s.\n", serial_dev_name);
	
	int dev_phone = ipc_connect_me_to(PHONE_NS, SERVICE_DEVMAP, DEVMAP_CONNECT_TO_DEVICE, serial_dev_handle);
	if(dev_phone < 0) {
		printf(NAME ": could not connect to %s device.\n", serial_dev_name);
		return;
	}
	
	printf("The application will read %d characters from %s: \n", cnt, serial_dev_name);
	
	int i, c;
	for (i = 0; i < cnt; i++) {
		async_req_0_1(dev_phone, SERIAL_GETCHAR, &c);
		printf("%c", (char)c);
	}
	
	return 0;
}*/


#include <ipc/devman.h>
#include <devman.h>
#include <device/char.h>


static void print_usage()
{
	printf("Usage: \n test_serial count \n where count is a number of characters to be read\n");	
}


int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf(NAME ": incorrect number of arguments.\n");
		print_usage();
		return 0;		
	}
	
	long int cnt = strtol(argv[1], NULL, 10);
	
	int res;
	res = devman_get_phone(DEVMAN_CLIENT, IPC_FLAG_BLOCKING);
	device_handle_t handle;
	
	if (EOK != (res = devman_device_get_handle("/hw/pci0/00:01.0/com1", &handle, IPC_FLAG_BLOCKING))) {
		printf(NAME ": could not get the device handle, errno = %d.\n", -res);
		return 1;
	}
	
	printf(NAME ": device handle is %d.\n", handle);	
	
	int phone;
	if (0 >= (phone = devman_device_connect(handle, IPC_FLAG_BLOCKING))) {
		printf(NAME ": could not connect to the device, errno = %d.\n", -res);
		devman_hangup_phone(DEVMAN_CLIENT);		
		return 2;
	}
	
	char *buf = (char *)malloc(cnt+1);
	if (NULL == buf) {
		printf(NAME ": failed to allocate the input buffer\n");
		ipc_hangup(phone);
		devman_hangup_phone(DEVMAN_CLIENT);
		return 3;
	}
	
	int read = read_dev(phone, buf, cnt);
	if (0 > read) {
		printf(NAME ": failed read from device, errno = %d.\n", -read);
		ipc_hangup(phone);
		devman_hangup_phone(DEVMAN_CLIENT);
		return 4;
	}
	
	buf[cnt+1] = 0;
	printf(NAME ": read data: '%s'.", buf);
	
	devman_hangup_phone(DEVMAN_CLIENT);
	ipc_hangup(phone);
	
	return 0;
}


/** @}
 */
