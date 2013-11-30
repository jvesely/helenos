/*
 * Copyright (c) 2012, 2013 Dominik Taborsky
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

 /** @addtogroup hdisk
 * @{
 */
/** @file
 */

#include <stdio.h>
#include <errno.h>
#include <str_error.h>
#include <sys/types.h>

#include "func_mbr.h"
#include "input.h"

static int set_mbr_partition(tinput_t *, mbr_part_t *, label_t *);

int construct_mbr_label(label_t *this)
{
	this->layout = LYT_MBR;
	this->alignment = 1;
	
	this->add_part      = add_mbr_part;
	this->delete_part   = delete_mbr_part;
	this->destroy_label = destroy_mbr_label;
	this->new_label     = new_mbr_label;
	this->print_parts   = print_mbr_parts;
	this->read_parts    = read_mbr_parts;
	this->write_parts   = write_mbr_parts;
	this->extra_funcs   = extra_mbr_funcs;
	
	return this->new_label(this);
}

int add_mbr_part(label_t *this, tinput_t *in)
{
	int rc;
	
	mbr_part_t *part = mbr_alloc_partition();
	
	rc = set_mbr_partition(in, part, this);
	if (rc != EOK)
		return rc;
	
	rc = mbr_add_partition(this->data.mbr, part);
	if (rc != ERR_OK) {
		printf("Error adding partition: %d\n", rc);
	}
	
	return EOK;
}

int delete_mbr_part(label_t *this, tinput_t *in)
{
	int rc;
	size_t idx;
	
	printf("Number of the partition to delete (counted from 0): ");
	idx = get_input_size_t(in);
	
	if (idx == 0 && errno != EOK)
		return errno;
	
	rc = mbr_remove_partition(this->data.mbr, idx);
	if (rc != EOK) {
		printf("Error: partition does not exist?\n");
	}
	
	return EOK;
}

int destroy_mbr_label(label_t *this)
{
	mbr_free_label(this->data.mbr);
	return EOK;
}

int new_mbr_label(label_t *this)
{
	this->data.mbr = mbr_alloc_label();
	if (this->data.mbr == NULL)
		return ENOMEM;
	
	mbr_set_device(this->data.mbr, this->device);
	return EOK;
}

/** Print current partition scheme */
int print_mbr_parts(label_t *this)
{
	int num = 0;
	
	printf("Current partition scheme (MBR)(number of blocks: %" PRIu64 "):\n", this->nblocks);
	printf("\t\t%10s  %10s %10s %10s %7s\n", "Bootable:", "Start:", "End:", "Length:", "Type:");
	
	mbr_part_t *it;
	
	for (it = mbr_get_first_partition(this->data.mbr); it != NULL;
	     it = mbr_get_next_partition(this->data.mbr, it)) {
		if (it->type == PT_UNUSED)
			continue;
		
		printf("\tP%d:\t", num);
		if (mbr_get_flag(it, ST_BOOT))
			printf("*");
		else
			printf(" ");
		
		printf("\t%10u %10u %10u %7u\n", it->start_addr, it->start_addr + it->length, it->length, it->type);
		
		num++;
	}
	
	printf("%d partitions found.\n", num);
	
	return EOK;
}

int read_mbr_parts(label_t *this)
{
	int rc;
	rc = mbr_read_mbr(this->data.mbr, this->device);
	if (rc != EOK)
		return rc;
	
	if (!mbr_is_mbr(this->data.mbr))
		return EINVAL;
	
	rc = mbr_read_partitions(this->data.mbr);
	if (rc != EOK)
		return rc;
	
	return EOK;
}

int write_mbr_parts(label_t *this)
{
	int rc = mbr_write_partitions(this->data.mbr, this->device);
	if (rc != EOK) {
		printf("Error occured during writing: ERR: %d: %s\n", rc, str_error(rc));
	}
	
	return rc;
}

int extra_mbr_funcs(label_t *this, tinput_t *in)
{
	printf("Not implemented.\n");
	return EOK;
}

static int set_mbr_partition(tinput_t *in, mbr_part_t *p, label_t * this)
{
	int c;
	uint8_t type;
	
	printf("Primary (p) or logical (l): ");
	c = getchar();
	printf("%c\n", c);

	switch (c) {
	case 'p':
		mbr_set_flag(p, ST_LOGIC, false);
		break;
	case 'l':
		mbr_set_flag(p, ST_LOGIC, true);
		break;
	default:
		printf("Invalid type. Cancelled.\n");
		return EINVAL;
	}
	
	printf("ST_LOGIC: %d, %hd\n", mbr_get_flag(p, ST_LOGIC), p->status);
	
	printf("Set type (0-255): ");
	type = get_input_uint8(in);
	if (type == 0 && errno != EOK)
		return errno;

	///TODO: there can only be one boolabel partition; let's do it just like fdisk
	printf("Bootable? (y/n): ");
	c = getchar();
	if (c != 'y' && c != 'Y' && c != 'n' && c != 'N') {
		printf("Invalid value. Cancelled.");
		return EINVAL;
	}
	printf("%c\n", c);
	mbr_set_flag(p, ST_BOOT, (c == 'y' || c == 'Y') ? true : false);


	uint32_t sa, ea;

	printf("Set starting address: ");
	sa = get_input_uint32(in);
	if (sa == 0 && errno != EOK)
		return errno;
	
	if (this->alignment != 0 && this->alignment != 1 && sa % this->alignment != 0) {
		sa = mbr_get_next_aligned(sa, this->alignment);
		printf("Starting address was aligned to %u.\n", sa);
	}
	
	printf("Set end addres (max: %" PRIu64 "): ", this->nblocks);
	ea = get_input_uint32(in);
	if (ea == 0 && errno != EOK)
		return errno;
	
	if (ea < sa) {
		printf("Invalid value. Canceled.\n");
		return EINVAL;
	}
	
	p->type = type;
	p->start_addr = sa;
	p->length = ea - sa;

	return EOK;
}






