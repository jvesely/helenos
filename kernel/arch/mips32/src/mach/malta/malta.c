/*
 * Copyright (c) 2013 Jakub Jermar
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
 *  @brief MIPS Malta platform driver.
 */

#include <arch/mach/malta/malta.h>

static void malta_init(void);
static void malta_cpu_halt(void);
static void malta_get_memory_extents(uintptr_t *, size_t *);
static void malta_frame_init(void);
static void malta_output_init(void);
static void malta_input_init(void);
static const char *malta_get_platform_name(void);

struct mips32_machine_ops malta_machine_ops = {
	.machine_init = malta_init,
	.machine_cpu_halt = malta_cpu_halt,
	.machine_get_memory_extents = malta_get_memory_extents,
	.machine_frame_init = malta_frame_init,
	.machine_output_init = malta_output_init,
	.machine_input_init = malta_input_init,
	.machine_get_platform_name = malta_get_platform_name	
};

void malta_init(void)
{
}

void malta_cpu_halt(void)
{
}

void malta_get_memory_extents(uintptr_t *start, size_t *size)
{
}

void malta_frame_init(void)
{
}

void malta_output_init(void)
{
}

void malta_input_init(void)
{
}

const char *malta_get_platform_name(void)
{
	return "malta";
}

/** @}
 */
