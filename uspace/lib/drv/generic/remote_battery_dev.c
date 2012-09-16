/*
 * Copyright (c) 2012 Maurizio Lombardi
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

/** @addtogroup libdrv
 * @{
 */
/** @file
 */

#include <async.h>
#include <errno.h>
#include <ops/battery_dev.h>
#include <device/battery_dev.h>
#include <ddf/driver.h>

static void remote_battery_status_get(ddf_fun_t *, void *, ipc_callid_t,
    ipc_call_t *);
static void remote_battery_charge_level_get(ddf_fun_t *, void *, ipc_callid_t,
    ipc_call_t *);

/** Remote battery interface operations */
static remote_iface_func_ptr_t remote_battery_dev_iface_ops[] = {
	&remote_battery_status_get,
	&remote_battery_charge_level_get,
};

/** Remote battery interface structure
 *
 * Interface for processing request from remote clients
 * addressed by the battery interface.
 *
 */
remote_iface_t remote_battery_dev_iface = {
	.method_count = sizeof(remote_battery_dev_iface_ops) /
	    sizeof(remote_iface_func_ptr_t),
	.methods = remote_battery_dev_iface_ops,
};

/** Process the status_get() request from the remote client
 *
 * @param fun    The function from which the battery status is read
 * @param ops    The local ops structure
 */
static void
remote_battery_status_get(ddf_fun_t *fun, void *ops, ipc_callid_t callid,
    ipc_call_t *call)
{
	const battery_dev_ops_t *bops = (battery_dev_ops_t *) ops;

	if (bops->battery_status_get == NULL) {
		async_answer_0(callid, ENOTSUP);
		return;
	}

	battery_status_t batt_status;
	const int rc = bops->battery_status_get(fun, &batt_status);

	if (rc != EOK)
		async_answer_0(callid, rc);
	else
		async_answer_1(callid, rc, batt_status);
}

/** Process the battery_charge_level_get() request from the remote client
 *
 * @param fun    The function from which the battery charge level is read
 * @param ops    The local ops structure
 *
 */
static void
remote_battery_charge_level_get(ddf_fun_t *fun, void *ops, ipc_callid_t callid,
    ipc_call_t *call)
{
	const battery_dev_ops_t *bops = (battery_dev_ops_t *) ops;

	if (bops->battery_charge_level_get == NULL) {
		async_answer_0(callid, ENOTSUP);
		return;
	}

	int battery_level;
	const int rc = bops->battery_charge_level_get(fun, &battery_level);

	if (rc != EOK)
		async_answer_0(callid, rc);
	else
		async_answer_1(callid, rc, battery_level);
}

