/*
 * Copyright (c) 2006 Jakub Vana
 * Copyright (c) 2012 Martin Decky
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
/** @file
 */

#include <assert.h>
#include <sysinfo/sysinfo.h>
#include <mm/slab.h>
#include <print.h>
#include <syscall/copy.h>
#include <synch/mutex.h>
#include <arch/asm.h>
#include <errno.h>
#include <macros.h>

/** Maximal sysinfo path length */
#define SYSINFO_MAX_PATH  2048

bool fb_exported = false;

/** Global sysinfo tree root item */
static sysinfo_item_t *global_root = NULL;

/** Sysinfo SLAB cache */
static slab_cache_t *sysinfo_item_slab;

/** Sysinfo lock */
static mutex_t sysinfo_lock;

/** Sysinfo item constructor
 *
 */
NO_TRACE static int sysinfo_item_constructor(void *obj, unsigned int kmflag)
{
	sysinfo_item_t *item = (sysinfo_item_t *) obj;
	
	item->name = NULL;
	item->val_type = SYSINFO_VAL_UNDEFINED;
	item->subtree_type = SYSINFO_SUBTREE_NONE;
	item->subtree.table = NULL;
	item->next = NULL;
	
	return 0;
}

/** Sysinfo item destructor
 *
 * Note that the return value is not perfectly correct
 * since more space might get actually freed thanks
 * to the disposal of item->name
 *
 */
NO_TRACE static size_t sysinfo_item_destructor(void *obj)
{
	sysinfo_item_t *item = (sysinfo_item_t *) obj;
	
	if (item->name != NULL)
		free(item->name);
	
	return 0;
}

/** Initialize sysinfo subsystem
 *
 * Create SLAB cache for sysinfo items.
 *
 */
void sysinfo_init(void)
{
	sysinfo_item_slab = slab_cache_create("sysinfo_item_t",
	    sizeof(sysinfo_item_t), 0, sysinfo_item_constructor,
	    sysinfo_item_destructor, SLAB_CACHE_MAGDEFERRED);
	
	mutex_initialize(&sysinfo_lock, MUTEX_ACTIVE);
}

/** Recursively find an item in sysinfo tree
 *
 * Should be called with sysinfo_lock held.
 *
 * @param name    Current sysinfo path suffix.
 * @param subtree Current sysinfo (sub)tree root item.
 * @param ret     If the return value is NULL, this argument
 *                can be set either to NULL (i.e. no item was
 *                found and no data was generated) or the
 *                original pointer is used to store the value
 *                generated by a generated subtree function.
 * @param dry_run Do not actually get any generated
 *                binary data, just calculate the size.
 *
 * @return Found item or NULL if no item in the fixed tree
 *         was found (N.B. ret).
 *
 */
NO_TRACE static sysinfo_item_t *sysinfo_find_item(const char *name,
    sysinfo_item_t *subtree, sysinfo_return_t **ret, bool dry_run)
{
	assert(subtree != NULL);
	
	sysinfo_item_t *cur = subtree;
	
	/* Walk all siblings */
	while (cur != NULL) {
		size_t i = 0;
		
		/* Compare name with path */
		while ((cur->name[i] != 0) && (name[i] == cur->name[i]))
			i++;
		
		/* Check for perfect name and path match */
		if ((name[i] == 0) && (cur->name[i] == 0))
			return cur;
		
		/* Partial match up to the delimiter */
		if ((name[i] == '.') && (cur->name[i] == 0)) {
			/* Look into the subtree */
			switch (cur->subtree_type) {
			case SYSINFO_SUBTREE_TABLE:
				/* Recursively find in subtree */
				return sysinfo_find_item(name + i + 1,
				    cur->subtree.table, ret, dry_run);
			case SYSINFO_SUBTREE_FUNCTION:
				/* Get generated data */
				if (ret != NULL)
					**ret = cur->subtree.generator.fn(name + i + 1,
					    dry_run, cur->subtree.generator.data);
				
				return NULL;
			default:
				/* Not found, no data generated */
				if (ret != NULL)
					*ret = NULL;
				
				return NULL;
			}
		}
		
		cur = cur->next;
	}
	
	/* Not found, no data generated */
	if (ret != NULL)
		*ret = NULL;
	
	return NULL;
}

