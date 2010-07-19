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

#ifndef KERN_mips32_EXCEPTION_H_
#define KERN_mips32_EXCEPTION_H_

#include <typedefs.h>
#include <arch/cp0.h>
#include <trace.h>

#define EXC_Int    0
#define EXC_Mod    1
#define EXC_TLBL   2
#define EXC_TLBS   3
#define EXC_AdEL   4
#define EXC_AdES   5
#define EXC_IBE    6
#define EXC_DBE    7
#define EXC_Sys    8
#define EXC_Bp     9
#define EXC_RI     10
#define EXC_CpU    11
#define EXC_Ov     12
#define EXC_Tr     13
#define EXC_VCEI   14
#define EXC_FPE    15
#define EXC_WATCH  23
#define EXC_VCED   31

typedef struct istate {
	uint32_t at;
	uint32_t v0;
	uint32_t v1;
	uint32_t a0;
	uint32_t a1;
	uint32_t a2;
	uint32_t a3;
	uint32_t t0;
	uint32_t t1;
	uint32_t t2;
	uint32_t t3;
	uint32_t t4;
	uint32_t t5;
	uint32_t t6;
	uint32_t t7;
	uint32_t t8;
	uint32_t t9;
	uint32_t gp;
	uint32_t sp;
	uint32_t ra;
	
	uint32_t lo;
	uint32_t hi;
	
	uint32_t status;  /* cp0_status */
	uint32_t epc;     /* cp0_epc */
	uint32_t k1;      /* We use it as thread-local pointer */
} istate_t;

NO_TRACE static inline void istate_set_retaddr(istate_t *istate,
    uintptr_t retaddr)
{
	istate->epc = retaddr;
}

/** Return true if exception happened while in userspace */
NO_TRACE static inline int istate_from_uspace(istate_t *istate)
{
	return istate->status & cp0_status_um_bit;
}

NO_TRACE static inline unative_t istate_get_pc(istate_t *istate)
{
	return istate->epc;
}

NO_TRACE static inline unative_t istate_get_fp(istate_t *istate)
{
	return istate->sp;
}

extern void exception(istate_t *istate);
extern void tlb_refill_entry(void);
extern void exception_entry(void);
extern void cache_error_entry(void);
extern void exception_init(void);

#endif

/** @}
 */
