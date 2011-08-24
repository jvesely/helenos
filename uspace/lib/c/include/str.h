/*
 * Copyright (c) 2005 Martin Decky
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

/** @addtogroup libc
 * @{
 */
/** @file
 */

#ifndef LIBC_STR_H_
#define LIBC_STR_H_

#include <mem.h>
#include <sys/types.h>
#include <bool.h>

#define U_SPECIAL  '?'

/** No size limit constant */
#define STR_NO_LIMIT  ((size_t) -1)

/** Maximum size of a string containing @c length characters */
#define STR_BOUNDS(length)  ((length) << 2)

/**
 * Maximum size of a buffer needed to a string converted from space-padded
 * ASCII of size @a spa_size using spascii_to_str().
 */
#define SPASCII_STR_BUFSIZE(spa_size) ((spa_size) + 1)

extern wchar_t str_decode(const char *str, size_t *offset, size_t sz);
extern int chr_encode(const wchar_t ch, char *str, size_t *offset, size_t sz);

extern size_t str_size(const char *str);
extern size_t wstr_size(const wchar_t *str);

extern size_t str_lsize(const char *str, size_t max_len);
extern size_t wstr_lsize(const wchar_t *str, size_t max_len);

extern size_t str_length(const char *str);
extern size_t wstr_length(const wchar_t *wstr);

extern size_t str_nlength(const char *str, size_t size);
extern size_t wstr_nlength(const wchar_t *str, size_t size);

extern bool ascii_check(wchar_t ch);
extern bool chr_check(wchar_t ch);

extern int str_cmp(const char *s1, const char *s2);
extern int str_lcmp(const char *s1, const char *s2, size_t max_len);

extern void str_cpy(char *dest, size_t size, const char *src);
extern void str_ncpy(char *dest, size_t size, const char *src, size_t n);
extern void str_append(char *dest, size_t size, const char *src);

extern int wstr_to_str(char *dest, size_t size, const wchar_t *src);
extern int spascii_to_str(char *dest, size_t size, const uint8_t *src, size_t n);
extern char *wstr_to_astr(const wchar_t *src);
extern wchar_t *str_to_awstr(const char *src);
extern int str_to_wstr(wchar_t *dest, size_t dlen, const char *src);
extern int utf16_to_str(char *dest, size_t size, const uint16_t *src);
extern int str_to_utf16(uint16_t *dest, size_t size, const char *src);

extern char *str_chr(const char *str, wchar_t ch);
extern char *str_rchr(const char *str, wchar_t ch);
extern wchar_t *wstr_chr(const wchar_t *wstr, wchar_t ch);
extern wchar_t *wstr_rchr(const wchar_t *wstr, wchar_t ch);

extern bool wstr_linsert(wchar_t *str, wchar_t ch, size_t pos, size_t max_pos);
extern bool wstr_remove(wchar_t *str, size_t pos);

extern char *str_dup(const char *);
extern char *str_ndup(const char *, size_t max_size);

extern void str_reverse(char* begin, char* end);
extern int size_t_str(size_t value, int base, char* str, size_t size);

extern int str_uint64(const char *, char **, unsigned int, bool, uint64_t *);
extern int str_size_t(const char *, char **, unsigned int, bool, size_t *);

extern void order_suffix(const uint64_t, uint64_t *, char *);
extern void bin_order_suffix(const uint64_t, uint64_t *, const char **, bool);

/*
 * TODO: Get rid of this.
 */

extern int stricmp(const char *, const char *);

extern long int strtol(const char *, char **, int);
extern unsigned long strtoul(const char *, char **, int);

extern char * strtok_r(char *, const char *, char **);
extern char * strtok(char *, const char *);

#endif

/** @}
 */
