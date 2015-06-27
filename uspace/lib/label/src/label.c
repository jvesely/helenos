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

/** @addtogroup liblabel
 * @{
 */
/**
 * @file Disk label library.
 */

#include <errno.h>
#include <label.h>
#include <mem.h>
#include <stdlib.h>

int label_open(service_id_t sid, label_t **rlabel)
{
	label_t *label;

	label = calloc(1, sizeof(label_t));
	if (label == NULL)
		return ENOMEM;

	*rlabel = label;
	return EOK;
}

int label_create(service_id_t sid, label_type_t ltype, label_t **rlabel)
{
	label_t *label;

	label = calloc(1, sizeof(label_t));
	if (label == NULL)
		return ENOMEM;

	*rlabel = label;
	return EOK;
}

void label_close(label_t *label)
{
	free(label);
}

int label_destroy(label_t *label)
{
	free(label);
	return EOK;
}

int label_get_info(label_t *label, label_info_t *linfo)
{
	memset(linfo, 0, sizeof(label_info_t));
	linfo->dcnt = dc_empty;
	return EOK;
}

label_part_t *label_part_first(label_t *label)
{
	return NULL;
}

label_part_t *label_part_next(label_part_t *oart)
{
	return NULL;
}


int label_part_create(label_t *label, label_part_spec_t *pspec,
    label_part_t **rpart)
{
	return ENOTSUP;
}

int label_part_destroy(label_part_t *part)
{
	return EOK;
}

void label_pspec_init(label_part_spec_t *pspec)
{
	memset(pspec, 0, sizeof(label_part_spec_t));
}

/** @}
 */
