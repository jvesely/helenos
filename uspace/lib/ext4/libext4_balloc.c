/*
 * Copyright (c) 2012 Frantisek Princ
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
 * @file	libext4_balloc.c
 * @brief	Physical block allocator.
 */

#include <errno.h>
#include <sys/types.h>
#include "libext4.h"

/** Convert block address to relative index in block group.
 *
 * @param sb			superblock pointer
 * @param block_addr	block number to convert
 * @return				relative number of block
 */
static uint32_t ext4_balloc_blockaddr2_index_in_group(ext4_superblock_t *sb,
		uint32_t block_addr)
{
	uint32_t blocks_per_group = ext4_superblock_get_blocks_per_group(sb);
	uint32_t first_block = ext4_superblock_get_first_data_block(sb);

	/* First block == 0 or 1 */
	if (first_block == 0) {
		return block_addr % blocks_per_group;
	} else {
		return (block_addr - 1) % blocks_per_group;
	}
}

/** Convert relative block number to absolute.
 *
 * @param sb			superblock pointer
 * @param index			relative index of block in group
 * @param bgid			index of block group
 * @return				absolute number of block
 */
static uint32_t ext4_balloc_index_in_group2blockaddr(ext4_superblock_t *sb,
		uint32_t index, uint32_t bgid)
{
	uint32_t blocks_per_group = ext4_superblock_get_blocks_per_group(sb);

	if (ext4_superblock_get_first_data_block(sb) == 0) {
		return bgid * blocks_per_group + index;
	} else {
		return bgid * blocks_per_group + index + 1;
	}

}

/** Compute number of block group from block address.
 *
 * @param sb			superblock pointer
 * @param block_addr	absolute address of block
 * @return 				block group index
 */
static uint32_t ext4_balloc_get_bgid_of_block(ext4_superblock_t *sb,
		uint32_t block_addr)
{
	uint32_t blocks_per_group = ext4_superblock_get_blocks_per_group(sb);
	uint32_t first_block = ext4_superblock_get_first_data_block(sb);

	/* First block == 0 or 1 */
	if (first_block == 0) {
		return block_addr / blocks_per_group;
	} else {
		return (block_addr - 1) / blocks_per_group;
	}
}


/** Free block.
 *
 * @param inode_ref			inode, where the block is allocated
 * @param block_addr		absolute block address to free
 * @return 					error code
 */
int ext4_balloc_free_block(ext4_inode_ref_t *inode_ref, uint32_t block_addr)
{
	int rc;

	ext4_filesystem_t *fs = inode_ref->fs;
	ext4_superblock_t *sb = fs->superblock;

	/* Compute indexes */
	uint32_t block_group = ext4_balloc_get_bgid_of_block(sb, block_addr);
	uint32_t index_in_group = ext4_balloc_blockaddr2_index_in_group(sb, block_addr);

	/* Load block group reference */
	ext4_block_group_ref_t *bg_ref;
	rc = ext4_filesystem_get_block_group_ref(fs, block_group, &bg_ref);
	if (rc != EOK) {
		EXT4FS_DBG("error in loading bg_ref \%d", rc);
		return rc;
	}

	/* Load block with bitmap */
	uint32_t bitmap_block_addr = ext4_block_group_get_block_bitmap(
			bg_ref->block_group, sb);
	block_t *bitmap_block;
	rc = block_get(&bitmap_block, fs->device, bitmap_block_addr, 0);
	if (rc != EOK) {
		EXT4FS_DBG("error in loading bitmap \%d", rc);
		return rc;
	}

	/* Modify bitmap */
	ext4_bitmap_free_bit(bitmap_block->data, index_in_group);
	bitmap_block->dirty = true;


	/* Release block with bitmap */
	rc = block_put(bitmap_block);
	if (rc != EOK) {
		/* Error in saving bitmap */
		ext4_filesystem_put_block_group_ref(bg_ref);
		EXT4FS_DBG("error in saving bitmap \%d", rc);
		return rc;
	}

	uint32_t block_size = ext4_superblock_get_block_size(sb);

	/* Update superblock free blocks count */
	uint32_t sb_free_blocks = ext4_superblock_get_free_blocks_count(sb);
	sb_free_blocks++;
	ext4_superblock_set_free_blocks_count(sb, sb_free_blocks);

	/* Update inode blocks count */
	uint64_t ino_blocks = ext4_inode_get_blocks_count(sb, inode_ref->inode);
	ino_blocks -= block_size / EXT4_INODE_BLOCK_SIZE;
	ext4_inode_set_blocks_count(sb, inode_ref->inode, ino_blocks);
	inode_ref->dirty = true;

	/* Update block group free blocks count */
	uint32_t free_blocks = ext4_block_group_get_free_blocks_count(
			bg_ref->block_group, sb);
	free_blocks++;
	ext4_block_group_set_free_blocks_count(bg_ref->block_group,
			sb, free_blocks);
	bg_ref->dirty = true;

	/* Release block group reference */
	rc = ext4_filesystem_put_block_group_ref(bg_ref);
	if (rc != EOK) {
		EXT4FS_DBG("error in saving bg_ref \%d", rc);
		return rc;
	}

	return EOK;
}