/** Recursively create items in sysinfo tree
 *
 * Should be called with sysinfo_lock held.
 *
 * @param name     Current sysinfo path suffix.
 * @param psubtree Pointer to an already existing (sub)tree root
 *                 item or where to store a new tree root item.
 *
 * @return Existing or newly allocated sysinfo item or NULL
 *         if the current tree configuration does not allow to
 *         create a new item.
 *
 */
NO_TRACE static sysinfo_item_t *sysinfo_create_path(const char *name,
    sysinfo_item_t **psubtree)
{
	assert(psubtree != NULL);
	
	if (*psubtree == NULL) {
		/* No parent */
		
		size_t i = 0;
		
		/* Find the first delimiter in name */
		while ((name[i] != 0) && (name[i] != '.'))
			i++;
		
		*psubtree =
		    (sysinfo_item_t *) slab_alloc(sysinfo_item_slab, 0);
		assert(*psubtree);
		
		/* Fill in item name up to the delimiter */
		(*psubtree)->name = str_ndup(name, i);
		assert((*psubtree)->name);
		
		/* Create subtree items */
		if (name[i] == '.') {
			(*psubtree)->subtree_type = SYSINFO_SUBTREE_TABLE;
			return sysinfo_create_path(name + i + 1,
			    &((*psubtree)->subtree.table));
		}
		
		/* No subtree needs to be created */
		return *psubtree;
	}
	
	sysinfo_item_t *cur = *psubtree;
	
	/* Walk all siblings */
	while (cur != NULL) {
		size_t i = 0;
		
		/* Compare name with path */
		while ((cur->name[i] != 0) && (name[i] == cur->name[i]))
			i++;
		
		/* Check for perfect name and path match
		 * -> item is already present.
		 */
		if ((name[i] == 0) && (cur->name[i] == 0))
			return cur;
		
		/* Partial match up to the delimiter */
		if ((name[i] == '.') && (cur->name[i] == 0)) {
			switch (cur->subtree_type) {
			case SYSINFO_SUBTREE_NONE:
				/* No subtree yet, create one */
				cur->subtree_type = SYSINFO_SUBTREE_TABLE;
				return sysinfo_create_path(name + i + 1,
				    &(cur->subtree.table));
			case SYSINFO_SUBTREE_TABLE:
				/* Subtree already created, add new sibling */
				return sysinfo_create_path(name + i + 1,
				    &(cur->subtree.table));
			default:
				/* Subtree items handled by a function, this
				 * cannot be overriden by a constant item.
				 */
				return NULL;
			}
		}
		
		/* No match and no more siblings to check
		 * -> create a new sibling item.
		 */
		if (cur->next == NULL) {
			/* Find the first delimiter in name */
			i = 0;
			while ((name[i] != 0) && (name[i] != '.'))
				i++;
			
			sysinfo_item_t *item =
			    (sysinfo_item_t *) slab_alloc(sysinfo_item_slab, 0);
			assert(item);
			
			cur->next = item;
			
			/* Fill in item name up to the delimiter */
			item->name = str_ndup(name, i);
			assert(item->name);
			
			/* Create subtree items */
			if (name[i] == '.') {
				item->subtree_type = SYSINFO_SUBTREE_TABLE;
				return sysinfo_create_path(name + i + 1,
				    &(item->subtree.table));
			}
			
			/* No subtree needs to be created */
			return item;
		}
		
		cur = cur->next;
	}
	
	/* Unreachable */
	assert(false);
	return NULL;
}

/** Set sysinfo item with a constant numeric value
 *
 * @param name Sysinfo path.
 * @param root Pointer to the root item or where to store
 *             a new root item (NULL for global sysinfo root).
 * @param val  Value to store in the item.
 *
 */
void sysinfo_set_item_val(const char *name, sysinfo_item_t **root,
    sysarg_t val)
{
	/* Protect sysinfo tree consistency */
	mutex_lock(&sysinfo_lock);
	
	if (root == NULL)
		root = &global_root;
	
	sysinfo_item_t *item = sysinfo_create_path(name, root);
	if (item != NULL) {
		item->val_type = SYSINFO_VAL_VAL;
		item->val.val = val;
	}
	
	mutex_unlock(&sysinfo_lock);
}

/** Set sysinfo item with a constant binary data
 *
 * Note that sysinfo only stores the pointer to the
 * binary data and does not touch it in any way. The
 * data should be static and immortal.
 *
 * @param name Sysinfo path.
 * @param root Pointer to the root item or where to store
 *             a new root item (NULL for global sysinfo root).
 * @param data Binary data.
 * @param size Size of the binary data.
 *
 */
