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

#ifndef LIBEXT4_LIBEXT4_SUPERBLOCK_H_
#define LIBEXT4_LIBEXT4_SUPERBLOCK_H_

#include <libblock.h>
#include <sys/types.h>

/*
 * Structure of the super block
 */
typedef struct ext4_superblock {
	uint32_t inodes_count; // Inodes count
	uint32_t s_blocks_count_lo; // Blocks count
	uint32_t s_r_blocks_count_lo; // Reserved blocks count
	uint32_t free_blocks_count_lo; // Free blocks count
	uint32_t free_inodes_count; // Free inodes count
	uint32_t first_data_block; // First Data Block
	uint32_t log_block_size; // Block size
	uint32_t s_obso_log_frag_size; // Obsoleted fragment size
	uint32_t s_blocks_per_group; // Number of blocks per group
	uint32_t s_obso_frags_per_group; // Obsoleted fragments per group
	uint32_t inodes_per_group; // Number of inodes per group
	uint32_t s_mtime; // Mount time
	uint32_t s_wtime; // Write time
	uint16_t mount_count; // Mount count
	uint16_t max_mount_count; // Maximal mount count
	uint16_t magic; // Magic signature
	uint16_t state; // File system state
	uint16_t errors; // Behaviour when detecting errors
	uint16_t minor_rev_level; // Minor revision level
	uint32_t last_check; // Time of last check
	uint32_t checkinterval; // Maximum time between checks
	uint32_t creator_os; // Creator OS
	uint32_t rev_level; // Revision level
	uint16_t s_def_resuid; // Default uid for reserved blocks
	uint16_t s_def_resgid; // Default gid for reserved blocks

	// Fields for EXT4_DYNAMIC_REV superblocks only.
	uint32_t s_first_ino; // First non-reserved inode
	uint16_t inode_size; // Size of inode structure
	uint16_t s_block_group_nr; // Block group number of this superblock
	uint32_t features_compatible; // Compatible feature set
	uint32_t features_incompatible; // Incompatible feature set
	uint32_t features_read_only; // Readonly-compatible feature set
	uint8_t s_uuid[16]; // 128-bit uuid for volume
	char s_volume_name[16]; // Volume name
	char s_last_mounted[64]; // Directory where last mounted
	uint32_t s_algorithm_usage_bitmap; // For compression

	/*
	 * Performance hints. Directory preallocation should only
	 * happen if the EXT4_FEATURE_COMPAT_DIR_PREALLOC flag is on.
	 */
	uint8_t s_prealloc_blocks; // Number of blocks to try to preallocate
	uint8_t s_prealloc_dir_blocks; // Number to preallocate for dirs
	uint16_t s_reserved_gdt_blocks; // Per group desc for online growth

	/*
	 * Journaling support valid if EXT4_FEATURE_COMPAT_HAS_JOURNAL set.
	 */
	uint8_t s_journal_uuid[16]; // UUID of journal superblock
	uint32_t s_journal_inum; // Inode number of journal file
	uint32_t s_journal_dev; // Device number of journal file
	uint32_t s_last_orphan; // Head of list of inodes to delete
	uint32_t s_hash_seed[4]; // HTREE hash seed
	uint8_t s_def_hash_version; // Default hash version to use
	uint8_t s_jnl_backup_type;
	uint16_t s_desc_size; // Size of group descriptor
	uint32_t s_default_mount_opts; // Default mount options
	uint32_t s_first_meta_bg; // First metablock block group
	uint32_t s_mkfs_time; // When the filesystem was created
	uint32_t s_jnl_blocks[17]; // Backup of the journal inode

	/* 64bit support valid if EXT4_FEATURE_COMPAT_64BIT */
	uint32_t s_blocks_count_hi; // Blocks count
	uint32_t s_r_blocks_count_hi; // Reserved blocks count
	uint32_t s_free_blocks_count_hi; // Free blocks count
	uint16_t s_min_extra_isize; // All inodes have at least # bytes
	uint16_t s_want_extra_isize; // New inodes should reserve # bytes
	uint32_t s_flags; // Miscellaneous flags
	uint16_t s_raid_stride; // RAID stride
	uint16_t s_mmp_interval; // # seconds to wait in MMP checking
	uint64_t s_mmp_block; // Block for multi-mount protection
	uint32_t s_raid_stripe_width; // blocks on all data disks (N*stride)
	uint8_t s_log_groups_per_flex; // FLEX_BG group size
	uint8_t s_reserved_char_pad;
	uint16_t s_reserved_pad;
	uint64_t s_kbytes_written; // Number of lifetime kilobytes written
	uint32_t s_snapshot_inum; // Inode number of active snapshot
	uint32_t s_snapshot_id; // Sequential ID of active snapshot
	uint64_t s_snapshot_r_blocks_count; /* reserved blocks for active snapshot's future use */
	uint32_t s_snapshot_list; // inode number of the head of the on-disk snapshot list
	uint32_t s_error_count; // number of fs errors
	uint32_t s_first_error_time; // First time an error happened
	uint32_t s_first_error_ino; // Inode involved in first error
	uint64_t s_first_error_block; // block involved of first error
	uint8_t s_first_error_func[32]; // Function where the error happened
	uint32_t s_first_error_line; // Line number where error happened
	uint32_t s_last_error_time; // Most recent time of an error
	uint32_t s_last_error_ino; // Inode involved in last error
	uint32_t s_last_error_line; // Line number where error happened
	uint64_t s_last_error_block;     /* block involved of last error */
	uint8_t s_last_error_func[32];  /* function where the error happened */
	uint8_t s_mount_opts[64];
	uint32_t s_reserved[112]; // Padding to the end of the block

} __attribute__((packed)) ext4_superblock_t;

#define EXT4_SUPERBLOCK_MAGIC		0xEF53
#define EXT4_SUPERBLOCK_SIZE		1024
#define EXT4_SUPERBLOCK_OFFSET		1024

extern uint16_t ext4_superblock_get_magic(ext4_superblock_t *);
extern uint32_t ext4_superblock_get_first_block(ext4_superblock_t *);
extern uint32_t ext4_superblock_get_block_size_log2(ext4_superblock_t *);
extern uint32_t ext4_superblock_get_block_size(ext4_superblock_t *);
extern uint32_t	ext4_superblock_get_rev_level(ext4_superblock_t *);
extern uint16_t	ext4_superblock_get_inode_size(ext4_superblock_t *);
extern uint32_t	ext4_superblock_get_inodes_per_group(ext4_superblock_t *);
extern uint32_t	ext4_superblock_get_features_compatible(ext4_superblock_t *);
extern uint32_t	ext4_superblock_get_features_incompatible(ext4_superblock_t *);
extern uint32_t	ext4_superblock_get_features_read_only(ext4_superblock_t *);

extern int ext4_superblock_read_direct(service_id_t, ext4_superblock_t **);
extern int ext4_superblock_check_sanity(ext4_superblock_t *);

#endif

/**
 * @}
 */
