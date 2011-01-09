/*
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
 
#ifndef LIBC_DEVICE_HW_RES_H_
#define LIBC_DEVICE_HW_RES_H_

#include <ipc/dev_iface.h>
#include <bool.h>

/** HW resource provider interface */
typedef enum {
	GET_RESOURCE_LIST = 0,
	ENABLE_INTERRUPT
} hw_res_funcs_t;

/** HW resource types */
typedef enum {
	INTERRUPT,
	IO_RANGE, 
	MEM_RANGE
} hw_res_type_t;

typedef enum {
	LITTLE_ENDIAN = 0,
	BIG_ENDIAN
} endianness_t;

/** HW resource (e.g. interrupt, memory register, i/o register etc.) */
typedef struct {
	hw_res_type_t type;
	union {
		struct {
			uint64_t address;
			endianness_t endianness;
			size_t size;
		} mem_range;

		struct {
			uint64_t address;
			endianness_t endianness;
			size_t size;
		} io_range;

		struct {
			int irq;
		} interrupt;
	} res;
} hw_resource_t;

typedef struct {
	size_t count;
	hw_resource_t *resources;
} hw_resource_list_t;

static inline void clean_hw_resource_list(hw_resource_list_t *hw_res)
{
	if (hw_res->resources != NULL) {
		free(hw_res->resources);

		hw_res->resources = NULL;
	}

	hw_res->count = 0;
}

extern int get_hw_resources(int, hw_resource_list_t *);
extern bool enable_interrupt(int);

#endif

/** @}
 */
