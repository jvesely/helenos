/*
 * Copyright (c) 2015 Jiri Svoboda
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

/** @addtogroup libmath
 * @{
 */
/** @file
 */

#include <math.h>
#include <modf.h>

/** Remainder function (32-bit floating point)
 *
 * Calculate the modulo of dividend by divisor.
 *
 * This is a very basic implementation that uses
 * division and multiplication (instead of exact
 * arithmetics). Thus the result might be very
 * imprecise (depending on the magnitude of the
 * arguments).
 *
 * @param dividend Dividend.
 * @param divisor  Divisor.
 *
 * @return Modulo.
 *
 */
float32_t float32_modf(float32_t value, float32_t *iptr)
{
	*iptr = trunc_f32(value);
	return value - *iptr;
}

/** Remainder function (64-bit floating point)
 *
 * Calculate the modulo of dividend by divisor.
 *
 * This is a very basic implementation that uses
 * division and multiplication (instead of exact
 * arithmetics). Thus the result might be very
 * imprecise (depending on the magnitude of the
 * arguments).
 *
 * @param dividend Dividend.
 * @param divisor  Divisor.
 *
 * @return Modulo.
 *
 */
float64_t float64_modf(float64_t value, float64_t *iptr)
{
	*iptr = trunc_f64(value);
	return value - *iptr;
}

/** @}
 */
