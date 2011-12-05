/*
 * Copyright (c) 2011 Jakub Jermar
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

/** @addtogroup generic
 * @{
 */

/**
 * @file
 * @brief Resource allocator.
 *
 * This is a generic resource allocator, loosely based on the ideas presented
 * in chapter 4 of the following paper and further simplified:
 *
 *   Bonwick J., Adams J.: Magazines and Vmem: Extending the Slab Allocator to
 *   Many CPUs and Arbitrary Resources, USENIX 2001
 *
 */

#include <lib/ra.h>
#include <typedefs.h>
#include <mm/slab.h>
#include <bitops.h>
#include <debug.h>
#include <panic.h>
#include <adt/list.h>
#include <adt/hash_table.h>
#include <align.h>
#include <macros.h>

static hash_table_operations_t used_ops = {
	.hash = NULL,
	.compare = NULL,
	.remove_callback = NULL,
};

/** Calculate the segment size. */
static size_t ra_segment_size_get(ra_segment_t *seg)
{
	ra_segment_t *nextseg;

	nextseg = list_get_instance(seg->segment_link.next, ra_segment_t,
	    segment_link);
	return nextseg->base - seg->base;
}

static ra_segment_t *ra_segment_create(uintptr_t base)
{
	ra_segment_t *seg;

	seg = (ra_segment_t *) malloc(sizeof(ra_segment_t), FRAME_ATOMIC);
	if (!seg)
		return NULL;

	link_initialize(&seg->segment_link);
	link_initialize(&seg->fu_link);

	seg->base = base;
	seg->flags = 0;

	return seg;
}

static void ra_segment_destroy(ra_segment_t *seg)
{
	free(seg);
}

static ra_span_t *ra_span_create(uintptr_t base, size_t size)
{
	ra_span_t *span;
	ra_segment_t *seg, *lastseg;
	unsigned int i;

	span = (ra_span_t *) malloc(sizeof(ra_span_t), FRAME_ATOMIC);
	if (!span)
		return NULL;

	span->max_order = fnzb(size);
	span->base = base;
	span->size = size;

	span->free = (list_t *) malloc((span->max_order + 1) * sizeof(list_t),
	    FRAME_ATOMIC);
	if (!span->free) {
		free(span);
		return NULL;
	}

	/*
	 * Create a segment to represent the entire size of the span.
	 */
	seg = ra_segment_create(base);
	if (!seg) {
		free(span->free);
		free(span);
		return NULL;
	}
	seg->flags = RA_SEGMENT_FREE;

	/*
	 * The last segment will be used as a sentinel at the end of the
	 * segment list so that it is possible to calculate the size for
	 * all other segments. It will not be placed in any free list or
	 * in the used segment hash and adjacent segments will not be
	 * coalesced with it.
	 */
	lastseg = ra_segment_create(base + size);
	if (!lastseg) {
		ra_segment_destroy(seg);
		free(span->free);
		free(span);
		return NULL;
	}

	link_initialize(&span->span_link);
	list_initialize(&span->segments);

	hash_table_create(&span->used, span->max_order + 1, 1, &used_ops);

	for (i = 0; i < span->max_order; i++)
		list_initialize(&span->free[i]);

	/* Insert the first segment into the list of segments. */
	list_append(&seg->segment_link, &span->segments);
	/* Insert the last segment into the list of segments. */
	list_append(&lastseg->segment_link, &span->segments);

	/* Insert the first segment into the respective free list. */
	list_append(&seg->fu_link, &span->free[span->max_order]);

	return span;
}

/** Create arena with initial span. */
ra_arena_t *ra_arena_create(uintptr_t base, size_t size)
{
	ra_arena_t *arena;
	ra_span_t *span;

	/*
	 * At the moment, we can only create resources that don't include 0.
	 * If 0 needs to be considered as a valid resource, we would need to
	 * slightly change the API of the resource allocator.
	 */
	if (base == 0)
		return NULL;

	arena = (ra_arena_t *) malloc(sizeof(ra_arena_t), FRAME_ATOMIC);
	if (!arena)
		return NULL;

	span = ra_span_create(base, size);
	if (!span) {
		free(arena);
		return NULL;
	}

	list_initialize(&arena->spans);
	list_append(&span->span_link, &arena->spans);

	return arena;
}

/** Add additional span to arena. */
bool ra_span_add(ra_arena_t *arena, uintptr_t base, size_t size)
{
	ra_span_t *span;

	if (base == 0)
		return false;

	span = ra_span_create(base, size);
	if (!span)
		return false;

	/* TODO: check for overlaps */
	list_append(&span->span_link, &arena->spans);
	return true;
}

