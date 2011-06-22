/*
 * Copyright (c) 2007 Josef Cejka
 * Copyright (c) 2009 Jiri Svoboda
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

#include <str.h>
#include <ipc/services.h>
#include <ns.h>
#include <ipc/loc.h>
#include <loc.h>
#include <fibril_synch.h>
#include <async.h>
#include <errno.h>
#include <malloc.h>
#include <bool.h>

static FIBRIL_MUTEX_INITIALIZE(loc_supp_block_mutex);
static FIBRIL_MUTEX_INITIALIZE(loc_cons_block_mutex);

static FIBRIL_MUTEX_INITIALIZE(loc_supplier_mutex);
static FIBRIL_MUTEX_INITIALIZE(loc_consumer_mutex);

static async_sess_t *loc_supp_block_sess = NULL;
static async_sess_t *loc_cons_block_sess = NULL;

static async_sess_t *loc_supplier_sess = NULL;
static async_sess_t *loc_consumer_sess = NULL;

static void clone_session(fibril_mutex_t *mtx, async_sess_t *src,
    async_sess_t **dst)
{
	fibril_mutex_lock(mtx);
	
	if ((*dst == NULL) && (src != NULL))
		*dst = src;
	
	fibril_mutex_unlock(mtx);
}

/** Start an async exchange on the loc session (blocking).
 *
 * @param iface Location service interface to choose
 *
 * @return New exchange.
 *
 */
async_exch_t *loc_exchange_begin_blocking(loc_interface_t iface)
{
	switch (iface) {
	case LOC_PORT_SUPPLIER:
		fibril_mutex_lock(&loc_supp_block_mutex);
		
		while (loc_supp_block_sess == NULL) {
			clone_session(&loc_supplier_mutex, loc_supplier_sess,
			    &loc_supp_block_sess);
			
			if (loc_supp_block_sess == NULL)
				loc_supp_block_sess =
				    service_connect_blocking(EXCHANGE_SERIALIZE,
				    SERVICE_LOC, LOC_PORT_SUPPLIER, 0);
		}
		
		fibril_mutex_unlock(&loc_supp_block_mutex);
		
		clone_session(&loc_supplier_mutex, loc_supp_block_sess,
		    &loc_supplier_sess);
		
		return async_exchange_begin(loc_supp_block_sess);
	case LOC_PORT_CONSUMER:
		fibril_mutex_lock(&loc_cons_block_mutex);
		
		while (loc_cons_block_sess == NULL) {
			clone_session(&loc_consumer_mutex, loc_consumer_sess,
			    &loc_cons_block_sess);
			
			if (loc_cons_block_sess == NULL)
				loc_cons_block_sess =
				    service_connect_blocking(EXCHANGE_SERIALIZE,
				    SERVICE_LOC, LOC_PORT_CONSUMER, 0);
		}
		
		fibril_mutex_unlock(&loc_cons_block_mutex);
		
		clone_session(&loc_consumer_mutex, loc_cons_block_sess,
		    &loc_consumer_sess);
		
		return async_exchange_begin(loc_cons_block_sess);
	default:
		return NULL;
	}
}

/** Start an async exchange on the loc session.
 *
 * @param iface Location service interface to choose
 *
 * @return New exchange.
 *
 */
async_exch_t *loc_exchange_begin(loc_interface_t iface)
{
	switch (iface) {
	case LOC_PORT_SUPPLIER:
		fibril_mutex_lock(&loc_supplier_mutex);
		
		if (loc_supplier_sess == NULL)
			loc_supplier_sess =
			    service_connect(EXCHANGE_SERIALIZE, SERVICE_LOC,
			    LOC_PORT_SUPPLIER, 0);
		
		fibril_mutex_unlock(&loc_supplier_mutex);
		
		if (loc_supplier_sess == NULL)
			return NULL;
		
		return async_exchange_begin(loc_supplier_sess);
	case LOC_PORT_CONSUMER:
		fibril_mutex_lock(&loc_consumer_mutex);
		
		if (loc_consumer_sess == NULL)
			loc_consumer_sess =
			    service_connect(EXCHANGE_SERIALIZE, SERVICE_LOC,
			    LOC_PORT_CONSUMER, 0);
		
		fibril_mutex_unlock(&loc_consumer_mutex);
		
		if (loc_consumer_sess == NULL)
			return NULL;
		
		return async_exchange_begin(loc_consumer_sess);
	default:
		return NULL;
	}
}

/** Finish an async exchange on the loc session.
 *
 * @param exch Exchange to be finished.
 *
 */
void loc_exchange_end(async_exch_t *exch)
{
	async_exchange_end(exch);
}

