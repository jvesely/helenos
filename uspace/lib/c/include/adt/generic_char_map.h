/*
 * Copyright (c) 2009 Lukas Mejdrech
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
 *  @{
 */

/** @file
 *  Character string to generic type map.
 */

#ifndef LIBC_GENERIC_CHAR_MAP_H_
#define LIBC_GENERIC_CHAR_MAP_H_

#include <unistd.h>
#include <errno.h>

#include <adt/char_map.h>
#include <adt/generic_field.h>

/** Internal magic value for a&nbsp;map consistency check. */
#define GENERIC_CHAR_MAP_MAGIC_VALUE	0x12345622

/** Character string to generic type map declaration.
 *  @param[in] name	Name of the map.
 *  @param[in] type	Inner object type.
 */
#define GENERIC_CHAR_MAP_DECLARE(name, type) \
	GENERIC_FIELD_DECLARE(name##_items, type) \
	\
	typedef	struct name name##_t; \
	typedef	name##_t *name##_ref; \
	\
	struct	name { \
		char_map_t names; \
		name##_items_t values; \
		int magic; \
	}; \
	\
	int name##_add(name##_ref, const char *, const size_t, type *); \
	int name##_count(name##_ref); \
	void name##_destroy(name##_ref); \
	void name##_exclude(name##_ref, const char *, const size_t); \
	type *name##_find(name##_ref, const char *, const size_t); \
	int name##_initialize(name##_ref); \
	int name##_is_valid(name##_ref);

/** Character string to generic type map implementation.
 *
 * Should follow declaration with the same parameters.
 *
 * @param[in] name	Name of the map.
 * @param[in] type	Inner object type.
 */
#define GENERIC_CHAR_MAP_IMPLEMENT(name, type) \
	GENERIC_FIELD_IMPLEMENT(name##_items, type) \
	\
	int name##_add(name##_ref map, const char *name, const size_t length, \
	     type *value) \
	{ \
		int rc; \
		int index; \
		if (!name##_is_valid(map)) \
			return EINVAL; \
		index = name##_items_add(&map->values, value); \
		if (index < 0) \
			return index; \
		rc = char_map_add(&map->names, name, length, index); \
		if (rc != EOK) { \
			name##_items_exclude_index(&map->values, index); \
			return rc; \
		} \
		return EOK; \
	} \
	\
	int name##_count(name##_ref map) \
	{ \
		return name##_is_valid(map) ? \
		    name##_items_count(&map->values) : -1; \
	} \
	\
	void name##_destroy(name##_ref map) \
	{ \
		if (name##_is_valid(map)) { \
			char_map_destroy(&map->names); \
			name##_items_destroy(&map->values); \
		} \
	} \
	\
	void name##_exclude(name##_ref map, const char *name, \
	    const size_t length) \
	{ \
		if (name##_is_valid(map)) { \
			int index; \
			index = char_map_exclude(&map->names, name, length); \
			if (index != CHAR_MAP_NULL) \
				name##_items_exclude_index(&map->values, \
				     index); \
		} \
	} \
	\
	type *name##_find(name##_ref map, const char *name, \
	    const size_t length) \
	{ \
		if (name##_is_valid(map)) { \
			int index; \
			index = char_map_find(&map->names, name, length); \
			if( index != CHAR_MAP_NULL) \
				return name##_items_get_index(&map->values, \
				    index); \
		} \
		return NULL; \
	} \
	\
	int name##_initialize(name##_ref map) \
	{ \
		int rc; \
		if (!map) \
			return EINVAL; \
		rc = char_map_initialize(&map->names); \
		if (rc != EOK) \
			return rc; \
		rc = name##_items_initialize(&map->values); \
		if (rc != EOK) { \
			char_map_destroy(&map->names); \
			return rc; \
		} \
		map->magic = GENERIC_CHAR_MAP_MAGIC_VALUE; \
		return EOK; \
	} \
	\
	int name##_is_valid(name##_ref map) \
	{ \
		return map && (map->magic == GENERIC_CHAR_MAP_MAGIC_VALUE); \
	}

#endif

/** @}
 */