/** Free continuous set of blocks.
 *
 * @param inode_ref			inode, where the blocks are allocated
 * @param first				first block to release
 * @param count				number of blocks to release
 */
int ext4_balloc_free_blocks(ext4_inode_ref_t *inode_ref,
		uint32_t first, uint32_t count)
{
	int rc;

	ext4_filesystem_t *fs = inode_ref->fs;
	ext4_superblock_t *sb = fs->superblock;

	/* Compute indexes */
	uint32_t block_group_first =
			ext4_balloc_get_bgid_of_block(sb, first);
	uint32_t block_group_last =
			ext4_balloc_get_bgid_of_block(sb, first + count - 1);

	assert(block_group_first == block_group_last);

	/* Load block group reference */
	ext4_block_group_ref_t *bg_ref;
	rc = ext4_filesystem_get_block_group_ref(fs, block_group_first, &bg_ref);
	if (rc != EOK) {
		EXT4FS_DBG("error in loading bg_ref \%d", rc);
		return rc;
	}

	uint32_t index_in_group_first =
			ext4_balloc_blockaddr2_index_in_group(sb, first);


	/* Load block with bitmap */
	uint32_t bitmap_block_addr = ext4_block_group_get_block_bitmap(
			bg_ref->block_group, sb);

	block_t *bitmap_block;
	rc = block_get(&bitmap_block, fs->device, bitmap_block_addr, 0);
	if (rc != EOK) {
		EXT4FS_DBG("error in loading bitmap \%d", rc);
		return rc;
	}

	/* Modify bitmap */
	ext4_bitmap_free_bits(bitmap_block->data, index_in_group_first, count);
	bitmap_block->dirty = true;

	/* Release block with bitmap */
	rc = block_put(bitmap_block);
	if (rc != EOK) {
		/* Error in saving bitmap */
		ext4_filesystem_put_block_group_ref(bg_ref);
		EXT4FS_DBG("error in saving bitmap \%d", rc);
		return rc;
	}

	uint32_t block_size = ext4_superblock_get_block_size(sb);

	/* Update superblock free blocks count */
	uint32_t sb_free_blocks = ext4_superblock_get_free_blocks_count(sb);
	sb_free_blocks += count;
	ext4_superblock_set_free_blocks_count(sb, sb_free_blocks);

	/* Update inode blocks count */
	uint64_t ino_blocks = ext4_inode_get_blocks_count(sb, inode_ref->inode);
	ino_blocks -= count * (block_size / EXT4_INODE_BLOCK_SIZE);
	ext4_inode_set_blocks_count(sb, inode_ref->inode, ino_blocks);
	inode_ref->dirty = true;

	/* Update block group free blocks count */
	uint32_t free_blocks = ext4_block_group_get_free_blocks_count(
			bg_ref->block_group, sb);
	free_blocks += count;
	ext4_block_group_set_free_blocks_count(bg_ref->block_group,
			sb, free_blocks);
	bg_ref->dirty = true;

	/* Release block group reference */
	rc = ext4_filesystem_put_block_group_ref(bg_ref);
	if (rc != EOK) {
		EXT4FS_DBG("error in saving bg_ref \%d", rc);
		return rc;
	}

	return EOK;
}

