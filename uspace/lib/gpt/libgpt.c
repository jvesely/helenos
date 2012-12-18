/*
 * Copyright (c) 2011 Dominik Taborsky
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

/** @addtogroup libgpt
 * @{
 */
/** @file
 */

/* TODO:
 * This implementation only supports fixed size partition entries. Specification
 * requires otherwise, though. Use void * array and casting to achieve that.
 */

#include <ipc/bd.h>
#include <async.h>
#include <stdio.h>
#include <block.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <byteorder.h>
#include <checksum.h>
#include <mem.h>

#include "libgpt.h"

static int load_and_check_header(service_id_t handle, aoff64_t addr, size_t b_size, gpt_header_t * header);
static gpt_parts_t * alloc_part_array(uint32_t num);
static int extend_part_array(gpt_parts_t * p);
static int reduce_part_array(gpt_parts_t * p);
static long long nearest_larger_int(double a);

/** Read GPT from specific device
 * @param	dev_handle	device to read GPT from
 * 
 * @return				GPT record on success, NULL on error
 */
gpt_t * gpt_read_gpt_header(service_id_t dev_handle)
{
	int rc;
	size_t b_size;
	
	rc = block_get_bsize(dev_handle, &b_size);
	if (rc != EOK) {
		errno = rc;
		return NULL;
	}
	
	gpt_t * gpt = malloc(sizeof(gpt_t));
	if (gpt == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	
	gpt->raw_data = malloc(b_size);// We might need only sizeof(gpt_header_t),
	if (gpt == NULL) {				// but we should follow specs and have 
		free(gpt);					// zeroes through all the rest of the block
		errno = ENOMEM;
		return NULL;
	}
	
	
	rc = load_and_check_header(dev_handle, GPT_HDR_BA, b_size, gpt->raw_data);
	if (rc == EBADCHECKSUM || rc == EINVAL) {
		aoff64_t n_blocks;
		rc = block_get_nblocks(dev_handle, &n_blocks);
		if (rc != EOK) {
			errno = rc;
			goto fail;
		}
		
		rc = load_and_check_header(dev_handle, n_blocks - 1, b_size, gpt->raw_data);
		if (rc == EBADCHECKSUM || rc == EINVAL) {
			errno = rc;
			goto fail;
		}
	}
	
	gpt->device = dev_handle;
	
	return gpt;

fail:
	gpt_free_gpt(gpt);
	return NULL;
}

/** Write GPT header to device
 * @param header		GPT header to be written
 * @param dev_handle	device handle to write the data to
 * 
 * @return				0 on success, libblock error code otherwise
 */
int gpt_write_gpt_header(gpt_t * gpt, service_id_t dev_handle)
{
	int rc;
	size_t b_size;
	
	gpt->raw_data->header_crc32 = 0;
	gpt->raw_data->header_crc32 = compute_crc32((uint8_t *) gpt->raw_data,
					uint32_t_le2host(gpt->raw_data->header_size));
	
	rc = block_get_bsize(dev_handle, &b_size);
	if (rc != EOK)
		return rc;
	
	rc = block_init(EXCHANGE_ATOMIC, dev_handle, b_size);
	if (rc != EOK)
		return rc;
	
	/* Write to main GPT header location */
	rc = block_write_direct(dev_handle, GPT_HDR_BA, GPT_HDR_BS, gpt->raw_data);
	if (rc != EOK)
		block_fini(dev_handle);
		return rc;
	
	aoff64_t n_blocks;
	rc = block_get_nblocks(dev_handle, &n_blocks);
	if (rc != EOK)
		return rc;
	
	/* Write to backup GPT header location */
	//FIXME: those idiots thought it would be cool to have these fields in reverse order...
	rc = block_write_direct(dev_handle, n_blocks - 1, GPT_HDR_BS, gpt->raw_data);
	block_fini(dev_handle);
	if (rc != EOK)
		return rc;
		
	return 0;
}

/** Parse partitions from GPT
 * @param gpt	GPT to be parsed
 * 
 * @return 		partition linked list pointer or NULL on error
 * 				error code is stored in errno
 */
gpt_parts_t * gpt_read_partitions(gpt_t * gpt)
{
	int rc;
	unsigned int i;
	gpt_parts_t * res;
	uint32_t num_ent = uint32_t_le2host(gpt->raw_data->num_entries);
	uint32_t ent_size = uint32_t_le2host(gpt->raw_data->entry_size);
	uint64_t ent_lba = uint64_t_le2host(gpt->raw_data->entry_lba);
	
	res = alloc_part_array(num_ent);
	if (res == NULL) {
		//errno = ENOMEM; // already set in alloc_part_array()
		return NULL;
	}
	
	/* We can limit comm_size like this:
	 *  - we don't need more bytes
	 *  - the size of GPT partition entry can be different to 128 bytes */
	rc = block_init(EXCHANGE_SERIALIZE, gpt->device, sizeof(gpt_entry_t));
	if (rc != EOK) {
		gpt_free_partitions(res);
		errno = rc;
		return NULL;
	}
	
	size_t block_size;
	rc = block_get_bsize(gpt->device, &block_size);
	if (rc != EOK) {
		gpt_free_partitions(res);
		errno = rc;
		return NULL;
	}
	
	//size_t bufpos = 0;
	//size_t buflen = 0;
	aoff64_t pos = ent_lba * block_size;
	
	/* Now we read just sizeof(gpt_entry_t) bytes for each entry from the device.
	 * Hopefully, this does not bypass cache (no mention in libblock.c),
	 * and also allows us to have variable partition entry size (but we
	 * will always read just sizeof(gpt_entry_t) bytes - hopefully they
	 * don't break backward compatibility) */
	for (i = 0; i < num_ent; ++i) {
		//FIXME: this does bypass cache...
		rc = block_read_bytes_direct(gpt->device, pos, sizeof(gpt_entry_t), res->part_array + i);
		//FIXME: but seqread() is just too complex...
		//rc = block_seqread(gpt->device, &bufpos, &buflen, &pos, res->part_array[i], sizeof(gpt_entry_t));
		pos += ent_size;
		
		if (rc != EOK) {
			gpt_free_partitions(res);
			errno = rc;
			return NULL;
		}
	}
	
	/* FIXME: so far my boasting about variable partition entry size
	 * will not work. The CRC32 checksums will be different.
	 * This can't be fixed easily - we'd have to run the checksum
	 * on all of the partition entry array.
	 */
	uint32_t crc = compute_crc32((uint8_t *) res->part_array, res->num_ent * sizeof(gpt_entry_t));
	
	if(uint32_t_le2host(gpt->raw_data->pe_array_crc32) != crc)
	{
		gpt_free_partitions(res);
		errno = EBADCHECKSUM;
		return NULL;
	}
	
	return res;
}

/** Write GPT and partitions to device
 * @param parts			partition list to be written
 * @param header		GPT header belonging to the 'parts' partitions
 * @param dev_handle	device to write the data to
 * 
 * @return				returns EOK on succes, specific error code otherwise
 */
int gpt_write_partitions(gpt_parts_t * parts, gpt_t * gpt, service_id_t dev_handle)
{
	int rc;
	size_t b_size;
	
	gpt->raw_data->pe_array_crc32 = compute_crc32((uint8_t *) parts->part_array, parts->num_ent * gpt->raw_data->entry_size);
	
	rc = block_get_bsize(dev_handle, &b_size);
	if (rc != EOK)
		return rc;
	
	rc = block_init(EXCHANGE_ATOMIC, dev_handle, b_size);
	if (rc != EOK)
		return rc;
	
	/* Write to main GPT partition array location */
	rc = block_write_direct(dev_handle, uint64_t_le2host(gpt->raw_data->entry_lba), 
			nearest_larger_int((uint64_t_le2host(gpt->raw_data->entry_size) * parts->num_ent) / b_size), 
			parts->part_array);
	if (rc != EOK)
		block_fini(dev_handle);
		return rc;
	
	aoff64_t n_blocks;
	rc = block_get_nblocks(dev_handle, &n_blocks);
	if (rc != EOK)
		return rc;
	
	/* Write to backup GPT partition array location */
	//rc = block_write_direct(dev_handle, n_blocks - 1, GPT_HDR_BS, header->raw_data);
	block_fini(dev_handle);
	if (rc != EOK)
		return rc;
	
	
	gpt_write_gpt_header(gpt, dev_handle);
	
	return 0;
	
}

gpt_parts_t * gpt_add_partition(gpt_parts_t * parts, g_part_t * partition)
{
	
}

gpt_parts_t * gpt_remove_partition(gpt_parts_t * parts, int idx)
{
	
}

/** free() GPT header including gpt->header_lba */
void gpt_free_gpt(gpt_t * gpt)
{
	free(gpt->raw_data);
	free(gpt);
}

/** Free partition list
 * 
 * @param parts		partition list to be freed
 */
void gpt_free_partitions(gpt_parts_t * parts)
{
	free(parts->part_array);
	free(parts);
}

/** Set partition type
 * @param p			partition to be set
 * @param type		partition type to set
 * 					- see gpt_ptypes to choose from
 * 
 */
void gpt_set_part_type(g_part_t * p, int type)
{
	/* Beware: first 3 blocks are byteswapped! */
	p->raw_data.part_type[3] = gpt_ptypes[type].guid[0];
	p->raw_data.part_type[2] = gpt_ptypes[type].guid[1];
	p->raw_data.part_type[1] = gpt_ptypes[type].guid[2];
	p->raw_data.part_type[0] = gpt_ptypes[type].guid[3];
	
	p->raw_data.part_type[5] = gpt_ptypes[type].guid[4];
	p->raw_data.part_type[4] = gpt_ptypes[type].guid[5];
	
	p->raw_data.part_type[7] = gpt_ptypes[type].guid[6];
	p->raw_data.part_type[6] = gpt_ptypes[type].guid[7];
	
	p->raw_data.part_type[8] = gpt_ptypes[type].guid[8];
	p->raw_data.part_type[9] = gpt_ptypes[type].guid[9];
	p->raw_data.part_type[10] = gpt_ptypes[type].guid[10];
	p->raw_data.part_type[11] = gpt_ptypes[type].guid[11];
	p->raw_data.part_type[12] = gpt_ptypes[type].guid[12];
	p->raw_data.part_type[13] = gpt_ptypes[type].guid[13];
	p->raw_data.part_type[14] = gpt_ptypes[type].guid[14];
	p->raw_data.part_type[15] = gpt_ptypes[type].guid[15];
}

/** Copy partition name */
void gpt_set_part_name(gpt_entry_t * p, char * name[], size_t length)
{
	memcpy(p->part_name, name, length);
}

// Internal functions follow //

static int load_and_check_header(service_id_t dev_handle, aoff64_t addr, size_t b_size, gpt_header_t * header)
{
	int rc;
	
	rc = block_init(EXCHANGE_ATOMIC, dev_handle, b_size);
	if (rc != EOK)
		return rc;
	
	rc = block_read_direct(dev_handle, addr, GPT_HDR_BS, header);
	block_fini(dev_handle);
	if (rc != EOK)
		return rc;
	
	
	unsigned int i;
	/* Check the EFI signature */
	for (i = 0; i < 8; ++i) {
		if (header->efi_signature[i] != efi_signature[i])
			return EINVAL;
	}
	
	/* Check the CRC32 of the header */
	uint32_t crc = header->header_crc32;
	header->header_crc32 = 0;
	if (crc != compute_crc32((uint8_t *) header, header->header_size))
		return EBADCHECKSUM;
	else
		header->header_crc32 = crc;
	
	/* Check for zeroes in the rest of the block */
	for (i = sizeof(gpt_header_t); i < b_size; ++i) {
		if (((uint8_t *) header)[i] != 0)
			return EINVAL;
	}
		
	return EOK;
}

static gpt_parts_t * alloc_part_array(uint32_t num)
{
	gpt_parts_t * res = malloc(sizeof(gpt_parts_t));
	if (res == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	
	uint32_t size = num > GPT_BASE_PART_NUM ? num : GPT_BASE_PART_NUM;
	res->part_array = malloc(size * sizeof(gpt_entry_t));
	if (res->part_array == NULL) {
		free(res);
		errno = ENOMEM;
		return NULL;
	}
	
	res->num_ent = num;
	res->arr_size = size;
	
	return res;
}

static int extend_part_array(gpt_parts_t * p)
{
	unsigned int nsize = p->arr_size * 2;
	gpt_entry_t * tmp = malloc(nsize * sizeof(gpt_entry_t));
	if(tmp == NULL) {
		errno = ENOMEM;
		return -1;
	}
	
	memcpy(tmp, p->part_array, p->num_ent);
	free(p->part_array);
	p->part_array = tmp;
	p->arr_size = nsize;
	
	return 0;
}

static int reduce_part_array(gpt_parts_t * p)
{
	if(p->arr_size > GPT_MIN_PART_NUM) {
		unsigned int nsize = p->arr_size / 2;
		gpt_entry_t * tmp = malloc(nsize * sizeof(gpt_entry_t));
		if(tmp == NULL) {
			errno = ENOMEM;
			return -1;
		}
		
		memcpy(tmp, p->part_array, p->num_ent < nsize ? p->num_ent : nsize);
		free(p->part_array);
		p->part_array = tmp;
		p->arr_size = nsize;
	}
	
	return 0;
}

//FIXME: replace this with a library call, if it exists
static long long nearest_larger_int(double a)
{
	if ((long long) a == a) {
		return (long long) a;
	}
	
	return ((long long) a) + 1;
}




