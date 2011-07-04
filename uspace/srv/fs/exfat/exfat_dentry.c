/*
 * Copyright (c) 2011 Oleg Romanenko
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
 * @file	exfat_dentry.c
 * @brief	Functions that work with exFAT directory entries.
 */

#include "exfat_dentry.h"
#include <ctype.h>
#include <str.h>
#include <errno.h>
#include <byteorder.h>
#include <assert.h>

exfat_dentry_clsf_t exfat_classify_dentry(const exfat_dentry_t *d)
{
	switch (d->type) {
	case EXFAT_TYPE_VOLLABEL:
		return EXFAT_DENTRY_VOLLABEL;
	case EXFAT_TYPE_BITMAP:
		return EXFAT_DENTRY_BITMAP;
	case EXFAT_TYPE_UCTABLE:
		return EXFAT_DENTRY_UCTABLE;
	case EXFAT_TYPE_GUID:
		return EXFAT_DENTRY_GUID;
	case EXFAT_TYPE_FILE:
		return EXFAT_DENTRY_FILE;
	case EXFAT_TYPE_STREAM:
		return EXFAT_DENTRY_STREAM;
	case EXFAT_TYPE_NAME:
		return EXFAT_DENTRY_NAME;
	case EXFAT_TYPE_UNUSED:
		return EXFAT_DENTRY_LAST;
	default:
		if (d->type & EXFAT_TYPE_USED)
			return EXFAT_DENTRY_SKIP;
		else
			return EXFAT_DENTRY_FREE;
	}
}

uint16_t exfat_name_hash(const uint16_t *name)
{
	/* TODO */
	return 0;
}

void exfat_set_checksum(const exfat_dentry_t *d, uint16_t *chksum)
{
	/* TODO */
}

int exfat_dentry_get_name(const exfat_name_dentry_t *name, size_t *count, uint16_t *dst)
{
	/* TODO */
	return EOK;
}

int exfat_dentry_set_name(const uint16_t *src, size_t *offset, exfat_name_dentry_t *name)
{
	/* TODO */
	return EOK;
}

bool exfat_valid_char(wchar_t ch)
{
	/* TODO */
	return false;
}

bool exfat_valid_name(const char *name)
{
	/* TODO */
	return false;
}

/**
 * @}
 */ 
