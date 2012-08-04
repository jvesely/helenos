/*
 * Copyright (c) 2012 Sean Bartell
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

/** @addtogroup bithenge
 * @{
 */
/**
 * @file
 * Sequence transforms.
 */

#include <stdlib.h>
#include "blob.h"
#include "expression.h"
#include "os.h"
#include "sequence.h"
#include "tree.h"



typedef struct {
	bithenge_node_t base;
	const struct seq_node_ops *ops;
	bithenge_blob_t *blob;
	bithenge_scope_t scope;
	aoff64_t *ends;
	size_t num_ends;
	bool end_on_empty;
	bithenge_int_t num_xforms;
} seq_node_t;

typedef struct seq_node_ops {
	int (*get_transform)(seq_node_t *self, bithenge_transform_t **out,
	    bithenge_int_t index);
} seq_node_ops_t;

static bithenge_node_t *seq_as_node(seq_node_t *node)
{
	return &node->base;
}

static seq_node_t *node_as_seq(bithenge_node_t *node)
{
	return (seq_node_t *)node;
}

static int seq_node_field_offset(seq_node_t *self, aoff64_t *out, size_t index)
{
	if (index == 0) {
		*out = 0;
		return EOK;
	}
	index--;
	aoff64_t prev_offset =
	    self->num_ends ? self->ends[self->num_ends - 1] : 0;
	for (; self->num_ends <= index; self->num_ends++) {
		bithenge_transform_t *subxform;
		int rc = self->ops->get_transform(self, &subxform,
		    self->num_ends);
		if (rc != EOK)
			return rc;

		bithenge_node_t *subblob_node;
		bithenge_blob_inc_ref(self->blob);
		rc = bithenge_new_offset_blob(&subblob_node, self->blob,
		    prev_offset);
		if (rc != EOK) {
			bithenge_transform_dec_ref(subxform);
			return rc;
		}

		if (self->end_on_empty) {
			bool empty;
			rc = bithenge_blob_empty(
			    bithenge_node_as_blob(subblob_node), &empty);
			if (rc == EOK && empty) {
				self->num_xforms = self->num_ends;
				rc = ENOENT;
			}
			if (rc != EOK) {
				bithenge_transform_dec_ref(subxform);
				bithenge_node_dec_ref(subblob_node);
				return rc;
			}
		}

		bithenge_blob_t *subblob = bithenge_node_as_blob(subblob_node);
		aoff64_t field_size;
		rc = bithenge_transform_prefix_length(subxform, &self->scope,
		    subblob, &field_size);
		bithenge_node_dec_ref(subblob_node);
		bithenge_transform_dec_ref(subxform);
		if (rc != EOK)
			return rc;

		if (self->num_xforms == -1) {
			aoff64_t *new_ends = realloc(self->ends,
			    (self->num_ends + 1)*sizeof(*new_ends));
			if (!new_ends)
				return ENOMEM;
			self->ends = new_ends;
		}

		prev_offset = self->ends[self->num_ends] =
		    prev_offset + field_size;
	}
	*out = self->ends[index];
	return EOK;
}

