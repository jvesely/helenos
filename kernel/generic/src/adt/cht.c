/*
 * Copyright (c) 2012 Adam Hraska
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

/** @addtogroup genericadt
 * @{
 */

/**
 * @file
 * @brief Concurrent resizable lock-free hash table.
 *
 */

#include <adt/cht.h>
#include <debug.h>
#include <memstr.h>
#include <mm/slab.h>
#include <barrier.h>
#include <compiler/barrier.h>
#include <atomic.h>
#include <synch/rcu.h>

/* Must be a power of 2. */
#define CHT_MIN_BUCKET_CNT 128 
/* Must be a power of 2. */
#define CHT_MAX_LOAD 2 

typedef cht_ptr_t marked_ptr_t;
typedef bool (*equal_pred_t)(void *arg, const cht_link_t *item);


typedef enum mark {
	N_NORMAL = 0,
	N_DELETED = 1,
	N_INVALID = 1,
	N_CONST = 3,
	N_JOIN = 2,
	N_JOIN_FOLLOWS = 2,
	N_MARK_MASK = 3
} mark_t;

typedef enum walk_mode {
	WM_NORMAL = 4,
	WM_LEAVE_JOIN,
	WM_MOVE_JOIN_FOLLOWS
} walk_mode_t;

typedef struct wnd {
	marked_ptr_t *ppred;
	cht_link_t *cur;
	cht_link_t *last;
} wnd_t;


static size_t size_to_order(size_t bucket_cnt);
static cht_buckets_t *alloc_buckets(size_t order);

static marked_ptr_t make_link(cht_link_t *next, mark_t mark);
static cht_link_t * get_next(marked_ptr_t link);
static mark_t get_mark(marked_ptr_t link);

static size_t key_hash(cht_t *h, void *key);
static size_t node_hash(cht_t *h, const cht_link_t *item);

static size_t calc_bucket_idx(size_t hash, size_t order);
static size_t grow_idx(size_t idx);
static size_t shrink_idx(size_t idx);



bool cht_create(cht_t *h, size_t init_size, cht_ops_t *op)
{
	ASSERT(h);
	ASSERT(op && op->hash && op->key_hash && op->equal && op->key_equal);

	if (!op || !op->hash || !op->key_hash || !op->equal || !op->key_equal)
		return false;
	
	size_t order = size_to_order(init_size);
	
	h->b = alloc_buckets(order);
	
	if (!h->b)
		return false;
	
	h->new_b = 0;
	h->op = op;
	atomic_set(&h->item_cnt, 0);
	atomic_set(&h->resize_reqs, 0);
	/* Ensure the initialization takes place before we start using the table. */
	write_barrier();
	
	return true;
}

void cht_destroy(cht_t *h)
{
	/* todo: impl */
	1 = 1;
	rcu_synchronize();
}

cht_link_t *cht_find(cht_t *h, void *key)
{
	read_barrier();
	return cht_find_lazy(h, key);
}


cht_link_t *cht_find_lazy(cht_t *h, void *key)
{
	ASSERT(h);
	ASSERT(rcu_read_locked());
	
	size_t hash = key_hash(h, key);
	
	cht_buckets_t *b = rcu_access(h->b);
	size_t idx = calc_bucket_idx(hash, b->order);
	/* todo: access once? */
	marked_ptr_t head = b->head[idx];
	
	if (N_INVALID == get_mark(head))
		return find_resizing(h, key, hash, head, idx);
	
	return search_bucket(h, head, key, hash);
}


static cht_link_t *search_bucket(cht_t *h, marked_ptr_t head, void *key, 
	size_t search_hash)
{
	cht_link_t *cur = get_next(head);
	
	while (cur) {
		/* 
		 * It is safe to access nodes even outside of this bucket (eg when
		 * splitting the bucket). The resizer makes sure that any node we 
		 * may find by following the next pointers is allocated.
		 */
		size_t cur_hash = node_hash(cur);

		if (cur_hash >= search_hash) {
			if (cur_hash != search_hash)
				return 0;

			int present = !(N_DELETED & get_mark(cur->link));
			if (present && h->op->key_equal(key, cur))
				return cur;
		}
		
		cur = get_next(cur->link);
	}
	
	return 0;
}

