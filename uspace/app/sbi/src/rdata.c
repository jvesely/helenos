/*
 * Copyright (c) 2010 Jiri Svoboda
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

/** @file Run-time data representation.
 *
 * At run time SBI represents all data as a graph of interconnected @c var
 * nodes (variable nodes). Any piece of memory addressable by the program
 * (i.e. all variables) are stored in var nodes. However, var nodes are also
 * used internally to implement value items. (I.e. values in value items
 * have exactly the same structure as program variables).
 *
 * Unlike byte- or word-oriented memory on a real machine, var nodes provide
 * structured and typed storage. (This typing is dynamic, however and has
 * nothing to do with the static type system).
 *
 * There are several types of var nodes, one for each primitive type,
 * reference, delegate, array, and object. A reference var node contains
 * a pointer to another var node. Delegate var node points to some stree
 * declaration. Array and object var nodes refer to a collection of child
 * nodes (fields, elements).
 */

#include <stdlib.h>
#include <assert.h>
#include "bigint.h"
#include "mytypes.h"
#include "stree.h"

#include "rdata.h"

static void rdata_int_copy(rdata_int_t *src, rdata_int_t **dest);
static void rdata_string_copy(rdata_string_t *src, rdata_string_t **dest);
static void rdata_ref_copy(rdata_ref_t *src, rdata_ref_t **dest);
static void rdata_deleg_copy(rdata_deleg_t *src, rdata_deleg_t **dest);
static void rdata_array_copy(rdata_array_t *src, rdata_array_t **dest);
static void rdata_object_copy(rdata_object_t *src, rdata_object_t **dest);
static void rdata_resource_copy(rdata_resource_t *src,
    rdata_resource_t **dest);

static int rdata_array_get_dim(rdata_array_t *array);

static void rdata_address_print(rdata_address_t *address);
static void rdata_var_print(rdata_var_t *var);

/** Allocate new data item.
 *
 * @param ic	Item class.
 * @return	New item.
 */
