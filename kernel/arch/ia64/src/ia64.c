/*
 * Copyright (c) 2005 Jakub Jermar
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

/** @addtogroup ia64
 * @{
 */
/** @file
 */

#include <arch.h>
#include <arch/drivers/ski.h>
#include <arch/drivers/it.h>
#include <arch/interrupt.h>
#include <arch/barrier.h>
#include <arch/asm.h>
#include <arch/register.h>
#include <typedefs.h>
#include <arch/context.h>
#include <arch/stack.h>
#include <arch/mm/page.h>
#include <interrupt.h>
#include <mm/as.h>
#include <config.h>
#include <userspace.h>
#include <console/console.h>
#include <proc/uarg.h>
#include <syscall/syscall.h>
#include <ddi/irq.h>
#include <arch/bootinfo.h>
#include <genarch/drivers/legacy/ia32/io.h>
#include <genarch/drivers/ega/ega.h>
#include <genarch/kbrd/kbrd.h>
#include <genarch/srln/srln.h>
#include <genarch/drivers/i8042/i8042.h>
#include <genarch/drivers/ns16550/ns16550.h>
#include <arch/drivers/kbd.h>
#include <smp/smp.h>
#include <smp/ipi.h>
#include <arch/atomic.h>
#include <panic.h>
#include <print.h>
#include <sysinfo/sysinfo.h>
#include <str.h>

/* NS16550 as a COM 1 */
#define NS16550_IRQ  (4 + LEGACY_INTERRUPT_BASE)

bootinfo_t *bootinfo;

static uint64_t iosapic_base = 0xfec00000;

/** Performs ia64-specific initialization before main_bsp() is called. */
void arch_pre_main(void)
{
	/* Setup usermode init tasks. */

	unsigned int i;
	
	init.cnt = bootinfo->taskmap.count;
	
	for (i = 0; i < init.cnt; i++) {
		init.tasks[i].addr =
		    ((unsigned long) bootinfo->taskmap.tasks[i].addr) |
		    VRN_MASK;
		init.tasks[i].size = bootinfo->taskmap.tasks[i].size;
		str_cpy(init.tasks[i].name, CONFIG_TASK_NAME_BUFLEN,
		    bootinfo->taskmap.tasks[i].name);
	}
}

void arch_pre_mm_init(void)
{
	/*
	 * Set Interruption Vector Address (i.e. location of interruption vector
	 * table).
	 */
	iva_write((uintptr_t) &ivt);
	srlz_d();
	
}

static void iosapic_init(void)
{
	uint64_t IOSAPIC = PA2KA((unative_t)(iosapic_base)) | FW_OFFSET;
	int i;
	
	int myid, myeid;
	
	myid = ia64_get_cpu_id();
	myeid = ia64_get_cpu_eid();

	for (i = 0; i < 16; i++) { 
		if (i == 2)
			continue;	 /* Disable Cascade interrupt */
		((uint32_t *)(IOSAPIC + 0x00))[0] = 0x10 + 2 * i;
		srlz_d();
		((uint32_t *)(IOSAPIC + 0x10))[0] = LEGACY_INTERRUPT_BASE + i;
		srlz_d();
		((uint32_t *)(IOSAPIC + 0x00))[0] = 0x10 + 2 * i + 1;
		srlz_d();
		((uint32_t *)(IOSAPIC + 0x10))[0] = myid << (56 - 32) |
		    myeid << (48 - 32);
		srlz_d();
	}

}

void arch_post_mm_init(void)
{
	if (config.cpu_active == 1) {
		iosapic_init();
		irq_init(INR_COUNT, INR_COUNT);
	}
	it_init();	
}

void arch_post_cpu_init(void)
{
}

void arch_pre_smp_init(void)
{
}