static cht_link_t *find_resizing(cht_t *h, void *key, size_t hash, 
	marked_ptr_t old_head, size_t old_idx)
{
	ASSERT(N_INVALID == get_mark(old_head)); 
	ASSERT(h->new_b);
	
	size_t new_idx = calc_bucket_idx(hash, h->new_b->order);
	marked_ptr_t new_head = h->new_b->head[new_idx];
	marked_ptr_t search_head = new_head;
	
	/* Growing. */
	if (h->b->order < h->new_b->order) {
		/* 
		 * Old bucket head is invalid, so it must have been already
		 * moved. Make the new head visible if still not visible, ie
		 * invalid.
		 */
		if (N_INVALID == get_mark(new_head)) {
			/* 
			 * We should be searching a newly added bucket but the old
			 * moved bucket has not yet been split (its marked invalid) 
			 * or we have not yet seen the split. 
			 */
			if (grow_idx(old_idx) != new_idx) {
				/* 
				 * Search the moved bucket. It is guaranteed to contain
				 * items of the newly added bucket that were present
				 * before the moved bucket was split.
				 */
				new_head = h->new_b->head[grow_idx(old_idx)];
			}
			
			/* new_head is now the moved bucket, either valid or invalid. */
			
			/* 
			 * The old bucket was definitely moved to new_head but the
			 * change of new_head had not yet propagated to this cpu.
			 */
			if (N_INVALID == get_mark(new_head)) {
				/*
				 * We could issue a read_barrier() and make the now valid
				 * moved bucket head new_head visible, but instead fall back
				 * on using the old bucket. Although the old bucket head is 
				 * invalid, it points to a node that is allocated and in the 
				 * right bucket. Before the node can be freed, it must be
				 * unlinked from the head (or another item after that item
				 * modified the new_head) and a grace period must elapse. 
				 * As a result had the node been already freed the grace
				 * period preceeding the free() would make the unlink and
				 * any changes to new_head visible. Therefore, it is safe
				 * to use the node pointed to from the old bucket head.
				 */

				search_head = old_head;
			} else {
				search_head = new_head;
			}
		}
		
		return search_bucket(h, search_head, key, hash);
	} else if (h->b->order > h->new_b->order) {
		/* Shrinking. */
		
		/* Index of the bucket in the old table that was moved. */
		size_t move_src_idx = grow_idx(new_idx);
		marked_ptr_t moved_old_head = h->b->head[move_src_idx];
		
		/*
		 * h->b->head[move_src_idx] had already been moved to new_head 
		 * but the change to new_head had not yet propagated to us.
		 */
		if (N_INVALID == get_mark(new_head)) {
			/*
			 * new_head is definitely valid and we could make it visible 
			 * to this cpu with a read_barrier(). Instead, use the bucket 
			 * in the old table that was moved even though it is now marked 
			 * as invalid. The node it points to must be allocated because
			 * a grace period would have to elapse before it could be freed;
			 * and the grace period would make the now valid new_head 
			 * visible to all cpus. 
			 * 
			 * Note that move_src_idx may not be the same as old_idx.
			 * If move_src_idx != old_idx then old_idx is the bucket
			 * in the old table that is not moved but instead it is
			 * appended to the moved bucket, ie it is added at the tail
			 * of new_head. In that case an invalid old_head notes that
			 * it had already been merged into (the moved) new_head. 
			 * We will try to search that bucket first because it
			 * may contain some newly added nodes after the bucket 
			 * join. Moreover, the bucket joining link may already be 
			 * visible even if new_head is not. Therefore, if we're
			 * lucky we'll find the item via moved_old_head. In any
			 * case, we'll retry in proper old_head if not found.
			 */
			search_head = moved_old_head;
		}
		
		cht_link_t *ret = search_bucket(h, search_head, key, hash);
		
		if (ret)
			return ret;
		/*
		 * Bucket old_head was already joined with moved_old_head
		 * in the new table but we have not yet seen change of the
		 * joining link (or the item is not in the table).
		 */
		if (move_src_idx != old_idx && get_next(old_head)) {
			/*
			 * Note that old_head (the bucket to be merged into new_head) 
			 * points to an allocated join node (if non-null) even if marked 
			 * invalid. Before the resizer lets join nodes to be unlinked
			 * (and freed) it sets old_head to 0 and waits for a grace period.
			 * So either the invalid old_head points to join node; or old_head
			 * is null and we would have seen a completed bucket join while
			 * traversing search_head.
			 */
			ASSERT(N_JOIN & get_mark(get_next(old_head)->link));
			return search_bucket(h, old_head, key, hash);
		}
		
		return 0;
	} else {
		/* 
		 * Resize is almost done. The resizer is waiting to make
		 * sure all cpus see that the new table replaced the old one.
		 */
		ASSERT(h->b->order == h->new_b->order);
		/* 
		 * The resizer must ensure all new bucket heads are visible before
		 * replacing the old table.
		 */
		ASSERT(N_NORMAL == get_mark(new_head));
		return search_bucket(h, new_head, key, hash);
	}
}


void cht_insert(cht_t *h, cht_link_t *item)
{
	return insert_impl(h, item, true);
}

bool cht_insert_unique(cht_t *h, cht_link_t *item)
{
	insert_impl(h, item, false);
}

bool insert_impl(cht_t *h, cht_link_t *item, bool unique)
{
	rcu_read_lock();

	
	rcu_read_unlock();
}