void sysinfo_set_item_data(const char *name, sysinfo_item_t **root,
    void *data, size_t size)
{
	/* Protect sysinfo tree consistency */
	mutex_lock(&sysinfo_lock);
	
	if (root == NULL)
		root = &global_root;
	
	sysinfo_item_t *item = sysinfo_create_path(name, root);
	if (item != NULL) {
		item->val_type = SYSINFO_VAL_DATA;
		item->val.data.data = data;
		item->val.data.size = size;
	}
	
	mutex_unlock(&sysinfo_lock);
}

/** Set sysinfo item with a generated numeric value
 *
 * @param name Sysinfo path.
 * @param root Pointer to the root item or where to store
 *             a new root item (NULL for global sysinfo root).
 * @param fn   Numeric value generator function.
 * @param data Private data.
 *
 */
void sysinfo_set_item_gen_val(const char *name, sysinfo_item_t **root,
    sysinfo_fn_val_t fn, void *data)
{
	/* Protect sysinfo tree consistency */
	mutex_lock(&sysinfo_lock);
	
	if (root == NULL)
		root = &global_root;
	
	sysinfo_item_t *item = sysinfo_create_path(name, root);
	if (item != NULL) {
		item->val_type = SYSINFO_VAL_FUNCTION_VAL;
		item->val.gen_val.fn = fn;
		item->val.gen_val.data = data;
	}
	
	mutex_unlock(&sysinfo_lock);
}

/** Set sysinfo item with a generated binary data
 *
 * Note that each time the generator function is called
 * it is supposed to return a new dynamically allocated
 * data. This data is then freed by sysinfo in the context
 * of the current sysinfo request.
 *
 * @param name Sysinfo path.
 * @param root Pointer to the root item or where to store
 *             a new root item (NULL for global sysinfo root).
 * @param fn   Binary data generator function.
 * @param data Private data.
 *
 */
void sysinfo_set_item_gen_data(const char *name, sysinfo_item_t **root,
    sysinfo_fn_data_t fn, void *data)
{
	/* Protect sysinfo tree consistency */
	mutex_lock(&sysinfo_lock);
	
	if (root == NULL)
		root = &global_root;
	
	sysinfo_item_t *item = sysinfo_create_path(name, root);
	if (item != NULL) {
		item->val_type = SYSINFO_VAL_FUNCTION_DATA;
		item->val.gen_data.fn = fn;
		item->val.gen_data.data = data;
	}
	
	mutex_unlock(&sysinfo_lock);
}

/** Set sysinfo item with an undefined value
 *
 * @param name Sysinfo path.
 * @param root Pointer to the root item or where to store
 *             a new root item (NULL for global sysinfo root).
 *
 */
void sysinfo_set_item_undefined(const char *name, sysinfo_item_t **root)
{
	/* Protect sysinfo tree consistency */
	mutex_lock(&sysinfo_lock);
	
	if (root == NULL)
		root = &global_root;
	
	sysinfo_item_t *item = sysinfo_create_path(name, root);
	if (item != NULL)
		item->val_type = SYSINFO_VAL_UNDEFINED;
	
	mutex_unlock(&sysinfo_lock);
}

/** Set sysinfo item with a generated subtree
 *
 * @param name Sysinfo path.
 * @param root Pointer to the root item or where to store
 *             a new root item (NULL for global sysinfo root).
 * @param fn   Subtree generator function.
 * @param data Private data to be passed to the generator.
 *
 */
void sysinfo_set_subtree_fn(const char *name, sysinfo_item_t **root,
    sysinfo_fn_subtree_t fn, void *data)
{
	/* Protect sysinfo tree consistency */
	mutex_lock(&sysinfo_lock);
	
	if (root == NULL)
		root = &global_root;
	
	sysinfo_item_t *item = sysinfo_create_path(name, root);
	
	/* Change the type of the subtree only if it is not already
	   a fixed subtree */
	if ((item != NULL) && (item->subtree_type != SYSINFO_SUBTREE_TABLE)) {
		item->subtree_type = SYSINFO_SUBTREE_FUNCTION;
		item->subtree.generator.fn = fn;
		item->subtree.generator.data = data;
	}
	
	mutex_unlock(&sysinfo_lock);
}

