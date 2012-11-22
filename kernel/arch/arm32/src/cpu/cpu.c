/*
 * Copyright (c) 2007 Michal Kebrt
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

/** @addtogroup arm32
 * @{
 */
/** @file
 *  @brief CPU identification.
 */

#include <arch/cpu.h>
#include <cpu.h>
#include <arch.h>
#include <print.h>

/** Number of indexes left out in the #imp_data array */
#define IMP_DATA_START_OFFSET 0x40

/** Implementators (vendor) names */
static const char *imp_data[] = {
	"?",                                     /* IMP_DATA_START_OFFSET */
	"ARM Limited",                           /* 0x41 */
	"", "",                                  /* 0x42 - 0x43 */
	"Digital Equipment Corporation",         /* 0x44 */
	"", "", "", "", "", "", "", "",          /* 0x45 - 0x4c */
	"Motorola, Freescale Semicondutor Inc.", /* 0x4d */
	"", "", "",                              /* 0x4e - 0x50 */
	"Qualcomm Inc.",                         /* 0x51 */
	"", "", "", "",                          /* 0x52 - 0x55 */
	"Marvell Semiconductor",                 /* 0x56 */
	"", "", "", "", "", "", "", "", "", "",  /* 0x57 - 0x60 */
	"", "", "", "", "", "", "", "",          /* 0x61 - 0x68 */
	"Intel Corporation"                      /* 0x69 */
};

/** Length of the #imp_data array */
static unsigned int imp_data_length = sizeof(imp_data) / sizeof(char *);

/** Architecture names */
static const char *arch_data[] = {
	"?",       /* 0x0 */
	"4",       /* 0x1 */
	"4T",      /* 0x2 */
	"5",       /* 0x3 */
	"5T",      /* 0x4 */
	"5TE",     /* 0x5 */
	"5TEJ",    /* 0x6 */
	"6"        /* 0x7 */
};

/** Length of the #arch_data array */
static unsigned int arch_data_length = sizeof(arch_data) / sizeof(char *);


/** Retrieves processor identification from CP15 register 0.
 * 
 * @param cpu Structure for storing CPU identification.
 */
static void arch_cpu_identify(cpu_arch_t *cpu)
{
	uint32_t ident;
	asm volatile (
		"mrc p15, 0, %[ident], c0, c0, 0\n"
		: [ident] "=r" (ident)
	);
	
	cpu->imp_num = ident >> 24;
	cpu->variant_num = (ident << 8) >> 28;
	cpu->arch_num = (ident << 12) >> 28;
	cpu->prim_part_num = (ident << 16) >> 20;
	cpu->rev_num = (ident << 28) >> 28;
}

/** Enables unaligned access and caching for armv6+ */
void cpu_arch_init(void)
{
#if defined(PROCESSOR_armv7_a) | defined(PROCESSOR_armv6)
	uint32_t control_reg = 0;
	asm volatile (
		"mrc p15, 0, %[control_reg], c1, c0"
		: [control_reg] "=r" (control_reg)
	);
	
	/* Turn off tex remap, RAZ ignores writes prior to armv7 */
	control_reg &= ~CP15_R1_TEX_REMAP_EN;
	/* Turn off accessed flag, RAZ ignores writes prior to armv7 */
	control_reg &= ~(CP15_R1_ACCESS_FLAG_EN | CP15_R1_HW_ACCESS_FLAG_EN);
	/* Enable unaligned access, RAZ ignores writes prior to armv6
	 * switchable on armv6, RAO ignores writes on armv7,
	 * see ARM Architecture Reference Manual ARMv7-A and ARMv7-R edition
	 * L.3.1 (p. 2456) */
	control_reg |= CP15_R1_UNALIGNED_EN;
	/* Disable alignment checks, this turns unaligned access to undefined,
	 * unless U bit is set. */
	control_reg &= ~CP15_R1_ALIGN_CHECK_EN;
	/* Enable caching, On arm prior to armv7 there is only one level
	 * of caches. Data cache is coherent.
	 * "This means that the behavior of accesses from the same observer to
	 * different VAs, that are translated to the same PA
	 * with the same memory attributes, is fully coherent."
	 *    ARM Architecture Reference Manual ARMv7-A and ARMv7-R Edition
	 *    B3.11.1 (p. 1383)
	 * ICache coherency is elaborate on in barrier.h.
	 * We are safe to turn these on.
	 */
	control_reg |= CP15_R1_CACHE_EN | CP15_R1_INST_CACHE_EN;
	
	asm volatile (
		"mcr p15, 0, %[control_reg], c1, c0"
		:: [control_reg] "r" (control_reg)
	);
#endif
}

/** Retrieves processor identification and stores it to #CPU.arch */
void cpu_identify(void)
{
	arch_cpu_identify(&CPU->arch);
}

/** Prints CPU identification. */
void cpu_print_report(cpu_t *m)
{
	const char *vendor = imp_data[0];
	const char *architecture = arch_data[0];
	cpu_arch_t * cpu_arch = &m->arch;

	const unsigned imp_offset = cpu_arch->imp_num - IMP_DATA_START_OFFSET;

	if (imp_offset < imp_data_length) {
		vendor = imp_data[cpu_arch->imp_num - IMP_DATA_START_OFFSET];
	}

	// TODO CPUs with arch_num == 0xf use CPUID scheme for identification
	if (cpu_arch->arch_num < arch_data_length) {
		architecture = arch_data[cpu_arch->arch_num];
	}

	printf("cpu%d: vendor=%s, architecture=ARM%s, part number=%x, "
	    "variant=%x, revision=%x\n",
	    m->id, vendor, architecture, cpu_arch->prim_part_num,
	    cpu_arch->variant_num, cpu_arch->rev_num);
}

/** @}
 */