size_t cht_remove_key(cht_t *h, void *key)
{
	ASSERT(h);
	
	size_t hash = key_hash(h, key);
	size_t removed = 0;
	
	while (remove_pred(h, hash, h->op->key_equal, key)) 
		++removed;
	
	return removed;
}

bool cht_remove_item(cht_t *h, cht_link_t *item)
{
	ASSERT(h);
	ASSERT(item);

	/* 
	 * Even though we know the node we want to delete we must unlink it
	 * from the correct bucket and from a clean/normal predecessor. Therefore, 
	 * we search for it again from the beginning of the correct bucket.
	 */
	size_t hash = node_hash(h, item);
	return remove_pred(h, hash, same_node_pred, item);
}


static bool remove_pred(cht_t *h, size_t hash, equal_pred_t pred, void *pred_arg)
{
	rcu_read_lock();
	
	bool resizing = false;
	bool deleted = false;
	bool deleted_but_gc = false;
	
	cht_buckets_t *b = rcu_access(h->b);
	size_t idx = calc_bucket_idx(hash, b->order);
	marked_ptr_t *phead = &b->head[idx];
	
	do {
		walk_mode_t walk_mode = WM_NORMAL;
		bool join_finishing = false;
		
		resizing = resizing || (N_NORMAL != get_mark(*phead));
		
		/* The table is resizing. Get the correct bucket head. */
		if (resizing) {
			upd_resizing_head(hash, &phead, &join_finishing, &walk_mode);
		}
		
		wnd_t wnd = {
			.ppred = phead,
			.cur = get_next(*phead),
			.last = 0
		};
		
		if (!find_wnd_and_gc_pred(
			h, hash, walk_mode, pred, pred_arg, &wnd, &resizing)) {
			/* Could not GC a node; or detected an unexpected resize. */
			continue;
		}
		
		/* 
		 * The item lookup is affected by a bucket join but effects of
		 * the bucket join have not been seen while searching for the item.
		 */
		if (join_finishing && !join_completed(h, &wnd)) {
			/* 
			 * Bucket was appended at the end of another but the next 
			 * ptr linking them together was not visible on this cpu. 
			 * join_completed() makes this appended bucket visible.
			 */
			continue;
		}
		
		/* Already deleted, but delete_at() requested one GC pass. */
		if (deleted_but_gc)
			break;
		
		bool found = wnd.cur && pred(pred_arg, wnd.cur);
		
		if (!found) {
			rcu_read_unlock();
			return false;
		}
		
		deleted = delete_at(wnd, walk_mode, &deleted_but_gc, &resizing);		
	} while (!deleted || deleted_but_gc);
	
	rcu_read_unlock();
	return true;
}


static bool delete_at(cht_t *h, wnd_t *wnd, walk_mode_t walk_mode, 
	bool *deleted_but_gc, bool *resizing)
{
	ASSERT(wnd->cur);
	
	*deleted_but_gc = false;
	
	if (!mark_deleted(wnd->cur, walk_mode, resizing)) {
		/* Already deleted, or unexpectedly marked as JOIN/JOIN_FOLLOWS. */
		return false;
	}
	
	/* Marked deleted. Unlink from the bucket. */
	
	/* Never unlink join nodes. */
	if (walk_mode == WM_LEAVE_JOIN && (N_JOIN & get_mark(wnd->cur->link)))
		return true;
	
	if (unlink_from_pred(wnd, walk_mode, resizing)) {
		free_later(h, wnd->cur);
	} else {
		*deleted_but_gc = true;
	}
	
	return true;
}

static bool mark_deleted(cht_link_t *cur, walk_mode_t walk_mode, bool *resizing)
{
	ASSERT(cur);
	
	/* 
	 * Btw, we could loop here if the cas fails but let's not complicate
	 * things and let's retry from the head of the bucket. 
	 */
	
	cht_link_t *next = get_next(cur->link);
	
	if (walk_mode == WM_NORMAL) {
		/* Only mark clean/normal nodes - JF/JN is used only during resize. */
		marked_ptr_t normal_link = make_link(next, N_NORMAL);
		marked_ptr_t del_link = make_link(next, N_DELETED);
		
		marked_ptr_t ret = cas_link(&cur->link, normal_link, del_link);
		
		if (normal_link != ret) {
			*resizing = (N_JOIN | N_JOIN_FOLLOWS | N_INVALID) & get_mark(ret);
			return false;
		}
	} else {
		ASSERT(N_JOIN == N_JOIN_FOLLOWS);
		
		/* Keep the N_JOIN/N_JOIN_FOLLOWS mark but strip N_DELETED. */
		mark_t cur_mark = get_mark(cur->link) & N_JOIN_FOLLOWS;
		
		marked_ptr_t nondel_link = make_link(next, cur_mark);
		marked_ptr_t del_link = make_link(next, cur_mark | N_DELETED);
		
		if (nondel_link != cas_link(&cur->link, nondel_link, del_link))
			return false;
	} 
	
	return true;
}