/** Sysinfo dump indentation helper routine
 *
 * @param depth Number of spaces to print.
 *
 */
NO_TRACE static void sysinfo_indent(size_t spaces)
{
	for (size_t i = 0; i < spaces; i++)
		printf(" ");
}

/** Dump the structure of sysinfo tree
 *
 * Should be called with sysinfo_lock held.
 *
 * @param root   Root item of the current (sub)tree.
 * @param spaces Current indentation level.
 *
 */
NO_TRACE static void sysinfo_dump_internal(sysinfo_item_t *root, size_t spaces)
{
	/* Walk all siblings */
	for (sysinfo_item_t *cur = root; cur; cur = cur->next) {
		size_t length;
		
		if (spaces == 0) {
			printf("%s", cur->name);
			length = str_length(cur->name);
		} else {
			sysinfo_indent(spaces);
			printf(".%s", cur->name);
			length = str_length(cur->name) + 1;
		}
		
		sysarg_t val;
		size_t size;
		
		/* Display node value and type */
		switch (cur->val_type) {
		case SYSINFO_VAL_UNDEFINED:
			printf(" [undefined]\n");
			break;
		case SYSINFO_VAL_VAL:
			printf(" -> %" PRIun" (%#" PRIxn ")\n", cur->val.val,
			    cur->val.val);
			break;
		case SYSINFO_VAL_DATA:
			printf(" (%zu bytes)\n", cur->val.data.size);
			break;
		case SYSINFO_VAL_FUNCTION_VAL:
			val = cur->val.gen_val.fn(cur, cur->val.gen_val.data);
			printf(" -> %" PRIun" (%#" PRIxn ") [generated]\n", val,
			    val);
			break;
		case SYSINFO_VAL_FUNCTION_DATA:
			/* N.B.: No data was actually returned (only a dry run) */
			(void) cur->val.gen_data.fn(cur, &size, true,
			    cur->val.gen_data.data);
			printf(" (%zu bytes) [generated]\n", size);
			break;
		default:
			printf("+ %s [unknown]\n", cur->name);
		}
		
		/* Recursivelly nest into the subtree */
		switch (cur->subtree_type) {
		case SYSINFO_SUBTREE_NONE:
			break;
		case SYSINFO_SUBTREE_TABLE:
			sysinfo_dump_internal(cur->subtree.table, spaces + length);
			break;
		case SYSINFO_SUBTREE_FUNCTION:
			sysinfo_indent(spaces + length);
			printf("<generated subtree>\n");
			break;
		default:
			sysinfo_indent(spaces + length);
			printf("<unknown subtree>\n");
		}
	}
}

/** Dump the structure of sysinfo tree
 *
 * @param root  Root item of the sysinfo (sub)tree.
 *              If it is NULL then consider the global
 *              sysinfo tree.
 *
 */
void sysinfo_dump(sysinfo_item_t *root)
{
	/* Avoid other functions to mess with sysinfo
	   while we are dumping it */
	mutex_lock(&sysinfo_lock);
	
	if (root == NULL)
		sysinfo_dump_internal(global_root, 0);
	else
		sysinfo_dump_internal(root, 0);
	
	mutex_unlock(&sysinfo_lock);
}

/** Return sysinfo item value determined by name
 *
 * Should be called with sysinfo_lock held.
 *
 * @param name    Sysinfo path.
 * @param root    Root item of the sysinfo (sub)tree.
 *                If it is NULL then consider the global
 *                sysinfo tree.
 * @param dry_run Do not actually get any generated
 *                binary data, just calculate the size.
 *
 * @return Item value (constant or generated).
 *
 */
