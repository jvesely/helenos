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

/** @addtogroup libfdisk
 * @{
 */
/**
 * @file Disk management library.
 */

#include <adt/list.h>
#include <block.h>
#include <errno.h>
#include <fdisk.h>
#include <loc.h>
#include <stdio.h>
#include <stdlib.h>
#include <str.h>

static void fdisk_dev_info_delete(fdisk_dev_info_t *info)
{
	if (info == NULL)
		return;

	if (info->blk_inited)
		block_fini(info->svcid);

	free(info->svcname);
	free(info);
}

int fdisk_dev_list_get(fdisk_dev_list_t **rdevlist)
{
	fdisk_dev_list_t *devlist = NULL;
	fdisk_dev_info_t *info;
	category_id_t disk_cat;
	service_id_t *svcs = NULL;
	size_t count, i;
	int rc;

	devlist = calloc(1, sizeof(fdisk_dev_list_t));
	if (devlist == NULL)
		return ENOMEM;

	list_initialize(&devlist->devinfos);

	rc = loc_category_get_id("disk", &disk_cat, 0);
	if (rc != EOK) {
		rc = EIO;
		goto error;
	}

	rc = loc_category_get_svcs(disk_cat, &svcs, &count);
	if (rc != EOK) {
		rc = EIO;
		goto error;
	}

	for (i = 0; i < count; i++) {
		info = calloc(1, sizeof(fdisk_dev_info_t));
		if (info == NULL) {
			rc = ENOMEM;
			goto error;
		}

		info->svcid = svcs[i];
		info->devlist = devlist;
		list_append(&info->ldevlist, &devlist->devinfos);
	}

	*rdevlist = devlist;
	free(svcs);
	return EOK;
error:
	free(svcs);
	fdisk_dev_list_free(devlist);
	return rc;
}

void fdisk_dev_list_free(fdisk_dev_list_t *devlist)
{
	fdisk_dev_info_t *info;

	if (devlist == NULL)
		return;

	while (!list_empty(&devlist->devinfos)) {
		info = list_get_instance(list_first(
		    &devlist->devinfos), fdisk_dev_info_t,
		    ldevlist);

		list_remove(&info->ldevlist);
		fdisk_dev_info_delete(info);
	}

	free(devlist);
}

fdisk_dev_info_t *fdisk_dev_first(fdisk_dev_list_t *devlist)
{
	if (list_empty(&devlist->devinfos))
		return NULL;

	return list_get_instance(list_first(&devlist->devinfos),
	    fdisk_dev_info_t, ldevlist);
}

fdisk_dev_info_t *fdisk_dev_next(fdisk_dev_info_t *devinfo)
{
	link_t *lnext;

	lnext = list_next(&devinfo->ldevlist,
	    &devinfo->devlist->devinfos);
	if (lnext == NULL)
		return NULL;

	return list_get_instance(lnext, fdisk_dev_info_t,
	    ldevlist);
}

void fdisk_dev_get_svcid(fdisk_dev_info_t *info, service_id_t *rsid)
{
	*rsid = info->svcid;
}

int fdisk_dev_get_svcname(fdisk_dev_info_t *info, char **rname)
{
	char *name;
	int rc;

	if (info->svcname == NULL) {
		rc = loc_service_get_name(info->svcid,
		    &info->svcname);
		if (rc != EOK)
			return rc;
	}

	name = str_dup(info->svcname);
	if (name == NULL)
		return ENOMEM;

	*rname = name;
	return EOK;
}

int fdisk_dev_capacity(fdisk_dev_info_t *info, fdisk_cap_t *cap)
{
	size_t bsize;
	aoff64_t nblocks;
	int rc;

	if (!info->blk_inited) {
		rc = block_init(EXCHANGE_SERIALIZE, info->svcid, 2048);
		if (rc != EOK)
			return rc;
	}

	rc = block_get_bsize(info->svcid, &bsize);
	if (rc != EOK)
		return EIO;

	rc = block_get_nblocks(info->svcid, &nblocks);
	if (rc != EOK)
		return EIO;

	cap->value = bsize * nblocks;
	cap->cunit = cu_byte;

	return EOK;
}

int fdisk_dev_open(service_id_t sid, fdisk_dev_t **rdev)
{
	fdisk_dev_t *dev;

	dev = calloc(1, sizeof(fdisk_dev_t));
	if (dev == NULL)
		return ENOMEM;

	dev->ltype = fdl_none;
	*rdev = dev;
	return EOK;
}

void fdisk_dev_close(fdisk_dev_t *dev)
{
	free(dev);
}

int fdisk_label_get_info(fdisk_dev_t *dev, fdisk_label_info_t *info)
{
	info->ltype = dev->ltype;
	return EOK;
}

int fdisk_label_create(fdisk_dev_t *dev, fdisk_label_type_t ltype)
{
	if (dev->ltype != fdl_none)
		return EEXISTS;

	dev->ltype = ltype;
	return EOK;
}

int fdisk_label_destroy(fdisk_dev_t *dev)
{
	if (dev->ltype == fdl_none)
		return ENOENT;

	dev->ltype = fdl_none;
	return EOK;
}

fdisk_part_t *fdisk_part_first(fdisk_dev_t *dev)
{
	return NULL;
}

fdisk_part_t *fdisk_part_next(fdisk_part_t *part)
{
	return NULL;
}

int fdisk_part_get_max_avail(fdisk_dev_t *dev, fdisk_cap_t *cap)
{
	return EOK;
}

int fdisk_part_create(fdisk_dev_t *dev, fdisk_partspec_t *pspec,
    fdisk_part_t **rpart)
{
	return EOK;
}

int fdisk_part_destroy(fdisk_part_t *part)
{
	return EOK;
}

int fdisk_cap_format(fdisk_cap_t *cap, char **rstr)
{
	int rc;

	rc = asprintf(rstr, "%" PRIu64 " B", cap->value);
	if (rc < 0)
		return ENOMEM;

	return EOK;
}

int fdisk_ltype_format(fdisk_label_type_t ltype, char **rstr)
{
	const char *sltype;
	char *s;

	sltype = NULL;
	switch (ltype) {
	case fdl_none:
		sltype = "None";
		break;
	case fdl_unknown:
		sltype = "Unknown";
		break;
	case fdl_mbr:
		sltype = "MBR";
		break;
	case fdl_gpt:
		sltype = "GPT";
		break;
	}

	s = str_dup(sltype);
	if (s == NULL)
		return ENOMEM;

	*rstr = s;
	return EOK;
}

/** @}
 */
