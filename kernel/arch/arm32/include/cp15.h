/*
 * Copyright (c) 2013 Jan Vesely
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
 *  @brief System Control Coprocessor (CP15)
 */

#ifndef KERN_arm32_CP15_H_
#define KERN_arm32_CP15_H_


/** See ARM Architecture reference manual ch. B3.17.1 page B3-1456
 * for the list */

#define CONTROL_REG_GEN_READ(name, crn, opc1, crm, opc2) \
static inline uint32_t name##_read() \
{ \
	uint32_t val; \
	asm volatile ( "mrc p15, "#opc1", %0, "#crn", "#crm", "#opc2"\n" : "=r" (val) ); \
	return val; \
}
#define CONTROL_REG_GEN_WRITE(name, crn, opc1, crm, opc2) \
static inline void name##_write(uint32_t val) \
{ \
	asm volatile ( "mcr p15, "#opc1", %0, "#crn", "#crm", "#opc2"\n" :: "r" (val) ); \
}

/* Identification registers */
CONTROL_REG_GEN_READ(MIDR, c0, 0, c0, 0);
CONTROL_REG_GEN_READ(CTR, c0, 0, c0, 1);
CONTROL_REG_GEN_READ(TCMR, c0, 0, c0, 2);
CONTROL_REG_GEN_READ(TLBTR, c0, 0, c0, 3);
CONTROL_REG_GEN_READ(MPIDR, c0, 0, c0, 5);
CONTROL_REG_GEN_READ(REVIDR, c0, 0, c0, 6);

enum {
	ID_PFR0_THUMBEE_MASK = 0xf << 12,
	ID_PFR0_THUMBEE = 0x1 << 12,
	ID_PFR0_JAZELLE_MASK = 0xf << 8,
	ID_PFR0_JAZELLE = 0x1 << 8,
	ID_PFR0_JAZELLE_CV_CLEAR = 0x2 << 8,
	ID_PFR0_THUMB_MASK = 0xf << 4,
	ID_PFR0_THUMB = 0x1 << 4,
	ID_PFR0_THUMB2 = 0x3 << 4,
	ID_PFR0_ARM_MASK = 0xf << 0,
	ID_PFR0_ARM = 0x1 << 0,
};
CONTROL_REG_GEN_READ(ID_PFR0, c0, 0, c1, 0);

enum {
	ID_PFR1_GEN_TIMER_EXT_MASK = 0xf << 16,
	ID_PFR1_GEN_TIMER_EXT = 0x1 << 16,
	ID_PFR1_VIRT_EXT_MASK = 0xf << 12,
	ID_PFR1_VIRT_EXT = 0x1 << 12,
	ID_PFR1_M_PROF_MASK = 0xf << 8,
	ID_PFR1_M_PROF_MODEL = 0x2 << 8,
	ID_PFR1_SEC_EXT_MASK = 0xf << 4,
	ID_PFR1_SEC_EXT = 0x1 << 4,
	ID_PFR1_SEC_EXT_RFR = 0x2 << 4,
	ID_PFR1_ARMV4_MODEL_MASK = 0xf << 0,
	ID_PFR1_ARMV4_MODEL = 0x1 << 0,
};
CONTROL_REG_GEN_READ(ID_PFR1, c0, 0, c1, 1);
CONTROL_REG_GEN_READ(ID_DFR0, c0, 0, c1, 2);
CONTROL_REG_GEN_READ(ID_AFR0, c0, 0, c1, 3);
CONTROL_REG_GEN_READ(ID_MMFR0, c0, 0, c1, 4);
CONTROL_REG_GEN_READ(ID_MMFR1, c0, 0, c1, 5);
CONTROL_REG_GEN_READ(ID_MMFR2, c0, 0, c1, 6);
CONTROL_REG_GEN_READ(ID_MMFR3, c0, 0, c1, 7);

CONTROL_REG_GEN_READ(ID_ISAR0, c0, 0, c2, 0);
CONTROL_REG_GEN_READ(ID_ISAR1, c0, 0, c2, 1);
CONTROL_REG_GEN_READ(ID_ISAR2, c0, 0, c2, 2);
CONTROL_REG_GEN_READ(ID_ISAR3, c0, 0, c2, 3);
CONTROL_REG_GEN_READ(ID_ISAR4, c0, 0, c2, 4);
CONTROL_REG_GEN_READ(ID_ISAR5, c0, 0, c2, 5);

enum {
	CCSIDR_WT_FLAG = 1 << 31,
	CCSIDR_WB_FLAG = 1 << 30,
	CCSIDR_RA_FLAG = 1 << 29,
	CCSIDR_WA_FLAG = 1 << 28,
	CCSIDR_NUMSETS_MASK = 0x7fff,
	CCSIDR_NUMSETS_SHIFT = 13,
	CCSIDR_ASSOC_MASK = 0x3ff,
	CCSIDR_ASSOC_SHIFT = 3,
	CCSIDR_LINESIZE_MASK = 0x7,
	CCSIDR_LINESIZE_SHIFT = 0,
};
CONTROL_REG_GEN_READ(CCSIDR, c0, 1, c0, 0);

enum {
	CLIDR_LOUU_MASK = 0x7,
	CLIDR_LOUU_SHIFT = 27,
	CLIDR_LOC_MASK = 0x7,
	CLIDR_LOC_SHIFT = 24,
	CLIDR_LOUIS_MASK = 0x7,
	CLIDR_LOUIS_SHIFT = 21,
	CLIDR_NOCACHE = 0x0,
	CLIDR_ICACHE_ONLY = 0x1,
	CLIDR_DCACHE_ONLY = 0x2,
	CLIDR_SEP_CACHE = 0x3,
	CLIDR_UNI_CACHE = 0x4,
	CLIDR_CACHE_MASK = 0x7,
#define CLIDR_CACHE(level, val)   ((val >> (level - 1) * 3) & CLIDR_CACHE_MASK)
};
CONTROL_REG_GEN_READ(CLIDR, c0, 1, c0, 1);
CONTROL_REG_GEN_READ(AIDR, c0, 1, c0, 7); /* Implementation defined or MIDR */

enum {
	CCSELR_LEVEL_MASK = 0x7,
	CCSELR_LEVEL_SHIFT = 1,
	CCSELR_INSTRUCTION_FLAG = 1 << 0,
};
CONTROL_REG_GEN_READ(CSSELR, c0, 2, c0, 0);
CONTROL_REG_GEN_WRITE(CSSELR, c0, 2, c0, 0);
CONTROL_REG_GEN_READ(VPIDR, c0, 4, c0, 0);
CONTROL_REG_GEN_WRITE(VPIDR, c0, 4, c0, 0);
CONTROL_REG_GEN_READ(VMPIDR, c0, 4, c0, 5);
CONTROL_REG_GEN_WRITE(VMPIDR, c0, 4, c0, 5);

/* System control registers */
CONTROL_REG_GEN_READ(SCTLR, c1, 0, c0, 0);
CONTROL_REG_GEN_WRITE(SCTLR, c1, 0, c0, 0);
CONTROL_REG_GEN_READ(ACTLR, c1, 0, c0, 1);
CONTROL_REG_GEN_WRITE(ACTLR, c1, 0, c0, 1);

enum {
	CPACR_ASEDIS_FLAG = 1 << 31,
	CPACR_D32DIS_FLAG = 1 << 30,
	CPACR_TRCDIS_FLAG = 1 << 28,
#define CPACR_CP_MASK(cp) (0x3 << (cp * 2))
#define CPACR_CP_NO_ACCESS(cp) (0x0 << (cp * 2))
#define CPACR_CP_PL1_ACCESS(cp) (0x1 << (cp * 2))
#define CPACR_CP_FULL_ACCESS(cp) (0x3 << (cp * 2))
};
CONTROL_REG_GEN_READ(CPACR, c1, 0, c0, 2);
CONTROL_REG_GEN_WRITE(CPACR, c1, 0, c0, 2);

/* Implemented as part of Security extensions */
enum {
	SCR_SIF_FLAG = 1 << 9,
	SCR_HCE_FLAG = 1 << 8,
	SCR_SCD_FLAG = 1 << 7,
	SCR_nET_FLAG = 1 << 6,
	SCR_AW_FLAG = 1 << 5,
	SCR_FW_FLAG = 1 << 4,
	SCR_EA_FLAG = 1 << 3,
	SCR_FIQ_FLAG = 1 << 2,
	SCR_IRQ_FLAG = 1 << 1,
	SCR_NS_FLAG = 1 << 0,
};
CONTROL_REG_GEN_READ(SCR, c1, 0, c1, 0);
CONTROL_REG_GEN_WRITE(SCR, c1, 0, c1, 0);
CONTROL_REG_GEN_READ(SDER, c1, 0, c1, 1);
CONTROL_REG_GEN_WRITE(SDER, c1, 0, c1, 1);

enum {
	NSACR_NSTRCDIS_FLAG = 1 << 20,
	NSACR_RFR_FLAG = 1 << 19,
	NSACR_NSASEDIS = 1 << 15,
	NSACR_NSD32DIS = 1 << 14,
#define NSACR_CP_FLAG(cp) (1 << cp)
};
CONTROL_REG_GEN_READ(NSACR, c1, 0, c1, 2);
CONTROL_REG_GEN_WRITE(NSACR, c1, 0, c1, 2);

/* Implemented as part of Virtualization extensions */
CONTROL_REG_GEN_READ(HSCTLR, c1, 4, c0, 0);
CONTROL_REG_GEN_WRITE(HSCTLR, c1, 4, c0, 0);
CONTROL_REG_GEN_READ(HACTLR, c1, 4, c0, 1);
CONTROL_REG_GEN_WRITE(HACTLR, c1, 4, c0, 1);

CONTROL_REG_GEN_READ(HCR, c1, 4, c1, 0);
CONTROL_REG_GEN_WRITE(HCR, c1, 4, c1, 0);
CONTROL_REG_GEN_READ(HDCR, c1, 4, c1, 1);
CONTROL_REG_GEN_WRITE(HDCR, c1, 4, c1, 1);
CONTROL_REG_GEN_READ(HCPTR, c1, 4, c1, 2);
CONTROL_REG_GEN_WRITE(HCPTR, c1, 4, c1, 2);
CONTROL_REG_GEN_READ(HSTR, c1, 4, c1, 3);
CONTROL_REG_GEN_WRITE(HSTR, c1, 4, c1, 3);
CONTROL_REG_GEN_READ(HACR, c1, 4, c1, 7);
CONTROL_REG_GEN_WRITE(HACR, c1, 4, c1, 7);

/* Memory protection and control registers */
CONTROL_REG_GEN_READ(TTBR0, c2, 0, c0, 0);
CONTROL_REG_GEN_WRITE(TTBR0, c2, 0, c0, 0);
CONTROL_REG_GEN_READ(TTBR1, c2, 0, c0, 1);
CONTROL_REG_GEN_WRITE(TTBR1, c2, 0, c0, 1);
CONTROL_REG_GEN_READ(TTBCR, c2, 0, c0, 2);
CONTROL_REG_GEN_WRITE(TTBCR, c2, 0, c0, 2);

CONTROL_REG_GEN_READ(HTCR, c2, 4, c0, 2);
CONTROL_REG_GEN_WRITE(HTCR, c2, 4, c0, 2);
CONTROL_REG_GEN_READ(VTCR, c2, 4, c1, 2);
CONTROL_REG_GEN_WRITE(VTCR, c2, 4, c1, 2);

/* PAE */
CONTROL_REG_GEN_READ(TTBR0H, c2, 0, c2, 0);
CONTROL_REG_GEN_WRITE(TTBR0H, c2, 0, c2, 0);
CONTROL_REG_GEN_READ(TTBR1H, c2, 0, c2, 1);
CONTROL_REG_GEN_WRITE(TTBR1H, c2, 0, c2, 1);
CONTROL_REG_GEN_READ(HTTBRH, c2, 0, c2, 4);
CONTROL_REG_GEN_WRITE(HTTBRH, c2, 0, c2, 4);
CONTROL_REG_GEN_READ(VTTBRH, c2, 0, c2, 6);
CONTROL_REG_GEN_WRITE(VTTBRH, c2, 0, c2, 6);

CONTROL_REG_GEN_READ(DACR, c3, 0, c0, 0);
CONTROL_REG_GEN_WRITE(DACR, c3, 0, c0, 0);

/* Memory system fault registers */
CONTROL_REG_GEN_READ(DFSR, c5, 0, c0, 0);
CONTROL_REG_GEN_WRITE(DFSR, c5, 0, c0, 0);
CONTROL_REG_GEN_READ(IFSR, c5, 0, c0, 1);
CONTROL_REG_GEN_WRITE(IFSR, c5, 0, c0, 1);

CONTROL_REG_GEN_READ(ADFSR, c5, 0, c1, 0);
CONTROL_REG_GEN_WRITE(ADFSR, c5, 0, c1, 0);
CONTROL_REG_GEN_READ(AIFSR, c5, 0, c1, 1);
CONTROL_REG_GEN_WRITE(AIFSR, c5, 0, c1, 1);

CONTROL_REG_GEN_READ(HADFSR, c5, 4, c1, 0);
CONTROL_REG_GEN_WRITE(HADFSR, c5, 4, c1, 0);
CONTROL_REG_GEN_READ(HAIFSR, c5, 4, c1, 1);
CONTROL_REG_GEN_WRITE(HAIFSR, c5, 4, c1, 1);
CONTROL_REG_GEN_READ(HSR, c5, 4, c2, 0);
CONTROL_REG_GEN_WRITE(HSR, c5, 4, c2, 0);

CONTROL_REG_GEN_READ(DFAR, c6, 0, c0, 0);
CONTROL_REG_GEN_WRITE(DFAR, c6, 0, c0, 0);
CONTROL_REG_GEN_READ(IFAR, c6, 0, c0, 2);
CONTROL_REG_GEN_WRITE(IFAR, c6, 0, c0, 2);

CONTROL_REG_GEN_READ(HDFAR, c6, 4, c0, 0);
CONTROL_REG_GEN_WRITE(HDFAR, c6, 4, c0, 0);
CONTROL_REG_GEN_READ(HIFAR, c6, 4, c0, 2);
CONTROL_REG_GEN_WRITE(HIFAR, c6, 4, c0, 2);
CONTROL_REG_GEN_READ(HPFAR, c6, 4, c0, 4);
CONTROL_REG_GEN_WRITE(HPFAR, c6, 4, c0, 4);

/* Cache maintenance, address translation and other */
CONTROL_REG_GEN_WRITE(WFI, c7, 0, c0, 4); /* armv6 only */
CONTROL_REG_GEN_WRITE(ICIALLLUIS, c7, 0, c1, 0);
CONTROL_REG_GEN_WRITE(BPIALLIS, c7, 0, c1, 6);
CONTROL_REG_GEN_READ(PAR, c7, 0, c4, 0);
CONTROL_REG_GEN_WRITE(PAR, c7, 0, c4, 0);
CONTROL_REG_GEN_READ(PARH, c7, 0, c7, 0);   /* PAE */
CONTROL_REG_GEN_WRITE(PARH, c7, 0, c7, 0);   /* PAE */
CONTROL_REG_GEN_WRITE(ICIALLU, c7, 0, c5, 0);
CONTROL_REG_GEN_WRITE(ICIMVAU, c7, 0, c5, 1);
CONTROL_REG_GEN_WRITE(CP15ISB, c7, 0, c5, 4);
CONTROL_REG_GEN_WRITE(BPIALL, c7, 0, c5, 6);
CONTROL_REG_GEN_WRITE(BPIMVA, c7, 0, c5, 7);

CONTROL_REG_GEN_WRITE(DCIMVAC, c7, 0, c6, 1);
CONTROL_REG_GEN_WRITE(DCIMSW, c7, 0, c6, 2);

CONTROL_REG_GEN_WRITE(ATS1CPR, c7, 0, c8, 0);
CONTROL_REG_GEN_WRITE(ATS1CPW, c7, 0, c8, 1);
CONTROL_REG_GEN_WRITE(ATS1CUR, c7, 0, c8, 2);
CONTROL_REG_GEN_WRITE(ATS1CUW, c7, 0, c8, 3);
CONTROL_REG_GEN_WRITE(ATS1NSOPR, c7, 0, c8, 4);
CONTROL_REG_GEN_WRITE(ATS1NSOPW, c7, 0, c8, 5);
CONTROL_REG_GEN_WRITE(ATS1NSOUR, c7, 0, c8, 6);
CONTROL_REG_GEN_WRITE(ATS1NSOUW, c7, 0, c8, 7);


CONTROL_REG_GEN_WRITE(DCCMVAC, c7, 0, c10, 1);
CONTROL_REG_GEN_WRITE(DCCSW, c7, 0, c10, 2);
CONTROL_REG_GEN_WRITE(CP15DSB, c7, 0, c10, 4);
CONTROL_REG_GEN_WRITE(CP15DMB, c7, 0, c10, 5);
CONTROL_REG_GEN_WRITE(DCCMVAU, c7, 0, c11, 1);

CONTROL_REG_GEN_WRITE(PFI, c7, 0, c11, 1); /* armv6 only */

CONTROL_REG_GEN_WRITE(DCCIMVAC, c7, 0, c14, 1);
CONTROL_REG_GEN_WRITE(DCCISW, c7, 0, c14, 2);

CONTROL_REG_GEN_WRITE(ATS1HR, c7, 4, c8, 0);
CONTROL_REG_GEN_WRITE(ATS1HW, c7, 4, c8, 1);

/* TLB maintenance */
CONTROL_REG_GEN_WRITE(TLBIALLIS, c8, 0, c3, 0); /* Inner shareable */
CONTROL_REG_GEN_WRITE(TLBIMVAIS, c8, 0, c3, 1); /* Inner shareable */
CONTROL_REG_GEN_WRITE(TLBIASIDIS, c8, 0, c3, 2); /* Inner shareable */
CONTROL_REG_GEN_WRITE(TLBIMVAAIS, c8, 0, c3, 3); /* Inner shareable */

CONTROL_REG_GEN_WRITE(ITLBIALL, c8, 0, c5, 0);
CONTROL_REG_GEN_WRITE(ITLBIMVA, c8, 0, c5, 1);
CONTROL_REG_GEN_WRITE(ITLBIASID, c8, 0, c5, 2);

CONTROL_REG_GEN_WRITE(DTLBIALL, c8, 0, c6, 0);
CONTROL_REG_GEN_WRITE(DTLBIMVA, c8, 0, c6, 1);
CONTROL_REG_GEN_WRITE(DTLBIASID, c8, 0, c6, 2);

CONTROL_REG_GEN_WRITE(TLBIALL, c8, 0, c7, 0);
CONTROL_REG_GEN_WRITE(TLBIMVA, c8, 0, c7, 1);
CONTROL_REG_GEN_WRITE(TLBIASID, c8, 0, c7, 2);
CONTROL_REG_GEN_WRITE(TLBIMVAA, c8, 0, c7, 3);

CONTROL_REG_GEN_WRITE(TLBIALLHIS, c8, 4, c3, 0); /* Inner shareable */
CONTROL_REG_GEN_WRITE(TLBIMVAHIS, c8, 4, c3, 1); /* Inner shareable */
CONTROL_REG_GEN_WRITE(TLBIALLNSNHIS, c8, 4, c3, 4); /* Inner shareable */

CONTROL_REG_GEN_WRITE(TLBIALLH, c8, 4, c7, 0);
CONTROL_REG_GEN_WRITE(TLBIMVAH, c8, 4, c7, 1);
CONTROL_REG_GEN_WRITE(TLBIALLNSNHS, c8, 4, c7, 4);

/* c9 are reserved */

/*c10 has tons of reserved too */
CONTROL_REG_GEN_READ(PRRR, c10, 0, c2, 0); /* no PAE */
CONTROL_REG_GEN_WRITE(PRRR, c10, 0, c2, 0); /* no PAE */
CONTROL_REG_GEN_READ(MAIR0, c10, 0, c2, 0); /* PAE */
CONTROL_REG_GEN_WRITE(MAIR0, c10, 0, c2, 0); /* PAE */
CONTROL_REG_GEN_READ(NMRR, c10, 0, c2, 1); /* no PAE */
CONTROL_REG_GEN_WRITE(NMRR, c10, 0, c2, 1); /* no PAE */
CONTROL_REG_GEN_READ(MAIR1, c10, 0, c2, 1); /* PAE */
CONTROL_REG_GEN_WRITE(MAIR1, c10, 0, c2, 1); /* PAE */

CONTROL_REG_GEN_READ(AMAIR0, c10, 0, c3, 0); /* PAE */
CONTROL_REG_GEN_WRITE(AMAIR0, c10, 0, c3, 0); /* PAE */
CONTROL_REG_GEN_READ(AMAIR1, c10, 0, c3, 1); /* PAE */
CONTROL_REG_GEN_WRITE(AMAIR1, c10, 0, c3, 1); /* PAE */

CONTROL_REG_GEN_READ(HMAIR0, c10, 4, c2, 0);
CONTROL_REG_GEN_WRITE(HMAIR0, c10, 4, c2, 0);
CONTROL_REG_GEN_READ(HMAIR1, c10, 4, c2, 1);
CONTROL_REG_GEN_WRITE(HMAIR1, c10, 4, c2, 1);

CONTROL_REG_GEN_READ(HAMAIR0, c10, 4, c3, 0);
CONTROL_REG_GEN_WRITE(HAMAIR0, c10, 4, c3, 0);
CONTROL_REG_GEN_READ(HAMAIR1, c10, 4, c3, 1);
CONTROL_REG_GEN_WRITE(HAMAIR1, c10, 4, c3, 1);

/* c11 is reserved for TCM and DMA */

/* Security extensions */
CONTROL_REG_GEN_READ(VBAR, c12, 0, c0, 0);
CONTROL_REG_GEN_WRITE(VBAR, c12, 0, c0, 0);
CONTROL_REG_GEN_READ(MVBAR, c12, 0, c0, 1);
CONTROL_REG_GEN_WRITE(MVBAR, c12, 0, c0, 1);

CONTROL_REG_GEN_READ(ISR, c12, 0, c1, 0);

CONTROL_REG_GEN_READ(HVBAR, c12, 4, c0, 0);
CONTROL_REG_GEN_WRITE(HVBAR, c12, 4, c0, 0);

/* Process context and thread id (FCSE) */
CONTROL_REG_GEN_READ(FCSEIDR, c13, 0, c0, 0);

CONTROL_REG_GEN_READ(CONTEXTIDR, c13, 0, c0, 1);
CONTROL_REG_GEN_WRITE(CONTEXTIDR, c13, 0, c0, 1);
CONTROL_REG_GEN_READ(TPIDRURW, c13, 0, c0, 2);
CONTROL_REG_GEN_WRITE(TPIDRURW, c13, 0, c0, 2);
CONTROL_REG_GEN_READ(TPIDRURO, c13, 0, c0, 3);
CONTROL_REG_GEN_WRITE(TPIDRURO, c13, 0, c0, 3);
CONTROL_REG_GEN_READ(TPIDRPRW, c13, 0, c0, 4);
CONTROL_REG_GEN_WRITE(TPIDRPRW, c13, 0, c0, 4);

CONTROL_REG_GEN_READ(HTPIDR, c13, 4, c0, 2);
CONTROL_REG_GEN_WRITE(HTPIDR, c13, 4, c0, 2);

/* Generic Timer Extensions */
CONTROL_REG_GEN_READ(CNTFRQ, c14, 0, c0, 0);
CONTROL_REG_GEN_WRITE(CNTFRQ, c14, 0, c0, 0);
CONTROL_REG_GEN_READ(CNTKCTL, c14, 0, c1, 0);
CONTROL_REG_GEN_WRITE(CNTKCTL, c14, 0, c1, 0);

CONTROL_REG_GEN_READ(CNTP_TVAL, c14, 0, c2, 0);
CONTROL_REG_GEN_WRITE(CNTP_TVAL, c14, 0, c2, 0);
CONTROL_REG_GEN_READ(CNTP_CTL, c14, 0, c2, 1);
CONTROL_REG_GEN_WRITE(CNTP_CTL, c14, 0, c2, 1);

CONTROL_REG_GEN_READ(CNTV_TVAL, c14, 0, c3, 0);
CONTROL_REG_GEN_WRITE(CNTV_TVAL, c14, 0, c3, 0);
CONTROL_REG_GEN_READ(CNTV_CTL, c14, 0, c3, 1);
CONTROL_REG_GEN_WRITE(CNTV_CTL, c14, 0, c3, 1);

CONTROL_REG_GEN_READ(CNTHCTL, c14, 4, c1, 0);
CONTROL_REG_GEN_WRITE(CNTHCTL, c14, 4, c1, 0);

CONTROL_REG_GEN_READ(CNTHP_TVAL, c14, 4, c2, 0);
CONTROL_REG_GEN_WRITE(CNTHP_TVAL, c14, 4, c2, 0);
CONTROL_REG_GEN_READ(CNTHP_CTL, c14, 4, c2, 1);
CONTROL_REG_GEN_WRITE(CNTHP_CTL, c14, 4, c2, 1);

#endif

/** @}
 */
