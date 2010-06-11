/*
 * Copyright (c) 2001-2004 Jakub Jermar
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

#include <typedefs.h>
#include <arch/smp/apic.h>
#include <arch/smp/ap.h>
#include <arch/smp/mps.h>
#include <arch/boot/boot.h>
#include <mm/page.h>
#include <time/delay.h>
#include <interrupt.h>
#include <arch/interrupt.h>
#include <print.h>
#include <arch/asm.h>
#include <arch.h>
#include <ddi/irq.h>
#include <ddi/device.h>

#ifdef CONFIG_SMP

/*
 * Advanced Programmable Interrupt Controller for SMP systems.
 * Tested on:
 *    Bochs 2.0.2 - Bochs 2.2.6 with 2-8 CPUs
 *    Simics 2.0.28 - Simics 2.2.19 2-15 CPUs
 *    VMware Workstation 5.5 with 2 CPUs
 *    QEMU 0.8.0 with 2-15 CPUs
 *    ASUS P/I-P65UP5 + ASUS C-P55T2D REV. 1.41 with 2x 200Mhz Pentium CPUs
 *    ASUS PCH-DL with 2x 3000Mhz Pentium 4 Xeon (HT) CPUs
 *    MSI K7D Master-L with 2x 2100MHz Athlon MP CPUs
 *
 */

/*
 * These variables either stay configured as initilalized, or are changed by
 * the MP configuration code.
 *
 * Pay special attention to the volatile keyword. Without it, gcc -O2 would
 * optimize the code too much and accesses to l_apic and io_apic, that must
 * always be 32-bit, would use byte oriented instructions.
 *
 */
volatile uint32_t *l_apic = (uint32_t *) 0xfee00000;
volatile uint32_t *io_apic = (uint32_t *) 0xfec00000;

uint32_t apic_id_mask = 0;
static irq_t l_apic_timer_irq;

static int apic_poll_errors(void);

#ifdef LAPIC_VERBOSE
static const char *delmod_str[] = {
	"Fixed",
	"Lowest Priority",
	"SMI",
	"Reserved",
	"NMI",
	"INIT",
	"STARTUP",
	"ExtInt"
};

static const char *destmod_str[] = {
	"Physical",
	"Logical"
};

static const char *trigmod_str[] = {
	"Edge",
	"Level"
};

static const char *mask_str[] = {
	"Unmasked",
	"Masked"
};

static const char *delivs_str[] = {
	"Idle",
	"Send Pending"
};

static const char *tm_mode_str[] = {
	"One-shot",
	"Periodic"
};

static const char *intpol_str[] = {
	"Polarity High",
	"Polarity Low"
};
#endif /* LAPIC_VERBOSE */

/** APIC spurious interrupt handler.
 *
 * @param n      Interrupt vector.
 * @param istate Interrupted state.
 *
 */
static void apic_spurious(unsigned int n __attribute__((unused)),
    istate_t *istate __attribute__((unused)))
{
#ifdef CONFIG_DEBUG
	printf("cpu%u: APIC spurious interrupt\n", CPU->id);
#endif
}

static irq_ownership_t l_apic_timer_claim(irq_t *irq)
{
	return IRQ_ACCEPT;
}

static void l_apic_timer_irq_handler(irq_t *irq)
{
	/*
	 * Holding a spinlock could prevent clock() from preempting
	 * the current thread. In this case, we don't need to hold the
	 * irq->lock so we just unlock it and then lock it again.
	 */
	irq_spinlock_unlock(&irq->lock, false);
	clock();
	irq_spinlock_lock(&irq->lock, false);
}

/** Initialize APIC on BSP. */
void apic_init(void)
{
	exc_register(VECTOR_APIC_SPUR, "apic_spurious", false,
	    (iroutine_t) apic_spurious);
	
	enable_irqs_function = io_apic_enable_irqs;
	disable_irqs_function = io_apic_disable_irqs;
	eoi_function = l_apic_eoi;
	
	/*
	 * Configure interrupt routing.
	 * IRQ 0 remains masked as the time signal is generated by l_apic's themselves.
	 * Other interrupts will be forwarded to the lowest priority CPU.
	 */
	io_apic_disable_irqs(0xffff);
	
	irq_initialize(&l_apic_timer_irq);
	l_apic_timer_irq.preack = true;
	l_apic_timer_irq.devno = device_assign_devno();
	l_apic_timer_irq.inr = IRQ_CLK;
	l_apic_timer_irq.claim = l_apic_timer_claim;
	l_apic_timer_irq.handler = l_apic_timer_irq_handler;
	irq_register(&l_apic_timer_irq);
	
	uint8_t i;
	for (i = 0; i < IRQ_COUNT; i++) {
		int pin;
		
		if ((pin = smp_irq_to_pin(i)) != -1)
			io_apic_change_ioredtbl((uint8_t) pin, DEST_ALL, (uint8_t) (IVT_IRQBASE + i), LOPRI);
	}
	
	/*
	 * Ensure that io_apic has unique ID.
	 */
	io_apic_id_t idreg;
	
	idreg.value = io_apic_read(IOAPICID);
	if ((1 << idreg.apic_id) & apic_id_mask) {  /* See if IO APIC ID is used already */
		for (i = 0; i < APIC_ID_COUNT; i++) {
			if (!((1 << i) & apic_id_mask)) {
				idreg.apic_id = i;
				io_apic_write(IOAPICID, idreg.value);
				break;
			}
		}
	}
	
	/*
	 * Configure the BSP's lapic.
	 */
	l_apic_init();
	l_apic_debug();
}

