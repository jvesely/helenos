/*
 * Copyright (c) 2011 Petr Koupy
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

/** @addtogroup libposix
 * @{
 */
/** @file Floating type support.
 */

#ifndef POSIX_FLOAT_H_
#define POSIX_FLOAT_H_

/* Rouding direction -1 - unknown */
#define FLT_ROUNDS (-1)

/* define some standard C constants in terms of GCC built-ins */
#ifdef __GNUC__
	#undef DBL_MANT_DIG
	#define DBL_MANT_DIG __DBL_MANT_DIG__
	#undef DBL_MIN_EXP
	#define DBL_MIN_EXP __DBL_MIN_EXP__
	#undef DBL_MAX_EXP
	#define DBL_MAX_EXP __DBL_MAX_EXP__
	#undef DBL_MAX
	#define DBL_MAX __DBL_MAX__
	#undef DBL_MAX_10_EXP
	#define DBL_MAX_10_EXP __DBL_MAX_10_EXP__
	#undef DBL_MIN_10_EXP
	#define DBL_MIN_10_EXP __DBL_MIN_10_EXP__
	#undef DBL_MIN
	#define DBL_MIN __DBL_MIN__
	#undef DBL_DIG
	#define DBL_DIG __DBL_DIG__
	#undef DBL_EPSILON
	#define DBL_EPSILON __DBL_EPSILON__
	#undef LDBL_EPSILON
	#define LDBL_EPSILON __LDBL_EPSILON__
	#undef FLT_RADIX
	#define FLT_RADIX __FLT_RADIX__
	#undef FLT_MIN
	#define FLT_MIN __FLT_MIN__
	#undef FLT_MAX
	#define FLT_MAX __FLT_MAX__
	#undef FLT_EPSILON
	#define FLT_EPSILON __FLT_EPSILON__
	#undef FLT_MANT_DIG
	#define FLT_MANT_DIG __FLT_MANT_DIG__
	#undef LDBL_MIN
	#define LDBL_MIN __LDBL_MIN__
	#undef LDBL_MAX
	#define LDBL_MAX __LDBL_MAX__
	#undef LDBL_MANT_DIG 
	#define LDBL_MANT_DIG __LDBL_MANT_DIG__
#else
/* For something else than GCC, following definitions are provided.
 * They are intentionally guarded by the given macro to ensure that anyone
 * who wants them states this explicitly.
 *
 * The values are the ones GCC prints when compiled on i686 Linux.
 *
 * WARNING: the values are not accurate (especially the long double ones)!
 *
 */
#ifdef FLOAT_H_YES_I_REALLY_WANT_LIMITS
/* float limits */
#define FLT_MIN 1.1754943508222875079687365372222456778186655567720875215087517062784172594547271728515625e-38
#define FLT_MAX 340282346638528859811704183484516925440

/* double limits */
#define DBL_MIN 2.2250738585072013830902327173324040642192159804623318305533274168872044348139181958542831590125110205640673397310358110051524341615534601088560123853777188211307779935320023304796101474425836360719216e-308
#define DBL_MAX 1.7976931348623157081452742373170435679807056752584499659891747680315726078002853876058955863276687817154045895351438246423432132688946418276846754670353751698604991057655128207624549009038932894407587e+308

/* long double limits */
#define LDBL_MIN 3.3621031431120935062626778173217526025980793448464712401088272298087426993907289670430927063650562228625019066688234732270901114717276781407474941951906317291667263914849985862188944930687409323125832e-4932L
#define LDBL_MAX 1.1897314953572317650212638530309702051690633222946242004403237338917370055229707226164102903365288828535456978074955773144274431536702884341981255738537436786735932007069732632019159182829615243655295e+4932L

/* epsilons */
#define FLT_EPSILON 1.1920928955078125e-07
#define DBL_EPSILON 2.220446049250313080847263336181640625e-16
#define LDBL_EPSILON 1.08420217248550443400745280086994171142578125e-19L

/* float radix */
#define FLT_RADIX 2

/* mantisa */
#define FLT_MANT_DIG 24
#define DBL_MANT_DIG 53
#define LDBL_MANT_DIG 64

/* exponents */
#define DBL_MIN_EXP -1021
#define DBL_MAX_EXP 1024

#endif

#endif /* __GNUC__ */

#endif /* POSIX_FLOAT_H_ */

/** @}
 */