/** Compute first block for data in block group.
 *
 * @param sb		pointer to superblock
 * @param bg		pointer to block group
 * @param bgid		index of block group
 * @return			absolute block index of first block
 */
static uint32_t ext4_balloc_get_first_data_block_in_group(
		ext4_superblock_t *sb, ext4_block_group_ref_t *bg_ref)
{
	uint32_t block_group_count = ext4_superblock_get_block_group_count(sb);
	uint32_t inode_table_first_block = ext4_block_group_get_inode_table_first_block(
			bg_ref->block_group, sb);
	uint16_t inode_table_item_size = ext4_superblock_get_inode_size(sb);
	uint32_t inodes_per_group = ext4_superblock_get_inodes_per_group(sb);
	uint32_t block_size = ext4_superblock_get_block_size(sb);
	uint32_t inode_table_bytes;

	if (bg_ref->index < block_group_count - 1) {
		inode_table_bytes = inodes_per_group * inode_table_item_size;
	} else {
		/* last block group could be smaller */
		uint32_t inodes_count_total = ext4_superblock_get_inodes_count(sb);
		inode_table_bytes =
				(inodes_count_total - ((block_group_count - 1) * inodes_per_group))
				* inode_table_item_size;
	}

	uint32_t inode_table_blocks = inode_table_bytes / block_size;

	if (inode_table_bytes % block_size) {
		inode_table_blocks++;
	}

	return inode_table_first_block + inode_table_blocks;
}

/** Compute 'goal' for allocation algorithm.
 *
 * @param inode_ref		reference to inode, to allocate block for
 * @return				goal block number
 */
static uint32_t ext4_balloc_find_goal(ext4_inode_ref_t *inode_ref)
{
	int rc;
	uint32_t goal = 0;

	ext4_superblock_t *sb = inode_ref->fs->superblock;

	uint64_t inode_size = ext4_inode_get_size(sb, inode_ref->inode);
	uint32_t block_size = ext4_superblock_get_block_size(sb);
	uint32_t inode_block_count = inode_size / block_size;

	if (inode_size % block_size != 0) {
		inode_block_count++;
	}

	/* If inode has some blocks, get last block address + 1 */
	if (inode_block_count > 0) {

		rc = ext4_filesystem_get_inode_data_block_index(inode_ref, inode_block_count - 1, &goal);
		if (rc != EOK) {
			return 0;
		}

		if (goal != 0) {
			goal++;
			return goal;
		}

		/* if goal == 0, sparse file -> continue */
	}

	/* Identify block group of inode */
	uint32_t inodes_per_group = ext4_superblock_get_inodes_per_group(sb);
	uint32_t block_group = (inode_ref->index - 1) / inodes_per_group;
	block_size = ext4_superblock_get_block_size(sb);

	/* Load block group reference */
	ext4_block_group_ref_t *bg_ref;
	rc = ext4_filesystem_get_block_group_ref(inode_ref->fs, block_group, &bg_ref);
	if (rc != EOK) {
		return 0;
	}

	/* Compute indexes */
	uint32_t block_group_count = ext4_superblock_get_block_group_count(sb);
	uint32_t inode_table_first_block = ext4_block_group_get_inode_table_first_block(
			bg_ref->block_group, sb);
	uint16_t inode_table_item_size = ext4_superblock_get_inode_size(sb);
	uint32_t inode_table_bytes;

	/* Check for last block group */
	if (block_group < block_group_count - 1) {
		inode_table_bytes = inodes_per_group * inode_table_item_size;
	} else {
		/* last block group could be smaller */
		uint32_t inodes_count_total = ext4_superblock_get_inodes_count(sb);
		inode_table_bytes =
				(inodes_count_total - ((block_group_count - 1) * inodes_per_group))
				* inode_table_item_size;
	}

	uint32_t inode_table_blocks = inode_table_bytes / block_size;

	if (inode_table_bytes % block_size) {
		inode_table_blocks++;
	}

	goal = inode_table_first_block + inode_table_blocks;

	ext4_filesystem_put_block_group_ref(bg_ref);

	return goal;
}