/** Poll for APIC errors.
 *
 * Examine Error Status Register and report all errors found.
 *
 * @return 0 on error, 1 on success.
 *
 */
int apic_poll_errors(void)
{
	esr_t esr;
	
	esr.value = l_apic[ESR];
	
	if (esr.send_checksum_error)
		printf("Send Checksum Error\n");
	if (esr.receive_checksum_error)
		printf("Receive Checksum Error\n");
	if (esr.send_accept_error)
		printf("Send Accept Error\n");
	if (esr.receive_accept_error)
		printf("Receive Accept Error\n");
	if (esr.send_illegal_vector)
		printf("Send Illegal Vector\n");
	if (esr.received_illegal_vector)
		printf("Received Illegal Vector\n");
	if (esr.illegal_register_address)
		printf("Illegal Register Address\n");
	
	return !esr.err_bitmap;
}

/** Send all CPUs excluding CPU IPI vector.
 *
 * @param vector Interrupt vector to be sent.
 *
 * @return 0 on failure, 1 on success.
 *
 */
int l_apic_broadcast_custom_ipi(uint8_t vector)
{
	icr_t icr;
	
	icr.lo = l_apic[ICRlo];
	icr.delmod = DELMOD_FIXED;
	icr.destmod = DESTMOD_LOGIC;
	icr.level = LEVEL_ASSERT;
	icr.shorthand = SHORTHAND_ALL_EXCL;
	icr.trigger_mode = TRIGMOD_LEVEL;
	icr.vector = vector;
	
	l_apic[ICRlo] = icr.lo;
	
	icr.lo = l_apic[ICRlo];
	if (icr.delivs == DELIVS_PENDING) {
#ifdef CONFIG_DEBUG
		printf("IPI is pending.\n");
#endif
	}
	
	return apic_poll_errors();
}

/** Universal Start-up Algorithm for bringing up the AP processors.
 *
 * @param apicid APIC ID of the processor to be brought up.
 *
 * @return 0 on failure, 1 on success.
 *
 */
int l_apic_send_init_ipi(uint8_t apicid)
{
	/*
	 * Read the ICR register in and zero all non-reserved fields.
	 */
	icr_t icr;
	
	icr.lo = l_apic[ICRlo];
	icr.hi = l_apic[ICRhi];
	
	icr.delmod = DELMOD_INIT;
	icr.destmod = DESTMOD_PHYS;
	icr.level = LEVEL_ASSERT;
	icr.trigger_mode = TRIGMOD_LEVEL;
	icr.shorthand = SHORTHAND_NONE;
	icr.vector = 0;
	icr.dest = apicid;
	
	l_apic[ICRhi] = icr.hi;
	l_apic[ICRlo] = icr.lo;
	
	/*
	 * According to MP Specification, 20us should be enough to
	 * deliver the IPI.
	 */
	delay(20);
	
	if (!apic_poll_errors())
		return 0;
	
	icr.lo = l_apic[ICRlo];
	if (icr.delivs == DELIVS_PENDING) {
#ifdef CONFIG_DEBUG
		printf("IPI is pending.\n");
#endif
	}
	
	icr.delmod = DELMOD_INIT;
	icr.destmod = DESTMOD_PHYS;
	icr.level = LEVEL_DEASSERT;
	icr.shorthand = SHORTHAND_NONE;
	icr.trigger_mode = TRIGMOD_LEVEL;
	icr.vector = 0;
	l_apic[ICRlo] = icr.lo;
	
	/*
	 * Wait 10ms as MP Specification specifies.
	 */
	delay(10000);
	
	if (!is_82489DX_apic(l_apic[LAVR])) {
		/*
		 * If this is not 82489DX-based l_apic we must send two STARTUP IPI's.
		 */
		unsigned int i;
		for (i = 0; i < 2; i++) {
			icr.lo = l_apic[ICRlo];
			icr.vector = (uint8_t) (((uintptr_t) ap_boot) >> 12); /* calculate the reset vector */
			icr.delmod = DELMOD_STARTUP;
			icr.destmod = DESTMOD_PHYS;
			icr.level = LEVEL_ASSERT;
			icr.shorthand = SHORTHAND_NONE;
			icr.trigger_mode = TRIGMOD_LEVEL;
			l_apic[ICRlo] = icr.lo;
			delay(200);
		}
	}
	
	return apic_poll_errors();
}

