/*
 * Copyright (c) 2007 Pavel Jancik, Michal Kebrt
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

/** @addtogroup arm32boot
 * @{
 */
/** @file
 * @brief Memory management used while booting the kernel.
 */

#include <typedefs.h>
#include <arch/asm.h>
#include <arch/mm.h>

/** Check if caching can be enabled for a given memory section.
 *
 * Memory areas used for I/O are excluded from caching.
 * At the moment caching is enabled only on GTA02.
 *
 * @param section	The section number.
 *
 * @return	1 if the given section can be mapped as cacheable, 0 otherwise.
*/
static inline int section_cacheable(pfn_t section)
{
#ifdef MACHINE_gta02
	unsigned long address = section << PTE_SECTION_SHIFT;

	if (address >= GTA02_IOMEM_START && address < GTA02_IOMEM_END)
		return 0;
	else
		return 1;
#elif defined MACHINE_beagleboardxm
	const unsigned long address = section << PTE_SECTION_SHIFT;
	if (address >= BBXM_RAM_START && address < BBXM_RAM_END)
		return 1;
#endif
	return 0;
}

/** Initialize "section" page table entry.
 *
 * Will be readable/writable by kernel with no access from user mode.
 * Will belong to domain 0. No cache or buffering is enabled.
 *
 * @param pte   Section entry to initialize.
 * @param frame First frame in the section (frame number).
 *
 * @note If frame is not 1 MB aligned, first lower 1 MB aligned frame will be
 *       used.
 *
 */
static void init_ptl0_section(pte_level0_section_t* pte,
    pfn_t frame)
{
	pte->descriptor_type = PTE_DESCRIPTOR_SECTION;
	pte->bufferable = 1;
	pte->cacheable = section_cacheable(frame);
	pte->xn = 0;
	pte->domain = 0;
	pte->should_be_zero_1 = 0;
	pte->access_permission_0 = PTE_AP_USER_NO_KERNEL_RW;
	pte->tex = 0;
	pte->access_permission_1 = 0;
	pte->non_global = 0;
	pte->should_be_zero_2 = 0;
	pte->non_secure = 0;
	pte->section_base_addr = frame;
}

/** Initialize page table used while booting the kernel. */
static void init_boot_pt(void)
{
	const pfn_t split_page = PTL0_ENTRIES;
	/* Create 1:1 virtual-physical mapping (in lower 2 GB). */
	pfn_t page;
	for (page = 0; page < split_page; page++)
		init_ptl0_section(&boot_pt[page], page);
	
	/*
	 * Create 1:1 virtual-physical mapping in kernel space
	 * (upper 2 GB), physical addresses start from 0.
	 */
	/* BeagleBoard-xM (DM37x) memory starts at 2GB border,
	 * thus mapping only lower 2GB is not not enough.
	 * Map entire AS 1:1 instead and hope it works. */
	for (page = split_page; page < PTL0_ENTRIES; page++)
#ifndef MACHINE_beagleboardxm
		init_ptl0_section(&boot_pt[page], page - split_page);
#else
		init_ptl0_section(&boot_pt[page], page);
#endif
	
	asm volatile (
		"mcr p15, 0, %[pt], c2, c0, 0\n"
		:: [pt] "r" (boot_pt)
	);
}

static void enable_paging()
{
	/* c3   - each two bits controls access to the one of domains (16)
	 * 0b01 - behave as a client (user) of a domain
	 */
	asm volatile (
		/* Behave as a client of domains */
		"ldr r0, =0x55555555\n"
		"mcr p15, 0, r0, c3, c0, 0\n"
		
#ifdef PROCESSOR_ARCH_armv7_a
		/* armv7 no longer requires cache entries to be invalid
		 * upon reset, do this manually */
		/* Invalidate ICache */
		"mcr p15, 0, r0, c7, c5, 6\n"
		//TODO: Invalidate data cache
#endif

		/* Current settings */
		"mrc p15, 0, r0, c1, c0, 0\n"
		
#if defined(PROCESSOR_cortex_a8) | defined(MACHINE_gta02)
		/* Mask to enable paging, I-cache D-cache and branch predict
		 * See kernel/arch/arm32/include/regutils.h for bit values.
		 * It's safe because Cortex-A8 implements IVIPT extension
		 * See Cortex-A8 TRM ch. 7.2.6 p. 7-4 (PDF 245).
		 * It's safe for gta02 too because we turn the caches off
		 * before switching to kernel. */
		"ldr r1, =0x00001805\n"
#elif defined(PROCESSOR_ARCH_armv7_a) | defined(PROCESSOR_ARCH_armv6)
		/* Enable paging, data cache and branch prediction
		 * see arch/arm32/src/cpu/cpu.c for reasoning */
		"ldr r1, =0x00000805\n"
#else
		/* Mask to enable paging and branch prediction */
		"ldr r1, =0x00000801\n"
#endif
		"orr r0, r0, r1\n"
		
		/* Store settings */
		"mcr p15, 0, r0, c1, c0, 0\n"
		::: "r0", "r1"
	);
}

/** Start the MMU - initialize page table and enable paging. */
void mmu_start() {
	init_boot_pt();
	enable_paging();
}

/** @}
 */
