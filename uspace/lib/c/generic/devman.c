/*
 * Copyright (c) 2007 Josef Cejka
 * Copyright (c) 2009 Jiri Svoboda
 * Copyright (c) 2010 Lenka Trochtova
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

/** @addtogroup libc
 * @{
 */
/** @file
 */

#include <str.h>
#include <stdio.h>
#include <ipc/services.h>
#include <ipc/devman.h>
#include <devman.h>
#include <async.h>
#include <fibril_synch.h>
#include <errno.h>
#include <malloc.h>
#include <bool.h>
#include <adt/list.h>

static int devman_phone_driver = -1;
static int devman_phone_client = -1;

static FIBRIL_MUTEX_INITIALIZE(devman_phone_mutex);

int devman_get_phone(devman_interface_t iface, unsigned int flags)
{
	switch (iface) {
	case DEVMAN_DRIVER:
		fibril_mutex_lock(&devman_phone_mutex);
		if (devman_phone_driver >= 0) {
			fibril_mutex_unlock(&devman_phone_mutex);
			return devman_phone_driver;
		}
		
		if (flags & IPC_FLAG_BLOCKING)
			devman_phone_driver = async_connect_me_to_blocking(
			    PHONE_NS, SERVICE_DEVMAN, DEVMAN_DRIVER, 0);
		else
			devman_phone_driver = async_connect_me_to(PHONE_NS,
			    SERVICE_DEVMAN, DEVMAN_DRIVER, 0);
		
		fibril_mutex_unlock(&devman_phone_mutex);
		return devman_phone_driver;
	case DEVMAN_CLIENT:
		fibril_mutex_lock(&devman_phone_mutex);
		if (devman_phone_client >= 0) {
			fibril_mutex_unlock(&devman_phone_mutex);
			return devman_phone_client;
		}
		
		if (flags & IPC_FLAG_BLOCKING) {
			devman_phone_client = async_connect_me_to_blocking(
			    PHONE_NS, SERVICE_DEVMAN, DEVMAN_CLIENT, 0);
		} else {
			devman_phone_client = async_connect_me_to(PHONE_NS,
			    SERVICE_DEVMAN, DEVMAN_CLIENT, 0);
		}
		
		fibril_mutex_unlock(&devman_phone_mutex);
		return devman_phone_client;
	default:
		return -1;
	}
}

/** Register running driver with device manager. */
int devman_driver_register(const char *name, async_client_conn_t conn)
{
	int phone = devman_get_phone(DEVMAN_DRIVER, IPC_FLAG_BLOCKING);
	
	if (phone < 0)
		return phone;
	
	async_serialize_start();
	
	ipc_call_t answer;
	aid_t req = async_send_2(phone, DEVMAN_DRIVER_REGISTER, 0, 0, &answer);
	
	sysarg_t retval = async_data_write_start(phone, name, str_size(name));
	if (retval != EOK) {
		async_wait_for(req, NULL);
		async_serialize_end();
		return -1;
	}
	
	async_set_client_connection(conn);
	
	async_connect_to_me(phone, 0, 0, 0, NULL);
	async_wait_for(req, &retval);
	
	async_serialize_end();
	
	return retval;
}

static int devman_send_match_id(int phone, match_id_t *match_id)
{
	ipc_call_t answer;

	aid_t req = async_send_1(phone, DEVMAN_ADD_MATCH_ID, match_id->score,
	    &answer);
	int retval = async_data_write_start(phone, match_id->id,
	    str_size(match_id->id));

	async_wait_for(req, NULL);
	return retval;
}


static int devman_send_match_ids(int phone, match_id_list_t *match_ids)
{
	link_t *link = match_ids->ids.next;
	match_id_t *match_id = NULL;
	int ret = EOK;

	while (link != &match_ids->ids) {
		match_id = list_get_instance(link, match_id_t, link); 
		ret = devman_send_match_id(phone, match_id);
		if (ret != EOK) {
			return ret;
		}

		link = link->next;
	}

	return ret;
}