static int seq_node_subtransform(seq_node_t *self, bithenge_node_t **out,
    size_t index)
{
	aoff64_t start_pos;
	int rc = seq_node_field_offset(self, &start_pos, index);
	if (rc != EOK)
		return rc;

	bithenge_transform_t *subxform;
	rc = self->ops->get_transform(self, &subxform, index);
	if (rc != EOK)
		return rc;

	if (index == self->num_ends) {
		/* We can apply the subtransform and cache its prefix length at
		 * the same time. */
		bithenge_node_t *blob_node;
		bithenge_blob_inc_ref(self->blob);
		rc = bithenge_new_offset_blob(&blob_node, self->blob,
		    start_pos);
		if (rc != EOK) {
			bithenge_transform_dec_ref(subxform);
			return rc;
		}

		if (self->end_on_empty) {
			bool empty;
			rc = bithenge_blob_empty(
			    bithenge_node_as_blob(blob_node), &empty);
			if (rc == EOK && empty) {
				self->num_xforms = self->num_ends;
				rc = ENOENT;
			}
			if (rc != EOK) {
				bithenge_transform_dec_ref(subxform);
				bithenge_node_dec_ref(blob_node);
				return rc;
			}
		}

		aoff64_t size;
		rc = bithenge_transform_prefix_apply(subxform, &self->scope,
		    bithenge_node_as_blob(blob_node), out, &size);
		bithenge_node_dec_ref(blob_node);
		bithenge_transform_dec_ref(subxform);
		if (rc != EOK)
			return rc;

		if (self->num_xforms == -1) {
			aoff64_t *new_ends = realloc(self->ends,
			    (self->num_ends + 1)*sizeof(*new_ends));
			if (!new_ends)
				return ENOMEM;
			self->ends = new_ends;
		}
		self->ends[self->num_ends++] = start_pos + size;
	} else {
		aoff64_t end_pos;
		int rc = seq_node_field_offset(self, &end_pos, index + 1);
		if (rc != EOK) {
			bithenge_transform_dec_ref(subxform);
			return rc;
		}

		bithenge_node_t *blob_node;
		bithenge_blob_inc_ref(self->blob);
		rc = bithenge_new_subblob(&blob_node, self->blob, start_pos,
		    end_pos - start_pos);
		if (rc != EOK) {
			bithenge_transform_dec_ref(subxform);
			return rc;
		}

		rc = bithenge_transform_apply(subxform, &self->scope,
		    blob_node, out);
		bithenge_node_dec_ref(blob_node);
		bithenge_transform_dec_ref(subxform);
		if (rc != EOK)
			return rc;
	}

	return EOK;
}

static int seq_node_complete(seq_node_t *self, bool *out)
{
	aoff64_t blob_size, end_pos;
	int rc = bithenge_blob_size(self->blob, &blob_size);
	if (rc != EOK)
		return rc;
	rc = seq_node_field_offset(self, &end_pos, self->num_xforms);
	if (rc != EOK)
		return rc;
	*out = blob_size == end_pos;
	return EOK;
}

static void seq_node_destroy(seq_node_t *self)
{
	bithenge_scope_destroy(&self->scope);
	bithenge_blob_dec_ref(self->blob);
	free(self->ends);
}

static bithenge_scope_t *seq_node_scope(seq_node_t *self)
{
	return &self->scope;
}

static int seq_node_init(seq_node_t *self, const seq_node_ops_t *ops,
    bithenge_scope_t *scope, bithenge_blob_t *blob, bithenge_int_t num_xforms,
    bool end_on_empty)
{
	self->ops = ops;
	if (num_xforms != -1) {
		self->ends = malloc(sizeof(*self->ends) * num_xforms);
		if (!self->ends)
			return ENOMEM;
	} else
		self->ends = NULL;
	bithenge_blob_inc_ref(blob);
	self->blob = blob;
	self->num_xforms = num_xforms;
	self->num_ends = 0;
	self->end_on_empty = end_on_empty;
	bithenge_scope_init(&self->scope);
	int rc = bithenge_scope_copy(&self->scope, scope);
	if (rc != EOK) {
		bithenge_scope_destroy(&self->scope);
		return rc;
	}
	return EOK;
}



typedef struct {
	bithenge_transform_t base;
	bithenge_named_transform_t *subtransforms;
	size_t num_subtransforms;
} struct_transform_t;

static bithenge_transform_t *struct_as_transform(struct_transform_t *xform)
{
	return &xform->base;
}

static struct_transform_t *transform_as_struct(bithenge_transform_t *xform)
{
	return (struct_transform_t *)xform;
}

typedef struct {
	seq_node_t base;
	struct_transform_t *transform;
	bool prefix;
} struct_node_t;

static seq_node_t *struct_as_seq(struct_node_t *node)
{
	return &node->base;
}

static struct_node_t *seq_as_struct(seq_node_t *base)
{
	return (struct_node_t *)base;
}

static bithenge_node_t *struct_as_node(struct_node_t *node)
{
	return seq_as_node(struct_as_seq(node));
}

static struct_node_t *node_as_struct(bithenge_node_t *node)
{
	return seq_as_struct(node_as_seq(node));
}

