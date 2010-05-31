/*
 * Copyright (c) 2008 Jakub Jermar
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

/** @addtogroup ia32
 * @{
 */
/** @file
 */

#include <smp/smp.h>
#include <arch/smp/smp.h>
#include <arch/smp/mps.h>
#include <arch/smp/ap.h>
#include <arch/boot/boot.h>
#include <genarch/acpi/acpi.h>
#include <genarch/acpi/madt.h>
#include <config.h>
#include <synch/waitq.h>
#include <synch/synch.h>
#include <arch/pm.h>
#include <func.h>
#include <panic.h>
#include <debug.h>
#include <arch/asm.h>
#include <mm/frame.h>
#include <mm/page.h>
#include <mm/slab.h>
#include <mm/as.h>
#include <print.h>
#include <memstr.h>
#include <arch/drivers/i8259.h>

#ifdef CONFIG_SMP

static struct smp_config_operations *ops = NULL;

void smp_init(void)
{
	uintptr_t l_apic_address, io_apic_address;

	if (acpi_madt) {
		acpi_madt_parse();
		ops = &madt_config_operations;
	}
	if (config.cpu_count == 1) {
		mps_init();
		ops = &mps_config_operations;
	}

	l_apic_address = (uintptr_t) frame_alloc(ONE_FRAME,
	    FRAME_ATOMIC | FRAME_KA);
	if (!l_apic_address)
		panic("Cannot allocate address for l_apic.");

	io_apic_address = (uintptr_t) frame_alloc(ONE_FRAME,
	    FRAME_ATOMIC | FRAME_KA);
	if (!io_apic_address)
		panic("Cannot allocate address for io_apic.");

	if (config.cpu_count > 1) {
		page_table_lock(AS_KERNEL, true);
		page_mapping_insert(AS_KERNEL, l_apic_address,
		    (uintptr_t) l_apic, PAGE_NOT_CACHEABLE | PAGE_WRITE);
		page_mapping_insert(AS_KERNEL, io_apic_address,
		    (uintptr_t) io_apic, PAGE_NOT_CACHEABLE | PAGE_WRITE);
		page_table_unlock(AS_KERNEL, true);
				  
		l_apic = (uint32_t *) l_apic_address;
		io_apic = (uint32_t *) io_apic_address;
	}
}

/*
 * Kernel thread for bringing up application processors. It becomes clear
 * that we need an arrangement like this (AP's being initialized by a kernel
 * thread), for a thread has its dedicated stack. (The stack used during the
 * BSP initialization (prior the very first call to scheduler()) will be used
 * as an initialization stack for each AP.)
 */
void kmp(void *arg __attribute__((unused)))
{
	unsigned int i;
	
	ASSERT(ops != NULL);

	/*
	 * We need to access data in frame 0.
	 * We boldly make use of kernel address space mapping.
	 */

	/*
	 * Set the warm-reset vector to the real-mode address of 4K-aligned ap_boot()
	 */
	*((uint16_t *) (PA2KA(0x467 + 0))) =
	    (uint16_t) (((uintptr_t) ap_boot) >> 4);	/* segment */
	*((uint16_t *) (PA2KA(0x467 + 2))) = 0;		/* offset */
	
	/*
	 * Save 0xa to address 0xf of the CMOS RAM.
	 * BIOS will not do the POST after the INIT signal.
	 */
	pio_write_8((ioport8_t *)0x70, 0xf);
	pio_write_8((ioport8_t *)0x71, 0xa);

	pic_disable_irqs(0xffff);
	apic_init();
	
	uint8_t apic = l_apic_id();

	for (i = 0; i < ops->cpu_count(); i++) {
		descriptor_t *gdt_new;
		
		/*
		 * Skip processors marked unusable.
		 */
		if (!ops->cpu_enabled(i))
			continue;

		/*
		 * The bootstrap processor is already up.
		 */
		if (ops->cpu_bootstrap(i))
			continue;

		if (ops->cpu_apic_id(i) == apic) {
			printf("%s: bad processor entry #%u, will not send IPI "
			    "to myself\n", __FUNCTION__, i);
			continue;
		}
		
		/*
		 * Prepare new GDT for CPU in question.
		 */
		
		/* XXX Flag FRAME_LOW_4_GiB was removed temporarily,
		 * it needs to be replaced by a generic fuctionality of
		 * the memory subsystem
		 */
		gdt_new = (descriptor_t *) malloc(GDT_ITEMS *
		    sizeof(descriptor_t), FRAME_ATOMIC);
		if (!gdt_new)
			panic("Cannot allocate memory for GDT.");

		memcpy(gdt_new, gdt, GDT_ITEMS * sizeof(descriptor_t));
		memsetb(&gdt_new[TSS_DES], sizeof(descriptor_t), 0);
		protected_ap_gdtr.limit = GDT_ITEMS * sizeof(descriptor_t);
		protected_ap_gdtr.base = KA2PA((uintptr_t) gdt_new);
		gdtr.base = (uintptr_t) gdt_new;

		if (l_apic_send_init_ipi(ops->cpu_apic_id(i))) {
			/*
			 * There may be just one AP being initialized at
			 * the time. After it comes completely up, it is
			 * supposed to wake us up.
			 */
			if (waitq_sleep_timeout(&ap_completion_wq, 1000000,
			    SYNCH_FLAGS_NONE) == ESYNCH_TIMEOUT) {
				unsigned int cpu = (config.cpu_active > i) ?
				    config.cpu_active : i;
				printf("%s: waiting for cpu%u (APIC ID = %d) "
				    "timed out\n", __FUNCTION__, cpu,
				    ops->cpu_apic_id(i));
			}
		} else
			printf("INIT IPI for l_apic%d failed\n",
			    ops->cpu_apic_id(i));
	}
}

int smp_irq_to_pin(unsigned int irq)
{
	ASSERT(ops != NULL);
	return ops->irq_to_pin(irq);
}

#endif /* CONFIG_SMP */

/** @}
 */