static bool unlink_from_pred(wnd_t *wnd, walk_mode_t walk_mode, bool *resizing)
{
	ASSERT(wnd->cur && (N_DELETED & get_mark(wnd->cur->link)));
	
	cht_link_t *next = get_next(wnd->cur->link);
		
	if (walk_mode == WM_LEAVE_JOIN) {
		/* Never try to unlink join nodes. */
		ASSERT(!(N_JOIN & get_mark(wnd->cur->link)));

		mark_t pred_mark = get_mark(*wnd->ppred);
		/* Succeed only of the predecessor is clean/normal or a join node. */
		mark_t exp_pred_mark = (N_JOIN & pred_mark) ? pred_mark : N_NORMAL;
		
		marked_ptr_t pred_link = make_link(wnd->cur, exp_pred_mark);
		marked_ptr_t next_link = make_link(next, exp_pred_mark);
		
		if (pred_link != cas_link(wnd->ppred, pred_link, next_link))
			return false;
	} else {
		ASSERT(walk_mode == WM_MOVE_JOIN_FOLLOWS || walk_mode == WM_NORMAL);
		/* Move the JF mark if set. Clear DEL mark. */
		mark_t cur_mark = N_JOIN_FOLLOWS & get_mark(wnd->cur->link);
		
		/* The predecessor must be clean/normal. */
		marked_ptr_t pred_link = make_link(wnd->cur, N_NORMAL);
		/* Link to cur's successor keeping/copying cur's JF mark. */
		marked_ptr_t next_link = make_link(next, cur_mark);		
		
		marked_ptr_t ret = cas_link(wnd->ppred, pred_link, next_link);
		
		if (pred_link != ret) {
			/* If we're not resizing the table there are no JF/JN nodes. */
			*resizing = (walk_mode == WM_NORMAL) 
				&& (N_JOIN_FOLLOWS & get_mark(ret));
			return false;
		}
	}
	
	return true;
}


static bool find_wnd_and_gc_pred(cht_t *h, size_t hash, walk_mode_t walk_mode, 
	equal_pred_t pred, void *pred_arg, wnd_t *wnd, bool *resizing)
{
	if (!wnd->cur)
		return true;
	
	/* 
	 * A read barrier is not needed here to bring up the most recent 
	 * node marks (esp the N_DELETED). At worst we'll try to delete
	 * an already deleted node; fail in delete_at(); and retry.
	 */
	
	size_t cur_hash = node_hash(h, wnd->cur);
		
	while (cur_hash <= hash) {
		/* GC any deleted nodes on the way. */
		if (N_DELETED & get_mark(wnd->cur->link)) {
			if (!gc_deleted_node(h, walk_mode, wnd, resizing)) {
				/* Retry from the head of a bucket. */
				return false;
			}
		} else {
			/* Is this the node we were looking for? */
			if (cur_hash == hash && pred(pred_arg, wnd->cur))
				return true;
			
			next_wnd(wnd);
		}
		
		/* The searched for node is not in the current bucket. */
		if (wnd->cur)
			return true;
		
		cur_hash = node_hash(h, wnd->cur);
	}
	
	/* The searched for node is not in the current bucket. */
	return true;
}

/* todo: comment different semantics (eg deleted JN first w/ specific hash) */
static bool find_wnd_and_gc(cht_t *h, size_t hash, walk_mode_t walk_mode, 
	wnd_t *wnd, bool *resizing)
{
	while (wnd->cur && node_hash(h, wnd->cur) < hash) {
		/* GC any deleted nodes along the way to our desired node. */
		if (N_DELETED & get_mark(wnd->cur->link)) {
			if (!gc_deleted_node(h, walk_mode, wnd, resizing)) {
				/* Failed to remove the garbage node. Retry. */
				return false;
			}
		} else {
			next_wnd(wnd);
		}
	}
	
	/* wnd->cur may be 0 or even marked N_DELETED. */
	return true;
}

static bool gc_deleted_node(cht_t *h, walk_mode_t walk_mode, wnd_t *wnd,
	bool *resizing)
{
	ASSERT(N_DELETED & get_mark(wnd->cur->link));

	/* Skip deleted JOIN nodes. */
	if (walk_mode == WM_LEAVE_JOIN && (N_JOIN & get_mark(wnd->cur->link))) {
		next_wnd(wnd);
	} else {
		/* Ordinary deleted node or a deleted JOIN_FOLLOWS. */
		ASSERT(walk_mode != WM_LEAVE_JOIN 
			|| !((N_JOIN | N_JOIN_FOLLOWS) & get_mark(wnd->cur->link)));

		/* Unlink an ordinary deleted node, move JOIN_FOLLOWS mark. */
		if (!unlink_from_pred(wnd, walk_mode, resizing)) {
			/* Retry. The predecessor was deleted, invalid, const, join_follows. */
			return false;
		}

		free_later(h, wnd->cur);

		/* Leave ppred as is. */
		wnd->last = wnd->cur;
		wnd->cur = get_next(wnd->cur->link);
	}
	
	return true;
}