NO_TRACE static sysinfo_return_t sysinfo_get_item(const char *name,
    sysinfo_item_t **root, bool dry_run)
{
	if (root == NULL)
		root = &global_root;
	
	/* Try to find the item or generate data */
	sysinfo_return_t ret;
	sysinfo_return_t *ret_ptr = &ret;
	sysinfo_item_t *item = sysinfo_find_item(name, *root, &ret_ptr,
	    dry_run);
	
	if (item != NULL) {
		/* Item found in the fixed sysinfo tree */
		
		ret.tag = item->val_type;
		switch (item->val_type) {
		case SYSINFO_VAL_UNDEFINED:
			break;
		case SYSINFO_VAL_VAL:
			ret.val = item->val.val;
			break;
		case SYSINFO_VAL_DATA:
			ret.data = item->val.data;
			break;
		case SYSINFO_VAL_FUNCTION_VAL:
			ret.val = item->val.gen_val.fn(item, item->val.gen_val.data);
			break;
		case SYSINFO_VAL_FUNCTION_DATA:
			ret.data.data = item->val.gen_data.fn(item, &ret.data.size,
			    dry_run, item->val.gen_data.data);
			break;
		}
	} else {
		/* No item in the fixed sysinfo tree */
		if (ret_ptr == NULL) {
			/* Even no data was generated */
			ret.tag = SYSINFO_VAL_UNDEFINED;
		}
	}
	
	return ret;
}

/** Return sysinfo item determined by name from user space
 *
 * The path string passed from the user space has to be properly null-terminated
 * (the last passed character must be null).
 *
 * @param ptr     Sysinfo path in the user address space.
 * @param size    Size of the path string.
 * @param dry_run Do not actually get any generated
 *                binary data, just calculate the size.
 *
 */
NO_TRACE static sysinfo_return_t sysinfo_get_item_uspace(void *ptr, size_t size,
    bool dry_run)
{
	sysinfo_return_t ret;
	ret.tag = SYSINFO_VAL_UNDEFINED;
	
	if (size > SYSINFO_MAX_PATH)
		return ret;
	
	char *path = (char *) malloc(size + 1, 0);
	assert(path);
	
	if ((copy_from_uspace(path, ptr, size + 1) == 0) &&
	    (path[size] == 0)) {
		/*
		 * Prevent other functions from messing with sysinfo while we
		 * are reading it.
		 */
		mutex_lock(&sysinfo_lock);
		ret = sysinfo_get_item(path, NULL, dry_run);
		mutex_unlock(&sysinfo_lock);
	}
	
	free(path);
	return ret;
}

/** Return sysinfo keys determined by name
 *
 * Should be called with sysinfo_lock held.
 *
 * @param name    Sysinfo path.
 * @param root    Root item of the sysinfo (sub)tree.
 *                If it is NULL then consider the global
 *                sysinfo tree.
 * @param dry_run Do not actually get any generated
 *                binary data, just calculate the size.
 *
 * @return Item value (constant or generated).
 *
 */
NO_TRACE static sysinfo_return_t sysinfo_get_keys(const char *name,
    sysinfo_item_t **root, bool dry_run)
{
	if (root == NULL)
		root = &global_root;
	
	sysinfo_item_t *subtree = NULL;
	
	if (name[0] != 0) {
		/* Try to find the item */
		sysinfo_item_t *item =
		    sysinfo_find_item(name, *root, NULL, dry_run);
		if ((item != NULL) &&
		    (item->subtree_type == SYSINFO_SUBTREE_TABLE))
			subtree = item->subtree.table;
	} else
		subtree = *root;
	
	sysinfo_return_t ret;
	ret.tag = SYSINFO_VAL_UNDEFINED;
	
	if (subtree != NULL) {
		/*
		 * Calculate the size of subkeys.
		 */
		size_t size = 0;
		for (sysinfo_item_t *cur = subtree; cur; cur = cur->next)
			size += str_size(cur->name) + 1;
		
		if (dry_run) {
			ret.tag = SYSINFO_VAL_DATA;
			ret.data.data = NULL;
			ret.data.size = size;
		} else {
			/* Allocate buffer for subkeys */
			char *names = (char *) malloc(size, FRAME_ATOMIC);
			if (names == NULL)
				return ret;
			
			size_t pos = 0;
			for (sysinfo_item_t *cur = subtree; cur; cur = cur->next) {
				str_cpy(names + pos, size - pos, cur->name);
				pos += str_size(cur->name) + 1;
			}
			
			/* Correct return value */
			ret.tag = SYSINFO_VAL_DATA;
			ret.data.data = (void *) names;
			ret.data.size = size;
		}
	}
	
	return ret;
}

/** Return sysinfo keys determined by name from user space
 *
 * The path string passed from the user space has to be properly
 * null-terminated (the last passed character must be null).
 *
 * @param ptr     Sysinfo path in the user address space.
 * @param size    Size of the path string.
 * @param dry_run Do not actually get any generated
 *                binary data, just calculate the size.
 *
 */