/** Register new driver with loc. */
int loc_server_register(const char *name, async_client_conn_t conn)
{
	async_exch_t *exch = loc_exchange_begin_blocking(LOC_PORT_SUPPLIER);
	
	ipc_call_t answer;
	aid_t req = async_send_2(exch, LOC_SERVER_REGISTER, 0, 0, &answer);
	sysarg_t retval = async_data_write_start(exch, name, str_size(name));
	
	loc_exchange_end(exch);
	
	if (retval != EOK) {
		async_wait_for(req, NULL);
		return retval;
	}
	
	async_set_client_connection(conn);
	
	exch = loc_exchange_begin(LOC_PORT_SUPPLIER);
	async_connect_to_me(exch, 0, 0, 0, NULL, NULL);
	loc_exchange_end(exch);
	
	async_wait_for(req, &retval);
	return retval;
}

/** Register new device.
 *
 * The @p interface is used when forwarding connection to the driver.
 * If not 0, the first argument is the interface and the second argument
 * is the service ID.
 *
 * When the interface is zero (default), the first argument is directly
 * the handle (to ensure backward compatibility).
 *
 * @param      fqdn      Fully qualified device name.
 * @param[out] handle    Handle to the created instance of device.
 * @param      interface Interface when forwarding.
 *
 */
int loc_service_register_with_iface(const char *fqdn,
    service_id_t *handle, sysarg_t interface)
{
	async_exch_t *exch = loc_exchange_begin_blocking(LOC_PORT_SUPPLIER);
	
	ipc_call_t answer;
	aid_t req = async_send_2(exch, LOC_SERVICE_REGISTER, interface, 0,
	    &answer);
	sysarg_t retval = async_data_write_start(exch, fqdn, str_size(fqdn));
	
	loc_exchange_end(exch);
	
	if (retval != EOK) {
		async_wait_for(req, NULL);
		return retval;
	}
	
	async_wait_for(req, &retval);
	
	if (retval != EOK) {
		if (handle != NULL)
			*handle = -1;
		
		return retval;
	}
	
	if (handle != NULL)
		*handle = (service_id_t) IPC_GET_ARG1(answer);
	
	return retval;
}

/** Register new device.
 *
 * @param fqdn   Fully qualified device name.
 * @param handle Output: Handle to the created instance of device.
 *
 */
int loc_service_register(const char *fqdn, service_id_t *handle)
{
	return loc_service_register_with_iface(fqdn, handle, 0);
}

int loc_service_get_id(const char *fqdn, service_id_t *handle,
    unsigned int flags)
{
	async_exch_t *exch;
	
	if (flags & IPC_FLAG_BLOCKING)
		exch = loc_exchange_begin_blocking(LOC_PORT_CONSUMER);
	else {
		exch = loc_exchange_begin(LOC_PORT_CONSUMER);
		if (exch == NULL)
			return errno;
	}
	
	ipc_call_t answer;
	aid_t req = async_send_2(exch, LOC_SERVICE_GET_ID, flags, 0,
	    &answer);
	sysarg_t retval = async_data_write_start(exch, fqdn, str_size(fqdn));
	
	loc_exchange_end(exch);
	
	if (retval != EOK) {
		async_wait_for(req, NULL);
		return retval;
	}
	
	async_wait_for(req, &retval);
	
	if (retval != EOK) {
		if (handle != NULL)
			*handle = (service_id_t) -1;
		
		return retval;
	}
	
	if (handle != NULL)
		*handle = (service_id_t) IPC_GET_ARG1(answer);
	
	return retval;
}

int loc_namespace_get_id(const char *name, service_id_t *handle,
    unsigned int flags)
{
	async_exch_t *exch;
	
	if (flags & IPC_FLAG_BLOCKING)
		exch = loc_exchange_begin_blocking(LOC_PORT_CONSUMER);
	else {
		exch = loc_exchange_begin(LOC_PORT_CONSUMER);
		if (exch == NULL)
			return errno;
	}
	
	ipc_call_t answer;
	aid_t req = async_send_2(exch, LOC_NAMESPACE_GET_ID, flags, 0,
	    &answer);
	sysarg_t retval = async_data_write_start(exch, name, str_size(name));
	
	loc_exchange_end(exch);
	
	if (retval != EOK) {
		async_wait_for(req, NULL);
		return retval;
	}
	
	async_wait_for(req, &retval);
	
	if (retval != EOK) {
		if (handle != NULL)
			*handle = (service_id_t) -1;
		
		return retval;
	}
	
	if (handle != NULL)
		*handle = (service_id_t) IPC_GET_ARG1(answer);
	
	return retval;
}

loc_object_type_t loc_id_probe(service_id_t handle)
{
	async_exch_t *exch = loc_exchange_begin_blocking(LOC_PORT_CONSUMER);
	
	sysarg_t type;
	int retval = async_req_1_1(exch, LOC_ID_PROBE, handle, &type);
	
	loc_exchange_end(exch);
	
	if (retval != EOK)
		return LOC_OBJECT_NONE;
	
	return (loc_object_type_t) type;
}

