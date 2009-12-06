/*
 * Copyright (c) 2005 Jakub Jermar
 * Copyright (c) 2008 Pavel Rimsky
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

/** @addtogroup sparc64mm	
 * @{
 */
/** @file
 */

#include <arch/mm/tlb.h>
#include <mm/tlb.h>
#include <mm/as.h>
#include <mm/asid.h>
#include <arch/sun4v/hypercall.h>
#include <arch/mm/frame.h>
#include <arch/mm/page.h>
#include <arch/mm/tte.h>
#include <arch/mm/tlb.h>
#include <arch/interrupt.h>
#include <interrupt.h>
#include <arch.h>
#include <print.h>
#include <arch/types.h>
#include <config.h>
#include <arch/trap/trap.h>
#include <arch/trap/exception.h>
#include <panic.h>
#include <arch/asm.h>
#include <arch/cpu.h>
#include <arch/mm/pagesize.h>

#ifdef CONFIG_TSB
#include <arch/mm/tsb.h>
#endif

//static void dtlb_pte_copy(pte_t *, size_t, bool);
static void itlb_pte_copy(pte_t *);
static void do_fast_instruction_access_mmu_miss_fault(istate_t *, const char *);
//static void do_fast_data_access_mmu_miss_fault(istate_t *, uint64_t,
//    const char *);
//static void do_fast_data_access_protection_fault(istate_t *,
//    uint64_t, const char *);

char *context_encoding[] = {
	"Primary",
	"Secondary",
	"Nucleus",
	"Reserved"
};

/** Invalidate all unlocked ITLB and DTLB entries. */
void tlb_invalidate_all(void)
{
	uint64_t errno =  __hypercall_fast3(MMU_DEMAP_ALL, 0, 0,
		MMU_FLAG_DTLB | MMU_FLAG_ITLB);
	if (errno != EOK) {
		panic("Error code = %d.\n", errno);
	}
}

void tlb_arch_init(void)
{
	tlb_invalidate_all();
}

/** Insert privileged mapping into DMMU TLB.
 *
 * @param page		Virtual page address.
 * @param frame		Physical frame address.
 * @param pagesize	Page size.
 * @param locked	True for permanent mappings, false otherwise.
 * @param cacheable	True if the mapping is cacheable, false otherwise.
 */
void dtlb_insert_mapping(uintptr_t page, uintptr_t frame, int pagesize,
    bool locked, bool cacheable)
{
#if 0
	tlb_tag_access_reg_t tag;
	tlb_data_t data;
	page_address_t pg;
	frame_address_t fr;

	pg.address = page;
	fr.address = frame;

	tag.context = ASID_KERNEL;
	tag.vpn = pg.vpn;

	dtlb_tag_access_write(tag.value);

	data.value = 0;
	data.v = true;
	data.size = pagesize;
	data.pfn = fr.pfn;
	data.l = locked;
	data.cp = cacheable;
#ifdef CONFIG_VIRT_IDX_DCACHE
	data.cv = cacheable;
#endif /* CONFIG_VIRT_IDX_DCACHE */
	data.p = true;
	data.w = true;
	data.g = false;

	dtlb_data_in_write(data.value);
#endif
}

#if 0
/** Copy PTE to TLB.
 *
 * @param t 		Page Table Entry to be copied.
 * @param index		Zero if lower 8K-subpage, one if higher 8K-subpage.
 * @param ro		If true, the entry will be created read-only, regardless
 * 			of its w field.
 */
void dtlb_pte_copy(pte_t *t, size_t index, bool ro)
{
	tlb_tag_access_reg_t tag;
	tlb_data_t data;
	page_address_t pg;
	frame_address_t fr;

	pg.address = t->page + (index << MMU_PAGE_WIDTH);
	fr.address = t->frame + (index << MMU_PAGE_WIDTH);

	tag.value = 0;
	tag.context = t->as->asid;
	tag.vpn = pg.vpn;

	dtlb_tag_access_write(tag.value);

	data.value = 0;
	data.v = true;
	data.size = PAGESIZE_8K;
	data.pfn = fr.pfn;
	data.l = false;
	data.cp = t->c;
#ifdef CONFIG_VIRT_IDX_DCACHE
	data.cv = t->c;
#endif /* CONFIG_VIRT_IDX_DCACHE */
	data.p = t->k;		/* p like privileged */
	data.w = ro ? false : t->w;
	data.g = t->g;

	dtlb_data_in_write(data.value);
}
#endif

