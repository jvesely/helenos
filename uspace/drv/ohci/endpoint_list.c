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
/** @addtogroup drvusbohci
 * @{
 */
/** @file
 * @brief OHCI driver transfer list implementation
 */
#include <errno.h>
#include <usb/debug.h>

#include "endpoint_list.h"

/** Initialize transfer list structures.
 *
 * @param[in] instance Memory place to use.
 * @param[in] name Name of the new list.
 * @return Error code
 *
 * Allocates memory for internal qh_t structure.
 */
int endpoint_list_init(endpoint_list_t *instance, const char *name)
{
	assert(instance);
	instance->name = name;
	instance->list_head = malloc32(sizeof(ed_t));
	if (!instance->list_head) {
		usb_log_error("Failed to allocate list head.\n");
		return ENOMEM;
	}
	instance->list_head_pa = addr_to_phys(instance->list_head);
	usb_log_debug2("Transfer list %s setup with ED: %p(%p).\n",
	    name, instance->list_head, instance->list_head_pa);

	ed_init(instance->list_head, NULL);
	list_initialize(&instance->endpoint_list);
	fibril_mutex_initialize(&instance->guard);
	return EOK;
}
/*----------------------------------------------------------------------------*/
/** Set the next list in transfer list chain.
 *
 * @param[in] instance List to lead.
 * @param[in] next List to append.
 * @return Error code
 *
 * Does not check whether this replaces an existing list .
 */
void endpoint_list_set_next(endpoint_list_t *instance, endpoint_list_t *next)
{
	assert(instance);
	assert(next);
	ed_append_ed(instance->list_head, next->list_head);
}
/*----------------------------------------------------------------------------*/
/** Submit transfer endpoint to the list and queue.
 *
 * @param[in] instance List to use.
 * @param[in] endpoint Transfer endpoint to submit.
 * @return Error code
 *
 * The endpoint is added to the end of the list and queue.
 */
void endpoint_list_add_ep(endpoint_list_t *instance, hcd_endpoint_t *hcd_ep)
{
	assert(instance);
	assert(hcd_ep);
	usb_log_debug2("Queue %s: Adding endpoint(%p).\n",
	    instance->name, hcd_ep);

	fibril_mutex_lock(&instance->guard);

	ed_t *last_ed = NULL;
	/* Add to the hardware queue. */
	if (list_empty(&instance->endpoint_list)) {
		/* There is nothing scheduled */
		last_ed = instance->list_head;
	} else {
		/* There is something scheduled */
		hcd_endpoint_t *last = list_get_instance(
		    instance->endpoint_list.prev, hcd_endpoint_t, link);
		last_ed = last->ed;
	}
	/* keep link */
	hcd_ep->ed->next = last_ed->next;
	ed_append_ed(last_ed, hcd_ep->ed);

	asm volatile ("": : :"memory");

	/* Add to the driver list */
	list_append(&hcd_ep->link, &instance->endpoint_list);

	hcd_endpoint_t *first = list_get_instance(
	    instance->endpoint_list.next, hcd_endpoint_t, link);
	usb_log_debug("HCD EP(%p) added to list %s, first is %p(%p).\n",
		hcd_ep, instance->name, first, first->ed);
	if (last_ed == instance->list_head) {
		usb_log_debug2("%s head ED(%p-%p): %x:%x:%x:%x.\n",
		    instance->name, last_ed, instance->list_head_pa,
		    last_ed->status, last_ed->td_tail, last_ed->td_head,
		    last_ed->next);
	}
	fibril_mutex_unlock(&instance->guard);
}
/*----------------------------------------------------------------------------*/
#if 0
/** Create list for finished endpoints.
 *
 * @param[in] instance List to use.
 * @param[in] done list to fill
 */
void endpoint_list_remove_finished(endpoint_list_t *instance, link_t *done)
{
	assert(instance);
	assert(done);

	fibril_mutex_lock(&instance->guard);
	usb_log_debug2("Checking list %s for completed endpointes(%d).\n",
	    instance->name, list_count(&instance->endpoint_list));
	link_t *current = instance->endpoint_list.next;
	while (current != &instance->endpoint_list) {
		link_t *next = current->next;
		hcd_endpoint_t *endpoint =
		    list_get_instance(current, hcd_endpoint_t, link);

		if (endpoint_is_complete(endpoint)) {
			/* Save for post-processing */
			endpoint_list_remove_endpoint(instance, endpoint);
			list_append(current, done);
		}
		current = next;
	}
	fibril_mutex_unlock(&instance->guard);
}
/*----------------------------------------------------------------------------*/
/** Walk the list and abort all endpointes.
 *
 * @param[in] instance List to use.
 */
void endpoint_list_abort_all(endpoint_list_t *instance)
{
	fibril_mutex_lock(&instance->guard);
	while (!list_empty(&instance->endpoint_list)) {
		link_t *current = instance->endpoint_list.next;
		hcd_endpoint_t *endpoint =
		    list_get_instance(current, hcd_endpoint_t, link);
		endpoint_list_remove_endpoint(instance, endpoint);
		hcd_endpoint_finish_error(endpoint, EIO);
	}
	fibril_mutex_unlock(&instance->guard);
}
#endif
/*----------------------------------------------------------------------------*/
/** Remove a transfer endpoint from the list and queue.
 *
 * @param[in] instance List to use.
 * @param[in] endpoint Transfer endpoint to remove.
 * @return Error code
 *
 * Does not lock the transfer list, caller is responsible for that.
 */
void endpoint_list_remove_ep(endpoint_list_t *instance, hcd_endpoint_t *hcd_ep)
{
	assert(instance);
	assert(instance->list_head);
	assert(hcd_ep);
	assert(hcd_ep->ed);

	fibril_mutex_lock(&instance->guard);

	usb_log_debug2(
	    "Queue %s: removing endpoint(%p).\n", instance->name, hcd_ep);

	const char *qpos = NULL;
	ed_t *prev_ed;
	/* Remove from the hardware queue */
	if (instance->endpoint_list.next == &hcd_ep->link) {
		/* I'm the first one here */
		prev_ed = instance->list_head;
		qpos = "FIRST";
	} else {
		hcd_endpoint_t *prev =
		    list_get_instance(hcd_ep->link.prev, hcd_endpoint_t, link);
		prev_ed = prev->ed;
		qpos = "NOT FIRST";
	}
	assert((prev_ed->next & ED_NEXT_PTR_MASK) == addr_to_phys(hcd_ep->ed));
	prev_ed->next = hcd_ep->ed->next;

	asm volatile ("": : :"memory");
	usb_log_debug("HCD EP(%p) removed (%s) from %s, next %x.\n",
	    hcd_ep, qpos, instance->name, hcd_ep->ed->next);

	/* Remove from the endpoint list */
	list_remove(&hcd_ep->link);
	fibril_mutex_unlock(&instance->guard);
}
/**
 * @}
 */
