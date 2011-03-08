/*
 * Copyright (c) 2011 Jan Vesely
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
/** @addtogroup usb
 * @{
 */
/** @file
 * @brief UHCI driver
 */
#include <errno.h>

#include <usb/debug.h>

#include "transfer_list.h"

static void transfer_list_remove_batch(
    transfer_list_t *instance, batch_t *batch);
/*----------------------------------------------------------------------------*/
/** Initializes transfer list structures.
 *
 * @param[in] instance Memory place to use.
 * @param[in] name Name of te new list.
 * @return Error code
 *
 * Allocates memory for internat queue_head_t structure.
 */
int transfer_list_init(transfer_list_t *instance, const char *name)
{
	assert(instance);
	instance->next = NULL;
	instance->name = name;
	instance->queue_head = malloc32(sizeof(queue_head_t));
	if (!instance->queue_head) {
		usb_log_error("Failed to allocate queue head.\n");
		return ENOMEM;
	}
	instance->queue_head_pa = addr_to_phys(instance->queue_head);

	queue_head_init(instance->queue_head);
	list_initialize(&instance->batch_list);
	fibril_mutex_initialize(&instance->guard);
	return EOK;
}
/*----------------------------------------------------------------------------*/
/** Set the next list in chain.
 *
 * @param[in] instance List to lead.
 * @param[in] next List to append.
 * @return Error code
 */
void transfer_list_set_next(transfer_list_t *instance, transfer_list_t *next)
{
	assert(instance);
	assert(next);
	if (!instance->queue_head)
		return;
	queue_head_append_qh(instance->queue_head, next->queue_head_pa);
	instance->queue_head->element = instance->queue_head->next_queue;
}
/*----------------------------------------------------------------------------*/
/** Submits a new transfer batch to list and queue.
 *
 * @param[in] instance List to use.
 * @param[in] batch Transfer batch to submit.
 * @return Error code
 */
void transfer_list_add_batch(transfer_list_t *instance, batch_t *batch)
{
	assert(instance);
	assert(batch);
	usb_log_debug2(
	    "Adding batch(%p) to queue %s.\n", batch, instance->name);

	uint32_t pa = (uintptr_t)addr_to_phys(batch->qh);
	assert((pa & LINK_POINTER_ADDRESS_MASK) == pa);
	pa |= LINK_POINTER_QUEUE_HEAD_FLAG;

	batch->qh->next_queue = instance->queue_head->next_queue;

	fibril_mutex_lock(&instance->guard);

	if (instance->queue_head->element == instance->queue_head->next_queue) {
		/* there is nothing scheduled */
		list_append(&batch->link, &instance->batch_list);
		instance->queue_head->element = pa;
		usb_log_debug("Batch(%p) added to queue %s first.\n",
			batch, instance->name);
		fibril_mutex_unlock(&instance->guard);
		return;
	}
	/* now we can be sure that there is someting scheduled */
	assert(!list_empty(&instance->batch_list));
	batch_t *first = list_get_instance(
	          instance->batch_list.next, batch_t, link);
	batch_t *last = list_get_instance(
	    instance->batch_list.prev, batch_t, link);
	queue_head_append_qh(last->qh, pa);
	list_append(&batch->link, &instance->batch_list);

	usb_log_debug("Batch(%p) added to queue %s last, first is %p.\n",
		batch, instance->name, first);
	fibril_mutex_unlock(&instance->guard);
}
/*----------------------------------------------------------------------------*/
/** Removes a transfer batch from list and queue.
 *
 * @param[in] instance List to use.
 * @param[in] batch Transfer batch to remove.
 * @return Error code
 */
void transfer_list_remove_batch(transfer_list_t *instance, batch_t *batch)
{
	assert(instance);
	assert(batch);
	assert(instance->queue_head);
	assert(batch->qh);
	usb_log_debug2(
	    "Removing batch(%p) from queue %s.\n", batch, instance->name);

	if (batch->link.prev == &instance->batch_list) {
		/* I'm the first one here */
		usb_log_debug(
		    "Batch(%p) removed (FIRST) from %s, next element %x.\n",
		    batch, instance->name, batch->qh->next_queue);
		instance->queue_head->element = batch->qh->next_queue;
	} else {
		usb_log_debug(
		    "Batch(%p) removed (FIRST:NO) from %s, next element %x.\n",
		    batch, instance->name, batch->qh->next_queue);
		batch_t *prev =
		    list_get_instance(batch->link.prev, batch_t, link);
		prev->qh->next_queue = batch->qh->next_queue;
	}
	list_remove(&batch->link);
}
/*----------------------------------------------------------------------------*/
/** Checks list for finished transfers.
 *
 * @param[in] instance List to use.
 * @return Error code
 */
void transfer_list_remove_finished(transfer_list_t *instance)
{
	assert(instance);

	LIST_INITIALIZE(done);

	fibril_mutex_lock(&instance->guard);
	link_t *current = instance->batch_list.next;
	while (current != &instance->batch_list) {
		link_t *next = current->next;
		batch_t *batch = list_get_instance(current, batch_t, link);

		if (batch_is_complete(batch)) {
			transfer_list_remove_batch(instance, batch);
			list_append(current, &done);
		}
		current = next;
	}
	fibril_mutex_unlock(&instance->guard);

	while (!list_empty(&done)) {
		link_t *item = done.next;
		list_remove(item);
		batch_t *batch = list_get_instance(item, batch_t, link);
		batch->next_step(batch);
	}
}
/**
 * @}
 */