static uintptr_t ra_span_alloc(ra_span_t *span, size_t size, size_t align)
{
	/*
	 * We need to add the maximum of align - 1 to be able to compensate for
	 * the worst case unaligned segment.
	 */
	size_t needed = size + align - 1;
	size_t order = ispwr2(needed) ? fnzb(needed) : fnzb(needed) + 1;
	ra_segment_t *pred = NULL;
	ra_segment_t *succ = NULL;

	/*
	 * Find the free list of the smallest order which can satisfy this
	 * request.
	 */
	for (; order <= span->max_order; order++) {
		ra_segment_t *seg;
		uintptr_t newbase;

		if (list_empty(&span->free[order]))
			continue;

		/* Take the first segment from the free list. */
		seg = list_get_instance(list_first(&span->free[order]),
		    ra_segment_t, fu_link);

		ASSERT(seg->flags & RA_SEGMENT_FREE);

		/*
		 * See if we need to allocate new segments for the chopped-off
		 * parts of this segment.
		 */
		if (!IS_ALIGNED(seg->base, align)) {
			pred = ra_segment_create(seg->base);
			if (!pred) {
				/*
				 * Fail as we are unable to split the segment.
				 */
				break;
			}
			pred->flags |= RA_SEGMENT_FREE;
		}
		newbase = ALIGN_UP(seg->base, align);
		if (newbase + size != seg->base + ra_segment_size_get(seg)) {
			ASSERT(newbase + size < seg->base +
			    ra_segment_size_get(seg));
			succ = ra_segment_create(newbase + size);
			if (!succ) {
				if (pred)
					ra_segment_destroy(pred);
				/*
				 * Fail as we are unable to split the segment.
				 */
				break;
			}
			succ->flags |= RA_SEGMENT_FREE;
		}
		
	
		/* Put unneeded parts back. */
		if (pred) {
			size_t pred_order;

			list_insert_before(&pred->segment_link,
			    &seg->segment_link);
			pred_order = fnzb(ra_segment_size_get(pred));
			list_append(&pred->fu_link, &span->free[pred_order]);
		}
		if (succ) {
			size_t succ_order;

			list_insert_after(&succ->segment_link,
			    &seg->segment_link);
			succ_order = fnzb(ra_segment_size_get(succ));
			list_append(&succ->fu_link, &span->free[succ_order]);
		}

		/* Now remove the found segment from the free list. */
		list_remove(&seg->fu_link);
		seg->base = newbase;
		seg->flags &= ~RA_SEGMENT_FREE;

		/* Hash-in the segment into the used hash. */
		sysarg_t key = seg->base;
		hash_table_insert(&span->used, &key, &seg->fu_link);

		return newbase;
	}

	return 0;
}

static void ra_span_free(ra_span_t *span, size_t base, size_t size)
{
	sysarg_t key = base;
	link_t *link;
	ra_segment_t *seg;
	ra_segment_t *pred;
	ra_segment_t *succ;
	size_t order;

	/*
	 * Locate the segment in the used hash table.
	 */
	link = hash_table_find(&span->used, &key);
	if (!link) {
		panic("Freeing segment which is not known to be used (base=%"
		    PRIxn ", size=%" PRIdn ").", base, size);
	}
	seg = hash_table_get_instance(link, ra_segment_t, fu_link);

	/*
	 * Hash out the segment.
	 */
	hash_table_remove(&span->used, &key, 1);

	ASSERT(!(seg->flags & RA_SEGMENT_FREE));
	ASSERT(seg->base == base);
	ASSERT(ra_segment_size_get(seg) == size);

	/*
	 * Check whether the segment can be coalesced with its left neighbor.
	 */
	if (list_first(&span->segments) != &seg->segment_link) {
		pred = hash_table_get_instance(seg->segment_link.prev,
		    ra_segment_t, segment_link);

		ASSERT(pred->base < seg->base);

		if (pred->flags & RA_SEGMENT_FREE) {
			/*
			 * The segment can be coalesced with its predecessor.
			 * Remove the predecessor from the free and segment
			 * lists, rebase the segment and throw the predecessor
			 * away.
			 */
			list_remove(&pred->fu_link);
			list_remove(&pred->segment_link);
			seg->base = pred->base;
			ra_segment_destroy(pred);
		}
	}

	/*
	 * Check whether the segment can be coalesced with its right neighbor.
	 */
	succ = hash_table_get_instance(seg->segment_link.next, ra_segment_t,
	    segment_link);
	ASSERT(succ->base > seg->base);
	if (succ->flags & RA_SEGMENT_FREE) {
		/*
		 * The segment can be coalesced with its successor.
		 * Remove the successor from the free and segment lists
		 * and throw it away.
		 */
		list_remove(&succ->fu_link);
		list_remove(&succ->segment_link);
		ra_segment_destroy(succ);
	}

	/* Put the segment on the appropriate free list. */
	seg->flags |= RA_SEGMENT_FREE;
	order = fnzb(ra_segment_size_get(seg));
	list_append(&seg->fu_link, &span->free[order]);
}

/** Allocate resources from arena. */
uintptr_t ra_alloc(ra_arena_t *arena, size_t size, size_t alignment)
{
	uintptr_t base = 0;

	ASSERT(size >= 1);
	ASSERT(alignment >= 1);
	ASSERT(ispwr2(alignment));

	list_foreach(arena->spans, cur) {
		ra_span_t *span = list_get_instance(cur, ra_span_t, span_link);

		base = ra_span_alloc(span, size, alignment);
		if (base)
			break;
	}

	return base;
}

/* Return resources to arena. */
void ra_free(ra_arena_t *arena, uintptr_t base, size_t size)
{
	list_foreach(arena->spans, cur) {
		ra_span_t *span = list_get_instance(cur, ra_span_t, span_link);

		if (iswithin(span->base, span->size, base, size)) {
			ra_span_free(span, base, size);
			return;
		}
	}

	panic("Freeing to wrong arena (base=%" PRIxn ", size=%" PRIdn ").",
	    base, size);
}


/** @}
 */