async_sess_t *loc_service_connect(exch_mgmt_t mgmt, service_id_t handle,
    unsigned int flags)
{
	async_sess_t *sess;
	
	if (flags & IPC_FLAG_BLOCKING)
		sess = service_connect_blocking(mgmt, SERVICE_LOC,
		    LOC_CONNECT_TO_SERVICE, handle);
	else
		sess = service_connect(mgmt, SERVICE_LOC,
		    LOC_CONNECT_TO_SERVICE, handle);
	
	return sess;
}

int loc_null_create(void)
{
	async_exch_t *exch = loc_exchange_begin_blocking(LOC_PORT_CONSUMER);
	
	sysarg_t null_id;
	int retval = async_req_0_1(exch, LOC_NULL_CREATE, &null_id);
	
	loc_exchange_end(exch);
	
	if (retval != EOK)
		return -1;
	
	return (int) null_id;
}

void loc_null_destroy(int null_id)
{
	async_exch_t *exch = loc_exchange_begin_blocking(LOC_PORT_CONSUMER);
	async_req_1_0(exch, LOC_NULL_DESTROY, (sysarg_t) null_id);
	loc_exchange_end(exch);
}

static size_t loc_count_namespaces_internal(async_exch_t *exch)
{
	sysarg_t count;
	int retval = async_req_0_1(exch, LOC_GET_NAMESPACE_COUNT, &count);
	if (retval != EOK)
		return 0;
	
	return count;
}

static size_t loc_count_services_internal(async_exch_t *exch,
    service_id_t ns_handle)
{
	sysarg_t count;
	int retval = async_req_1_1(exch, LOC_GET_SERVICE_COUNT, ns_handle,
	    &count);
	if (retval != EOK)
		return 0;
	
	return count;
}

size_t loc_count_namespaces(void)
{
	async_exch_t *exch = loc_exchange_begin_blocking(LOC_PORT_CONSUMER);
	size_t size = loc_count_namespaces_internal(exch);
	loc_exchange_end(exch);
	
	return size;
}

size_t loc_count_services(service_id_t ns_handle)
{
	async_exch_t *exch = loc_exchange_begin_blocking(LOC_PORT_CONSUMER);
	size_t size = loc_count_services_internal(exch, ns_handle);
	loc_exchange_end(exch);
	
	return size;
}

size_t loc_get_namespaces(loc_sdesc_t **data)
{
	/* Loop until namespaces read succesful */
	while (true) {
		async_exch_t *exch = loc_exchange_begin_blocking(LOC_PORT_CONSUMER);
		size_t count = loc_count_namespaces_internal(exch);
		loc_exchange_end(exch);
		
		if (count == 0)
			return 0;
		
		loc_sdesc_t *devs = (loc_sdesc_t *) calloc(count, sizeof(loc_sdesc_t));
		if (devs == NULL)
			return 0;
		
		exch = loc_exchange_begin(LOC_PORT_CONSUMER);
		
		ipc_call_t answer;
		aid_t req = async_send_0(exch, LOC_GET_NAMESPACES, &answer);
		int rc = async_data_read_start(exch, devs, count * sizeof(loc_sdesc_t));
		
		loc_exchange_end(exch);
		
		if (rc == EOVERFLOW) {
			/*
			 * Number of namespaces has changed since
			 * the last call of LOC_GET_NAMESPACE_COUNT
			 */
			free(devs);
			continue;
		}
		
		if (rc != EOK) {
			async_wait_for(req, NULL);
			free(devs);
			return 0;
		}
		
		sysarg_t retval;
		async_wait_for(req, &retval);
		
		if (retval != EOK)
			return 0;
		
		*data = devs;
		return count;
	}
}

size_t loc_get_services(service_id_t ns_handle, loc_sdesc_t **data)
{
	/* Loop until devices read succesful */
	while (true) {
		async_exch_t *exch = loc_exchange_begin_blocking(LOC_PORT_CONSUMER);
		size_t count = loc_count_services_internal(exch, ns_handle);
		loc_exchange_end(exch);
		
		if (count == 0)
			return 0;
		
		loc_sdesc_t *devs = (loc_sdesc_t *) calloc(count, sizeof(loc_sdesc_t));
		if (devs == NULL)
			return 0;
		
		exch = loc_exchange_begin(LOC_PORT_CONSUMER);
		
		ipc_call_t answer;
		aid_t req = async_send_1(exch, LOC_GET_SERVICES, ns_handle, &answer);
		int rc = async_data_read_start(exch, devs, count * sizeof(loc_sdesc_t));
		
		loc_exchange_end(exch);
		
		if (rc == EOVERFLOW) {
			/*
			 * Number of services has changed since
			 * the last call of LOC_GET_SERVICE_COUNT
			 */
			free(devs);
			continue;
		}
		
		if (rc != EOK) {
			async_wait_for(req, NULL);
			free(devs);
			return 0;
		}
		
		sysarg_t retval;
		async_wait_for(req, &retval);
		
		if (retval != EOK)
			return 0;
		
		*data = devs;
		return count;
	}
}