void arch_post_smp_init(void)
{
#ifdef MACHINE_ski
	ski_instance_t *ski_instance = skiin_init();
	if (ski_instance) {
		srln_instance_t *srln_instance = srln_init();
		if (srln_instance) {
			indev_t *sink = stdin_wire();
			indev_t *srln = srln_wire(srln_instance, sink);
			skiin_wire(ski_instance, srln);
		}
	}
	
	outdev_t *skidev = skiout_init();
	if (skidev)
		stdout_wire(skidev);
#endif
	
#ifdef CONFIG_EGA
	outdev_t *egadev = ega_init(EGA_BASE, EGA_VIDEORAM);
	if (egadev)
		stdout_wire(egadev);
#endif
	
#ifdef CONFIG_NS16550
	ns16550_instance_t *ns16550_instance
	    = ns16550_init((ns16550_t *) NS16550_BASE, NS16550_IRQ, NULL, NULL);
	if (ns16550_instance) {
		srln_instance_t *srln_instance = srln_init();
		if (srln_instance) {
			indev_t *sink = stdin_wire();
			indev_t *srln = srln_wire(srln_instance, sink);
			ns16550_wire(ns16550_instance, srln);
		}
	}
	
	sysinfo_set_item_val("kbd", NULL, true);
	sysinfo_set_item_val("kbd.inr", NULL, NS16550_IRQ);
	sysinfo_set_item_val("kbd.type", NULL, KBD_NS16550);
	sysinfo_set_item_val("kbd.address.physical", NULL,
	    (uintptr_t) NS16550_BASE);
	sysinfo_set_item_val("kbd.address.kernel", NULL,
	    (uintptr_t) NS16550_BASE);
#endif
	
#ifdef CONFIG_I8042
	i8042_instance_t *i8042_instance = i8042_init((i8042_t *) I8042_BASE, IRQ_KBD);
	if (i8042_instance) {
		kbrd_instance_t *kbrd_instance = kbrd_init();
		if (kbrd_instance) {
			indev_t *sink = stdin_wire();
			indev_t *kbrd = kbrd_wire(kbrd_instance, sink);
			i8042_wire(i8042_instance, kbrd);
		}
	}
	
	sysinfo_set_item_val("i8042", NULL, true);
	sysinfo_set_item_val("i8042.inr_a", NULL, IRQ_KBD);
	sysinfo_set_item_val("i8042.inr_b", NULL, IRQ_MOUSE);
	sysinfo_set_item_val("i8042.address.physical", NULL,
	    (uintptr_t) I8042_BASE);
	sysinfo_set_item_val("i8042.address.kernel", NULL,
	    (uintptr_t) I8042_BASE);
#endif

	sysinfo_set_item_val("netif.dp8390.inr", NULL, IRQ_DP8390);

	sysinfo_set_item_val("ia64_iospace", NULL, true);
	sysinfo_set_item_val("ia64_iospace.address", NULL, true);
	sysinfo_set_item_val("ia64_iospace.address.virtual", NULL, IO_OFFSET);
}


/** Enter userspace and never return. */
void userspace(uspace_arg_t *kernel_uarg)
{
	psr_t psr;
	rsc_t rsc;

	psr.value = psr_read();
	psr.cpl = PL_USER;
	psr.i = true;			/* start with interrupts enabled */
	psr.ic = true;
	psr.ri = 0;			/* start with instruction #0 */
	psr.bn = 1;			/* start in bank 0 */

	asm volatile ("mov %0 = ar.rsc\n" : "=r" (rsc.value));
	rsc.loadrs = 0;
	rsc.be = false;
	rsc.pl = PL_USER;
	rsc.mode = 3;			/* eager mode */

	switch_to_userspace((uintptr_t) kernel_uarg->uspace_entry,
	    ((uintptr_t) kernel_uarg->uspace_stack) + PAGE_SIZE -
	    ALIGN_UP(STACK_ITEM_SIZE, STACK_ALIGNMENT),
	    ((uintptr_t) kernel_uarg->uspace_stack) + PAGE_SIZE,
	    (uintptr_t) kernel_uarg->uspace_uarg, psr.value, rsc.value);

	while (1)
		;
}

/** Set thread-local-storage pointer.
 *
 * We use r13 (a.k.a. tp) for this purpose.
 */
unative_t sys_tls_set(unative_t addr)
{
	return 0;
}

void arch_reboot(void)
{
	pio_write_8((ioport8_t *)0x64, 0xfe);
	while (1)
		;
}

/** Construct function pointer
 *
 * @param fptr   function pointer structure
 * @param addr   function address
 * @param caller calling function address
 *
 * @return address of the function pointer
 *
 */
void *arch_construct_function(fncptr_t *fptr, void *addr, void *caller)
{
	fptr->fnc = (unative_t) addr;
	fptr->gp = ((unative_t *) caller)[1];
	
	return (void *) fptr;
}

void irq_initialize_arch(irq_t *irq)
{
	(void) irq;
}

/** @}
 */
