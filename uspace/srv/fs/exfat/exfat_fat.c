/*
 * Copyright (c) 2008 Jakub Jermar
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
 * @file	exfat_fat.c
 * @brief	Functions that manipulate the File Allocation Tables.
 */

#include "exfat_fat.h"
#include "exfat.h"
#include "../../vfs/vfs.h"
#include <libfs.h>
#include <libblock.h>
#include <errno.h>
#include <byteorder.h>
#include <align.h>
#include <assert.h>
#include <fibril_synch.h>
#include <malloc.h>
#include <mem.h>


/**
 * The fat_alloc_lock mutex protects all copies of the File Allocation Table
 * during allocation of clusters. The lock does not have to be held durring
 * deallocation of clusters.
 */
static FIBRIL_MUTEX_INITIALIZE(exfat_alloc_lock);

/** Walk the cluster chain.
 *
 * @param bs		Buffer holding the boot sector for the file.
 * @param devmap_handle	Device handle of the device with the file.
 * @param firstc	First cluster to start the walk with.
 * @param lastc		If non-NULL, output argument hodling the last cluster
 *			number visited.
 * @param numc		If non-NULL, output argument holding the number of
 *			clusters seen during the walk.
 * @param max_clusters	Maximum number of clusters to visit.
 *
 * @return		EOK on success or a negative error code.
 */
int
exfat_cluster_walk(exfat_bs_t *bs, devmap_handle_t devmap_handle, 
    exfat_cluster_t firstc, exfat_cluster_t *lastc, uint32_t *numc,
    uint32_t max_clusters)
{
	uint32_t clusters = 0;
	exfat_cluster_t clst = firstc;
	int rc;

	if (firstc < EXFAT_CLST_FIRST) {
		/* No space allocated to the file. */
		if (lastc)
			*lastc = firstc;
		if (numc)
			*numc = 0;
		return EOK;
	}

	while (clst != EXFAT_CLST_EOF && clusters < max_clusters) {
		assert(clst >= EXFAT_CLST_FIRST);
		if (lastc)
			*lastc = clst;	/* remember the last cluster number */

		rc = exfat_get_cluster(bs, devmap_handle, clst, &clst);
		if (rc != EOK)
			return rc;

		assert(clst != EXFAT_CLST_BAD);
		clusters++;
	}

	if (lastc && clst != EXFAT_CLST_EOF)
		*lastc = clst;
	if (numc)
		*numc = clusters;

	return EOK;
}

/** Read block from file located on a exFAT file system.
 *
 * @param block		Pointer to a block pointer for storing result.
 * @param bs		Buffer holding the boot sector of the file system.
 * @param nodep		FAT node.
 * @param bn		Block number.
 * @param flags		Flags passed to libblock.
 *
 * @return		EOK on success or a negative error code.
 */
int
exfat_block_get(block_t **block, exfat_bs_t *bs, exfat_node_t *nodep,
    aoff64_t bn, int flags)
{
	exfat_cluster_t firstc = nodep->firstc;
	exfat_cluster_t currc;
	aoff64_t relbn = bn;
	int rc;

	if (!nodep->size)
		return ELIMIT;

	if (nodep->fragmented) {
		if (((((nodep->size - 1) / BPS(bs)) / SPC(bs)) == bn / SPC(bs)) &&
			nodep->lastc_cached_valid) {
				/*
			* This is a request to read a block within the last cluster
			* when fortunately we have the last cluster number cached.
			*/
			return block_get(block, nodep->idx->devmap_handle, DATA_FS(bs) + 
		        (nodep->lastc_cached_value-EXFAT_CLST_FIRST)*SPC(bs) + 
			    (bn % SPC(bs)), flags);
		}

		if (nodep->currc_cached_valid && bn >= nodep->currc_cached_bn) {
			/*
			* We can start with the cluster cached by the previous call to
			* fat_block_get().
			*/
			firstc = nodep->currc_cached_value;
			relbn -= (nodep->currc_cached_bn / SPC(bs)) * SPC(bs);
		}
	}

	rc = exfat_block_get_by_clst(block, bs, nodep->idx->devmap_handle,
	    nodep->fragmented, firstc, &currc, relbn, flags);
	if (rc != EOK)
		return rc;

	/*
	 * Update the "current" cluster cache.
	 */
	nodep->currc_cached_valid = true;
	nodep->currc_cached_bn = bn;
	nodep->currc_cached_value = currc;

	return rc;
}