static bool find_dup_node_to_del(cht_t *h, bool is_pos, void *key_or_pos, 
	size_t hash, wnd_t *wnd)
{
	ASSERT(!wnd->cur || hash <= node_hash(h, wnd->cur));
	
	if (!wnd->cur || hash < node_hash(h, wnd->cur))
		return false;
	
	
	if (is_pos) {
		
	} else {
	}
}


static bool join_completed(cht_t *h, const wnd_t *wnd)
{
	/* 
	 * The table is shrinking and the searched for item is in a bucket 
	 * appended to another. Check that the link joining these two buckets 
	 * is visible and if not, make it visible to this cpu.
	 */
	
	/* 
	 * Resizer ensures h->b->order stays the same for the duration of this 
	 * func. We got here because there was an alternative head to search.
	 * The resizer waits for all preexisting readers to finish after
	 * it 
	 */
	ASSERT(h->b->order > h->new_b->order);
	
	/* Either we did not need the joining link or we have already followed it.*/
	if (wnd->cur)
		return true;
	
	/* We have reached the end of a bucket. */
	
	if (wnd->last) {
		size_t last_seen_hash = node_hash(h, wnd->last);
		size_t last_old_idx = calc_bucket_idx(last_seen_hash, h->b->order);
		size_t move_src_idx = grow_idx(shrink_idx(last_old_idx));
		
		/* 
		 * Last was in the joining bucket - if the searched for node is there
		 * we will find it. 
		 */
		if (move_src_idx != last_old_idx) 
			return true;
	}
	
	/* 
	 * Reached the end of the bucket but no nodes from the joining bucket
	 * were seen. There should have at least been a JOIN node so we have
	 * definitely not seen (and followed) the joining link. Make the link
	 * visible and retry.
	 */
	read_barrier();
	return false;
}

static void upd_resizing_head(cht_t *h, size_t hash, marked_ptr_t **phead, 
	bool *join_finishing,  bool *walk_mode)
{
	cht_buckets_t *b = rcu_access(h->b);
	size_t old_idx = calc_bucket_idx(hash, b->order);
	size_t new_idx = calc_bucket_idx(hash, h->new_b->order);
	
	marked_ptr_t *pold_head = &b->head[old_idx];
	marked_ptr_t *pnew_head = &h->new_b->head[new_idx];
	
	/* In any case, use the bucket in the new table. */
	*phead = pnew_head;

	/* Growing the table. */
	if (b->order < h->new_b->order) {
		size_t move_dest_idx = grow_idx(old_idx);
		marked_ptr_t *pmoved_head = &h->new_b->head[move_dest_idx];
		
		/* Complete moving the bucket from the old to the new table. */
		move_head(pold_head, pmoved_head);
		
		/* The hash belongs to the moved bucket. */
		if (move_dest_idx == new_idx) {
			/* 
			 * move_head() makes the new head of the moved bucket visible. 
			 * The new head may be marked with a JOIN_FOLLOWS
			 */
			ASSERT(!(N_CONST & get_mark(*pnew_head)));
			*walk_mode = WM_MOVE_JOIN_FOLLOWS;
		} else {
			/* 
			 * The hash belongs to the bucket that is the result of splitting 
			 * the old/moved bucket, ie the bucket that contains the second
			 * half of the split/old/moved bucket.
			 */
			
			/* The moved bucket has not yet been split. */
			if (N_NORMAL != get_mark(*pnew_head)) {
				split_bucket(pmoved_head, pnew_head);
				/* 
				 * split_bucket() makes the new head visible. No 
				 * JOIN_FOLLOWS in this part of split bucket.
				 */
				ASSERT(N_NORMAL == get_mark(*pnew_head));
			}
			
			*walk_mode = WM_LEAVE_JOIN;
		}
	} else if (h->new_b->order < b->order ) {
		/* Shrinking the table. */
		
		size_t move_src_idx = grow_idx(new_idx);
		
		/* 
		 * Complete moving the bucket from the old to the new table. 
		 * Makes a valid pnew_head visible if already moved.
		 */
		move_head(&b->head[move_src_idx], pnew_head);
		
		/* Hash belongs to the bucket to be joined with the moved bucket. */
		if (move_src_idx != old_idx) {
			/* Bucket join not yet completed. */
			if (N_INVALID != get_mark(*pold_head)) {
				join_buckets(pold_head, pnew_head);
			}
			
			/* The resizer sets pold_head to 0 when all cpus see the bucket join.*/
			*join_finishing = (0 != get_next(*pold_head));
		}
		
		/* move_head() or join_buckets() makes it so or makes the mark visible.*/
		ASSERT(N_INVALID == get_mark(*pold_head));
		/* move_head() makes it visible. No JOIN_FOLLOWS used when shrinking. */
		ASSERT(N_NORMAL == get_mark(*pnew_head));

		*walk_mode = WM_LEAVE_JOIN;
	} else {
		/* 
		 * Final stage of resize. The resizer is waiting for all 
		 * readers to notice that the old table had been replaced.
		 */
		ASSERT(b == h->new_b);
		*walk_mode = WM_NORMAL;
	}
}


