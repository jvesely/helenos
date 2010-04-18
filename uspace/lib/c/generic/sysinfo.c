/*
 * Copyright (c) 2006 Jakub Jermar
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

#include <libc.h>
#include <sysinfo.h>
#include <str.h>
#include <errno.h>
#include <malloc.h>
#include <bool.h>

/** Get sysinfo item type
 *
 * @param path Sysinfo path.
 *
 * @return Sysinfo item type.
 *
 */
sysinfo_item_tag_t sysinfo_get_tag(const char *path)
{
	return (sysinfo_item_tag_t) __SYSCALL2(SYS_SYSINFO_GET_TAG,
	    (sysarg_t) path, (sysarg_t) str_size(path));
}

/** Get sysinfo numerical value
 *
 * @param path  Sysinfo path.
 * @param value Pointer to store the numerical value to.
 *
 * @return EOK if the value was successfully read and
 *         is of SYSINFO_VAL_VAL type.
 *
 */
int sysinfo_get_value(const char *path, sysarg_t *value)
{
	return (int) __SYSCALL3(SYS_SYSINFO_GET_VALUE, (sysarg_t) path,
	    (sysarg_t) str_size(path), (sysarg_t) value);
}

/** Get sysinfo binary data size
 *
 * @param path  Sysinfo path.
 * @param value Pointer to store the binary data size.
 *
 * @return EOK if the value was successfully read and
 *         is of SYSINFO_VAL_DATA type.
 *
 */
static int sysinfo_get_data_size(const char *path, size_t *size)
{
	return (int) __SYSCALL3(SYS_SYSINFO_GET_DATA_SIZE, (sysarg_t) path,
	    (sysarg_t) str_size(path), (sysarg_t) size);
}

/** Get sysinfo binary data
 *
 * @param path  Sysinfo path.
 * @param value Pointer to store the binary data size.
 *
 * @return Binary data read from sysinfo or NULL if the
 *         sysinfo item value type is not binary data.
 *         The returned non-NULL pointer should be
 *         freed by free().
 *
 */
void *sysinfo_get_data(const char *path, size_t *size)
{
	/* The binary data size might change during time.
	   Unfortunatelly we cannot allocate the buffer
	   and transfer the data as a single atomic operation.
	
	   Let's hope that the number of iterations is bounded
	   in common cases. */
	
	void *data = NULL;
	
	while (true) {
		/* Get the binary data size */
		int ret = sysinfo_get_data_size(path, size);
		if ((ret != EOK) || (size == 0)) {
			/* Not a binary data item
			   or an empty item */
			break;
		}
		
		data = realloc(data, *size);
		if (data == NULL)
			break;
		
		/* Get the data */
		ret = __SYSCALL4(SYS_SYSINFO_GET_DATA, (sysarg_t) path,
		    (sysarg_t) str_size(path), (sysarg_t) data, (sysarg_t) *size);
		if (ret == EOK)
			return data;
		
		if (ret != ENOMEM) {
			/* The failure to get the data was not caused
			   by wrong buffer size */
			break;
		}
	}
	
	if (data != NULL)
		free(data);
	
	*size = 0;
	return NULL;
}

/** @}
 */