NO_TRACE static sysinfo_return_t sysinfo_get_keys_uspace(void *ptr, size_t size,
    bool dry_run)
{
	sysinfo_return_t ret;
	ret.tag = SYSINFO_VAL_UNDEFINED;
	ret.data.data = NULL;
	ret.data.size = 0;
	
	if (size > SYSINFO_MAX_PATH)
		return ret;
	
	char *path = (char *) malloc(size + 1, 0);
	assert(path);
	
	if ((copy_from_uspace(path, ptr, size + 1) == 0) &&
	    (path[size] == 0)) {
		/*
		 * Prevent other functions from messing with sysinfo while we
		 * are reading it.
		 */
		mutex_lock(&sysinfo_lock);
		ret = sysinfo_get_keys(path, NULL, dry_run);
		mutex_unlock(&sysinfo_lock);
	}
	
	free(path);
	return ret;
}

/** Get the sysinfo keys size (syscall)
 *
 * The path string passed from the user space has
 * to be properly null-terminated (the last passed
 * character must be null).
 *
 * @param path_ptr  Sysinfo path in the user address space.
 * @param path_size Size of the path string.
 * @param size_ptr  User space pointer where to store the
 *                  keys size.
 *
 * @return Error code (EOK in case of no error).
 *
 */
sysarg_t sys_sysinfo_get_keys_size(void *path_ptr, size_t path_size,
    void *size_ptr)
{
	int rc;
	
	/*
	 * Get the keys.
	 *
	 * N.B.: There is no need to free any potential keys
	 * since we request a dry run.
	 */
	sysinfo_return_t ret =
	    sysinfo_get_keys_uspace(path_ptr, path_size, true);
	
	/* Check return data tag */
	if (ret.tag == SYSINFO_VAL_DATA)
		rc = copy_to_uspace(size_ptr, &ret.data.size,
		    sizeof(ret.data.size));
	else
		rc = EINVAL;
	
	return (sysarg_t) rc;
}

/** Get the sysinfo keys (syscall)
 *
 * The path string passed from the user space has
 * to be properly null-terminated (the last passed
 * character must be null).
 *
 * If the user space buffer size does not equal
 * the actual size of the returned data, the data
 * is truncated.
 *
 * The actual size of data returned is stored to
 * size_ptr.
 *
 * @param path_ptr    Sysinfo path in the user address space.
 * @param path_size   Size of the path string.
 * @param buffer_ptr  User space pointer to the buffer where
 *                    to store the binary data.
 * @param buffer_size User space buffer size.
 * @param size_ptr    User space pointer where to store the
 *                    binary data size.
 *
 * @return Error code (EOK in case of no error).
 *
 */
sysarg_t sys_sysinfo_get_keys(void *path_ptr, size_t path_size,
    void *buffer_ptr, size_t buffer_size, size_t *size_ptr)
{
	int rc;
	
	/* Get the keys */
	sysinfo_return_t ret = sysinfo_get_keys_uspace(path_ptr, path_size,
	    false);
	
	/* Check return data tag */
	if (ret.tag == SYSINFO_VAL_DATA) {
		size_t size = min(ret.data.size, buffer_size);
		rc = copy_to_uspace(buffer_ptr, ret.data.data, size);
		if (rc == EOK)
			rc = copy_to_uspace(size_ptr, &size, sizeof(size));
		
		free(ret.data.data);
	} else
		rc = EINVAL;
	
	return (sysarg_t) rc;
}

/** Get the sysinfo value type (syscall)
 *
 * The path string passed from the user space has
 * to be properly null-terminated (the last passed
 * character must be null).
 *
 * @param path_ptr  Sysinfo path in the user address space.
 * @param path_size Size of the path string.
 *
 * @return Item value type.
 *
 */
sysarg_t sys_sysinfo_get_val_type(void *path_ptr, size_t path_size)
{
	/*
	 * Get the item.
	 *
	 * N.B.: There is no need to free any potential generated
	 * binary data since we request a dry run.
	 */
	sysinfo_return_t ret = sysinfo_get_item_uspace(path_ptr, path_size, true);
	
	/*
	 * Map generated value types to constant types (user space does
	 * not care whether the value is constant or generated).
	 */
	if (ret.tag == SYSINFO_VAL_FUNCTION_VAL)
		ret.tag = SYSINFO_VAL_VAL;
	else if (ret.tag == SYSINFO_VAL_FUNCTION_DATA)
		ret.tag = SYSINFO_VAL_DATA;
	
	return (sysarg_t) ret.tag;
}

