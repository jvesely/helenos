/*
 * Copyright (c) 2009 Jakub Jermar
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

/** @addtogroup obio 
 * @{
 */ 

/**
 * @file	obio.c
 * @brief	OBIO driver.
 *
 * OBIO is a short for on-board I/O. On UltraSPARC IIi and systems with U2P,
 * there is a piece of the root PCI bus controller address space, which
 * contains interrupt mapping and clear registers for all on-board devices.
 * Although UltraSPARC IIi and U2P are different in general, these registers can
 * be found at the same addresses.
 */

#include <ipc/ipc.h>
#include <ipc/services.h>
#include <ipc/bus.h>
#include <ipc/ns.h>
#include <sysinfo.h>
#include <as.h>
#include <ddi.h>
#include <align.h>
#include <bool.h>
#include <errno.h>
#include <async.h>
#include <align.h>
#include <async.h>
#include <stdio.h>
#include <ipc/devmap.h>

#define NAME "obio"

#define OBIO_SIZE	0x1898	

#define OBIO_IMR_BASE	0x200
#define OBIO_IMR(ino)	(OBIO_IMR_BASE + ((ino) & INO_MASK))

#define OBIO_CIR_BASE	0x300
#define OBIO_CIR(ino)	(OBIO_CIR_BASE + ((ino) & INO_MASK))

#define INO_MASK	0x1f

static void *base_phys;
static volatile uint64_t *base_virt;

/** Handle one connection to obio.
 *
 * @param iid		Hash of the request that opened the connection.
 * @param icall		Call data of the request that opened the connection.
 */
static void obio_connection(ipc_callid_t iid, ipc_call_t *icall)
{
	ipc_callid_t callid;
	ipc_call_t call;

	/*
	 * Answer the first IPC_M_CONNECT_ME_TO call.
	 */
	ipc_answer_0(iid, EOK);

	while (1) {
		int inr;
	
		callid = async_get_call(&call);
		switch (IPC_GET_METHOD(call)) {
		case BUS_CLEAR_INTERRUPT:
			inr = IPC_GET_ARG1(call);
			base_virt[OBIO_CIR(inr & INO_MASK)] = 0;
			ipc_answer_0(callid, EOK);
			break;
		default:
			ipc_answer_0(callid, EINVAL);
			break;
		}
	}
}

/** Initialize the OBIO driver.
 *
 * So far, the driver heavily depends on information provided by the kernel via
 * sysinfo. In the future, there should be a standalone OBIO driver.
 */
static bool obio_init(void)
{
	sysarg_t paddr;
	
	if (sysinfo_get_value("obio.base.physical", &paddr) != EOK) {
		printf(NAME ": no OBIO registers found\n");
		return false;
	}
	
	base_phys = (void *) paddr;
	base_virt = as_get_mappable_page(OBIO_SIZE);
	
	int flags = AS_AREA_READ | AS_AREA_WRITE;
	int retval = physmem_map(base_phys, (void *) base_virt,
	    ALIGN_UP(OBIO_SIZE, PAGE_SIZE) >> PAGE_WIDTH, flags);
	
	if (retval < 0) {
		printf(NAME ": Error mapping OBIO registers\n");
		return false;
	}
	
	printf(NAME ": OBIO registers with base at %p\n", base_phys);
	
	async_set_client_connection(obio_connection);
	sysarg_t phonead;
	ipc_connect_to_me(PHONE_NS, SERVICE_OBIO, 0, 0, &phonead);
	
	return true;
}

int main(int argc, char **argv)
{
	printf(NAME ": HelenOS OBIO driver\n");
	
	if (!obio_init())
		return -1;
	
	printf(NAME ": Accepting connections\n");
	async_manager();

	/* Never reached */
	return 0;
}

/**
 * @}
 */ 