static int struct_node_for_each(bithenge_node_t *base,
    bithenge_for_each_func_t func, void *data)
{
	int rc = EOK;
	struct_node_t *self = node_as_struct(base);
	bithenge_named_transform_t *subxforms =
	    self->transform->subtransforms;

	for (bithenge_int_t i = 0; subxforms[i].transform; i++) {
		bithenge_node_t *subxform_result;
		rc = seq_node_subtransform(struct_as_seq(self),
		    &subxform_result, i);
		if (rc != EOK)
			return rc;

		if (subxforms[i].name) {
			bithenge_node_t *name_node;
			rc = bithenge_new_string_node(&name_node,
			    subxforms[i].name, false);
			if (rc == EOK) {
				rc = func(name_node, subxform_result, data);
				subxform_result = NULL;
			}
		} else {
			if (bithenge_node_type(subxform_result) !=
			    BITHENGE_NODE_INTERNAL) {
				rc = EINVAL;
			} else {
				rc = bithenge_node_for_each(subxform_result,
				    func, data);
			}
		}
		bithenge_node_dec_ref(subxform_result);
		if (rc != EOK)
			return rc;
	}

	if (!self->prefix) {
		bool complete;
		rc = seq_node_complete(struct_as_seq(self), &complete);
		if (rc != EOK)
			return rc;
		if (!complete)
			return EINVAL;
	}

	return rc;
}

static int struct_node_get(bithenge_node_t *base, bithenge_node_t *key,
    bithenge_node_t **out)
{
	struct_node_t *self = node_as_struct(base);

	int rc = ENOENT;
	if (bithenge_node_type(key) != BITHENGE_NODE_STRING)
		goto end;
	const char *name = bithenge_string_node_value(key);

	const bithenge_named_transform_t *subxforms =
	    self->transform->subtransforms;
	for (bithenge_int_t i = 0; subxforms[i].transform; i++) {
		if (subxforms[i].name && !str_cmp(name, subxforms[i].name)) {
			rc = seq_node_subtransform(struct_as_seq(self), out, i);
			goto end;
		}
	}

	for (bithenge_int_t i = 0; subxforms[i].transform; i++) {
		if (subxforms[i].name)
			continue;
		bithenge_node_t *subxform_result;
		rc = seq_node_subtransform(struct_as_seq(self),
		    &subxform_result, i);
		if (rc != EOK)
			goto end;
		if (bithenge_node_type(subxform_result) !=
		    BITHENGE_NODE_INTERNAL) {
			bithenge_node_dec_ref(subxform_result);
			rc = EINVAL;
			goto end;
		}
		bithenge_node_inc_ref(key);
		rc = bithenge_node_get(subxform_result, key, out);
		bithenge_node_dec_ref(subxform_result);
		if (rc != ENOENT)
			goto end;
	}

	rc = ENOENT;
end:
	bithenge_node_dec_ref(key);
	return rc;
}

static void struct_node_destroy(bithenge_node_t *base)
{
	struct_node_t *node = node_as_struct(base);

	/* We didn't inc_ref for the scope in struct_transform_make_node, so
	 * make sure it doesn't try to dec_ref. */
	seq_node_scope(struct_as_seq(node))->current_node = NULL;
	seq_node_destroy(struct_as_seq(node));

	bithenge_transform_dec_ref(struct_as_transform(node->transform));
	free(node);
}

static const bithenge_internal_node_ops_t struct_node_ops = {
	.for_each = struct_node_for_each,
	.get = struct_node_get,
	.destroy = struct_node_destroy,
};

static int struct_node_get_transform(seq_node_t *base,
    bithenge_transform_t **out, bithenge_int_t index)
{
	struct_node_t *self = seq_as_struct(base);
	*out = self->transform->subtransforms[index].transform;
	bithenge_transform_inc_ref(*out);
	return EOK;
}

static const seq_node_ops_t struct_node_seq_ops = {
	.get_transform = struct_node_get_transform,
};

