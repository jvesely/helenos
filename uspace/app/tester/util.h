/*
 * Copyright (c) 2011 Martin Sucha
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

/** @addtogroup tester
 * @{
 */
/** @file
 */

#ifndef UTIL_H_
#define UTIL_H_

#include <stdio.h>
#include <inttypes.h>
#include "tester.h"

#define ASSERT_EQ_FN_DEF(type, fmt, fmtx) \
extern bool assert_eq_fn_##type(type, type, const char *);

#include "util_functions.def"

#define ASSERT_EQ_64(exp, act, msg) \
{if (assert_eq_fn_uint64_t(exp, act, #act)) {return msg;}}

#define ASSERT_EQ_32(exp, act, msg) \
{if (assert_eq_fn_uint32_t(exp, act, #act)) {return msg;}}

#define ASSERT_EQ_16(exp, act, msg) \
{if (assert_eq_fn_uint16_t(exp, act, #act)) {return msg;}}

#define ASSERT_EQ_8(exp, act, msg) \
{if (assert_eq_fn_uint8_t(exp, act, #act)) {return msg;}}

#endif

/** @}
 */