/** Add function to a device.
 *
 * Request devman to add a new function to the specified device owned by
 * this driver task.
 *
 * @param name		Name of the new function
 * @param ftype		Function type, fun_inner or fun_exposed
 * @param match_ids	Match IDs (should be empty for fun_exposed)
 * @param devh		Devman handle of the device
 * @param funh		Place to store handle of the new function
 *
 * @return		EOK on success or negative error code.
 */
int devman_add_function(const char *name, fun_type_t ftype,
    match_id_list_t *match_ids, devman_handle_t devh, devman_handle_t *funh)
{
	int phone = devman_get_phone(DEVMAN_DRIVER, IPC_FLAG_BLOCKING);
	int fun_handle;
	
	if (phone < 0)
		return phone;
	
	async_serialize_start();
	
	int match_count = list_count(&match_ids->ids);
	ipc_call_t answer;

	aid_t req = async_send_3(phone, DEVMAN_ADD_FUNCTION, (sysarg_t) ftype,
	    devh, match_count, &answer);

	sysarg_t retval = async_data_write_start(phone, name, str_size(name));
	if (retval != EOK) {
		async_wait_for(req, NULL);
		async_serialize_end();
		return retval;
	}
	
	int match_ids_rc = devman_send_match_ids(phone, match_ids);
	
	async_wait_for(req, &retval);
	
	async_serialize_end();
	
	/* Prefer the answer to DEVMAN_ADD_FUNCTION in case of errors. */
	if ((match_ids_rc != EOK) && (retval == EOK)) {
		retval = match_ids_rc;
	}

	if (retval == EOK)
		fun_handle = (int) IPC_GET_ARG1(answer);
	else
		fun_handle = -1;
	
	*funh = fun_handle;

	return retval;
}

int devman_add_device_to_class(devman_handle_t devman_handle,
    const char *class_name)
{
	int phone = devman_get_phone(DEVMAN_DRIVER, IPC_FLAG_BLOCKING);
	
	if (phone < 0)
		return phone;
	
	async_serialize_start();
	ipc_call_t answer;
	aid_t req = async_send_1(phone, DEVMAN_ADD_DEVICE_TO_CLASS,
	    devman_handle, &answer);
	
	sysarg_t retval = async_data_write_start(phone, class_name,
	    str_size(class_name));
	if (retval != EOK) {
		async_wait_for(req, NULL);
		async_serialize_end();
		return retval;
	}
	
	async_wait_for(req, &retval);
	async_serialize_end();
	
	return retval;
}

void devman_hangup_phone(devman_interface_t iface)
{
	switch (iface) {
	case DEVMAN_DRIVER:
		if (devman_phone_driver >= 0) {
			async_hangup(devman_phone_driver);
			devman_phone_driver = -1;
		}
		break;
	case DEVMAN_CLIENT:
		if (devman_phone_client >= 0) {
			async_hangup(devman_phone_client);
			devman_phone_client = -1;
		}
		break;
	default:
		break;
	}
}

int devman_device_connect(devman_handle_t handle, unsigned int flags)
{
	int phone;
	
	if (flags & IPC_FLAG_BLOCKING) {
		phone = async_connect_me_to_blocking(PHONE_NS, SERVICE_DEVMAN,
		    DEVMAN_CONNECT_TO_DEVICE, handle);
	} else {
		phone = async_connect_me_to(PHONE_NS, SERVICE_DEVMAN,
		    DEVMAN_CONNECT_TO_DEVICE, handle);
	}
	
	return phone;
}

int devman_parent_device_connect(devman_handle_t handle, unsigned int flags)
{
	int phone;
	
	if (flags & IPC_FLAG_BLOCKING) {
		phone = async_connect_me_to_blocking(PHONE_NS, SERVICE_DEVMAN,
		    DEVMAN_CONNECT_TO_PARENTS_DEVICE, handle);
	} else {
		phone = async_connect_me_to(PHONE_NS, SERVICE_DEVMAN,
		    DEVMAN_CONNECT_TO_PARENTS_DEVICE, handle);
	}
	
	return phone;
}