static int struct_transform_make_node(struct_transform_t *self,
    bithenge_node_t **out, bithenge_scope_t *scope, bithenge_blob_t *blob,
    bool prefix)
{
	struct_node_t *node = malloc(sizeof(*node));
	if (!node)
		return ENOMEM;

	int rc = bithenge_init_internal_node(struct_as_node(node),
	    &struct_node_ops);
	if (rc != EOK) {
		free(node);
		return rc;
	}

	rc = seq_node_init(struct_as_seq(node), &struct_node_seq_ops, scope,
	    blob, self->num_subtransforms, false);
	if (rc != EOK) {
		free(node);
		return rc;
	}

	bithenge_transform_inc_ref(struct_as_transform(self));
	node->transform = self;
	node->prefix = prefix;
	*out = struct_as_node(node);

	/* We should inc_ref(*out) here, but that would make a cycle. Instead,
	 * we leave it 1 too low, so that when the only remaining use of *out
	 * is the scope, *out will be destroyed. Also see the comment in
	 * struct_node_destroy. */
	bithenge_scope_set_current_node(seq_node_scope(struct_as_seq(node)),
	    *out);

	return EOK;
}

static int struct_transform_apply(bithenge_transform_t *base,
    bithenge_scope_t *scope, bithenge_node_t *in, bithenge_node_t **out)
{
	struct_transform_t *self = transform_as_struct(base);
	if (bithenge_node_type(in) != BITHENGE_NODE_BLOB)
		return EINVAL;
	return struct_transform_make_node(self, out, scope,
	    bithenge_node_as_blob(in), false);
}

static int struct_transform_prefix_length(bithenge_transform_t *base,
    bithenge_scope_t *scope, bithenge_blob_t *blob, aoff64_t *out)
{
	struct_transform_t *self = transform_as_struct(base);
	bithenge_node_t *struct_node;
	int rc = struct_transform_make_node(self, &struct_node, scope, blob,
	    true);
	if (rc != EOK)
		return rc;

	rc = seq_node_field_offset(node_as_seq(struct_node), out,
	    self->num_subtransforms);
	bithenge_node_dec_ref(struct_node);
	return rc;
}

static int struct_transform_prefix_apply(bithenge_transform_t *base,
    bithenge_scope_t *scope, bithenge_blob_t *blob, bithenge_node_t **out_node,
    aoff64_t *out_size)
{
	struct_transform_t *self = transform_as_struct(base);
	int rc = struct_transform_make_node(self, out_node, scope, blob,
	    true);
	if (rc != EOK)
		return rc;

	rc = seq_node_field_offset(node_as_seq(*out_node), out_size,
	    self->num_subtransforms);
	if (rc != EOK) {
		bithenge_node_dec_ref(*out_node);
		return rc;
	}

	return EOK;
}

static void free_subtransforms(bithenge_named_transform_t *subtransforms)
{
	for (size_t i = 0; subtransforms[i].transform; i++) {
		free((void *)subtransforms[i].name);
		bithenge_transform_dec_ref(subtransforms[i].transform);
	}
	free(subtransforms);
}

static void struct_transform_destroy(bithenge_transform_t *base)
{
	struct_transform_t *self = transform_as_struct(base);
	free_subtransforms(self->subtransforms);
	free(self);
}

static bithenge_transform_ops_t struct_transform_ops = {
	.apply = struct_transform_apply,
	.prefix_length = struct_transform_prefix_length,
	.prefix_apply = struct_transform_prefix_apply,
	.destroy = struct_transform_destroy,
};

/** Create a struct transform. The transform will apply its subtransforms
 * sequentially to a blob to create an internal node. Each result is either
 * given a key from @a subtransforms or, if the name is NULL, the result's keys
 * and values are merged into the struct transform's result. This function
 * takes ownership of @a subtransforms and the names and references therein.
 * @param[out] out Stores the created transform.
 * @param subtransforms The subtransforms and field names.
 * @return EOK on success or an error code from errno.h. */
int bithenge_new_struct(bithenge_transform_t **out,
    bithenge_named_transform_t *subtransforms)
{
	int rc;
	struct_transform_t *self = malloc(sizeof(*self));
	if (!self) {
		rc = ENOMEM;
		goto error;
	}
	rc = bithenge_init_transform(struct_as_transform(self),
	    &struct_transform_ops, 0);
	if (rc != EOK)
		goto error;
	self->subtransforms = subtransforms;
	self->num_subtransforms = 0;
	for (self->num_subtransforms = 0;
	    subtransforms[self->num_subtransforms].transform;
	    self->num_subtransforms++);
	*out = struct_as_transform(self);
	return EOK;
error:
	free_subtransforms(subtransforms);
	free(self);
	return rc;
}



