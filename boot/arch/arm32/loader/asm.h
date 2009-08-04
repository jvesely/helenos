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


/** @addtogroup arm32boot
 * @{
 */
/** @file
 *  @brief Functions implemented in assembly.
 */


#ifndef BOOT_arm32_ASM_H
#define BOOT_arm32_ASM_H


/** Copies cnt bytes from dst to src.
 *
 * @param dst Destination address.
 * @param src Source address.
 * @param cnt Count of bytes to be copied.
 */
#define memcpy(dst, src, cnt) __builtin_memcpy((dst), (src), (cnt))


/** Called when the CPU is switched on.
 *
 *  This function is placed to the 0x0 address where ARM CPU starts execution. 
 *  Jumps to the #bootstrap only.
 */
extern void start(void);


/** Jumps to the kernel entry point.
 *
 * @param entry    Kernel entry point address.
 * @param bootinfo Structure holding information about loaded tasks.
 *
 */
extern void jump_to_kernel(void *entry, void *bootinfo) __attribute__((noreturn));


#endif

/** @}
 */
