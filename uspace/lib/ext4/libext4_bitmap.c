/*
 * Copyright (c) 2011 Frantisek Princ
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

/** @addtogroup libext4
 * @{
 */ 

/**
 * @file	libext4_bitmap.c
 * @brief	TODO
 */

#include <errno.h>
#include <libblock.h>
#include <sys/types.h>
#include "libext4.h"

static void ext4_bitmap_free_bit(uint8_t *bitmap, uint32_t index)
{
	uint32_t byte_index = index / 8;
	uint32_t bit_index = index % 8;

	uint8_t *target = bitmap + byte_index;
	uint8_t old_value = *target;

	uint8_t new_value = old_value & (~ (1 << bit_index));

	*target = new_value;
}

int ext4_bitmap_free_block(ext4_filesystem_t *fs, uint32_t block_index)
{
	int rc;
	uint32_t blocks_per_group;
	uint32_t block_group;
	uint32_t index_in_group;
	uint32_t bitmap_block;
	ext4_block_group_ref_t *bg_ref;
	block_t *block;

	blocks_per_group = ext4_superblock_get_blocks_per_group(fs->superblock);
	block_group = block_index / blocks_per_group;
	index_in_group = block_index % blocks_per_group;

	rc = ext4_filesystem_get_block_group_ref(fs, block_group, &bg_ref);
	if (rc != EOK) {
		return rc;
	}

	bitmap_block = ext4_block_group_get_block_bitmap(bg_ref->block_group);

	rc = block_get(&block, fs->device, bitmap_block, 0);
	if (rc != EOK) {
		return rc;
	}

	ext4_bitmap_free_bit(block->data, index_in_group);

	block->dirty = true;
	rc = block_put(block);
	if (rc != EOK) {
		return rc;
	}

	uint32_t free_blocks = ext4_block_group_get_free_blocks_count(bg_ref->block_group);
	free_blocks++;
	ext4_block_group_set_free_blocks_count(bg_ref->block_group, free_blocks);

	rc = ext4_filesystem_put_block_group_ref(bg_ref);
	if (rc != EOK) {
		// TODO error
		return rc;
	}

	return EOK;
}


/**
 * @}
 */ 