/** Initialize Local APIC. */
void l_apic_init(void)
{
	/* Initialize LVT Error register. */
	lvt_error_t error;
	
	error.value = l_apic[LVT_Err];
	error.masked = true;
	l_apic[LVT_Err] = error.value;
	
	/* Initialize LVT LINT0 register. */
	lvt_lint_t lint;
	
	lint.value = l_apic[LVT_LINT0];
	lint.masked = true;
	l_apic[LVT_LINT0] = lint.value;
	
	/* Initialize LVT LINT1 register. */
	lint.value = l_apic[LVT_LINT1];
	lint.masked = true;
	l_apic[LVT_LINT1] = lint.value;
	
	/* Task Priority Register initialization. */
	tpr_t tpr;
	
	tpr.value = l_apic[TPR];
	tpr.pri_sc = 0;
	tpr.pri = 0;
	l_apic[TPR] = tpr.value;
	
	/* Spurious-Interrupt Vector Register initialization. */
	svr_t svr;
	
	svr.value = l_apic[SVR];
	svr.vector = VECTOR_APIC_SPUR;
	svr.lapic_enabled = true;
	svr.focus_checking = true;
	l_apic[SVR] = svr.value;
	
	if (CPU->arch.family >= 6)
		enable_l_apic_in_msr();
	
	/* Interrupt Command Register initialization. */
	icr_t icr;
	
	icr.lo = l_apic[ICRlo];
	icr.delmod = DELMOD_INIT;
	icr.destmod = DESTMOD_PHYS;
	icr.level = LEVEL_DEASSERT;
	icr.shorthand = SHORTHAND_ALL_INCL;
	icr.trigger_mode = TRIGMOD_LEVEL;
	l_apic[ICRlo] = icr.lo;
	
	/* Timer Divide Configuration Register initialization. */
	tdcr_t tdcr;
	
	tdcr.value = l_apic[TDCR];
	tdcr.div_value = DIVIDE_1;
	l_apic[TDCR] = tdcr.value;
	
	/* Program local timer. */
	lvt_tm_t tm;
	
	tm.value = l_apic[LVT_Tm];
	tm.vector = VECTOR_CLK;
	tm.mode = TIMER_PERIODIC;
	tm.masked = false;
	l_apic[LVT_Tm] = tm.value;
	
	/*
	 * Measure and configure the timer to generate timer
	 * interrupt with period 1s/HZ seconds.
	 */
	uint32_t t1 = l_apic[CCRT];
	l_apic[ICRT] = 0xffffffff;
	
	while (l_apic[CCRT] == t1);
	
	t1 = l_apic[CCRT];
	delay(1000000 / HZ);
	uint32_t t2 = l_apic[CCRT];
	
	l_apic[ICRT] = t1 - t2;
	
	/* Program Logical Destination Register. */
	ASSERT(CPU->id < 8);
	ldr_t ldr;
	
	ldr.value = l_apic[LDR];
	ldr.id = (uint8_t) (1 << CPU->id);
	l_apic[LDR] = ldr.value;
	
	/* Program Destination Format Register for Flat mode. */
	dfr_t dfr;
	
	dfr.value = l_apic[DFR];
	dfr.model = MODEL_FLAT;
	l_apic[DFR] = dfr.value;
}

/** Local APIC End of Interrupt. */
void l_apic_eoi(void)
{
	l_apic[EOI] = 0;
}

/** Dump content of Local APIC registers. */
void l_apic_debug(void)
{
#ifdef LAPIC_VERBOSE
	printf("LVT on cpu%" PRIs ", LAPIC ID: %" PRIu8 "\n", CPU->id, l_apic_id());
	
	lvt_tm_t tm;
	tm.value = l_apic[LVT_Tm];
	printf("LVT Tm: vector=%hhd, %s, %s, %s\n", tm.vector, delivs_str[tm.delivs], mask_str[tm.masked], tm_mode_str[tm.mode]);
	
	lvt_lint_t lint;
	lint.value = l_apic[LVT_LINT0];
	printf("LVT LINT0: vector=%hhd, %s, %s, %s, irr=%d, %s, %s\n", tm.vector, delmod_str[lint.delmod], delivs_str[lint.delivs], intpol_str[lint.intpol], lint.irr, trigmod_str[lint.trigger_mode], mask_str[lint.masked]);
	lint.value = l_apic[LVT_LINT1];	
	printf("LVT LINT1: vector=%hhd, %s, %s, %s, irr=%d, %s, %s\n", tm.vector, delmod_str[lint.delmod], delivs_str[lint.delivs], intpol_str[lint.intpol], lint.irr, trigmod_str[lint.trigger_mode], mask_str[lint.masked]);	
	
	lvt_error_t error;
	error.value = l_apic[LVT_Err];
	printf("LVT Err: vector=%hhd, %s, %s\n", error.vector, delivs_str[error.delivs], mask_str[error.masked]);
#endif
}