/** Read block from file located on a FAT file system.
 *
 * @param block		Pointer to a block pointer for storing result.
 * @param bs		Buffer holding the boot sector of the file system.
 * @param devmap_handle	Device handle of the file system.
 * @param fcl		First cluster used by the file. Can be zero if the file
 *			is empty.
 * @param clp		If not NULL, address where the cluster containing bn
 *			will be stored.
 *			stored
 * @param bn		Block number.
 * @param flags		Flags passed to libblock.
 *
 * @return		EOK on success or a negative error code.
 */
int
exfat_block_get_by_clst(block_t **block, exfat_bs_t *bs, 
    devmap_handle_t devmap_handle, bool fragmented, exfat_cluster_t fcl,
    exfat_cluster_t *clp, aoff64_t bn, int flags)
{
	uint32_t clusters;
	uint32_t max_clusters;
	exfat_cluster_t c;
	int rc;

	if (fcl < EXFAT_CLST_FIRST)
		return ELIMIT;

	if (!fragmented) {
		rc = block_get(block, devmap_handle, DATA_FS(bs) + 
		    (fcl-EXFAT_CLST_FIRST)*SPC(bs) + bn, flags);
	} else {
		max_clusters = bn / SPC(bs);
		rc = exfat_cluster_walk(bs, devmap_handle, fcl, &c, &clusters, max_clusters);
		if (rc != EOK)
			return rc;
		assert(clusters == max_clusters);

		rc = block_get(block, devmap_handle, DATA_FS(bs) + 
		    (c-EXFAT_CLST_FIRST)*SPC(bs) + (bn % SPC(bs)), flags);

		if (clp)
			*clp = c;
	}

	return rc;
}


/** Get cluster from the FAT.
 *
 * @param bs		Buffer holding the boot sector for the file system.
 * @param devmap_handle	Device handle for the file system.
 * @param clst		Cluster which to get.
 * @param value		Output argument holding the value of the cluster.
 *
 * @return		EOK or a negative error code.
 */
int
exfat_get_cluster(exfat_bs_t *bs, devmap_handle_t devmap_handle,
    exfat_cluster_t clst, exfat_cluster_t *value)
{
	block_t *b;
	aoff64_t offset;
	int rc;

	offset = clst * sizeof(exfat_cluster_t);

	rc = block_get(&b, devmap_handle, FAT_FS(bs) + offset / BPS(bs), BLOCK_FLAGS_NONE);
	if (rc != EOK)
		return rc;

	*value = uint32_t_le2host(*(uint32_t *)(b->data + offset % BPS(bs)));

	rc = block_put(b);

	return rc;
}

/** Set cluster in FAT.
 *
 * @param bs		Buffer holding the boot sector for the file system.
 * @param devmap_handle	Device handle for the file system.
 * @param clst		Cluster which is to be set.
 * @param value		Value to set the cluster with.
 *
 * @return		EOK on success or a negative error code.
 */
int
exfat_set_cluster(exfat_bs_t *bs, devmap_handle_t devmap_handle,
    exfat_cluster_t clst, exfat_cluster_t value)
{
	block_t *b;
	aoff64_t offset;
	int rc;

	offset = clst * sizeof(exfat_cluster_t);

	rc = block_get(&b, devmap_handle, FAT_FS(bs) + offset / BPS(bs), BLOCK_FLAGS_NONE);
	if (rc != EOK)
		return rc;

	*(uint32_t *)(b->data + offset % BPS(bs)) = host2uint32_t_le(value);

	b->dirty = true;	/* need to sync block */
	rc = block_put(b);
	return rc;
}

