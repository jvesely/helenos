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
#include <mem.h>
#include <stdio.h>
#include <stdlib.h>
#include <str.h>
#include <vbd.h>
#include <vol.h>

static const char *cu_str[] = {
	[cu_byte] = "B",
	[cu_kbyte] = "kB",
	[cu_mbyte] = "MB",
	[cu_gbyte] = "GB",
	[cu_tbyte] = "TB",
	[cu_pbyte] = "PB",
	[cu_ebyte] = "EB",
	[cu_zbyte] = "ZB",
	[cu_ybyte] = "YB"
};

static int fdisk_part_spec_prepare(fdisk_dev_t *, fdisk_part_spec_t *,
    vbd_part_spec_t *);

static void fdisk_dev_info_delete(fdisk_dev_info_t *info)
{
	if (info == NULL)
		return;

	if (info->blk_inited)
		block_fini(info->svcid);

	free(info->svcname);
	free(info);
}

int fdisk_create(fdisk_t **rfdisk)
{
	fdisk_t *fdisk = NULL;
	int rc;

	fdisk = calloc(1, sizeof(fdisk_t));
	if (fdisk == NULL) {
		rc = ENOMEM;
		goto error;
	}

	rc = vol_create(&fdisk->vol);
	if (rc != EOK) {
		rc = EIO;
		goto error;
	}

	rc = vbd_create(&fdisk->vbd);
	if (rc != EOK) {
		rc = EIO;
		goto error;
	}

	*rfdisk = fdisk;
	return EOK;
error:
	fdisk_destroy(fdisk);

	return rc;
}

void fdisk_destroy(fdisk_t *fdisk)
{
	if (fdisk == NULL)
		return;

	vol_destroy(fdisk->vol);
	vbd_destroy(fdisk->vbd);
	free(fdisk);
}