/** Get Local APIC ID.
 *
 * @return Local APIC ID.
 *
 */
uint8_t l_apic_id(void)
{
	l_apic_id_t idreg;
	
	idreg.value = l_apic[L_APIC_ID];
	return idreg.apic_id;
}

/** Read from IO APIC register.
 *
 * @param address IO APIC register address.
 *
 * @return Content of the addressed IO APIC register.
 *
 */
uint32_t io_apic_read(uint8_t address)
{
	io_regsel_t regsel;
	
	regsel.value = io_apic[IOREGSEL];
	regsel.reg_addr = address;
	io_apic[IOREGSEL] = regsel.value;
	return io_apic[IOWIN];
}

/** Write to IO APIC register.
 *
 * @param address IO APIC register address.
 * @param val     Content to be written to the addressed IO APIC register.
 *
 */
void io_apic_write(uint8_t address, uint32_t val)
{
	io_regsel_t regsel;
	
	regsel.value = io_apic[IOREGSEL];
	regsel.reg_addr = address;
	io_apic[IOREGSEL] = regsel.value;
	io_apic[IOWIN] = val;
}

/** Change some attributes of one item in I/O Redirection Table.
 *
 * @param pin   IO APIC pin number.
 * @param dest  Interrupt destination address.
 * @param vec   Interrupt vector to trigger.
 * @param flags Flags.
 *
 */
void io_apic_change_ioredtbl(uint8_t pin, uint8_t dest, uint8_t vec,
    unsigned int flags)
{
	unsigned int dlvr;
	
	if (flags & LOPRI)
		dlvr = DELMOD_LOWPRI;
	else
		dlvr = DELMOD_FIXED;
	
	io_redirection_reg_t reg;
	reg.lo = io_apic_read((uint8_t) (IOREDTBL + pin * 2));
	reg.hi = io_apic_read((uint8_t) (IOREDTBL + pin * 2 + 1));
	
	reg.dest = dest;
	reg.destmod = DESTMOD_LOGIC;
	reg.trigger_mode = TRIGMOD_EDGE;
	reg.intpol = POLARITY_HIGH;
	reg.delmod = dlvr;
	reg.intvec = vec;
	
	io_apic_write((uint8_t) (IOREDTBL + pin * 2), reg.lo);
	io_apic_write((uint8_t) (IOREDTBL + pin * 2 + 1), reg.hi);
}

/** Mask IRQs in IO APIC.
 *
 * @param irqmask Bitmask of IRQs to be masked (0 = do not mask, 1 = mask).
 *
 */
void io_apic_disable_irqs(uint16_t irqmask)
{
	unsigned int i;
	for (i = 0; i < 16; i++) {
		if (irqmask & (1 << i)) {
			/*
			 * Mask the signal input in IO APIC if there is a
			 * mapping for the respective IRQ number.
			 */
			int pin = smp_irq_to_pin(i);
			if (pin != -1) {
				io_redirection_reg_t reg;
				
				reg.lo = io_apic_read((uint8_t) (IOREDTBL + pin * 2));
				reg.masked = true;
				io_apic_write((uint8_t) (IOREDTBL + pin * 2), reg.lo);
			}
			
		}
	}
}

/** Unmask IRQs in IO APIC.
 *
 * @param irqmask Bitmask of IRQs to be unmasked (0 = do not unmask, 1 = unmask).
 *
 */
void io_apic_enable_irqs(uint16_t irqmask)
{
	unsigned int i;
	for (i = 0; i < 16; i++) {
		if (irqmask & (1 << i)) {
			/*
			 * Unmask the signal input in IO APIC if there is a
			 * mapping for the respective IRQ number.
			 */
			int pin = smp_irq_to_pin(i);
			if (pin != -1) {
				io_redirection_reg_t reg;
				
				reg.lo = io_apic_read((uint8_t) (IOREDTBL + pin * 2));
				reg.masked = false;
				io_apic_write((uint8_t) (IOREDTBL + pin * 2), reg.lo);
			}
			
		}
	}
}

#endif /* CONFIG_SMP */

/** @}
 */
