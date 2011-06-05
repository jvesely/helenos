/*
 * Copyright (c) 2008 Jakub Jermar
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

#ifndef FAT_FAT_DENTRY_H_
#define FAT_FAT_DENTRY_H_

#include <stdint.h>
#include <bool.h>

#define FAT_NAME_LEN		8
#define FAT_EXT_LEN		3

#define FAT_NAME_DOT		".       "
#define FAT_NAME_DOT_DOT	"..      "
#define FAT_EXT_PAD		"   "

#define FAT_ATTR_RDONLY   0x01
#define FAT_ATTR_HIDDEN   0x02
#define FAT_ATTR_SYSTEM   0x04
#define FAT_ATTR_VOLLABEL 0x08
#define FAT_ATTR_SUBDIR   0x10
#define FAT_ATTR_ARCHIVE  0x20
#define FAT_ATTR_LNAME \
    (FAT_ATTR_RDONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLLABEL)
    
#define FAT_LCASE_LOWER_NAME	0x08
#define FAT_LCASE_LOWER_EXT	0x10

#define FAT_PAD			' ' 

#define FAT_DENTRY_UNUSED	0x00
#define FAT_DENTRY_E5_ESC	0x05
#define FAT_DENTRY_DOT		0x2e
#define FAT_DENTRY_ERASED	0xe5
#define FAT_LAST_LONG_ENTRY 0x40

typedef enum {
	FAT_DENTRY_SKIP,
	FAT_DENTRY_LAST,
	FAT_DENTRY_FREE,
	FAT_DENTRY_VALID
} fat_dentry_clsf_t;

typedef struct {
	union {
		struct {
			uint8_t		name[8];
			uint8_t		ext[3];
			uint8_t		attr;
			uint8_t		lcase;
			uint8_t		ctime_fine;
			uint16_t	ctime;
			uint16_t	cdate;
			uint16_t	adate;
			union {
				uint16_t	eaidx;		/* FAT12/FAT16 */
				uint16_t	firstc_hi;	/* FAT32 */
			} __attribute__ ((packed));
			uint16_t	mtime;
			uint16_t	mdate;
			union {
				uint16_t	firstc;		/* FAT12/FAT16 */
				uint16_t	firstc_lo;	/* FAT32 */
			} __attribute__ ((packed));
			uint32_t	size;
		} __attribute__ ((packed));
		struct {
			uint8_t		order;
			uint8_t		name1[10];
			uint8_t		attr;
			uint8_t		type;
			uint8_t		check_sum;
			uint8_t		name2[12];
			uint16_t	firstc_lo; /* MUST be 0 */
			uint8_t		name3[4];
		} long_entry __attribute__ ((packed));
	};
} __attribute__ ((packed)) fat_dentry_t;

extern int fat_dentry_namecmp(char *, const char *);
extern bool fat_dentry_name_verify(const char *);
extern void fat_dentry_name_get(const fat_dentry_t *, char *);
extern void fat_dentry_name_set(fat_dentry_t *, const char *);
extern fat_dentry_clsf_t fat_classify_dentry(const fat_dentry_t *);
extern uint8_t fat_dentry_chksum(uint8_t *);

#endif

/**
 * @}
 */
