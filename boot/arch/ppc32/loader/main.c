/*
 * Copyright (c) 2005 Martin Decky
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

#include "main.h" 
#include <printf.h>
#include "asm.h"
#include "_components.h"
#include <ofw.h>
#include <align.h>
#include <macros.h>
#include <string.h>

#define HEAP_GAP 1024000

bootinfo_t bootinfo;


static void check_align(const void *addr, const char *desc)
{
	if ((unsigned int) addr % PAGE_SIZE != 0) {
		printf("Error: %s not on page boundary, halting.\n", desc);
		halt();
	}
}


static void fix_overlap(void *va, void **pa, const char *desc, unsigned int *top)
{
	if ((unsigned int) *pa + PAGE_SIZE < *top) {
		printf("Warning: %s overlaps kernel physical area\n", desc);
		
		void *new_va = (void *) (ALIGN_UP((unsigned int) KERNEL_END + HEAP_GAP, PAGE_SIZE) + *top);
		void *new_pa = (void *) (HEAP_GAP + *top);
		*top += PAGE_SIZE;
		
		if (ofw_map(new_pa, new_va, PAGE_SIZE, 0) != 0) {
			printf("Error: Unable to map page aligned memory at %L (physical %L), halting.\n", new_va, new_pa);
			halt();
		}
		
		if ((unsigned int) new_pa + PAGE_SIZE < KERNEL_SIZE) {
			printf("Error: %s cannot be relocated, halting.\n", desc);
			halt();	
		}
		
		printf("Relocating %L -> %L (physical %L -> %L)\n", va, new_va, *pa, new_pa);
		*pa = new_pa;
		memcpy(new_va, va, PAGE_SIZE);
	}
}

char *release = STRING(RELEASE);

#ifdef REVISION
	char *revision = ", revision " STRING(REVISION);
#else
	char *revision = "";
#endif

#ifdef TIMESTAMP
	char *timestamp = "\nBuilt on " STRING(TIMESTAMP);
#else
	char *timestamp = "";
#endif

/** Print version information. */
static void version_print(void)
{
	printf("HelenOS PPC32 Bootloader\nRelease %s%s%s\nCopyright (c) 2006 HelenOS project\n\n", release, revision, timestamp);
}

void bootstrap(void)
{
	version_print();
	
	component_t components[COMPONENTS];
	init_components(components);
		
	unsigned int i;
	for (i = 0; i < COMPONENTS; i++)
		check_align(components[i].start, components[i].name);
	
	check_align(&real_mode, "bootstrap trampoline");
	check_align(&trans, "translation table");
	
	if (!ofw_memmap(&bootinfo.memmap)) {
		printf("Error: Unable to get memory map, halting.\n");
		halt();
	}
	
	if (bootinfo.memmap.total == 0) {
		printf("Error: No memory detected, halting.\n");
		halt();
	}
	
	if (!ofw_screen(&bootinfo.screen))
		printf("Warning: Unable to get screen properties.\n");
	
	if (!ofw_macio(&bootinfo.macio))
		printf("Warning: Unable to get macio properties.\n");
	
	printf("Device statistics\n");
	
	if (bootinfo.screen.addr)
		printf(" screen at %L, resolution %dx%d, %d bpp (scanline %d bytes)\n", bootinfo.screen.addr, bootinfo.screen.width, bootinfo.screen.height, bootinfo.screen.bpp, bootinfo.screen.scanline);
	
	if (bootinfo.macio.addr)
		printf(" macio at %L (size %d bytes)\n", bootinfo.macio.addr, bootinfo.macio.size);
	
	void *real_mode_pa = ofw_translate(&real_mode);
	void *trans_pa = ofw_translate(&trans);
	void *bootinfo_pa = ofw_translate(&bootinfo);
	
	printf("\nMemory statistics (total %d MB)\n", bootinfo.memmap.total >> 20);
	printf(" %L: boot info structure (physical %L)\n", &bootinfo, bootinfo_pa);
	printf(" %L: bootstrap trampoline (physical %L)\n", &real_mode, real_mode_pa);
	printf(" %L: translation table (physical %L)\n", &trans, trans_pa);
	for (i = 0; i < COMPONENTS; i++)
		printf(" %L: %s image (size %d bytes)\n", components[i].start, components[i].name, components[i].size);
	
	unsigned int top = 0;
	for (i = 0; i < COMPONENTS; i++)
		top += ALIGN_UP(components[i].size, PAGE_SIZE);
	
	unsigned int pages = ALIGN_UP(KERNEL_SIZE, PAGE_SIZE) >> PAGE_WIDTH;
	
	for (i = 0; i < pages; i++) {
		void *pa = ofw_translate(KERNEL_START + (i << PAGE_WIDTH));
		fix_overlap(KERNEL_START + (i << PAGE_WIDTH), &pa, "kernel", &top);
		trans[i] = pa;
	}
	
	bootinfo.taskmap.count = 0;
	for (i = 1; i < COMPONENTS; i++) {
		unsigned int component_pages = ALIGN_UP(components[i].size, PAGE_SIZE) >> PAGE_WIDTH;
		unsigned int j;
		
		for (j = 0; j < component_pages; j++) {
			void *pa = ofw_translate(components[i].start + (j << PAGE_WIDTH));
			fix_overlap(components[i].start + (j << PAGE_WIDTH), &pa, components[i].name, &top);
			trans[pages + j] = pa;
			if (j == 0) {
				bootinfo.taskmap.tasks[bootinfo.taskmap.count].addr = (void *) (pages << PAGE_WIDTH);
				bootinfo.taskmap.tasks[bootinfo.taskmap.count].size = components[i].size;
				strncpy(bootinfo.taskmap.tasks[bootinfo.taskmap.count].name,
				    components[i].name, BOOTINFO_TASK_NAME_BUFLEN);

				bootinfo.taskmap.count++;
			}
		}
		
		pages += component_pages;
	}
	
	fix_overlap(&real_mode, &real_mode_pa, "bootstrap trampoline", &top);
	fix_overlap(&trans, &trans_pa, "translation table", &top);
	fix_overlap(&bootinfo, &bootinfo_pa, "boot info", &top);
	
	ofw_setup_palette();
	
	printf("\nBooting the kernel...\n");
	jump_to_kernel(bootinfo_pa, sizeof(bootinfo), trans_pa, pages << PAGE_WIDTH, real_mode_pa, (void *) bootinfo.screen.addr, bootinfo.screen.scanline);
}