int fdisk_dev_list_get(fdisk_t *fdisk, fdisk_dev_list_t **rdevlist)
{
	fdisk_dev_list_t *devlist = NULL;
	fdisk_dev_info_t *info;
	service_id_t *svcs = NULL;
	size_t count, i;
	int rc;

	devlist = calloc(1, sizeof(fdisk_dev_list_t));
	if (devlist == NULL)
		return ENOMEM;

	list_initialize(&devlist->devinfos);

	rc = vol_get_disks(fdisk->vol, &svcs, &count);
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

void fdisk_dev_info_get_svcid(fdisk_dev_info_t *info, service_id_t *rsid)
{
	*rsid = info->svcid;
}

int fdisk_dev_info_get_svcname(fdisk_dev_info_t *info, char **rname)
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

int fdisk_dev_info_capacity(fdisk_dev_info_t *info, fdisk_cap_t *cap)
{
	size_t bsize;
	aoff64_t nblocks;
	int rc;

	if (!info->blk_inited) {
		rc = block_init(EXCHANGE_SERIALIZE, info->svcid, 2048);
		if (rc != EOK)
			return rc;

		info->blk_inited = true;
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

static int fdisk_part_add(fdisk_dev_t *dev, vbd_part_id_t partid,
    fdisk_part_t **rpart)
{
	fdisk_part_t *part, *p;
	vbd_part_info_t pinfo;
	link_t *link;
	int rc;

	part = calloc(1, sizeof(fdisk_part_t));
	if (part == NULL)
		return ENOMEM;

	rc = vbd_part_get_info(dev->fdisk->vbd, partid, &pinfo);
	if (rc != EOK) {
		rc = EIO;
		goto error;
	}

	part->dev = dev;
	part->index = pinfo.index;
	part->block0 = pinfo.block0;
	part->nblocks = pinfo.nblocks;

	/* Insert to list by block address */
	link = list_first(&dev->parts_ba);
	while (link != NULL) {
		p = list_get_instance(link, fdisk_part_t, ldev_ba);
		if (p->block0 > part->block0) {
			list_insert_before(&part->ldev_ba, &p->ldev_ba);
			break;
		}

		link = list_next(link, &dev->parts_ba);
	}

	if (link == NULL)
		list_append(&part->ldev_ba, &dev->parts_ba);

	/* Insert to list by index */
	link = list_first(&dev->parts_idx);
	while (link != NULL) {
		p = list_get_instance(link, fdisk_part_t, ldev_idx);
		if (p->index > part->index) {
			list_insert_before(&part->ldev_idx, &p->ldev_idx);
			break;
		}

		link = list_next(link, &dev->parts_idx);
	}

	if (link == NULL)
		list_append(&part->ldev_idx, &dev->parts_idx);

	part->capacity.cunit = cu_byte;
	part->capacity.value = part->nblocks * dev->dinfo.block_size;
	part->part_id = partid;

	if (rpart != NULL)
		*rpart = part;
	return EOK;
error:
	free(part);
	return rc;
}


int fdisk_dev_open(fdisk_t *fdisk, service_id_t sid, fdisk_dev_t **rdev)
{
	fdisk_dev_t *dev = NULL;
	service_id_t *psids = NULL;
	size_t nparts, i;
	int rc;

	dev = calloc(1, sizeof(fdisk_dev_t));
	if (dev == NULL)
		return ENOMEM;

	dev->fdisk = fdisk;
	dev->sid = sid;
	list_initialize(&dev->parts_idx);
	list_initialize(&dev->parts_ba);

	printf("get info\n");
	rc = vbd_disk_info(fdisk->vbd, sid, &dev->dinfo);
	if (rc != EOK) {
		printf("failed\n");
		rc = EIO;
		goto error;
	}

	printf("block size: %zu\n", dev->dinfo.block_size);
	printf("get partitions\n");
	rc = vbd_label_get_parts(fdisk->vbd, sid, &psids, &nparts);
	if (rc != EOK) {
		printf("failed\n");
		rc = EIO;
		goto error;
	}
	printf("OK\n");

	printf("found %zu partitions.\n", nparts);
	for (i = 0; i < nparts; i++) {
		printf("add partition sid=%zu\n", psids[i]);
		rc = fdisk_part_add(dev, psids[i], NULL);
		if (rc != EOK) {
			printf("failed\n");
			goto error;
		}
		printf("OK\n");
	}

	free(psids);
	*rdev = dev;
	return EOK;
error:
	fdisk_dev_close(dev);
	return rc;
}

void fdisk_dev_close(fdisk_dev_t *dev)
{
	if (dev == NULL)
		return;

	/* XXX Clean up partitions */
	free(dev);
}

int fdisk_dev_get_svcname(fdisk_dev_t *dev, char **rname)
{
	char *name;
	int rc;

	rc = loc_service_get_name(dev->sid, &name);
	if (rc != EOK)
		return rc;

	*rname = name;
	return EOK;
}

int fdisk_dev_capacity(fdisk_dev_t *dev, fdisk_cap_t *cap)
{
	size_t bsize;
	aoff64_t nblocks;
	int rc;

	rc = block_init(EXCHANGE_SERIALIZE, dev->sid, 2048);
	if (rc != EOK)
		return rc;

	rc = block_get_bsize(dev->sid, &bsize);
	if (rc != EOK)
		return EIO;

	rc = block_get_nblocks(dev->sid, &nblocks);
	if (rc != EOK)
		return EIO;

	block_fini(dev->sid);

	cap->value = bsize * nblocks;
	cap->cunit = cu_byte;

	return EOK;
}

int fdisk_label_get_info(fdisk_dev_t *dev, fdisk_label_info_t *info)
{
	vol_disk_info_t vinfo;
	int rc;

	rc = vol_disk_info(dev->fdisk->vol, dev->sid, &vinfo);
	if (rc != EOK) {
		rc = EIO;
		goto error;
	}

	info->dcnt = vinfo.dcnt;
	info->ltype = vinfo.ltype;
	return EOK;
error:
	return rc;
}

int fdisk_label_create(fdisk_dev_t *dev, label_type_t ltype)
{
	return vol_label_create(dev->fdisk->vol, dev->sid, ltype);
}

int fdisk_label_destroy(fdisk_dev_t *dev)
{
	fdisk_part_t *part;
	int rc;

	part = fdisk_part_first(dev);
	while (part != NULL) {
		(void) fdisk_part_destroy(part); /* XXX */
		part = fdisk_part_first(dev);
	}

	rc = vol_disk_empty(dev->fdisk->vol, dev->sid);
	if (rc != EOK)
		return EIO;

	dev->dcnt = dc_empty;
	return EOK;
}

fdisk_part_t *fdisk_part_first(fdisk_dev_t *dev)
{
	link_t *link;

	link = list_first(&dev->parts_ba);
	if (link == NULL)
		return NULL;

	return list_get_instance(link, fdisk_part_t, ldev_ba);
}

fdisk_part_t *fdisk_part_next(fdisk_part_t *part)
{
	link_t *link;

	link = list_next(&part->ldev_ba, &part->dev->parts_ba);
	if (link == NULL)
		return NULL;

	return list_get_instance(link, fdisk_part_t, ldev_ba);
}

int fdisk_part_get_info(fdisk_part_t *part, fdisk_part_info_t *info)
{
	info->capacity = part->capacity;
	info->fstype = part->fstype;
	return EOK;
}

int fdisk_part_get_max_avail(fdisk_dev_t *dev, fdisk_cap_t *cap)
{
	return EOK;
}

int fdisk_part_create(fdisk_dev_t *dev, fdisk_part_spec_t *pspec,
    fdisk_part_t **rpart)
{
	fdisk_part_t *part;
	vbd_part_spec_t vpspec;
	vbd_part_id_t partid;
	int rc;

	printf("fdisk_part_create()\n");

	rc = fdisk_part_spec_prepare(dev, pspec, &vpspec);
	if (rc != EOK)
		return EIO;

	printf("fdisk_part_create() - call vbd_part_create\n");
	rc = vbd_part_create(dev->fdisk->vbd, dev->sid, &vpspec, &partid);
	if (rc != EOK)
		return EIO;

	printf("fdisk_part_create() - call fdisk_part_add\n");
	rc = fdisk_part_add(dev, partid, &part);
	if (rc != EOK) {
		/* Try rolling back */
		(void) vbd_part_delete(dev->fdisk->vbd, partid);
		return EIO;
	}

	printf("fdisk_part_create() - done\n");
	part->fstype = pspec->fstype;
	part->capacity = pspec->capacity;

	if (rpart != NULL)
		*rpart = part;
	return EOK;
}

int fdisk_part_destroy(fdisk_part_t *part)
{
	int rc;

	rc = vbd_part_delete(part->dev->fdisk->vbd, part->part_id);
	if (rc != EOK)
		return EIO;

	list_remove(&part->ldev_ba);
	list_remove(&part->ldev_idx);
	free(part);
	return EOK;
}

void fdisk_pspec_init(fdisk_part_spec_t *pspec)
{
	memset(pspec, 0, sizeof(fdisk_part_spec_t));
}

int fdisk_cap_format(fdisk_cap_t *cap, char **rstr)
{
	int rc;
	const char *sunit;

	sunit = NULL;

	if (cap->cunit < 0 || cap->cunit >= CU_LIMIT)
		assert(false);

	sunit = cu_str[cap->cunit];
	rc = asprintf(rstr, "%" PRIu64 " %s", cap->value, sunit);
	if (rc < 0)
		return ENOMEM;

	return EOK;
}

int fdisk_cap_parse(const char *str, fdisk_cap_t *cap)
{
	char *eptr;
	char *p;
	unsigned long val;
	int i;

	val = strtoul(str, &eptr, 10);

	while (*eptr == ' ')
		++eptr;

	if (*eptr == '\0') {
		cap->cunit = cu_byte;
	} else {
		for (i = 0; i < CU_LIMIT; i++) {
			if (str_lcasecmp(eptr, cu_str[i],
			    str_length(cu_str[i])) == 0) {
				p = eptr + str_size(cu_str[i]);
				while (*p == ' ')
					++p;
				if (*p == '\0')
					goto found;
			}
		}

		return EINVAL;
found:
		cap->cunit = i;
	}

	cap->value = val;
	return EOK;
}

int fdisk_ltype_format(label_type_t ltype, char **rstr)
{
	const char *sltype;
	char *s;

	sltype = NULL;
	switch (ltype) {
	case lt_mbr:
		sltype = "MBR";
		break;
	case lt_gpt:
		sltype = "GPT";
		break;
	}

	s = str_dup(sltype);
	if (s == NULL)
		return ENOMEM;

	*rstr = s;
	return EOK;
}

int fdisk_fstype_format(fdisk_fstype_t fstype, char **rstr)
{
	const char *sfstype;
	char *s;

	sfstype = NULL;
	switch (fstype) {
	case fdfs_none:
		sfstype = "None";
		break;
	case fdfs_unknown:
		sfstype = "Unknown";
		break;
	case fdfs_exfat:
		sfstype = "ExFAT";
		break;
	case fdfs_fat:
		sfstype = "FAT";
		break;
	case fdfs_minix:
		sfstype = "MINIX";
		break;
	case fdfs_ext4:
		sfstype = "Ext4";
		break;
	}

	s = str_dup(sfstype);
	if (s == NULL)
		return ENOMEM;

	*rstr = s;
	return EOK;
}

/** Get free partition index. */
static int fdisk_part_get_free_idx(fdisk_dev_t *dev, int *rindex)
{
	link_t *link;
	fdisk_part_t *part;
	int nidx;

	link = list_first(&dev->parts_idx);
	nidx = 1;
	while (link != NULL) {
		part = list_get_instance(link, fdisk_part_t, ldev_idx);
		if (part->index > nidx)
			break;
		nidx = part->index + 1;
		link = list_next(link, &dev->parts_idx);
	}

	if (nidx > 4 /* XXXX actual number of slots*/) {
		return ELIMIT;
	}

	*rindex = nidx;
	return EOK;
}

/** Get free range of blocks.
 *
 * Get free range of blocks of at least the specified size (first fit).
 */
static int fdisk_part_get_free_range(fdisk_dev_t *dev, aoff64_t nblocks,
    aoff64_t *rblock0, aoff64_t *rnblocks)
{
	link_t *link;
	fdisk_part_t *part;
	uint64_t avail;
	int nba;

	link = list_first(&dev->parts_ba);
	nba = dev->dinfo.ablock0;
	while (link != NULL) {
		part = list_get_instance(link, fdisk_part_t, ldev_ba);
		if (part->block0 - nba >= nblocks)
			break;
		nba = part->block0 + part->nblocks;
		link = list_next(link, &dev->parts_ba);
	}

	if (link != NULL) {
		/* Free range before a partition */
		avail = part->block0 - nba;
	} else {
		/* Free range at the end */
		avail = dev->dinfo.ablock0 + dev->dinfo.anblocks - nba;

		/* Verify that the range is large enough */
		if (avail < nblocks)
			return ELIMIT;
	}

	*rblock0 = nba;
	*rnblocks = avail;
	return EOK;
}

/** Prepare new partition specification for VBD. */
static int fdisk_part_spec_prepare(fdisk_dev_t *dev, fdisk_part_spec_t *pspec,
    vbd_part_spec_t *vpspec)
{
	uint64_t cbytes;
	aoff64_t req_blocks;
	aoff64_t fblock0;
	aoff64_t fnblocks;
	uint64_t block_size;
	unsigned i;
	int index;
	int rc;

//	pspec->fstype
	printf("fdisk_part_spec_prepare()\n");
	block_size = dev->dinfo.block_size;
	cbytes = pspec->capacity.value;
	for (i = 0; i < pspec->capacity.cunit; i++)
		cbytes = cbytes * 1000;

	req_blocks = (cbytes + block_size - 1) / block_size;

	rc = fdisk_part_get_free_idx(dev, &index);
	if (rc != EOK)
		return EIO;

	rc = fdisk_part_get_free_range(dev, req_blocks, &fblock0, &fnblocks);
	if (rc != EOK)
		return EIO;

	vpspec->index = index;
	vpspec->block0 = fblock0;
	vpspec->nblocks = req_blocks;
	vpspec->ptype = 42;
	return EOK;
}

/** @}
 */
