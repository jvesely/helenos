/*
 * Copyright (c) 2006 Martin Decky
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

/** @addtogroup ppc32
 * @{
 */
/** @file
 */

#ifndef KERN_ppc32_BOOT_H_
#define KERN_ppc32_BOOT_H_

#define BOOT_OFFSET  0x8000

/* Temporary stack size for boot process */
#define TEMP_STACK_SIZE  0x1000

#define TASKMAP_MAX_RECORDS  32
#define MEMMAP_MAX_RECORDS   32

#ifndef __ASM__

#define BOOTINFO_TASK_NAME_BUFLEN 32

#include <arch/types.h>

typedef struct {
	uintptr_t addr;
	uint32_t size;
	char name[BOOTINFO_TASK_NAME_BUFLEN];
} utask_t;

typedef struct {
	uint32_t count;
	utask_t tasks[TASKMAP_MAX_RECORDS];
} taskmap_t;

typedef struct {
	uintptr_t start;
	uint32_t size;
} memzone_t;

typedef struct {
	uint32_t total;
	uint32_t count;
	memzone_t zones[MEMMAP_MAX_RECORDS];
} memmap_t;

typedef struct {
	uintptr_t addr;
	unsigned int width;
	unsigned int height;
	unsigned int bpp;
	unsigned int scanline;
} screen_t;

typedef struct {
	uintptr_t addr;
	unsigned int size;
} macio_t;

typedef struct {
	memmap_t memmap;
	taskmap_t taskmap;
	screen_t screen;
	macio_t macio;
} bootinfo_t;

extern bootinfo_t bootinfo;

#endif

#endif

/** @}
 */