typedef struct {
	bithenge_transform_t base;
	bithenge_expression_t *expr;
	bithenge_transform_t *xform;
} repeat_transform_t;

static inline bithenge_transform_t *repeat_as_transform(
    repeat_transform_t *self)
{
	return &self->base;
}

static inline repeat_transform_t *transform_as_repeat(
    bithenge_transform_t *base)
{
	return (repeat_transform_t *)base;
}

typedef struct {
	seq_node_t base;
	bool prefix;
	bithenge_int_t count;
	bithenge_transform_t *xform;
} repeat_node_t;

static seq_node_t *repeat_as_seq(repeat_node_t *self)
{
	return &self->base;
}

static repeat_node_t *seq_as_repeat(seq_node_t *base)
{
	return (repeat_node_t *)base;
}

static bithenge_node_t *repeat_as_node(repeat_node_t *self)
{
	return seq_as_node(repeat_as_seq(self));
}

static repeat_node_t *node_as_repeat(bithenge_node_t *base)
{
	return seq_as_repeat(node_as_seq(base));
}

static int repeat_node_for_each(bithenge_node_t *base,
    bithenge_for_each_func_t func, void *data)
{
	int rc = EOK;
	repeat_node_t *self = node_as_repeat(base);

	for (bithenge_int_t i = 0; self->count == -1 || i < self->count; i++) {
		bithenge_node_t *subxform_result;
		rc = seq_node_subtransform(repeat_as_seq(self),
		    &subxform_result, i);
		if (rc != EOK && self->count == -1) {
			rc = EOK;
			break;
		}
		if (rc != EOK)
			return rc;

		bithenge_node_t *key_node;
		rc = bithenge_new_integer_node(&key_node, i);
		if (rc != EOK) {
			bithenge_node_dec_ref(subxform_result);
			return rc;
		}
		rc = func(key_node, subxform_result, data);
		if (rc != EOK)
			return rc;
	}

	if (!self->prefix) {
		bool complete;
		rc = seq_node_complete(repeat_as_seq(self), &complete);
		if (rc != EOK)
			return rc;
		if (!complete)
			return EINVAL;
	}

	return rc;
}

static int repeat_node_get(bithenge_node_t *base, bithenge_node_t *key,
    bithenge_node_t **out)
{
	repeat_node_t *self = node_as_repeat(base);

	if (bithenge_node_type(key) != BITHENGE_NODE_INTEGER) {
		bithenge_node_dec_ref(key);
		return ENOENT;
	}

	bithenge_int_t index = bithenge_integer_node_value(key);
	bithenge_node_dec_ref(key);
	if (index < 0 || (self->count != -1 && index >= self->count))
		return ENOENT;
	return seq_node_subtransform(repeat_as_seq(self), out, index);
}

static void repeat_node_destroy(bithenge_node_t *base)
{
	repeat_node_t *self = node_as_repeat(base);
	seq_node_destroy(repeat_as_seq(self));
	bithenge_transform_dec_ref(self->xform);
	free(self);
}

static const bithenge_internal_node_ops_t repeat_node_ops = {
	.for_each = repeat_node_for_each,
	.get = repeat_node_get,
	.destroy = repeat_node_destroy,
};

static int repeat_node_get_transform(seq_node_t *base,
    bithenge_transform_t **out, bithenge_int_t index)
{
	repeat_node_t *self = seq_as_repeat(base);
	*out = self->xform;
	bithenge_transform_inc_ref(*out);
	return EOK;
}

static const seq_node_ops_t repeat_node_seq_ops = {
	.get_transform = repeat_node_get_transform,
};

