/*
 * Copyright (c) 2006 Ondrej Palkovsky
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

/** @addtogroup ns
 * @{
 */

/**
 * @file  ns.c
 * @brief Naming service for HelenOS IPC.
 */

#include <ipc/ipc.h>
#include <ipc/services.h>
#include <ipc/ns.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <as.h>
#include <ddi.h>
#include <event.h>
#include <macros.h>
#include <sysinfo.h>
#include "ns.h"
#include "service.h"
#include "clonable.h"
#include "task.h"

static void *clockaddr = NULL;
static void *klogaddr = NULL;

static void get_as_area(ipc_callid_t callid, ipc_call_t *call, void *faddr,
    size_t pages, void **addr)
{
	if ((faddr == NULL) || (pages == 0)) {
		ipc_answer_0(callid, ENOENT);
		return;
	}
	
	if (*addr == NULL) {
		*addr = as_get_mappable_page(pages * PAGE_SIZE);
		
		if (*addr == NULL) {
			ipc_answer_0(callid, ENOENT);
			return;
		}
		
		if (physmem_map(faddr, *addr, pages,
		    AS_AREA_READ | AS_AREA_CACHEABLE) != 0) {
			ipc_answer_0(callid, ENOENT);
			return;
		}
	}
	
	ipc_answer_2(callid, EOK, (sysarg_t) *addr, AS_AREA_READ);
}

static void setup_clock_area(ipc_callid_t callid, ipc_call_t *call, void **addr)
{
	uintptr_t faddr;
	int err = sysinfo_get_value("clock.faddr", &faddr);
	
	if (err != EOK)
		ipc_answer_0(callid, err);
	
	get_as_area(callid, call, (void *) faddr, 1, addr);
}

static void setup_klog_area(ipc_callid_t callid, ipc_call_t *call, void **addr)
{
	uintptr_t faddr;
	int err = sysinfo_get_value("klog.faddr", &faddr);
	
	if (err != EOK)
		ipc_answer_0(callid, err);
	
	size_t pages;
	err = sysinfo_get_value("klog.pages", &pages);
	
	if (err != EOK)
		ipc_answer_0(callid, err);
	
	get_as_area(callid, call, (void *) faddr, pages, addr);
}

int main(int argc, char **argv)
{
	printf(NAME ": HelenOS IPC Naming Service\n");
	
	int rc = service_init();
	if (rc != EOK)
		return rc;
	
	rc = clonable_init();
	if (rc != EOK)
		return rc;
	
	rc = task_init();
	if (rc != EOK)
		return rc;
	
	printf("%s: Accepting connections\n", NAME);
	
	while (true) {
		process_pending_conn();
		process_pending_wait();
		
		ipc_call_t call;
		ipc_callid_t callid = ipc_wait_for_call(&call);
		
		task_id_t id;
		sysarg_t retval;
		
		switch (IPC_GET_METHOD(call)) {
		case IPC_M_SHARE_IN:
			switch (IPC_GET_ARG3(call)) {
			case SERVICE_MEM_REALTIME:
				setup_clock_area(callid, &call, &clockaddr);
				break;
			case SERVICE_MEM_KLOG:
				setup_klog_area(callid, &call, &klogaddr);
				break;
			default:
				ipc_answer_0(callid, ENOENT);
			}
			continue;
		case IPC_M_PHONE_HUNGUP:
			retval = ns_task_disconnect(&call);
			break;
		case IPC_M_CONNECT_TO_ME:
			/*
			 * Server requests service registration.
			 */
			if (service_clonable(IPC_GET_ARG1(call))) {
				register_clonable(IPC_GET_ARG1(call),
				    IPC_GET_ARG5(call), &call, callid);
				continue;
			} else {
				retval = register_service(IPC_GET_ARG1(call),
				    IPC_GET_ARG5(call), &call);
			}
			break;
		case IPC_M_CONNECT_ME_TO:
			/*
			 * Client requests to be connected to a service.
			 */
			if (service_clonable(IPC_GET_ARG1(call))) {
				connect_to_clonable(IPC_GET_ARG1(call),
				    &call, callid);
				continue;
			} else {
				connect_to_service(IPC_GET_ARG1(call), &call,
				    callid);
				continue;
			}
			break;
		case NS_PING:
			retval = EOK;
			break;
		case NS_TASK_WAIT:
			id = (task_id_t)
			    MERGE_LOUP32(IPC_GET_ARG1(call), IPC_GET_ARG2(call));
			wait_for_task(id, &call, callid);
			continue;
		case NS_ID_INTRO:
			retval = ns_task_id_intro(&call);
			break;
		case NS_RETVAL:
			retval = ns_task_retval(&call);
			break;
		default:
			retval = ENOENT;
			break;
		}
		
		if (!(callid & IPC_CALLID_NOTIFICATION))
			ipc_answer_0(callid, retval);
	}
	
	/* Not reached */
	return 0;
}

/**
 * @}
 */