/** Get the sysinfo numerical value (syscall)
 *
 * The path string passed from the user space has
 * to be properly null-terminated (the last passed
 * character must be null).
 *
 * @param path_ptr  Sysinfo path in the user address space.
 * @param path_size Size of the path string.
 * @param value_ptr User space pointer where to store the
 *                  numberical value.
 *
 * @return Error code (EOK in case of no error).
 *
 */
sysarg_t sys_sysinfo_get_value(void *path_ptr, size_t path_size,
    void *value_ptr)
{
	int rc;
	
	/*
	 * Get the item.
	 *
	 * N.B.: There is no need to free any potential generated binary
	 * data since we request a dry run.
	 */
	sysinfo_return_t ret = sysinfo_get_item_uspace(path_ptr, path_size, true);
	
	/* Only constant or generated numerical value is returned */
	if ((ret.tag == SYSINFO_VAL_VAL) || (ret.tag == SYSINFO_VAL_FUNCTION_VAL))
		rc = copy_to_uspace(value_ptr, &ret.val, sizeof(ret.val));
	else
		rc = EINVAL;
	
	return (sysarg_t) rc;
}

/** Get the sysinfo binary data size (syscall)
 *
 * The path string passed from the user space has
 * to be properly null-terminated (the last passed
 * character must be null).
 *
 * @param path_ptr  Sysinfo path in the user address space.
 * @param path_size Size of the path string.
 * @param size_ptr  User space pointer where to store the
 *                  binary data size.
 *
 * @return Error code (EOK in case of no error).
 *
 */
sysarg_t sys_sysinfo_get_data_size(void *path_ptr, size_t path_size,
    void *size_ptr)
{
	int rc;
	
	/*
	 * Get the item.
	 *
	 * N.B.: There is no need to free any potential generated binary
	 * data since we request a dry run.
	 */
	sysinfo_return_t ret = sysinfo_get_item_uspace(path_ptr, path_size, true);
	
	/* Only the size of constant or generated binary data is considered */
	if ((ret.tag == SYSINFO_VAL_DATA) || (ret.tag == SYSINFO_VAL_FUNCTION_DATA))
		rc = copy_to_uspace(size_ptr, &ret.data.size,
		    sizeof(ret.data.size));
	else
		rc = EINVAL;
	
	return (sysarg_t) rc;
}

/** Get the sysinfo binary data (syscall)
 *
 * The path string passed from the user space has
 * to be properly null-terminated (the last passed
 * character must be null).
 *
 * If the user space buffer size does not equal
 * the actual size of the returned data, the data
 * is truncated. Whether this is actually a fatal
 * error or the data can be still interpreted as valid
 * depends on the nature of the data and has to be
 * decided by the user space.
 *
 * The actual size of data returned is stored to
 * size_ptr.
 *
 * @param path_ptr    Sysinfo path in the user address space.
 * @param path_size   Size of the path string.
 * @param buffer_ptr  User space pointer to the buffer where
 *                    to store the binary data.
 * @param buffer_size User space buffer size.
 * @param size_ptr    User space pointer where to store the
 *                    binary data size.
 *
 * @return Error code (EOK in case of no error).
 *
 */
sysarg_t sys_sysinfo_get_data(void *path_ptr, size_t path_size,
    void *buffer_ptr, size_t buffer_size, size_t *size_ptr)
{
	int rc;
	
	/* Get the item */
	sysinfo_return_t ret = sysinfo_get_item_uspace(path_ptr, path_size,
	    false);
	
	/* Only constant or generated binary data is considered */
	if ((ret.tag == SYSINFO_VAL_DATA) ||
	    (ret.tag == SYSINFO_VAL_FUNCTION_DATA)) {
		size_t size = min(ret.data.size, buffer_size);
		rc = copy_to_uspace(buffer_ptr, ret.data.data, size);
		if (rc == EOK)
			rc = copy_to_uspace(size_ptr, &size, sizeof(size));
	} else
		rc = EINVAL;
	
	/* N.B.: The generated binary data should be freed */
	if ((ret.tag == SYSINFO_VAL_FUNCTION_DATA) && (ret.data.data != NULL))
		free(ret.data.data);
	
	return (sysarg_t) rc;
}

/** @}
 */
