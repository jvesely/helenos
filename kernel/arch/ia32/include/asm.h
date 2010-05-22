/*
 * Copyright (c) 2001-2004 Jakub Jermar
 * Copyright (c) 2005 Sergey Bondari
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

#ifndef KERN_ia32_ASM_H_
#define KERN_ia32_ASM_H_

#include <arch/pm.h>
#include <arch/cpu.h>
#include <typedefs.h>
#include <config.h>

extern uint32_t interrupt_handler_size;

extern void paging_on(void);

extern void interrupt_handlers(void);

extern void enable_l_apic_in_msr(void);


extern void asm_delay_loop(uint32_t t);
extern void asm_fake_loop(uint32_t t);


/** Halt CPU
 *
 * Halt the current CPU.
 *
 */
static inline __attribute__((noreturn)) void cpu_halt(void)
{
	while (true) {
		asm volatile (
			"hlt\n"
		);
	}
}

static inline void cpu_sleep(void)
{
	asm volatile ("hlt\n");
}

#define GEN_READ_REG(reg) static inline unative_t read_ ##reg (void) \
	{ \
		unative_t res; \
		asm volatile ( \
			"movl %%" #reg ", %[res]" \
			: [res] "=r" (res) \
		); \
		return res; \
	}

#define GEN_WRITE_REG(reg) static inline void write_ ##reg (unative_t regn) \
	{ \
		asm volatile ( \
			"movl %[regn], %%" #reg \
			:: [regn] "r" (regn) \
		); \
	}

GEN_READ_REG(cr0)
GEN_READ_REG(cr2)
GEN_READ_REG(cr3)
GEN_WRITE_REG(cr3)

GEN_READ_REG(dr0)
GEN_READ_REG(dr1)
GEN_READ_REG(dr2)
GEN_READ_REG(dr3)
GEN_READ_REG(dr6)
GEN_READ_REG(dr7)

GEN_WRITE_REG(dr0)
GEN_WRITE_REG(dr1)
GEN_WRITE_REG(dr2)
GEN_WRITE_REG(dr3)
GEN_WRITE_REG(dr6)
GEN_WRITE_REG(dr7)

/** Byte to port
 *
 * Output byte to port
 *
 * @param port Port to write to
 * @param val Value to write
 *
 */
static inline void pio_write_8(ioport8_t *port, uint8_t val)
{
	asm volatile (
		"outb %b[val], %w[port]\n"
		:: [val] "a" (val), [port] "d" (port)
	);
}

/** Word to port
 *
 * Output word to port
 *
 * @param port Port to write to
 * @param val Value to write
 *
 */
static inline void pio_write_16(ioport16_t *port, uint16_t val)
{
	asm volatile (
		"outw %w[val], %w[port]\n"
		:: [val] "a" (val), [port] "d" (port)
	);
}

/** Double word to port
 *
 * Output double word to port
 *
 * @param port Port to write to
 * @param val Value to write
 *
 */
static inline void pio_write_32(ioport32_t *port, uint32_t val)
{
	asm volatile (
		"outl %[val], %w[port]\n"
		:: [val] "a" (val), [port] "d" (port)
	);
}

/** Byte from port
 *
 * Get byte from port
 *
 * @param port Port to read from
 * @return Value read
 *
 */
static inline uint8_t pio_read_8(ioport8_t *port)
{
	uint8_t val;
	
	asm volatile (
		"inb %w[port], %b[val]\n"
		: [val] "=a" (val)
		: [port] "d" (port)
	);
	
	return val;
}

/** Word from port
 *
 * Get word from port
 *
 * @param port Port to read from
 * @return Value read
 *
 */
static inline uint16_t pio_read_16(ioport16_t *port)
{
	uint16_t val;
	
	asm volatile (
		"inw %w[port], %w[val]\n"
		: [val] "=a" (val)
		: [port] "d" (port)
	);
	
	return val;
}

/** Double word from port
 *
 * Get double word from port
 *
 * @param port Port to read from
 * @return Value read
 *
 */
static inline uint32_t pio_read_32(ioport32_t *port)
{
	uint32_t val;
	
	asm volatile (
		"inl %w[port], %[val]\n"
		: [val] "=a" (val)
		: [port] "d" (port)
	);
	
	return val;
}

/** Enable interrupts.
 *
 * Enable interrupts and return previous
 * value of EFLAGS.
 *
 * @return Old interrupt priority level.
 *
 */
