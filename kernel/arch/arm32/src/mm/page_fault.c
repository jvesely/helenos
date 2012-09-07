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

/** @addtogroup arm32mm
 * @{
 */
/** @file
 *  @brief Page fault related functions.
 */
#include <panic.h>
#include <arch/exception.h>
#include <arch/mm/page_fault.h>
#include <mm/as.h>
#include <genarch/mm/page_pt.h>
#include <arch.h>
#include <interrupt.h>
#include <print.h>

/** Returns value stored in comnbined/data fault status register.
 *
 *  @return Value stored in CP15 fault status register (FSR).
 *
 *  "VMSAv6 added a fifth fault status bit (bit[10]) to both the IFSR and DFSR.
 *  It is IMPLEMENTATION DEFINED how this bit is encoded in earlier versions of
 *  the architecture. A write flag (bit[11] of the DFSR) has also been
 *  introduced."
 *  ARM Architecture Reference Manual version i ch. B4.6 (PDF p. 719)
 *
 *  See ch. B4.9.6 for location of data/instruction FSR.
 *
 */
static inline fault_status_t read_data_fault_status_register(void)
{
	fault_status_t fsu;
	
	/* Combined/Data fault status is stored in CP15 register 5, c0. */
	asm volatile (
		"mrc p15, 0, %[dummy], c5, c0, 0"
		: [dummy] "=r" (fsu.raw)
	);
	
	return fsu;
}

/** Returns DFAR (fault address register) content.
 *
 * This register is equivalent to FAR on pre armv6 machines.
 *
 * @return DFAR (fault address register) content (address that caused a page
 *         fault)
 */
static inline uintptr_t read_data_fault_address_register(void)
{
	uintptr_t ret;
	
	/* fault adress is stored in CP15 register 6 */
	asm volatile (
		"mrc p15, 0, %[ret], c6, c0, 0"
		: [ret] "=r" (ret)
	);
	
	return ret;
}

#if defined(PROCESSOR_armv4) | defined(PROCESSOR_armv5)
/** Decides whether read or write into memory is requested.
 *
 * @param instr_addr   Address of instruction which tries to access memory.
 * @param badvaddr     Virtual address the instruction tries to access.
 *
 * @return Type of access into memory, PF_ACCESS_EXEC if no memory access is
 *	   requested.
 */
static pf_access_t get_memory_access_type(uint32_t instr_addr,
    uintptr_t badvaddr)
{
	instruction_union_t instr_union;
	instr_union.pc = instr_addr;

	instruction_t instr = *(instr_union.instr);

	/* undefined instructions */
	if (instr.condition == 0xf) {
		panic("page_fault - instruction does not access memory "
		    "(instr_code: %#0" PRIx32 ", badvaddr:%p).",
		    *(uint32_t*)instr_union.instr, (void *) badvaddr);
		return PF_ACCESS_EXEC;
	}

	/* See ARM Architecture reference manual ARMv7-A and ARMV7-R edition
	 * A5.3 (PDF p. 206) */
	static const struct {
		uint32_t mask;
		uint32_t value;
		pf_access_t access;
	} ls_inst[] = {
		/* Store word/byte */
		{ 0x0e100000, 0x04000000, PF_ACCESS_WRITE }, /*STR(B) imm*/
		{ 0x0e100010, 0x06000000, PF_ACCESS_WRITE }, /*STR(B) reg*/
		/* Load word/byte */
		{ 0x0e100000, 0x04100000, PF_ACCESS_READ }, /*LDR(B) imm*/
		{ 0x0e100010, 0x06100000, PF_ACCESS_READ }, /*LDR(B) reg*/
		/* Store half-word/dual  A5.2.8 */
		{ 0x0e1000b0, 0x000000b0, PF_ACCESS_WRITE }, /*STRH imm reg*/
		/* Load half-word/dual A5.2.8 */
		{ 0x0e0000f0, 0x000000d0, PF_ACCESS_READ }, /*LDRH imm reg*/
		{ 0x0e1000b0, 0x001000b0, PF_ACCESS_READ }, /*LDRH imm reg*/
		/* Block data transfer, Store */
		{ 0x0e100000, 0x08000000, PF_ACCESS_WRITE }, /* STM variants */
		{ 0x0e100000, 0x08100000, PF_ACCESS_READ },  /* LDM variants */
		/* Swap */
		{ 0x0fb00000, 0x01000000, PF_ACCESS_WRITE },
	};
	const uint32_t inst = *(uint32_t*)instr_addr;
	for (unsigned i = 0; i < sizeof(ls_inst) / sizeof(ls_inst[0]); ++i) {
		if ((inst & ls_inst[i].mask) == ls_inst[i].value) {
			return ls_inst[i].access;
		}
	}

	panic("page_fault - instruction doesn't access memory "
	    "(instr_code: %#0" PRIx32 ", badvaddr:%p).",
	    inst, (void *) badvaddr);
}
#endif

/** Handles "data abort" exception (load or store at invalid address).
 *
 * @param exc_no Exception number.
 * @param istate CPU state when exception occured.
 *
 */
void data_abort(unsigned int exc_no, istate_t *istate)
{
	uintptr_t badvaddr = read_data_fault_address_register();

#if defined(PROCESSOR_armv6) | defined(PROCESSOR_armv7_a)
	fault_status_t fsr = read_data_fault_status_register();
	const pf_access_t access =
	    fsr.data.wr ? PF_ACCESS_WRITE : PF_ACCESS_READ;
#elif defined(PROCESSOR_armv4) | defined(PROCESSOR_armv5)
	const pf_access_t access = get_memory_access_type(istate->pc, badvaddr);
#else
#error "Unsupported architecture"
#endif
	int ret = as_page_fault(badvaddr, access, istate);

	if (ret == AS_PF_FAULT) {
		fault_if_from_uspace(istate, "Page fault: %#x.", badvaddr);
		panic_memtrap(istate, access, badvaddr, NULL);
	}
}

/** Handles "prefetch abort" exception (instruction couldn't be executed).
 *
 * @param exc_no Exception number.
 * @param istate CPU state when exception occured.
 *
 */
void prefetch_abort(unsigned int exc_no, istate_t *istate)
{
	/* NOTE: We should use IFAR and IFSR here. */
	int ret = as_page_fault(istate->pc, PF_ACCESS_EXEC, istate);

	if (ret == AS_PF_FAULT) {
		fault_if_from_uspace(istate,
		    "Page fault - prefetch_abort: %#x.", istate->pc);
		panic_memtrap(istate, PF_ACCESS_EXEC, istate->pc, NULL);
	}
}

/** @}
 */