/** Data block allocation algorithm.
 *
 * @param inode_ref		inode to allocate block for
 * @param fblock		allocated block address
 * @return				error code
 */
int ext4_balloc_alloc_block(
		ext4_inode_ref_t *inode_ref, uint32_t *fblock)
{
	int rc;
	uint32_t allocated_block = 0;

	uint32_t bitmap_block_addr;
	block_t *bitmap_block;
	uint32_t rel_block_idx = 0;

	/* Find GOAL */
	uint32_t goal = ext4_balloc_find_goal(inode_ref);
	if (goal == 0) {
		/* no goal found => partition is full */
		EXT4FS_DBG("ERROR (goal == 0)");
		return ENOSPC;
	}

	ext4_superblock_t *sb = inode_ref->fs->superblock;

	/* Load block group number for goal and relative index */
	uint32_t block_group = ext4_balloc_get_bgid_of_block(sb, goal);
	uint32_t index_in_group = ext4_balloc_blockaddr2_index_in_group(sb, goal);


	/* Load block group reference */
	ext4_block_group_ref_t *bg_ref;
	rc = ext4_filesystem_get_block_group_ref(inode_ref->fs, block_group, &bg_ref);
	if (rc != EOK) {
		EXT4FS_DBG("initial BG ref not loaded");
		return rc;
	}

	/* Compute indexes */
	uint32_t first_in_group =
			ext4_balloc_get_first_data_block_in_group(sb, bg_ref);

	uint32_t first_in_group_index = ext4_balloc_blockaddr2_index_in_group(
			sb, first_in_group);

	if (index_in_group < first_in_group_index) {
		index_in_group = first_in_group_index;
	}

	/* Load block with bitmap */
	bitmap_block_addr = ext4_block_group_get_block_bitmap(
			bg_ref->block_group, sb);

	rc = block_get(&bitmap_block, inode_ref->fs->device,
			bitmap_block_addr, BLOCK_FLAGS_NONE);
	if (rc != EOK) {
		ext4_filesystem_put_block_group_ref(bg_ref);
		EXT4FS_DBG("initial bitmap not loaded");
		return rc;
	}

	/* Check if goal is free */
	if (ext4_bitmap_is_free_bit(bitmap_block->data, index_in_group)) {
		ext4_bitmap_set_bit(bitmap_block->data, index_in_group);
		bitmap_block->dirty = true;
		rc = block_put(bitmap_block);
		if (rc != EOK) {
			EXT4FS_DBG("goal check: error in saving bitmap \%d", rc);
			ext4_filesystem_put_block_group_ref(bg_ref);
			return rc;
		}

		allocated_block = ext4_balloc_index_in_group2blockaddr(
							sb, index_in_group, block_group);

		goto success;

	}

	uint32_t blocks_in_group = ext4_superblock_get_blocks_in_group(sb, block_group);

	uint32_t end_idx = (index_in_group + 63) & ~63;
	if (end_idx > blocks_in_group) {
		end_idx = blocks_in_group;
	}

	/* Try to find free block near to goal */
	for (uint32_t tmp_idx = index_in_group + 1; tmp_idx < end_idx; ++tmp_idx) {
		if (ext4_bitmap_is_free_bit(bitmap_block->data, tmp_idx)) {

			ext4_bitmap_set_bit(bitmap_block->data, tmp_idx);
			bitmap_block->dirty = true;
			rc = block_put(bitmap_block);
			if (rc != EOK) {
				EXT4FS_DBG("near blocks: error in saving initial bitmap \%d", rc);
				return rc;
			}

			allocated_block = ext4_balloc_index_in_group2blockaddr(
					sb, tmp_idx, block_group);

			goto success;
		}

	}

	/* Find free BYTE in bitmap */
	rc = ext4_bitmap_find_free_byte_and_set_bit(bitmap_block->data, index_in_group, &rel_block_idx, blocks_in_group);
	if (rc == EOK) {
		bitmap_block->dirty = true;
		rc = block_put(bitmap_block);
		if (rc != EOK) {
			EXT4FS_DBG("free byte: error in saving initial bitmap \%d", rc);
			return rc;
		}

		allocated_block = ext4_balloc_index_in_group2blockaddr(
				sb, rel_block_idx, block_group);

		goto success;
	}

	/* Find free bit in bitmap */
	rc = ext4_bitmap_find_free_bit_and_set(bitmap_block->data, index_in_group, &rel_block_idx, blocks_in_group);
	if (rc == EOK) {
		bitmap_block->dirty = true;
		rc = block_put(bitmap_block);
		if (rc != EOK) {
			EXT4FS_DBG("free bit: error in saving initial bitmap \%d", rc);
			return rc;
		}

		allocated_block = ext4_balloc_index_in_group2blockaddr(
				sb, rel_block_idx, block_group);

		goto success;
	}

	/* No free block found yet */
	block_put(bitmap_block);
	ext4_filesystem_put_block_group_ref(bg_ref);

	/* Try other block groups */
	uint32_t block_group_count = ext4_superblock_get_block_group_count(sb);

	uint32_t bgid = (block_group + 1) % block_group_count;
	uint32_t count = block_group_count;

	while (count > 0) {
		rc = ext4_filesystem_get_block_group_ref(inode_ref->fs, bgid, &bg_ref);
		if (rc != EOK) {
			EXT4FS_DBG("ERROR: unable to load block group \%u", bgid);
			return rc;
		}

		/* Load block with bitmap */
		bitmap_block_addr = ext4_block_group_get_block_bitmap(
				bg_ref->block_group, sb);

		rc = block_get(&bitmap_block, inode_ref->fs->device, bitmap_block_addr, 0);
		if (rc != EOK) {
			ext4_filesystem_put_block_group_ref(bg_ref);
			EXT4FS_DBG("ERROR: unable to load bitmap block");
			return rc;
		}

		/* Compute indexes */
		first_in_group = ext4_balloc_get_first_data_block_in_group(
				sb, bg_ref);
		index_in_group = ext4_balloc_blockaddr2_index_in_group(sb,
						first_in_group);
		blocks_in_group = ext4_superblock_get_blocks_in_group(sb, bgid);

		first_in_group_index = ext4_balloc_blockaddr2_index_in_group(
			sb, first_in_group);

		if (index_in_group < first_in_group_index) {
			index_in_group = first_in_group_index;
		}

		/* Try to find free byte in bitmap */
		rc = ext4_bitmap_find_free_byte_and_set_bit(bitmap_block->data, index_in_group, &rel_block_idx, blocks_in_group);
		if (rc == EOK) {
			bitmap_block->dirty = true;
			rc = block_put(bitmap_block);
			if (rc != EOK) {
				EXT4FS_DBG("ERROR: unable to save bitmap block");
				return rc;
			}

			allocated_block = ext4_balloc_index_in_group2blockaddr(
					sb, rel_block_idx, bgid);

			goto success;
		}

		/* Try to find free bit in bitmap */
		rc = ext4_bitmap_find_free_bit_and_set(bitmap_block->data, index_in_group, &rel_block_idx, blocks_in_group);
		if (rc == EOK) {
			bitmap_block->dirty = true;
			rc = block_put(bitmap_block);
			if (rc != EOK) {
				EXT4FS_DBG("ERROR: unable to save bitmap block");
				return rc;
			}

			allocated_block = ext4_balloc_index_in_group2blockaddr(
					sb, rel_block_idx, bgid);

			goto success;
		}

		block_put(bitmap_block);
		ext4_filesystem_put_block_group_ref(bg_ref);

		/* Goto next group */
		bgid = (bgid + 1) % block_group_count;
		count--;
	}

	return ENOSPC;

success:
	; 	/* Empty command - because of syntax */
	
	uint32_t block_size = ext4_superblock_get_block_size(sb);

	/* Update superblock free blocks count */
	uint32_t sb_free_blocks = ext4_superblock_get_free_blocks_count(sb);
	sb_free_blocks--;
	ext4_superblock_set_free_blocks_count(sb, sb_free_blocks);

	/* Update inode blocks (different block size!) count */
	uint64_t ino_blocks = ext4_inode_get_blocks_count(sb, inode_ref->inode);
	ino_blocks += block_size / EXT4_INODE_BLOCK_SIZE;
	ext4_inode_set_blocks_count(sb, inode_ref->inode, ino_blocks);
	inode_ref->dirty = true;

	/* Update block group free blocks count */
	uint32_t bg_free_blocks = ext4_block_group_get_free_blocks_count(
			bg_ref->block_group, sb);
	bg_free_blocks--;
	ext4_block_group_set_free_blocks_count(bg_ref->block_group, sb, bg_free_blocks);
	bg_ref->dirty = true;

	ext4_filesystem_put_block_group_ref(bg_ref);

	*fblock = allocated_block;
	return EOK;
}

