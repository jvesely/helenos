/*
 * Copyright (c) 2016 Jakub Jermar
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

/** @addtogroup fs
 * @{
 */

/**
 * @file vfs_pager.c
 * @brief VFS pager operations.
 */

#include "vfs.h"
#include <async.h>
#include <fibril_synch.h>
#include <errno.h>
#include <as.h>

void vfs_page_in(ipc_callid_t rid, ipc_call_t *request)
{
	aoff64_t offset = IPC_GET_ARG1(*request);
	size_t page_size = IPC_GET_ARG2(*request);
	int fd = IPC_GET_ARG3(*request);
	void *page;
	int rc;

	vfs_file_t *file = vfs_file_get(fd);
	if (!file) {
		async_answer_0(rid, ENOENT);
		return;
	}

	page = as_area_create(AS_AREA_ANY, page_size,
	    AS_AREA_READ | AS_AREA_WRITE | AS_AREA_CACHEABLE,
	    AS_AREA_UNPAGED);

	if (page == AS_MAP_FAILED) {
		vfs_file_put(file);
		async_answer_0(rid, ENOMEM);
		return;
	}

	rdwr_io_chunk_t chunk = {
		.buffer = page,
		.size = page_size
	};

	fibril_mutex_lock(&file->lock);
	file->pos = offset;
	fibril_mutex_unlock(&file->lock);

	size_t total = 0;
	do {
		rc = vfs_rdwr_internal(fd, true, &chunk);
		if (rc != EOK)
			break;
		if (chunk.size == 0)
			break;
		total += chunk.size;
		chunk.buffer += chunk.size;
		chunk.size = page_size - total;
	} while (total < page_size);

	vfs_file_put(file);

	async_answer_1(rid, rc, (sysarg_t) page);

	/*
	 * FIXME: 
	 * This is just for now until we implement proper page cache
	 * management.  Not keeping the pages around in a cache results in
	 * inherently non-coherent private mappings.
	 */
	as_area_destroy(page);
}

/**
 * @}
 */