/** Copy PTE to ITLB.
 *
 * @param t		Page Table Entry to be copied.
 */
void itlb_pte_copy(pte_t *t)
{
	tte_data_t data;
	
	data.value = 0;
	data.v = true;
	data.nfo = false;
	data.ra = (t->frame) >> FRAME_WIDTH;
	data.ie = false;
	data.e = false;
	data.cp = t->c;
	data.cv = false;
	data.p = t->k;
	data.x = true;
	data.w = false;
	data.size = PAGESIZE_8K;
	
	__hypercall_hyperfast(
		t->page, t->as->asid, data.value, MMU_FLAG_ITLB, 0, MMU_MAP_ADDR);
}

/** ITLB miss handler. */
void fast_instruction_access_mmu_miss(unative_t unused, istate_t *istate)
{
	uintptr_t va = ALIGN_DOWN(istate->tpc, PAGE_SIZE);
	pte_t *t;

	page_table_lock(AS, true);
	t = page_mapping_find(AS, va);

	if (t && PTE_EXECUTABLE(t)) {
		/*
		 * The mapping was found in the software page hash table.
		 * Insert it into ITLB.
		 */
		t->a = true;
		itlb_pte_copy(t);
#ifdef CONFIG_TSB
		itsb_pte_copy(t);
#endif
		page_table_unlock(AS, true);
	} else {
		/*
		 * Forward the page fault to the address space page fault
		 * handler.
		 */		
		page_table_unlock(AS, true);
		if (as_page_fault(va, PF_ACCESS_EXEC, istate) == AS_PF_FAULT) {
			do_fast_instruction_access_mmu_miss_fault(istate,
			    __func__);
		}
	}
}

/** DTLB miss handler.
 *
 * Note that some faults (e.g. kernel faults) were already resolved by the
 * low-level, assembly language part of the fast_data_access_mmu_miss handler.
 *
 * @param page_and_ctx	A 64-bit value describing the fault. The most
 * 			significant 51 bits of the value contain the virtual
 * 			address which caused the fault truncated to the page
 * 			boundary. The least significant 13 bits of the value
 * 			contain the number of the context in which the fault
 * 			occurred.
 * @param istate	Interrupted state saved on the stack.
 */
void fast_data_access_mmu_miss(uint64_t page_and_ctx, istate_t *istate)
{
#if 0
	pte_t *t;
	uintptr_t va = DMISS_ADDRESS(page_and_ctx);
	uint16_t ctx = DMISS_CONTEXT(page_and_ctx);

	if (ctx == ASID_KERNEL) {
		if (va == 0) {
			/* NULL access in kernel */
			do_fast_data_access_mmu_miss_fault(istate, page_and_ctx,
			    __func__);
		}
		do_fast_data_access_mmu_miss_fault(istate, page_and_ctx, "Unexpected "
		    "kernel page fault.");
	}

	page_table_lock(AS, true);
	t = page_mapping_find(AS, va);
	if (t) {
		/*
		 * The mapping was found in the software page hash table.
		 * Insert it into DTLB.
		 */
		t->a = true;
		dtlb_pte_copy(t, true);
#ifdef CONFIG_TSB
		dtsb_pte_copy(t, true);
#endif
		page_table_unlock(AS, true);
	} else {
		/*
		 * Forward the page fault to the address space page fault
		 * handler.
		 */		
		page_table_unlock(AS, true);
		if (as_page_fault(va, PF_ACCESS_READ, istate) == AS_PF_FAULT) {
			do_fast_data_access_mmu_miss_fault(istate, page_and_ctx,
			    __func__);
		}
	}
#endif
}

/** DTLB protection fault handler.
 *
 * @param tag		Content of the TLB Tag Access register as it existed
 * 			when the trap happened. This is to prevent confusion
 * 			created by clobbered Tag Access register during a nested
 * 			DTLB miss.
 * @param istate	Interrupted state saved on the stack.
 */