static void move_head(marked_ptr_t *psrc_head, marked_ptr_t *pdest_head)
{
	start_head_move(psrc_head);
	complete_head_move(psrc_head, pdest_head);
}

static void help_head_move(marked_ptr_t *psrc_head, marked_ptr_t *pdest_head)
{
	/* Head move has to in progress already when calling this func. */
	ASSERT(N_CONST & get_mark(*psrc_head));
	
	/* Head already moved. */
	if (N_INVALID == get_mark(*psrc_head)) {
		/* Effects of the head move have not yet propagated to this cpu. */
		if (N_INVALID == get_mark(*pdest_head)) {
			/* Make the move visible on this cpu. */
			read_barrier();
			ASSERT(!(N_CONST & get_mark(*pdest_head)));
		}
	} else {
		complete_head_move(psrc_head, pdest_head);
	}
}

static void start_head_move(marked_ptr_t *psrc_head)
{
	/* Mark src head immutable. */
	mark_const(psrc_head);
}

static void mark_const(marked_ptr_t *psrc_head)
{
	marked_ptr_t ret, src_link;
	
	/* Mark src head immutable. */
	do {
		cht_link_t *next = get_next(*psrc_head);
		src_link = make_link(next, N_NORMAL);
		
		/* Mark the normal/clean src link immutable/const. */
		ret = cas_link(psrc_head, next, N_NORMAL, next, N_CONST);
	} while(ret != src_link && !(N_CONST & get_mark(ret)));
}

static void complete_head_move(marked_ptr_t *psrc_head, marked_ptr_t *pdest_head)
{
	ASSERT(N_JOIN_FOLLOWS != get_mark(*psrc_head));
	ASSERT(N_CONST & get_mark(*psrc_head));
	
	cht_link_t *next = get_next(*psrc_head);
	/* todo: cas order barrier */
	cas_link(pdest_head, 0, N_INVALID, next, N_NORMAL);
	/* todo: cas order barrier */
	cas_link(psrc_head, next, N_CONST, next, N_INVALID);	
}

static void split_bucket(cht_t *h, marked_ptr_t *psrc_head, 
	marked_ptr_t *pdest_head, size_t split_hash)
{
	/* Already split. */
	if (N_NORMAL == get_mark(*pdest_head))
		return;
	
	/*
	 * L == Last node of the first part of the split bucket. That part
	 *      remains in the original/src bucket. 
	 * F == First node of the second part of the split bucket. That part
	 *      will be referenced from the dest bucket head.
	 *
	 * We want to first mark a clean L as JF so that updaters unaware of 
	 * the split (or table resize):
	 * - do not insert a new node between L and F
	 * - do not unlink L (that is why it has to be clean/normal)
	 * - do not unlink F
	 *
	 * Then we can safely mark F as JN even if it has been marked deleted. 
	 * Once F is marked as JN updaters aware of table resize will not 
	 * attempt to unlink it (JN will have two predecessors - we cannot
	 * safely unlink from both at the same time). Updaters unaware of 
	 * ongoing resize can reach F only via L and that node is already 
	 * marked JF, so they won't unlink F.
	 * 
	 * Last, link the new/dest head to F.
	 * 
	 * 
	 * 0)                           ,-- split_hash, first hash of the dest bucket 
	 *                              v  
	 *  [src_head | N] -> .. -> [L] -> [F]
	 *  [dest_head | Inv]
	 * 
	 * 1)                             ,-- split_hash
	 *                                v  
	 *  [src_head | N] -> .. -> [JF] -> [F]
	 *  [dest_head | Inv]
	 * 
	 * 2)                             ,-- split_hash
	 *                                v  
	 *  [src_head | N] -> .. -> [JF] -> [JN]
	 *  [dest_head | Inv]
	 * 
	 * 2)                             ,-- split_hash
	 *                                v  
	 *  [src_head | N] -> .. -> [JF] -> [JN]
	 *                                   ^
	 *  [dest_head | N] -----------------'
	 */
	wnd_t wnd;
	bool done;
	
	/* Mark the last node of the first part of the split bucket as JF. */
	mark_join_follows(h, psrc_head, split_hash, &wnd);
	
	/* todo: cas order barrier */
	
	/* There are nodes in the dest bucket, ie the second part of the split. */
	if (wnd.cur) {
		/* 
		 * Mark the first node of the dest bucket as a join node so 
		 * updaters do not attempt to unlink it if it is deleted. 
		 */
		mark_join_node(wnd.cur);
	} else {
		/* 
		 * Second part of the split bucket is empty. There are no nodes
		 * to mark as JOIN nodes and there never will be.
		 */
	}
	
	/* Link the dest head to the second part of the split. */
	cas_link(pdest_head, 0, N_INVALID, wnd.cur, N_NORMAL);
}