/** Try to allocate concrete block.
 *
 * @param inode_ref		inode to allocate block for
 * @param fblock		block address to allocate
 * @param free			output value - if target block is free
 * @return				error code
 */
int ext4_balloc_try_alloc_block(ext4_inode_ref_t *inode_ref,
		uint32_t fblock, bool *free)
{
	int rc = EOK;

	ext4_filesystem_t *fs = inode_ref->fs;
	ext4_superblock_t *sb = fs->superblock;

	/* Compute indexes */
	uint32_t block_group = ext4_balloc_get_bgid_of_block(sb, fblock);
	uint32_t index_in_group = ext4_balloc_blockaddr2_index_in_group(sb, fblock);

	/* Load block group reference */
	ext4_block_group_ref_t *bg_ref;
	rc = ext4_filesystem_get_block_group_ref(fs, block_group, &bg_ref);
	if (rc != EOK) {
		EXT4FS_DBG("error in loading bg_ref \%d", rc);
		return rc;
	}

	/* Load block with bitmap */
	uint32_t bitmap_block_addr = ext4_block_group_get_block_bitmap(
			bg_ref->block_group, sb);
	block_t *bitmap_block;
	rc = block_get(&bitmap_block, fs->device, bitmap_block_addr, 0);
	if (rc != EOK) {
		EXT4FS_DBG("error in loading bitmap \%d", rc);
		return rc;
	}

	/* Check if block is free */
	*free = ext4_bitmap_is_free_bit(bitmap_block->data, index_in_group);

	/* Allocate block if possible */
	if (*free) {
		ext4_bitmap_set_bit(bitmap_block->data, index_in_group);
		bitmap_block->dirty = true;
	}

	/* Release block with bitmap */
	rc = block_put(bitmap_block);
	if (rc != EOK) {
		/* Error in saving bitmap */
		ext4_filesystem_put_block_group_ref(bg_ref);
		EXT4FS_DBG("error in saving bitmap \%d", rc);
		return rc;
	}

	/* If block is not free, return */
	if (!(*free)) {
		goto terminate;
	}

	uint32_t block_size = ext4_superblock_get_block_size(sb);

	/* Update superblock free blocks count */
	uint32_t sb_free_blocks = ext4_superblock_get_free_blocks_count(sb);
	sb_free_blocks--;
	ext4_superblock_set_free_blocks_count(sb, sb_free_blocks);

	/* Update inode blocks count */
	uint64_t ino_blocks = ext4_inode_get_blocks_count(sb, inode_ref->inode);
	ino_blocks += block_size / EXT4_INODE_BLOCK_SIZE;
	ext4_inode_set_blocks_count(sb, inode_ref->inode, ino_blocks);
	inode_ref->dirty = true;

	/* Update block group free blocks count */
	uint32_t free_blocks = ext4_block_group_get_free_blocks_count(
			bg_ref->block_group, sb);
	free_blocks--;
	ext4_block_group_set_free_blocks_count(bg_ref->block_group,
			sb, free_blocks);
	bg_ref->dirty = true;

terminate:

	rc = ext4_filesystem_put_block_group_ref(bg_ref);
	if (rc != EOK) {
		EXT4FS_DBG("error in saving bg_ref \%d", rc);
		return rc;
	}

	return rc;
}

/**
 * @}
 */ 