int devman_device_get_handle(const char *pathname, devman_handle_t *handle,
    unsigned int flags)
{
	int phone = devman_get_phone(DEVMAN_CLIENT, flags);
	
	if (phone < 0)
		return phone;
	
	async_serialize_start();
	
	ipc_call_t answer;
	aid_t req = async_send_2(phone, DEVMAN_DEVICE_GET_HANDLE, flags, 0,
	    &answer);
	
	sysarg_t retval = async_data_write_start(phone, pathname,
	    str_size(pathname));
	if (retval != EOK) {
		async_wait_for(req, NULL);
		async_serialize_end();
		return retval;
	}
	
	async_wait_for(req, &retval);
	
	async_serialize_end();
	
	if (retval != EOK) {
		if (handle != NULL)
			*handle = (devman_handle_t) -1;
		return retval;
	}
	
	if (handle != NULL)
		*handle = (devman_handle_t) IPC_GET_ARG1(answer);
	
	return retval;
}

int devman_device_get_handle_by_class(const char *classname,
    const char *devname, devman_handle_t *handle, unsigned int flags)
{
	int phone = devman_get_phone(DEVMAN_CLIENT, flags);

	if (phone < 0)
		return phone;

	async_serialize_start();

	ipc_call_t answer;
	aid_t req = async_send_1(phone, DEVMAN_DEVICE_GET_HANDLE_BY_CLASS,
	    flags, &answer);

	sysarg_t retval = async_data_write_start(phone, classname,
	    str_size(classname));
	if (retval != EOK) {
		async_wait_for(req, NULL);
		async_serialize_end();
		return retval;
	}
	retval = async_data_write_start(phone, devname,
	    str_size(devname));
	if (retval != EOK) {
		async_wait_for(req, NULL);
		async_serialize_end();
		return retval;
	}

	async_wait_for(req, &retval);

	async_serialize_end();

	if (retval != EOK) {
		if (handle != NULL)
			*handle = (devman_handle_t) -1;
		return retval;
	}

	if (handle != NULL)
		*handle = (devman_handle_t) IPC_GET_ARG1(answer);

	return retval;
}

int devman_get_device_path(devman_handle_t handle, char *path, size_t path_size)
{
	int phone = devman_get_phone(DEVMAN_CLIENT, 0);

	if (phone < 0)
		return phone;

	async_serialize_start();

	ipc_call_t answer;
	aid_t req = async_send_1(phone, DEVMAN_DEVICE_GET_DEVICE_PATH,
	    handle, &answer);

	ipc_call_t data_request_call;
	aid_t data_request = async_data_read(phone, path, path_size,
	    &data_request_call);
	if (data_request == 0) {
		async_wait_for(req, NULL);
		async_serialize_end();
		return ENOMEM;
	}

	sysarg_t data_request_rc;
	sysarg_t opening_request_rc;
	async_wait_for(data_request, &data_request_rc);
	async_wait_for(req, &opening_request_rc);

	async_serialize_end();

	if (data_request_rc != EOK) {
		/* Prefer the return code of the opening request. */
		if (opening_request_rc != EOK) {
			return (int) opening_request_rc;
		} else {
			return (int) data_request_rc;
		}
	}
	if (opening_request_rc != EOK) {
		return (int) opening_request_rc;
	}

	/* To be on the safe-side. */
	path[path_size - 1] = 0;

	size_t transferred_size = IPC_GET_ARG2(data_request_call);

	if (transferred_size >= path_size) {
		return ELIMIT;
	}

	/* Terminate the string (trailing 0 not send over IPC). */
	path[transferred_size] = 0;

	return EOK;
}


/** @}
 */