static void mark_join_follows(cht_t *h, marked_ptr_t *psrc_head, 
	size_t split_hash, wnd_t *wnd)
{
	/* See comment in split_bucket(). */
	
	bool done;
	do {
		bool dummy;
		wnd->ppred = psrc_head;
		wnd->cur = get_next(*psrc_head);
		
		/* 
		 * Find the split window, ie the last node of the first part of
		 * the split bucket and the its successor - the first node of
		 * the second part of the split bucket. Retry if GC failed. 
		 */
		if (!find_wnd_and_gc(h, split_hash, WM_MOVE_JOIN_FOLLOWS, wnd, &dummy))
			continue;
		
		/* 
		 * Mark the last node of the first half of the split bucket 
		 * that a join node follows. It must be clean/normal.
		 */
		marked_ptr_t ret
			= cas_link(wnd->ppred, wnd->cur, N_NORMAL, wnd->cur, N_JOIN_FOLLOWS);

		/* Successfully marked as a JF node or already marked that way. */
		done = (ret == make_link(wnd->cur, N_NORMAL))
			|| (N_JOIN_FOLLOWS & get_mark(ret));
	} while (!done);
}

static void mark_join_node(cht_link_t *join_node)
{
	/* See comment in split_bucket(). */
	
	bool done;
	do {
		cht_link_t *next = get_next(*join_node);
		mark_t mark = get_mark(*join_node);
		
		/* 
		 * May already be marked as deleted, but it won't be unlinked 
		 * because its predecessor is marked with JOIN_FOLLOWS or CONST.
		 */
		marked_ptr_t ret 
			= cas_link(&join_node->link, next, mark, next, mark | N_JOIN);
		
		/* Successfully marked or already marked as a join node. */
		done = (ret == make_link(next, mark))
			|| (N_JOIN & get_mark(ret));
	} while(!done);
}


static void join_buckets(cht_t *h, marked_ptr_t *psrc_head, 
	marked_ptr_t *pdest_head)
{
	/* Buckets already joined. */
	if (N_INVALID == get_mark(*psrc_head))
		return;
	/*
	 * F == First node of psrc_head, ie the bucket we want to append 
	 *      to (ie join with) the bucket starting at pdest_head.
	 * L == Last node of pdest_head, ie the bucket that psrc_head will
	 *      be appended to. 
	 *
	 * (1) We first mark psrc_head immutable to signal that a join is 
	 * in progress and so that updaters unaware of the join (or table 
	 * resize):
	 * - do not insert new nodes between the head psrc_head and F
	 * - do not unlink F (it may already be marked deleted)
	 * 
	 * (2) Next, F is marked as a join node. Updaters aware of table resize
	 * will not attempt to unlink it. We cannot safely/atomically unlink 
	 * the join node because it will be pointed to from two different 
	 * buckets. Updaters unaware of resize will fail to unlink the join
	 * node due to the head being marked immutable.
	 *
	 * (3) Then the tail of the bucket at pdest_head is linked to the join
	 * node. From now on, nodes in both buckets can be found via pdest_head.
	 * 
	 * (4) Last, mark immutable psrc_head as invalid. It signals updaters
	 * that the join is complete and they can insert new nodes (originally
	 * destined for psrc_head) into pdest_head. 
	 * 
	 * Note that pdest_head keeps pointing at the join node. This allows
	 * lookups and updaters to determine if they should see a link between
	 * the tail L and F when searching for nodes originally in psrc_head
	 * via pdest_head. If they reach the tail of pdest_head without 
	 * encountering any nodes of psrc_head, either there were no nodes
	 * in psrc_head to begin with or the link between L and F did not
	 * yet propagate to their cpus. If psrc_head was empty, it remains
	 * NULL. Otherwise psrc_head points to a join node (it will not be 
	 * unlinked until table resize completes) and updaters/lookups
	 * should issue a read_barrier() to make the link [L]->[JN] visible.
	 * 
	 * 0)                           ,-- split_hash, first hash of the src bucket 
	 *                              v  
	 *  [dest_head | N]-> .. -> [L]
	 *  [src_head | N]--> [F] -> .. 
	 *  ^
	 *  ` split_hash, first hash of the src bucket
	 * 
	 * 1)                            ,-- split_hash
	 *                               v  
	 *  [dest_head | N]-> .. -> [L]
	 *  [src_head | C]--> [F] -> .. 
	 * 
	 * 2)                            ,-- split_hash
	 *                               v  
	 *  [dest_head | N]-> .. -> [L]
	 *  [src_head | C]--> [JN] -> .. 
	 * 
	 * 3)                            ,-- split_hash
	 *                               v  
	 *  [dest_head | N]-> .. -> [L] --+
	 *                                v
	 *  [src_head | C]-------------> [JN] -> .. 
	 * 
	 * 4)                            ,-- split_hash
	 *                               v  
	 *  [dest_head | N]-> .. -> [L] --+
	 *                                v
	 *  [src_head | Inv]-----------> [JN] -> .. 
	 */
	
	/* Mark src_head immutable - signals updaters bucket join started. */
	mark_const(psrc_head);
	/* todo: cas order barrier*/
	
	cht_link_t *join_node = get_next(*psrc_head);

	if (join_node) {
		mark_join_node(join_node);
		/* todo: cas order barrier*/
		
		link_to_join_node(h, pdest_head, join_node, split_hash);
		/* todo: cas order barrier*/
	} 
	
	cas_link(psrc_head, join_node, N_CONST, join_node, N_INVALID);
}

