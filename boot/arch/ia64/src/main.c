/*
 * Copyright (c) 2005 Martin Decky
 * Copyright (c) 2006 Jakub Jermar
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


#include <arch/main.h>
#include <arch/arch.h>
#include <arch/asm.h>
#include <arch/_components.h>
#include <halt.h>
#include <printf.h>
#include <memstr.h>
#include <version.h>
#include <macros.h>
#include <align.h>
#include <str.h>
#include <errno.h>
#include <inflate.h>

#define DEFAULT_MEMORY_BASE		0x4000000ULL
#define DEFAULT_MEMORY_SIZE		0x4000000ULL
#define DEFAULT_LEGACY_IO_BASE		0x00000FFFFC000000ULL
#define DEFAULT_LEGACY_IO_SIZE		0x4000000ULL

#define DEFAULT_FREQ_SCALE		0x0000000100000001ULL	/* 1/1 */
#define DEFAULT_SYS_FREQ		100000000ULL		/* 100MHz */

#define EFI_MEMMAP_FREE_MEM		0
#define EFI_MEMMAP_IO			1
#define EFI_MEMMAP_IO_PORTS		2

static bootinfo_t bootinfo;

void bootstrap(void)
{
	version_print();
	
	printf(" %p|%p: boot info structure\n", &bootinfo, &bootinfo);
	printf(" %p|%p: kernel entry point\n", KERNEL_ADDRESS, KERNEL_ADDRESS);
	printf(" %p|%p: loader entry point\n", LOADER_ADDRESS, LOADER_ADDRESS);
	
	size_t i;
	for (i = 0; i < COMPONENTS; i++)
		printf(" %p|%p: %s image (%u/%u bytes)\n", components[i].start,
		    components[i].start, components[i].name,
		    components[i].inflated, components[i].size);
	
	void *dest[COMPONENTS];
	size_t top = KERNEL_ADDRESS;
	size_t cnt = 0;
	bootinfo.taskmap.cnt = 0;
	for (i = 0; i < min(COMPONENTS, TASKMAP_MAX_RECORDS); i++) {
		top = ALIGN_UP(top, PAGE_SIZE);
		
		if (i > 0) {
			bootinfo.taskmap.tasks[bootinfo.taskmap.cnt].addr =
			    (void *) top;
			bootinfo.taskmap.tasks[bootinfo.taskmap.cnt].size =
			    components[i].inflated;
			
			str_cpy(bootinfo.taskmap.tasks[bootinfo.taskmap.cnt].name,
			    BOOTINFO_TASK_NAME_BUFLEN, components[i].name);
			
			bootinfo.taskmap.cnt++;
		}
		
		dest[i] = (void *) top;
		top += components[i].inflated;
		cnt++;
	}
	
	printf("\nInflating components ... ");
	
	for (i = cnt; i > 0; i--) {
		printf("%s ", components[i - 1].name);
		
		int err = inflate(components[i - 1].start, components[i - 1].size,
		    dest[i - 1], components[i - 1].inflated);
		
		if (err != EOK) {
			printf("\n%s: Inflating error %d, halting.\n",
			    components[i - 1].name, err);
			halt();
		}
	}
	
	printf(".\n");
	
	if (!bootinfo.hello_configured) {	/* XXX */
		/*
		 * Load configuration defaults for simulators.
		 */
		 bootinfo.memmap_items = 0;
		 
		 bootinfo.memmap[bootinfo.memmap_items].base =
		     DEFAULT_MEMORY_BASE;
		 bootinfo.memmap[bootinfo.memmap_items].size =
		     DEFAULT_MEMORY_SIZE;
		 bootinfo.memmap[bootinfo.memmap_items].type =
		     EFI_MEMMAP_FREE_MEM;
		 bootinfo.memmap_items++;

		 bootinfo.memmap[bootinfo.memmap_items].base =
		     DEFAULT_LEGACY_IO_BASE;
		 bootinfo.memmap[bootinfo.memmap_items].size =
		     DEFAULT_LEGACY_IO_SIZE;
		 bootinfo.memmap[bootinfo.memmap_items].type =
		     EFI_MEMMAP_IO_PORTS;
		 bootinfo.memmap_items++;
		 
		 bootinfo.freq_scale = DEFAULT_FREQ_SCALE;
		 bootinfo.sys_freq = DEFAULT_SYS_FREQ;
	}
	
	
	printf("Booting the kernel ...\n");
	jump_to_kernel(&bootinfo);
}