rdata_item_t *rdata_item_new(item_class_t ic)
{
	rdata_item_t *item;

	item = calloc(1, sizeof(rdata_item_t));
	if (item == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	item->ic = ic;
	return item;
}

/** Allocate new address.
 *
 * @return	New address.
 */
rdata_addr_var_t *rdata_addr_var_new(void)
{
	rdata_addr_var_t *addr_var;

	addr_var = calloc(1, sizeof(rdata_addr_var_t));
	if (addr_var == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return addr_var;
}

/** Allocate new named property address.
 *
 * @return	New named property address.
 */
rdata_aprop_named_t *rdata_aprop_named_new(void)
{
	rdata_aprop_named_t *aprop_named;

	aprop_named = calloc(1, sizeof(rdata_aprop_named_t));
	if (aprop_named == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return aprop_named;
}

/** Allocate new indexed property address.
 *
 * @return	New indexed property address.
 */
rdata_aprop_indexed_t *rdata_aprop_indexed_new(void)
{
	rdata_aprop_indexed_t *aprop_indexed;

	aprop_indexed = calloc(1, sizeof(rdata_aprop_indexed_t));
	if (aprop_indexed == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return aprop_indexed;
}

/** Allocate new property address.
 *
 * @param apc	Property address class.
 * @return	New property address.
 */
rdata_addr_prop_t *rdata_addr_prop_new(aprop_class_t apc)
{
	rdata_addr_prop_t *addr_prop;

	addr_prop = calloc(1, sizeof(rdata_addr_prop_t));
	if (addr_prop == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	addr_prop->apc = apc;
	return addr_prop;
}

/** Allocate new address.
 *
 * @param ac	Address class.
 * @return	New address.
 */
rdata_address_t *rdata_address_new(address_class_t ac)
{
	rdata_address_t *address;

	address = calloc(1, sizeof(rdata_address_t));
	if (address == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	address->ac = ac;
	return address;
}

/** Allocate new value.
 *
 * @return	New value.
 */
rdata_value_t *rdata_value_new(void)
{
	rdata_value_t *value;

	value = calloc(1, sizeof(rdata_value_t));
	if (value == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return value;
}

/** Allocate new var node.
 *
 * @param vc	Var node class (varclass).
 * @return	New var node.
 */
rdata_var_t *rdata_var_new(var_class_t vc)
{
	rdata_var_t *var;

	var = calloc(1, sizeof(rdata_var_t));
	if (var == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	var->vc = vc;
	return var;
}

/** Allocate new reference.
 *
 * @return	New reference.
 */
rdata_ref_t *rdata_ref_new(void)
{
	rdata_ref_t *ref;

	ref = calloc(1, sizeof(rdata_ref_t));
	if (ref == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return ref;
}

/** Allocate new delegate.
 *
 * @return	New delegate.
 */
rdata_deleg_t *rdata_deleg_new(void)
{
	rdata_deleg_t *deleg;

	deleg = calloc(1, sizeof(rdata_deleg_t));
	if (deleg == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return deleg;
}

/** Allocate new array.
 *
 * @return	New array.
 */
rdata_array_t *rdata_array_new(int rank)
{
	rdata_array_t *array;

	array = calloc(1, sizeof(rdata_array_t));
	if (array == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	array->rank = rank;
	array->extent = calloc(rank, sizeof(int));
	if (array == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return array;
}

/** Allocate new object.
 *
 * @return	New object.
 */
rdata_object_t *rdata_object_new(void)
{
	rdata_object_t *object;

	object = calloc(1, sizeof(rdata_object_t));
	if (object == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return object;
}

/** Allocate new integer.
 *
 * @return	New integer.
 */
rdata_int_t *rdata_int_new(void)
{
	rdata_int_t *int_v;

	int_v = calloc(1, sizeof(rdata_int_t));
	if (int_v == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return int_v;
}

/** Allocate new string.
 *
 * @return	New string.
 */
rdata_string_t *rdata_string_new(void)
{
	rdata_string_t *string_v;

	string_v = calloc(1, sizeof(rdata_string_t));
	if (string_v == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return string_v;
}

/** Allocate new resource.
 *
 * @return	New resource.
 */
rdata_resource_t *rdata_resource_new(void)
{
	rdata_resource_t *resource_v;

	resource_v = calloc(1, sizeof(rdata_resource_t));
	if (resource_v == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	return resource_v;
}

/** Allocate array elements.
 *
 * Allocates var nodes for elements of @a array.
 *
 * @param array		Array.
 */
void rdata_array_alloc_element(rdata_array_t *array)
{
	int dim, idx;

	dim = rdata_array_get_dim(array);

	array->element = calloc(dim, sizeof(rdata_var_t *));
	if (array->element == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	for (idx = 0; idx < dim; ++idx) {
		array->element[idx] = calloc(1, sizeof(rdata_var_t));
		if (array->element[idx] == NULL) {
			printf("Memory allocation failed.\n");
			exit(1);
		}
	}
}

/** Get array dimension.
 *
 * Dimension is the total number of elements in an array, in other words,
 * the product of all extents.
 *
 * @param array		Array.
 */
static int rdata_array_get_dim(rdata_array_t *array)
{
	int didx, dim;

	dim = 1;
	for (didx = 0; didx < array->rank; ++didx)
		dim = dim * array->extent[didx];

	return dim;
}

/** Make copy of a variable.
 *
 * Creates a new var node that is an exact copy of an existing var node.
 * This can be thought of as a shallow copy.
 *
 * @param src		Source var node.
 * @param dest		Place to store pointer to new var node.
 */
void rdata_var_copy(rdata_var_t *src, rdata_var_t **dest)
{
	rdata_var_t *nvar;

	nvar = rdata_var_new(src->vc);

	switch (src->vc) {
	case vc_int:
		rdata_int_copy(src->u.int_v, &nvar->u.int_v);
		break;
	case vc_string:
		rdata_string_copy(src->u.string_v, &nvar->u.string_v);
		break;
	case vc_ref:
		rdata_ref_copy(src->u.ref_v, &nvar->u.ref_v);
		break;
	case vc_deleg:
		rdata_deleg_copy(src->u.deleg_v, &nvar->u.deleg_v);
		break;
	case vc_array:
		rdata_array_copy(src->u.array_v, &nvar->u.array_v);
		break;
	case vc_object:
		rdata_object_copy(src->u.object_v, &nvar->u.object_v);
		break;
	case vc_resource:
		rdata_resource_copy(src->u.resource_v, &nvar->u.resource_v);
		break;
	}

	*dest = nvar;
}

/** Copy integer.
 *
 * @param src		Source integer.
 * @param dest		Place to store pointer to new integer.
 */
static void rdata_int_copy(rdata_int_t *src, rdata_int_t **dest)
{
	*dest = rdata_int_new();
	bigint_clone(&src->value, &(*dest)->value);
}

/** Copy string.
 *
 * @param src		Source string.
 * @param dest		Place to store pointer to new string.
 */
static void rdata_string_copy(rdata_string_t *src, rdata_string_t **dest)
{
	*dest = rdata_string_new();
	(*dest)->value = src->value;
}

/** Copy reference.
 *
 * @param src		Source reference.
 * @param dest		Place to store pointer to new reference.
 */
static void rdata_ref_copy(rdata_ref_t *src, rdata_ref_t **dest)
{
	*dest = rdata_ref_new();
	(*dest)->vref = src->vref;
}

/** Copy delegate.
 *
 * @param src		Source delegate.
 * @param dest		Place to store pointer to new delegate.
 */
static void rdata_deleg_copy(rdata_deleg_t *src, rdata_deleg_t **dest)
{
	(void) src; (void) dest;
	printf("Unimplemented: Copy delegate.\n");
	exit(1);
}

/** Copy array.
 *
 * @param src		Source array.
 * @param dest		Place to store pointer to new array.
 */
static void rdata_array_copy(rdata_array_t *src, rdata_array_t **dest)
{
	(void) src; (void) dest;
	printf("Unimplemented: Copy array.\n");
	exit(1);
}

/** Copy object.
 *
 * @param src		Source object.
 * @param dest		Place to store pointer to new object.
 */
static void rdata_object_copy(rdata_object_t *src, rdata_object_t **dest)
{
	(void) src; (void) dest;
	printf("Unimplemented: Copy object.\n");
	exit(1);
}

/** Copy resource.
 *
 * @param src		Source resource.
 * @param dest		Place to store pointer to new resource.
 */
static void rdata_resource_copy(rdata_resource_t *src, rdata_resource_t **dest)
{
	*dest = rdata_resource_new();
	(*dest)->data = src->data;
}

/** Read data from a variable.
 *
 * This copies data from the variable to a value item. Ideally any read access
 * to a program variable should go through this function. (Keep in mind
 * that although values are composed of var nodes internally, but are not
 * variables per se. Therefore this function is not used to read from values)
 *
 * @param var		Variable to read from (var node where it is stored).
 * @param ritem		Place to store pointer to new value item read from
 *			the variable.
 */
void rdata_var_read(rdata_var_t *var, rdata_item_t **ritem)
{
	rdata_value_t *value;
	rdata_var_t *rvar;

	/* Perform a shallow copy of @a var. */
	rdata_var_copy(var, &rvar);

	value = rdata_value_new();
	value->var = rvar;
	*ritem = rdata_item_new(ic_value);
	(*ritem)->u.value = value;
}

/** Write data to a variable.
 *
 * This copies data to the variable from a value. Ideally any write access
 * to a program variable should go through this function. (Keep in mind
 * that even though values are composed of var nodes internally, but are not
 * variables per se. Therefore this function is not used to write to values)
 *
 * @param var		Variable to write to (var node where it is stored).
 * @param value		The value to write.
 */
void rdata_var_write(rdata_var_t *var, rdata_value_t *value)
{
	rdata_var_t *nvar;

	/* Perform a shallow copy of @c value->var. */
	rdata_var_copy(value->var, &nvar);

	/* XXX do this in a prettier way. */

	var->vc = nvar->vc;
	switch (nvar->vc) {
	case vc_int: var->u.int_v = nvar->u.int_v; break;
	case vc_string: var->u.string_v = nvar->u.string_v; break;
	case vc_ref: var->u.ref_v = nvar->u.ref_v; break;
	case vc_deleg: var->u.deleg_v = nvar->u.deleg_v; break;
	case vc_array: var->u.array_v = nvar->u.array_v; break;
	case vc_object: var->u.object_v = nvar->u.object_v; break;
	case vc_resource: var->u.resource_v = nvar->u.resource_v; break;
	}

	/* XXX We should free some stuff around here. */
}

/** Print data item in human-readable form.
 *
 * @param item		Item to print.
 */
void rdata_item_print(rdata_item_t *item)
{
	if (item == NULL) {
		printf("none");
		return;
	}

	switch (item->ic) {
	case ic_address:
		printf("address:");
		rdata_address_print(item->u.address);
		break;
	case ic_value:
		printf("value:");
		rdata_value_print(item->u.value);
		break;
	}
}

/** Print address in human-readable form.
 *
 * Actually this displays contents of the var node that is being addressed.
 *
 * XXX Maybe we should really rather print the address and not the data
 * it is pointing to?
 *
 * @param item		Address to print.
 */
static void rdata_address_print(rdata_address_t *address)
{
	switch (address->ac) {
	case ac_var:
		rdata_var_print(address->u.var_a->vref);
		break;
	case ac_prop:
		printf("Warning: Unimplemented: Print property address.\n");
		break;
	}
}

/** Print value in human-readable form.
 *
 * @param value		Value to print.
 */
void rdata_value_print(rdata_value_t *value)
{
	rdata_var_print(value->var);
}

/** Print contents of var node in human-readable form.
 *
 * @param item		Var node to print.
 */
static void rdata_var_print(rdata_var_t *var)
{
	switch (var->vc) {
	case vc_int:
		printf("int(");
		bigint_print(&var->u.int_v->value);
		printf(")");
		break;
	case vc_string:
		printf("string(\"%s\")", var->u.string_v->value);
		break;
	case vc_ref:
		printf("ref");
		break;
	case vc_deleg:
		printf("deleg");
		break;
	case vc_object:
		printf("object");
		break;
	default:
		printf("print(%d)\n", var->vc);
		assert(b_false);
	}
}
