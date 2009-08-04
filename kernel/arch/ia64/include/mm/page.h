/*
 * Copyright (c) 2005 - 2006 Jakub Jermar
 * Copyright (c) 2006 Jakub Vana
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

/** @addtogroup ia64mm	
 * @{
 */
/** @file
 */

#ifndef KERN_ia64_PAGE_H_
#define KERN_ia64_PAGE_H_

#include <arch/mm/frame.h>

#define PAGE_SIZE	FRAME_SIZE
#define PAGE_WIDTH	FRAME_WIDTH

#ifdef KERNEL

/** Bit width of the TLB-locked portion of kernel address space. */
#define KERNEL_PAGE_WIDTH		28	/* 256M */
#define IO_PAGE_WIDTH			26	/* 64M */
#define FW_PAGE_WIDTH			28	/* 256M */

#define USPACE_IO_PAGE_WIDTH		12	/* 4K */


/*
 * Statically mapped IO spaces - offsets to 0xe...00 of virtual addresses
 * because of "minimal virtual bits implemented is 51" it is possible to
 * have values up to 0x0007000000000000
 */

/* Firmware area (bellow 4GB in phys mem) */
#define FW_OFFSET             0x00000000F0000000
/* Legacy IO space */
#define IO_OFFSET             0x0001000000000000
/* Videoram - now mapped to 0 as VGA text mode vram on 0xb8000 */
#define VIO_OFFSET            0x0002000000000000


#define PPN_SHIFT			12

#define VRN_SHIFT			61
#define VRN_MASK			(7LL << VRN_SHIFT)
#define VA2VRN(va)			((va)>>VRN_SHIFT)

#ifdef __ASM__
#define VRN_KERNEL	 		7
#else
#define VRN_KERNEL	 		7LL
#endif

#define REGION_REGISTERS 		8

#define KA2PA(x)	((uintptr_t) (x - (VRN_KERNEL << VRN_SHIFT)))
#define PA2KA(x)	((uintptr_t) (x + (VRN_KERNEL << VRN_SHIFT)))

#define VHPT_WIDTH 			20	/* 1M */
#define VHPT_SIZE 			(1 << VHPT_WIDTH)

#define PTA_BASE_SHIFT			15

/** Memory Attributes. */
#define MA_WRITEBACK	0x0
#define MA_UNCACHEABLE	0x4

/** Privilege Levels. Only the most and the least privileged ones are ever used. */
#define PL_KERNEL	0x0
#define PL_USER		0x3

/* Access Rigths. Only certain combinations are used by the kernel. */
#define AR_READ		0x0
#define AR_EXECUTE	0x1
#define AR_WRITE	0x2

#ifndef __ASM__

#include <arch/mm/as.h>
#include <arch/mm/frame.h>
#include <arch/interrupt.h>
#include <arch/barrier.h>
#include <arch/mm/asid.h>
#include <arch/types.h>
#include <debug.h>

struct vhpt_tag_info {
	unsigned long long tag : 63;
	unsigned ti : 1;
} __attribute__ ((packed));

union vhpt_tag {
	struct vhpt_tag_info tag_info;
	unsigned tag_word;
};

struct vhpt_entry_present {
	/* Word 0 */
	unsigned p : 1;
	unsigned : 1;
	unsigned ma : 3;
	unsigned a : 1;
	unsigned d : 1;
	unsigned pl : 2;
	unsigned ar : 3;
	unsigned long long ppn : 38;
	unsigned : 2;
	unsigned ed : 1;
	unsigned ig1 : 11;
	
	/* Word 1 */
	unsigned : 2;
	unsigned ps : 6;
	unsigned key : 24;
	unsigned : 32;
	
	/* Word 2 */
	union vhpt_tag tag;
	
	/* Word 3 */													
	uint64_t ig3 : 64;
} __attribute__ ((packed));

struct vhpt_entry_not_present {
	/* Word 0 */
	unsigned p : 1;
	unsigned long long ig0 : 52;
	unsigned ig1 : 11;
	
	/* Word 1 */
	unsigned : 2;
	unsigned ps : 6;
	unsigned long long ig2 : 56;

	/* Word 2 */
	union vhpt_tag tag;
	
	/* Word 3 */													
	uint64_t ig3 : 64;
} __attribute__ ((packed));

typedef union vhpt_entry {
	struct vhpt_entry_present present;
	struct vhpt_entry_not_present not_present;
	uint64_t word[4];
} vhpt_entry_t;

struct region_register_map {
	unsigned ve : 1;
	unsigned : 1;
	unsigned ps : 6;
	unsigned rid : 24;
	unsigned : 32;
} __attribute__ ((packed));

typedef union region_register {
	struct region_register_map map;
	unsigned long long word;
} region_register;

struct pta_register_map {
	unsigned ve : 1;
	unsigned : 1;
	unsigned size : 6;
	unsigned vf : 1;
	unsigned : 6;
	unsigned long long base : 49;
} __attribute__ ((packed));

typedef union pta_register {
	struct pta_register_map map;
	uint64_t word;
} pta_register;

/** Return Translation Hashed Entry Address.
 *
 * VRN bits are used to read RID (ASID) from one
 * of the eight region registers registers.
 *
 * @param va Virtual address including VRN bits.
 *
 * @return Address of the head of VHPT collision chain.
 */
static inline uint64_t thash(uint64_t va)
{
	uint64_t ret;

	asm volatile ("thash %0 = %1\n" : "=r" (ret) : "r" (va));

	return ret;
}

/** Return Translation Hashed Entry Tag.
 *
 * VRN bits are used to read RID (ASID) from one
 * of the eight region registers.
 *
 * @param va Virtual address including VRN bits.
 *
 * @return The unique tag for VPN and RID in the collision chain returned by thash().
 */
static inline uint64_t ttag(uint64_t va)
{
	uint64_t ret;

	asm volatile ("ttag %0 = %1\n" : "=r" (ret) : "r" (va));

	return ret;
}

/** Read Region Register.
 *
 * @param i Region register index.
 *
 * @return Current contents of rr[i].
 */
static inline uint64_t rr_read(size_t i)
{
	uint64_t ret;
	ASSERT(i < REGION_REGISTERS);
	asm volatile ("mov %0 = rr[%1]\n" : "=r" (ret) : "r" (i << VRN_SHIFT));
	return ret;
}

/** Write Region Register.
 *
 * @param i Region register index.
 * @param v Value to be written to rr[i].
 */
static inline void rr_write(size_t i, uint64_t v)
{
	ASSERT(i < REGION_REGISTERS);
	asm volatile (
		"mov rr[%0] = %1\n" 
		: 
		: "r" (i << VRN_SHIFT), "r" (v)
	);
}
 
/** Read Page Table Register.
 *
 * @return Current value stored in PTA.
 */
static inline uint64_t pta_read(void)
{
	uint64_t ret;
	
	asm volatile ("mov %0 = cr.pta\n" : "=r" (ret));
	
	return ret;
}

/** Write Page Table Register.
 *
 * @param v New value to be stored in PTA.
 */
static inline void pta_write(uint64_t v)
{
	asm volatile ("mov cr.pta = %0\n" : : "r" (v));
}

extern void page_arch_init(void);

extern vhpt_entry_t *vhpt_hash(uintptr_t page, asid_t asid);
extern bool vhpt_compare(uintptr_t page, asid_t asid, vhpt_entry_t *v);
extern void vhpt_set_record(vhpt_entry_t *v, uintptr_t page, asid_t asid, uintptr_t frame, int flags);

#endif /* __ASM__ */

#endif /* KERNEL */

#endif

/** @}
 */