void fast_data_access_protection(uint64_t page_and_ctx, istate_t *istate)
{
#if 0
	pte_t *t;

	uintptr_t va = DMISS_ADDRESS(page_and_ctx);
	uint16_t ctx = DMISS_CONTEXT(page_and_ctx);

	page_table_lock(AS, true);
	t = page_mapping_find(AS, va);
	if (t && PTE_WRITABLE(t)) {
		/*
		 * The mapping was found in the software page hash table and is
		 * writable. Demap the old mapping and insert an updated mapping
		 * into DTLB.
		 */
		t->a = true;
		t->d = true;
		mmu_demap_page(va, ctx, MMU_FLAG_DTLB);
		dtlb_pte_copy(t, false);
#ifdef CONFIG_TSB
		dtsb_pte_copy(t, false);
#endif
		page_table_unlock(AS, true);
	} else {
		/*
		 * Forward the page fault to the address space page fault
		 * handler.
		 */		
		page_table_unlock(AS, true);
		if (as_page_fault(page_16k, PF_ACCESS_WRITE, istate) ==
		    AS_PF_FAULT) {
			do_fast_data_access_protection_fault(istate, tag,
			    __func__);
		}
	}
#endif
}

/** Print TLB entry (for debugging purposes).
 *
 * The diag field has been left out in order to make this function more generic
 * (there is no diag field in US3 architeture). 
 *
 * @param i		TLB entry number 
 * @param t		TLB entry tag
 * @param d		TLB entry data 
 */
void tlb_print(void)
{
	printf("Operation not possible on Niagara.\n");
}

void do_fast_instruction_access_mmu_miss_fault(istate_t *istate,
    const char *str)
{
	fault_if_from_uspace(istate, "%s.", str);
	dump_istate(istate);
	panic("%s.", str);
}

#if 0
void do_fast_data_access_mmu_miss_fault(istate_t *istate,
    uint64_t page_and_ctx, const char *str)
{
	if (DMISS_CONTEXT(page_and_ctx)) {
		fault_if_from_uspace(istate, "%s, Page=%p (ASID=%d)\n", str, DMISS_ADDRESS(page_and_ctx),
		    DMISS_CONTEXT(page_and_ctx));
	}
	dump_istate(istate);
	printf("Faulting page: %p, ASID=%d.\n", va, tag.context);
	panic("%s.", str);
}
#endif

#if 0
void do_fast_data_access_protection_fault(istate_t *istate,
    uint64_t page_and_ctx, const char *str)
{
	if (DMISS_CONTEXT(page_and_ctx)) {
		fault_if_from_uspace(istate, "%s, Page=%p (ASID=%d)\n", str, DMISS_ADDRESS(page_and_ctx),
		    DMISS_CONTEXT(page_and_ctx));
	}
	printf("Faulting page: %p, ASID=%d\n", DMISS_ADDRESS(page_and_ctx), DMISS_CONTEXT(page_and_ctx));
	dump_istate(istate);
	panic("%s.", str);
}
#endif

/**
 * Describes the exact condition which caused the last DMMU fault.
 */
void describe_dmmu_fault(void)
{
#if 0
	uint64_t myid;
	__hypercall_fast_ret1(0, 0, 0, 0, 0, CPU_MYID, &myid);

	ASSERT(mmu_fsas[myid].dft < 16);

	printf("condition which caused the fault: %s\n",
		fault_types[mmu_fsas[myid].dft]);
}

/** Invalidate all unlocked ITLB and DTLB entries. */
void tlb_invalidate_all(void)
{
	uint64_t errno =  __hypercall_fast3(MMU_DEMAP_ALL, 0, 0,
		MMU_FLAG_DTLB | MMU_FLAG_ITLB);
	if (errno != EOK) {
		panic("Error code = %d.\n", errno);
	}
#endif
}

/** Invalidate all ITLB and DTLB entries that belong to specified ASID
 * (Context).
 *
 * @param asid Address Space ID.
 */
void tlb_invalidate_asid(asid_t asid)
{
	/* switch to nucleus because we are mapped by the primary context */
	nucleus_enter();
	__hypercall_fast4(MMU_DEMAP_CTX, 0, 0, asid,
		MMU_FLAG_ITLB | MMU_FLAG_DTLB);

	nucleus_leave();
}

/** Invalidate all ITLB and DTLB entries for specified page range in specified
 * address space.
 *
 * @param asid		Address Space ID.
 * @param page		First page which to sweep out from ITLB and DTLB.
 * @param cnt		Number of ITLB and DTLB entries to invalidate.
 */
void tlb_invalidate_pages(asid_t asid, uintptr_t page, size_t cnt)
{
	unsigned int i;
	
	/* switch to nucleus because we are mapped by the primary context */
	nucleus_enter();

	for (i = 0; i < cnt; i++) {
		__hypercall_fast5(MMU_DEMAP_PAGE, 0, 0, page, asid,
			MMU_FLAG_DTLB | MMU_FLAG_ITLB);
	}

	nucleus_leave();
}

/** @}
 */
