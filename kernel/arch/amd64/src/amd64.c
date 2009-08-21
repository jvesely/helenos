/*
 * Copyright (c) 2005 Ondrej Palkovsky
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

/** @addtogroup amd64
 * @{
 */
/** @file
 */

#include <arch.h>

#include <arch/types.h>

#include <config.h>

#include <proc/thread.h>
#include <genarch/multiboot/multiboot.h>
#include <genarch/drivers/legacy/ia32/io.h>
#include <genarch/drivers/ega/ega.h>
#include <arch/drivers/vesa.h>
#include <genarch/drivers/i8042/i8042.h>
#include <genarch/kbrd/kbrd.h>
#include <arch/drivers/i8254.h>
#include <arch/drivers/i8259.h>
#include <arch/boot/boot.h>

#ifdef CONFIG_SMP
#include <arch/smp/apic.h>
#endif

#include <arch/bios/bios.h>
#include <arch/cpu.h>
#include <print.h>
#include <arch/cpuid.h>
#include <genarch/acpi/acpi.h>
#include <panic.h>
#include <interrupt.h>
#include <arch/syscall.h>
#include <arch/debugger.h>
#include <syscall/syscall.h>
#include <console/console.h>
#include <ddi/irq.h>
#include <sysinfo/sysinfo.h>

/** Disable I/O on non-privileged levels
 *
 * Clean IOPL(12,13) and NT(14) flags in EFLAGS register
 */
static void clean_IOPL_NT_flags(void)
{
	asm volatile (
		"pushfq\n"
		"pop %%rax\n"
		"and $~(0x7000), %%rax\n"
		"pushq %%rax\n"
		"popfq\n"
		::: "%rax"
	);
}

/** Disable alignment check
 *
 * Clean AM(18) flag in CR0 register 
 */
static void clean_AM_flag(void)
{
	asm volatile (
		"mov %%cr0, %%rax\n"
		"and $~(0x40000), %%rax\n"
		"mov %%rax, %%cr0\n"
		::: "%rax"
	);
}

/** Perform amd64-specific initialization before main_bsp() is called.
 *
 * @param signature Should contain the multiboot signature.
 * @param mi        Pointer to the multiboot information structure.
 */
void arch_pre_main(uint32_t signature, const multiboot_info_t *mi)
{
	/* Parse multiboot information obtained from the bootloader. */
	multiboot_info_parse(signature, mi);
	
#ifdef CONFIG_SMP
	/* Copy AP bootstrap routines below 1 MB. */
	memcpy((void *) AP_BOOT_OFFSET, (void *) BOOT_OFFSET,
	    (size_t) &_hardcoded_unmapped_size);
#endif
}

void arch_pre_mm_init(void)
{
	/* Enable no-execute pages */
	set_efer_flag(AMD_NXE_FLAG);
	/* Enable FPU */
	cpu_setup_fpu();

	/* Initialize segmentation */
	pm_init();
	
	/* Disable I/O on nonprivileged levels
	 * clear the NT (nested-thread) flag 
	 */
	clean_IOPL_NT_flags();
	/* Disable alignment check */
	clean_AM_flag();

	if (config.cpu_active == 1) {
		interrupt_init();
		bios_init();
		
		/* PIC */
		i8259_init();
	}
}


void arch_post_mm_init(void)
{
	if (config.cpu_active == 1) {
		/* Initialize IRQ routing */
		irq_init(IRQ_COUNT, IRQ_COUNT);
		
		/* hard clock */
		i8254_init();
		
#if (defined(CONFIG_FB) || defined(CONFIG_EGA))
		bool vesa = false;
#endif
		
#ifdef CONFIG_FB
		vesa = vesa_init();
#endif
		
#ifdef CONFIG_EGA
		if (!vesa) {
			outdev_t *egadev = ega_init(EGA_BASE, EGA_VIDEORAM);
			if (egadev)
				stdout_wire(egadev);
		}
#endif
		
		/* Enable debugger */
		debugger_init();
		/* Merge all memory zones to 1 big zone */
		zone_merge_all();
	}
	
	/* Setup fast SYSCALL/SYSRET */
	syscall_setup_cpu();
}

void arch_post_cpu_init()
{
#ifdef CONFIG_SMP
	if (config.cpu_active > 1) {
		l_apic_init();
		l_apic_debug();
	}
#endif
}

void arch_pre_smp_init(void)
{
	if (config.cpu_active == 1) {
#ifdef CONFIG_SMP
		acpi_init();
#endif /* CONFIG_SMP */
	}
}

void arch_post_smp_init(void)
{
#ifdef CONFIG_PC_KBD
	/*
	 * Initialize the i8042 controller. Then initialize the keyboard
	 * module and connect it to i8042. Enable keyboard interrupts.
	 */
	i8042_instance_t *i8042_instance = i8042_init((i8042_t *) I8042_BASE, IRQ_KBD);
	if (i8042_instance) {
		kbrd_instance_t *kbrd_instance = kbrd_init();
		if (kbrd_instance) {
			indev_t *sink = stdin_wire();
			indev_t *kbrd = kbrd_wire(kbrd_instance, sink);
			i8042_wire(i8042_instance, kbrd);
			trap_virtual_enable_irqs(1 << IRQ_KBD);
		}
	}
	
	/*
	 * This is the necessary evil until the userspace driver is entirely
	 * self-sufficient.
	 */
	sysinfo_set_item_val("kbd", NULL, true);
	sysinfo_set_item_val("kbd.inr", NULL, IRQ_KBD);
	sysinfo_set_item_val("kbd.address.physical", NULL,
	    (uintptr_t) I8042_BASE);
	sysinfo_set_item_val("kbd.address.kernel", NULL,
	    (uintptr_t) I8042_BASE);
#endif
}

void calibrate_delay_loop(void)
{
	i8254_calibrate_delay_loop();
	if (config.cpu_active == 1) {
		/*
		 * This has to be done only on UP.
		 * On SMP, i8254 is not used for time keeping and its interrupt pin remains masked.
		 */
		i8254_normal_operation();
	}
}

/** Set thread-local-storage pointer
 *
 * TLS pointer is set in FS register. Unfortunately the 64-bit
 * part can be set only in CPL0 mode.
 *
 * The specs say, that on %fs:0 there is stored contents of %fs register,
 * we need not to go to CPL0 to read it.
 */
unative_t sys_tls_set(unative_t addr)
{
	THREAD->arch.tls = addr;
	write_msr(AMD_MSR_FS, addr);
	return 0;
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
	return addr;
}

void arch_reboot(void)
{
#ifdef CONFIG_PC_KBD
	i8042_cpu_reset((i8042_t *) I8042_BASE);
#endif
}

/** @}
 */
