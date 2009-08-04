/*
 * Copyright (c) 2003-2004 Jakub Jermar
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

/** @addtogroup mips32	
 * @{
 */
/** @file
 */

#include <arch/exception.h>
#include <arch/interrupt.h>
#include <arch/mm/tlb.h>
#include <panic.h>
#include <arch/cp0.h>
#include <arch/types.h>
#include <arch.h>
#include <debug.h>
#include <proc/thread.h>
#include <print.h>
#include <interrupt.h>
#include <func.h>
#include <ddi/irq.h>
#include <arch/debugger.h>
#include <symtab.h>

static char * exctable[] = {
	"Interrupt",
	"TLB Modified",
	"TLB Invalid",
	"TLB Invalid Store",
	"Address Error - load/instr. fetch",
	"Address Error - store",
	"Bus Error - fetch instruction",
	"Bus Error - data reference",
	"Syscall",
	"BreakPoint",
	"Reserved Instruction",
	"Coprocessor Unusable",
	"Arithmetic Overflow",
	"Trap",
	"Virtual Coherency - instruction",
	"Floating Point",
	NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	"WatchHi/WatchLo", /* 23 */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	"Virtual Coherency - data",
};

static void print_regdump(istate_t *istate)
{
	char *pcsymbol, *rasymbol;

	pcsymbol = symtab_fmt_name_lookup(istate->epc);
	rasymbol = symtab_fmt_name_lookup(istate->ra);

	printf("PC: %#x(%s) RA: %#x(%s), SP(%p)\n", istate->epc, pcsymbol,
	    istate->ra, rasymbol, istate->sp);
}

static void unhandled_exception(int n, istate_t *istate)
{
	fault_if_from_uspace(istate, "Unhandled exception %s.", exctable[n]);
	
	print_regdump(istate);
	panic("Unhandled exception %s.", exctable[n]);
}

static void reserved_instr_exception(int n, istate_t *istate)
{
	if (*((uint32_t *)istate->epc) == 0x7c03e83b) {
		ASSERT(THREAD);
		istate->epc += 4;
		istate->v1 = istate->k1;
	} else 
		unhandled_exception(n, istate);
}

static void breakpoint_exception(int n, istate_t *istate)
{
#ifdef CONFIG_DEBUG
	debugger_bpoint(istate);
#else
	/* it is necessary to not re-execute BREAK instruction after 
	   returning from Exception handler
	   (see page 138 in R4000 Manual for more information) */
	istate->epc += 4;
#endif
}

static void tlbmod_exception(int n, istate_t *istate)
{
	tlb_modified(istate);
}

static void tlbinv_exception(int n, istate_t *istate)
{
	tlb_invalid(istate);
}

#ifdef CONFIG_FPU_LAZY
static void cpuns_exception(int n, istate_t *istate)
{
	if (cp0_cause_coperr(cp0_cause_read()) == fpu_cop_id)
		scheduler_fpu_lazy_request();
	else {
		fault_if_from_uspace(istate, "Unhandled Coprocessor Unusable Exception.");
		panic("Unhandled Coprocessor Unusable Exception.");
	}
}
#endif

static void interrupt_exception(int n, istate_t *istate)
{
	uint32_t cause;
	int i;
	
	/* decode interrupt number and process the interrupt */
	cause = (cp0_cause_read() >> 8) & 0xff;
	
	for (i = 0; i < 8; i++) {
		if (cause & (1 << i)) {
			irq_t *irq = irq_dispatch_and_lock(i);
			if (irq) {
				/*
				 * The IRQ handler was found.
				 */
				irq->handler(irq);
				spinlock_unlock(&irq->lock);
			} else {
				/*
				 * Spurious interrupt.
				 */
#ifdef CONFIG_DEBUG
				printf("cpu%u: spurious interrupt (inum=%d)\n",
				    CPU->id, i);
#endif
			}
		}
	}
}

/** Handle syscall userspace call */
static void syscall_exception(int n, istate_t *istate)
{
	panic("Syscall is handled through shortcut.");
}

void exception_init(void)
{
	int i;

	/* Clear exception table */
	for (i = 0; i < IVT_ITEMS; i++)
		exc_register(i, "undef", (iroutine) unhandled_exception);
	
	exc_register(EXC_Bp, "bkpoint", (iroutine) breakpoint_exception);
	exc_register(EXC_RI, "resinstr", (iroutine) reserved_instr_exception);
	exc_register(EXC_Mod, "tlb_mod", (iroutine) tlbmod_exception);
	exc_register(EXC_TLBL, "tlbinvl", (iroutine) tlbinv_exception);
	exc_register(EXC_TLBS, "tlbinvl", (iroutine) tlbinv_exception);
	exc_register(EXC_Int, "interrupt", (iroutine) interrupt_exception);
#ifdef CONFIG_FPU_LAZY
	exc_register(EXC_CpU, "cpunus", (iroutine) cpuns_exception);
#endif
	exc_register(EXC_Sys, "syscall", (iroutine) syscall_exception);
}

/** @}
 */
