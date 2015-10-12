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

/** @addtogroup volsrv
 * @{
 */
/**
 * @file Empty partition handling
 * @brief
 */

#include <block.h>
#include <errno.h>
#include <io/log.h>
#include <loc.h>
#include <stdlib.h>

#include "empty.h"

static bool mem_is_zero(void *buf, size_t size)
{
	uint8_t *bp;
	size_t i;

	bp = (uint8_t *)buf;
	for (i = 0; i < size; i++) {
		if (bp[i] != 0)
			return false;
	}

	return true;
}

int vol_part_is_empty(service_id_t sid, bool *rempty)
{
	int rc;
	bool block_inited = false;
	void *buf = NULL;
	aoff64_t nblocks;
	aoff64_t n;
	aoff64_t i;
	size_t block_size;
	bool empty;

	rc = block_init(EXCHANGE_SERIALIZE, sid, 2048);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Error opening "
		    "block device service %zu", sid);
		rc = EIO;
		goto error;
	}

	block_inited = true;

	rc = block_get_bsize(sid, &block_size);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Error getting "
		    "block size.");
		rc = EIO;
		goto error;
	}

	rc = block_get_nblocks(sid, &nblocks);
	if (rc != EOK) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Error getting "
		    "number of blocks.");
		rc = EIO;
		goto error;
	}

	/* Check first 16 kiB / 16 blocks, whichever is more */
	n = (16384 + block_size - 1) / block_size;
	if (n < 16)
		n = 16;
	/*
	 * Limit to half of the device so we do not process the same blocks
	 * twice
	 */
	if (n > (nblocks + 1) / 2)
		n = (nblocks + 1) / 2;

	buf = calloc(block_size, 1);
	if (buf == NULL) {
		log_msg(LOG_DEFAULT, LVL_ERROR, "Error allocating buffer.");
		rc = ENOMEM;
		goto error;
	}

	empty = true;

	for (i = 0; i < n; i++) {
		rc = block_read_direct(sid, i, 1, buf);
		if (rc != EOK) {
			log_msg(LOG_DEFAULT, LVL_ERROR, "Error "
			    "reading blocks.");
			rc = EIO;
			goto error;
		}

		if (!mem_is_zero(buf, block_size)) {
			empty = false;
			goto done;
		}
	}

	for (i = 0; i < n; i++) {
		rc = block_read_direct(sid, nblocks - n + i, 1, buf);
		if (rc != EOK) {
			log_msg(LOG_DEFAULT, LVL_ERROR, "Error "
			    "reading blocks.");
			rc = EIO;
			goto error;
		}

		if (!mem_is_zero(buf, block_size)) {
			empty = false;
			goto done;
		}
	}

done:
	block_fini(sid);
	free(buf);
	*rempty = empty;
	return EOK;
error:
	if (block_inited)
		block_fini(sid);
	if (buf != NULL)
		free(buf);
	return rc;
}

/** @}
 */