/** Allocate clusters in FAT.
 *
 * This function will attempt to allocate the requested number of clusters in
 * the FAT.  The FAT will be altered so that the allocated
 * clusters form an independent chain (i.e. a chain which does not belong to any
 * file yet).
 *
 * @param bs		Buffer holding the boot sector of the file system.
 * @param devmap_handle	Device handle of the file system.
 * @param nclsts	Number of clusters to allocate.
 * @param mcl		Output parameter where the first cluster in the chain
 *			will be returned.
 * @param lcl		Output parameter where the last cluster in the chain
 *			will be returned.
 *
 * @return		EOK on success, a negative error code otherwise.
 */
int
exfat_alloc_clusters(exfat_bs_t *bs, devmap_handle_t devmap_handle, unsigned nclsts,
    exfat_cluster_t *mcl, exfat_cluster_t *lcl)
{
	exfat_cluster_t *lifo;    /* stack for storing free cluster numbers */
	unsigned found = 0;     /* top of the free cluster number stack */
	exfat_cluster_t clst, value;
	int rc = EOK;

	lifo = (exfat_cluster_t *) malloc(nclsts * sizeof(exfat_cluster_t));
	if (!lifo)
		return ENOMEM;

	fibril_mutex_lock(&exfat_alloc_lock);
	for (clst=EXFAT_CLST_FIRST; clst < DATA_CNT(bs)+2 && found < nclsts; clst++) {
		rc = exfat_get_cluster(bs, devmap_handle, clst, &value);
		if (rc != EOK)
			break;

		if (value == 0) {
		   /*
			* The cluster is free. Put it into our stack
			* of found clusters and mark it as non-free.
			*/
			lifo[found] = clst;
			rc = exfat_set_cluster(bs, devmap_handle, clst,
				(found == 0) ?  EXFAT_CLST_EOF : lifo[found - 1]);
			if (rc != EOK)
				break;

			found++;
		}
	}

	if (rc == EOK && found == nclsts) {
		*mcl = lifo[found - 1];
		*lcl = lifo[0];
		free(lifo);
		fibril_mutex_unlock(&exfat_alloc_lock);
		return EOK;
	}

	/* If something wrong - free the clusters */
	if (found > 0) {
		while (found--)
			rc = exfat_set_cluster(bs, devmap_handle, lifo[found], 0);
	}

	free(lifo);
	fibril_mutex_unlock(&exfat_alloc_lock);
	return ENOSPC;
}

/** Free clusters forming a cluster chain in FAT.
 *
 * @param bs		Buffer hodling the boot sector of the file system.
 * @param devmap_handle	Device handle of the file system.
 * @param firstc	First cluster in the chain which is to be freed.
 *
 * @return		EOK on success or a negative return code.
 */
int
exfat_free_clusters(exfat_bs_t *bs, devmap_handle_t devmap_handle, exfat_cluster_t firstc)
{
	exfat_cluster_t nextc;
	int rc;

	/* Mark all clusters in the chain as free */
	while (firstc != EXFAT_CLST_EOF) {
		assert(firstc >= EXFAT_CLST_FIRST && firstc < EXFAT_CLST_BAD);
		rc = exfat_get_cluster(bs, devmap_handle, firstc, &nextc);
		if (rc != EOK)
			return rc;
		rc = exfat_set_cluster(bs, devmap_handle, firstc, 0);
		if (rc != EOK)
			return rc;
		firstc = nextc;
	}

	return EOK;
}

/** Append a cluster chain to the last file cluster in FAT.
 *
 * @param bs		Buffer holding the boot sector of the file system.
 * @param nodep		Node representing the file.
 * @param mcl		First cluster of the cluster chain to append.
 * @param lcl		Last cluster of the cluster chain to append.
 *
 * @return		EOK on success or a negative error code.
 */