static void link_to_join_node(cht_t *h, marked_ptr_t *pdest_head, 
	cht_link_t *join_node, size_t split_hash)
{
	bool done;
	do {
		wnd_t wnd = {
			.ppred = pdest_head,
			.cur = get_next(*pdest_head)
		};
		
		bool dummy;
		
		if (!find_wnd_and_gc(h, split_hash, WM_LEAVE_JOIN, &wnd, &dummy))
			continue;

		if (wnd.cur) {
			/* Must be from the new appended bucket. */
			ASSERT(split_hash <= node_hash(h, wnd.cur));
			return;
		}
		
		/* Reached the tail of pdest_head - link it to the join node. */
		marked_ptr_t ret = cas_link(wnd.ppred, 0, N_NORMAL, join_node, N_NORMAL);
		
		done = (ret == make_link(0, N_NORMAL));
	} while (!done);
}

static void free_later(cht_t *h, cht_link_t *item)
{
	/* 
	 * remove_callback only works as rcu_func_t because rcu_link is the first
	 * field in cht_link_t.
	 */
	rcu_call(&item->rcu_link, (rcu_func_t)h->op->remove_callback);
	
	/* todo: --items */
}

static size_t size_to_order(size_t bucket_cnt)
{
}

static cht_buckets_t *alloc_buckets(size_t order, bool set_invalid)
{
	size_t bucket_cnt = (1 << order);
	cht_buckets_t *b = malloc(
		sizeof(cht_buckets_t) + (bucket_cnt - 1) * sizeof(uintptr_t));
	
	if (!b)
		return 0;
	
	b->order = order;
	
	marked_ptr_t head_link = set_invalid ? make_link(0, N_INVALID) : 0;
	
	for (size_t i = 0; i < bucket_cnt; ++i) {
		b->head[i] = head_link;
	}
	
	return b;
}


static size_t key_hash(cht_t *h, void *key)
{
	return hash_mix(h->op->key_hash(key));
}

static size_t node_hash(cht_t *h, const cht_link_t *item)
{
	return hash_mix(h->op->hash(item));
}


static marked_ptr_t make_link(cht_link_t *next, mark_t mark)
{
	marked_ptr_t ptr = (marked_ptr_t) next;
	
	ASSERT(!(ptr & N_MARK_MASK));
	ASSERT(!((unsigned)mark & ~N_MARK_MASK));
	
	return ptr | mark;
}


static cht_link_t * get_next(marked_ptr_t link)
{
	return (cht_link_t*)(link & ~N_MARK_MASK);
}


static mark_t get_mark(marked_ptr_t link)
{
	return (mark_t)(link & N_MARK_MASK);
}


static void next_wnd(wnd_t *wnd)
{
	ASSERT(wnd);
	ASSERT(wnd->cur);

	wnd->last = wnd->cur;
	wnd->ppred = &wnd->cur->link;
	wnd->cur = get_next(wnd->cur->link);
}


static bool same_node_pred(void *node, const cht_link_t *item2)
{
	const cht_link_t *item1 = (const cht_link_t*) node;
	return item1 == item2;
}

static marked_ptr_t cas_link(marked_ptr_t *link, cht_link_t *cur_next, 
	mark_t cur_mark, cht_link_t *new_next, mark_t new_mark)
{
	return _cas_link(link, make_link(cur_next, cur_mark), 
		make_link(new_next, new_mark));
}

static marked_ptr_t _cas_link(marked_ptr_t *link, marked_ptr_t cur, 
	marked_ptr_t new)
{
	
}

/** @}
 */