static int repeat_transform_make_node(repeat_transform_t *self,
    bithenge_node_t **out, bithenge_scope_t *scope, bithenge_blob_t *blob,
    bool prefix)
{
	bithenge_int_t count = -1;
	if (self->expr != NULL) {
		bithenge_node_t *count_node;
		int rc = bithenge_expression_evaluate(self->expr, scope,
		    &count_node);
		if (rc != EOK)
			return rc;
		if (bithenge_node_type(count_node) != BITHENGE_NODE_INTEGER) {
			bithenge_node_dec_ref(count_node);
			return EINVAL;
		}
		count = bithenge_integer_node_value(count_node);
		bithenge_node_dec_ref(count_node);
		if (count < 0)
			return EINVAL;
	}

	repeat_node_t *node = malloc(sizeof(*node));
	if (!node)
		return ENOMEM;

	int rc = bithenge_init_internal_node(repeat_as_node(node),
	    &repeat_node_ops);
	if (rc != EOK) {
		free(node);
		return rc;
	}

	rc = seq_node_init(repeat_as_seq(node), &repeat_node_seq_ops, scope,
	    blob, count, count == -1);
	if (rc != EOK) {
		free(node);
		return rc;
	}

	bithenge_transform_inc_ref(self->xform);
	node->xform = self->xform;
	node->count = count;
	node->prefix = prefix;
	*out = repeat_as_node(node);
	return EOK;
}

static int repeat_transform_apply(bithenge_transform_t *base,
    bithenge_scope_t *scope, bithenge_node_t *in, bithenge_node_t **out)
{
	repeat_transform_t *self = transform_as_repeat(base);
	if (bithenge_node_type(in) != BITHENGE_NODE_BLOB)
		return EINVAL;
	return repeat_transform_make_node(self, out, scope,
	    bithenge_node_as_blob(in), false);
}

static int repeat_transform_prefix_apply(bithenge_transform_t *base,
    bithenge_scope_t *scope, bithenge_blob_t *blob, bithenge_node_t **out_node,
    aoff64_t *out_size)
{
	repeat_transform_t *self = transform_as_repeat(base);
	int rc = repeat_transform_make_node(self, out_node, scope, blob, true);
	if (rc != EOK)
		return rc;

	bithenge_int_t count = node_as_repeat(*out_node)->count;
	if (count != -1) {
		rc = seq_node_field_offset(node_as_seq(*out_node), out_size, count);
		if (rc != EOK) {
			bithenge_node_dec_ref(*out_node);
			return rc;
		}
	} else {
		*out_size = 0;
		for (count = 1; ; count++) {
			aoff64_t size;
			rc = seq_node_field_offset(node_as_seq(*out_node),
			    &size, count);
			if (rc != EOK)
				break;
			*out_size = size;
		}
	}
	return EOK;
}

static void repeat_transform_destroy(bithenge_transform_t *base)
{
	repeat_transform_t *self = transform_as_repeat(base);
	bithenge_transform_dec_ref(self->xform);
	bithenge_expression_dec_ref(self->expr);
	free(self);
}

static const bithenge_transform_ops_t repeat_transform_ops = {
	.apply = repeat_transform_apply,
	.prefix_apply = repeat_transform_prefix_apply,
	.destroy = repeat_transform_destroy,
};

/** Create a transform that applies its subtransform repeatedly. Takes a
 * reference to @a xform and @a expr.
 * @param[out] out Holds the new transform.
 * @param xform The subtransform to apply repeatedly.
 * @param expr Used to calculate the number of times @a xform will be applied.
 * May be NULL, in which case @a xform will be applied indefinitely.
 * @return EOK on success or an error code from errno.h. */
int bithenge_repeat_transform(bithenge_transform_t **out,
    bithenge_transform_t *xform, bithenge_expression_t *expr)
{
	int rc;
	repeat_transform_t *self = malloc(sizeof(*self));
	if (!self) {
		rc = ENOMEM;
		goto error;
	}

	rc = bithenge_init_transform(repeat_as_transform(self),
	    &repeat_transform_ops, 0);
	if (rc != EOK)
		goto error;

	self->expr = expr;
	self->xform = xform;
	*out = repeat_as_transform(self);
	return EOK;

error:
	free(self);
	bithenge_expression_dec_ref(expr);
	bithenge_transform_dec_ref(xform);
	return rc;
}

/** @}
 */
