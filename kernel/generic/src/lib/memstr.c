/*
 * Copyright (c) 2001-2004 Jakub Jermar
 * Copyright (c) 2008 Jiri Svoboda
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

/** @addtogroup generic	
 * @{
 */

/**
 * @file
 * @brief	Memory string operations.
 *
 * This file provides architecture independent functions to manipulate blocks of
 * memory. These functions are optimized as much as generic functions of this
 * type can be. However, architectures are free to provide even more optimized
 * versions of these functions.
 */

#include <memstr.h>
#include <arch/types.h>
#include <align.h>

/** Copy block of memory.
 *
 * Copy cnt bytes from src address to dst address.  The copying is done
 * word-by-word and then byte-by-byte.  The source and destination memory areas
 * cannot overlap.
 *
 * @param src		Source address to copy from.
 * @param dst		Destination address to copy to.
 * @param cnt		Number of bytes to copy.
 *
 * @return		Destination address.
 */
void *_memcpy(void *dst, const void *src, size_t cnt)
{
	unsigned int i, j;
	
	if (ALIGN_UP((uintptr_t) src, sizeof(unative_t)) != (uintptr_t) src ||
	    ALIGN_UP((uintptr_t) dst, sizeof(unative_t)) != (uintptr_t) dst) {
		for (i = 0; i < cnt; i++)
			((uint8_t *) dst)[i] = ((uint8_t *) src)[i];
	} else { 
		for (i = 0; i < cnt / sizeof(unative_t); i++)
			((unative_t *) dst)[i] = ((unative_t *) src)[i];
		
		for (j = 0; j < cnt % sizeof(unative_t); j++)
			((uint8_t *)(((unative_t *) dst) + i))[j] =
			    ((uint8_t *)(((unative_t *) src) + i))[j];
	}
		
	return (char *) dst;
}

/** Move memory block with possible overlapping.
 *
 * Copy cnt bytes from src address to dst address. The source and destination
 * memory areas may overlap.
 *
 * @param src		Source address to copy from.
 * @param dst		Destination address to copy to.
 * @param cnt		Number of bytes to copy.
 *
 * @return		Destination address.
 */
void *memmove(void *dst, const void *src, size_t n)
{
	const uint8_t *sp;
	uint8_t *dp;

	/* Nothing to do? */
	if (src == dst)
		return dst;

	/* Non-overlapping? */
	if (dst >= src + n || src >= dst + n) {	
		return memcpy(dst, src, n);
	}

	/* Which direction? */
	if (src > dst) {
		/* Forwards. */
		sp = src;
		dp = dst;

		while (n-- != 0)
			*dp++ = *sp++;
	} else {
		/* Backwards. */
		sp = src + (n - 1);
		dp = dst + (n - 1);

		while (n-- != 0)
			*dp-- = *sp--;
	}

	return dst;
}

/** Fill block of memory
 *
 * Fill cnt bytes at dst address with the value x.  The filling is done
 * byte-by-byte.
 *
 * @param dst		Destination address to fill.
 * @param cnt		Number of bytes to fill.
 * @param x		Value to fill.
 *
 */
void _memsetb(void *dst, size_t cnt, uint8_t x)
{
	unsigned int i;
	uint8_t *p = (uint8_t *) dst;
	
	for (i = 0; i < cnt; i++)
		p[i] = x;
}

/** Fill block of memory.
 *
 * Fill cnt words at dst address with the value x.  The filling is done
 * word-by-word.
 *
 * @param dst		Destination address to fill.
 * @param cnt		Number of words to fill.
 * @param x		Value to fill.
 *
 */
void _memsetw(void *dst, size_t cnt, uint16_t x)
{
	unsigned int i;
	uint16_t *p = (uint16_t *) dst;
	
	for (i = 0; i < cnt; i++)
		p[i] = x;	
}

/** @}
 */