int
exfat_append_clusters(exfat_bs_t *bs, exfat_node_t *nodep, exfat_cluster_t mcl,
    exfat_cluster_t lcl)
{
	devmap_handle_t devmap_handle = nodep->idx->devmap_handle;
	exfat_cluster_t lastc;
	int rc;

	if (nodep->firstc == 0) {
		/* No clusters allocated to the node yet. */
		nodep->firstc = mcl;
		nodep->dirty = true;	/* need to sync node */
	} else {
		if (nodep->lastc_cached_valid) {
			lastc = nodep->lastc_cached_value;
			nodep->lastc_cached_valid = false;
		} else {
			rc = exfat_cluster_walk(bs, devmap_handle, nodep->firstc,
			    &lastc, NULL, (uint16_t) -1);
			if (rc != EOK)
				return rc;
		}

		rc = exfat_set_cluster(bs, nodep->idx->devmap_handle, lastc, mcl);
		if (rc != EOK)
			return rc;
	}

	nodep->lastc_cached_valid = true;
	nodep->lastc_cached_value = lcl;

	return EOK;
}

/** Chop off node clusters in FAT.
 *
 * @param bs		Buffer holding the boot sector of the file system.
 * @param nodep		FAT node where the chopping will take place.
 * @param lcl		Last cluster which will remain in the node. If this
 *			argument is FAT_CLST_RES0, then all clusters will
 *			be chopped off.
 *
 * @return		EOK on success or a negative return code.
 */
int exfat_chop_clusters(exfat_bs_t *bs, exfat_node_t *nodep, exfat_cluster_t lcl)
{
	int rc;
	devmap_handle_t devmap_handle = nodep->idx->devmap_handle;

	/*
	 * Invalidate cached cluster numbers.
	 */
	nodep->lastc_cached_valid = false;
	if (nodep->currc_cached_value != lcl)
		nodep->currc_cached_valid = false;

	if (lcl == 0) {
		/* The node will have zero size and no clusters allocated. */
		rc = exfat_free_clusters(bs, devmap_handle, nodep->firstc);
		if (rc != EOK)
			return rc;
		nodep->firstc = 0;
		nodep->dirty = true;		/* need to sync node */
	} else {
		exfat_cluster_t nextc;

		rc = exfat_get_cluster(bs, devmap_handle, lcl, &nextc);
		if (rc != EOK)
			return rc;

		/* Terminate the cluster chain */
		rc = exfat_set_cluster(bs, devmap_handle, lcl, EXFAT_CLST_EOF);
		if (rc != EOK)
			return rc;

		/* Free all following clusters. */
		rc = exfat_free_clusters(bs, devmap_handle, nextc);
		if (rc != EOK)
			return rc;
	}

	/*
	 * Update and re-enable the last cluster cache.
	 */
	nodep->lastc_cached_valid = true;
	nodep->lastc_cached_value = lcl;

	return EOK;
}

int bitmap_alloc_clusters(exfat_bs_t *bs, devmap_handle_t devmap_handle, 
    exfat_cluster_t *firstc, exfat_cluster_t count)
{
	/* TODO */
	return EOK;
}


int bitmap_append_clusters(exfat_bs_t *bs, exfat_node_t *nodep, 
    exfat_cluster_t count)
{
	/* TODO */
	return EOK;
}


int bitmap_free_clusters(exfat_bs_t *bs, exfat_node_t *nodep, 
    exfat_cluster_t count)
{
	/* TODO */
	return EOK;
}


int bitmap_replicate_clusters(exfat_bs_t *bs, exfat_node_t *nodep)
{
	/* TODO */
	return EOK;
}



/** Perform basic sanity checks on the file system.
 *
 * Verify if values of boot sector fields are sane. Also verify media
 * descriptor. This is used to rule out cases when a device obviously
 * does not contain a exfat file system.
 */
int exfat_sanity_check(exfat_bs_t *bs, devmap_handle_t devmap_handle)
{
	return EOK;
}

/**
 * @}
 */
