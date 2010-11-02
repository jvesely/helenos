/*
 * Copyright (c) 2006 Ondrej Palkovsky
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

/** @addtogroup libc
 * @{
 */
/** @file
 */

#ifndef LIBC_ERR_H_
#define LIBC_ERR_H_

#include <stdio.h>
#include <errno.h>

#ifdef CONFIG_DEBUG
#include <str_error.h>
#endif

#define errx(status, fmt, ...) { \
	printf((fmt), ##__VA_ARGS__); \
	_exit(status); \
}


/** An actual stored error code.  */
#define ERROR_CODE  error_check_return_value

/** An error processing routines declaration.
 *
 * This has to be declared in the block where the error processing
 * is desired.
 */
#define ERROR_DECLARE  int ERROR_CODE

/** Store the value as an error code and checks if an error occurred.
 *
 * @param[in] value	The value to be checked. May be a function call.
 * @return		False if the value indicates success (EOK).
 * @return		True otherwise.
 */
#ifdef CONFIG_DEBUG

#define ERROR_OCCURRED(value) \
	(((ERROR_CODE = (value)) != EOK) && \
	({ \
		fprintf(stderr, "libsocket error at %s:%d (%s)\n", \
		__FILE__, __LINE__, str_error(ERROR_CODE)); \
		1; \
	}))

#else

#define ERROR_OCCURRED(value)	((ERROR_CODE = (value)) != EOK)

#endif

#define ERROR_NONE(value)	!ERROR_OCCURRED((value))

/** Error propagation
 *
 * Check if an error occurred and immediately exit the actual
 * function returning the error code.
 *
 * @param[in] value	The value to be checked. May be a function call.
 *
 */

#define ERROR_PROPAGATE(value) \
	if (ERROR_OCCURRED(value)) \
		return ERROR_CODE

#endif

/** @}
 */