static inline ipl_t interrupts_enable(void)
{
	ipl_t v;
	
	asm volatile (
		"pushf\n"
		"popl %[v]\n"
		"sti\n"
		: [v] "=r" (v)
	);
	
	return v;
}

/** Disable interrupts.
 *
 * Disable interrupts and return previous
 * value of EFLAGS.
 *
 * @return Old interrupt priority level.
 *
 */
static inline ipl_t interrupts_disable(void)
{
	ipl_t v;
	
	asm volatile (
		"pushf\n"
		"popl %[v]\n"
		"cli\n"
		: [v] "=r" (v)
	);
	
	return v;
}

/** Restore interrupt priority level.
 *
 * Restore EFLAGS.
 *
 * @param ipl Saved interrupt priority level.
 *
 */
static inline void interrupts_restore(ipl_t ipl)
{
	asm volatile (
		"pushl %[ipl]\n"
		"popf\n"
		:: [ipl] "r" (ipl)
	);
}

/** Return interrupt priority level.
 *
 * @return EFLAFS.
 *
 */
static inline ipl_t interrupts_read(void)
{
	ipl_t v;
	
	asm volatile (
		"pushf\n"
		"popl %[v]\n"
		: [v] "=r" (v)
	);
	
	return v;
}

/** Check interrupts state.
 *
 * @return True if interrupts are disabled.
 *
 */
static inline bool interrupts_disabled(void)
{
	ipl_t v;
	
	asm volatile (
		"pushf\n"
		"popl %[v]\n"
		: [v] "=r" (v)
	);
	
	return ((v & EFLAGS_IF) == 0);
}

/** Write to MSR */
static inline void write_msr(uint32_t msr, uint64_t value)
{
	asm volatile (
		"wrmsr"
		:: "c" (msr), "a" ((uint32_t) (value)),
		   "d" ((uint32_t) (value >> 32))
	);
}

static inline uint64_t read_msr(uint32_t msr)
{
	uint32_t ax, dx;
	
	asm volatile (
		"rdmsr"
		: "=a" (ax), "=d" (dx)
		: "c" (msr)
	);
	
	return ((uint64_t) dx << 32) | ax;
}


/** Return base address of current stack
 *
 * Return the base address of the current stack.
 * The stack is assumed to be STACK_SIZE bytes long.
 * The stack must start on page boundary.
 *
 */
static inline uintptr_t get_stack_base(void)
{
	uintptr_t v;
	
	asm volatile (
		"andl %%esp, %[v]\n"
		: [v] "=r" (v)
		: "0" (~(STACK_SIZE - 1))
	);
	
	return v;
}

/** Invalidate TLB Entry.
 *
 * @param addr Address on a page whose TLB entry is to be invalidated.
 *
 */
static inline void invlpg(uintptr_t addr)
{
	asm volatile (
		"invlpg %[addr]\n"
		:: [addr] "m" (*(unative_t *) addr)
	);
}

/** Load GDTR register from memory.
 *
 * @param gdtr_reg Address of memory from where to load GDTR.
 *
 */
static inline void gdtr_load(ptr_16_32_t *gdtr_reg)
{
	asm volatile (
		"lgdtl %[gdtr_reg]\n"
		:: [gdtr_reg] "m" (*gdtr_reg)
	);
}

/** Store GDTR register to memory.
 *
 * @param gdtr_reg Address of memory to where to load GDTR.
 *
 */
static inline void gdtr_store(ptr_16_32_t *gdtr_reg)
{
	asm volatile (
		"sgdtl %[gdtr_reg]\n"
		: [gdtr_reg] "=m" (*gdtr_reg)
	);
}

/** Load IDTR register from memory.
 *
 * @param idtr_reg Address of memory from where to load IDTR.
 *
 */
static inline void idtr_load(ptr_16_32_t *idtr_reg)
{
	asm volatile (
		"lidtl %[idtr_reg]\n" 
		:: [idtr_reg] "m" (*idtr_reg)
	);
}

/** Load TR from descriptor table.
 *
 * @param sel Selector specifying descriptor of TSS segment.
 *
 */
static inline void tr_load(uint16_t sel)
{
	asm volatile (
		"ltr %[sel]"
		:: [sel] "r" (sel)
	);
}

#endif

/** @}
 */
