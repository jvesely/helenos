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
 * @brief OHCI driver USB transaction structure
 */
#include <errno.h>
#include <str_error.h>

#include <usb/usb.h>
#include <usb/debug.h>

#include "ohci_batch.h"
#include "ohci_endpoint.h"
#include "utils/malloc32.h"

void (*const batch_setup[4][3])(ohci_transfer_batch_t*);
/*----------------------------------------------------------------------------*/
/** Safely destructs ohci_transfer_batch_t structure
 *
 * @param[in] ohci_batch Instance to destroy.
 */
static void ohci_transfer_batch_dispose(ohci_transfer_batch_t *ohci_batch)
{
	if (!ohci_batch)
		return;
	unsigned i = 0;
	if (ohci_batch->tds) {
		for (; i< ohci_batch->td_count; ++i) {
			if (i != ohci_batch->leave_td)
				free32(ohci_batch->tds[i]);
		}
		free(ohci_batch->tds);
	}
	usb_transfer_batch_dispose(ohci_batch->usb_batch);
	free32(ohci_batch->device_buffer);
	free(ohci_batch);
}
/*----------------------------------------------------------------------------*/
void ohci_transfer_batch_finish_dispose(ohci_transfer_batch_t *ohci_batch)
{
	assert(ohci_batch);
	assert(ohci_batch->usb_batch);
	usb_transfer_batch_finish(ohci_batch->usb_batch,
	    ohci_batch->device_buffer + ohci_batch->usb_batch->setup_size,
	    ohci_batch->usb_batch->buffer_size);
	ohci_transfer_batch_dispose(ohci_batch);
}
/*----------------------------------------------------------------------------*/
ohci_transfer_batch_t * ohci_transfer_batch_get(usb_transfer_batch_t *usb_batch)
{
	assert(usb_batch);
#define CHECK_NULL_DISPOSE_RET(ptr, message...) \
if (ptr == NULL) { \
	usb_log_error(message); \
	ohci_transfer_batch_dispose(ohci_batch); \
	return NULL; \
} else (void)0

	ohci_transfer_batch_t *ohci_batch =
	    calloc(sizeof(ohci_transfer_batch_t), 1);
	CHECK_NULL_DISPOSE_RET(ohci_batch,
	    "Failed to allocate OHCI batch data.\n");
	link_initialize(&ohci_batch->link);
	ohci_batch->td_count =
	    (usb_batch->buffer_size + OHCI_TD_MAX_TRANSFER - 1)
	    / OHCI_TD_MAX_TRANSFER;
	/* Control transfer need Setup and Status stage */
	if (usb_batch->ep->transfer_type == USB_TRANSFER_CONTROL) {
		ohci_batch->td_count += 2;
	}

	/* We need an extra place for TD that was left at ED */
	ohci_batch->tds = calloc(ohci_batch->td_count + 1, sizeof(td_t*));
	CHECK_NULL_DISPOSE_RET(ohci_batch->tds,
	    "Failed to allocate OHCI transfer descriptors.\n");

	/* Add TD left over by the previous transfer */
	ohci_batch->ed = ohci_endpoint_get(usb_batch->ep)->ed;
	ohci_batch->tds[0] = ohci_endpoint_get(usb_batch->ep)->td;
	ohci_batch->leave_td = 0;
	unsigned i = 1;
	for (; i <= ohci_batch->td_count; ++i) {
		ohci_batch->tds[i] = malloc32(sizeof(td_t));
		CHECK_NULL_DISPOSE_RET(ohci_batch->tds[i],
		    "Failed to allocate TD %d.\n", i );
	}


	/* NOTE: OHCI is capable of handling buffer that crosses page boundaries
	 * it is, however, not capable of handling buffer that occupies more
	 * than two pages (the first page is computed using start pointer, the
	 * other using the end pointer) */
        if (usb_batch->setup_size + usb_batch->buffer_size > 0) {
		/* Use one buffer for setup and data stage */
		ohci_batch->device_buffer =
		    malloc32(usb_batch->setup_size + usb_batch->buffer_size);
                CHECK_NULL_DISPOSE_RET(ohci_batch->device_buffer,
                    "Failed to allocate device accessible buffer.\n");
		/* Copy setup data */
                memcpy(ohci_batch->device_buffer, usb_batch->setup_buffer,
		    usb_batch->setup_size);
		/* Copy generic data */
		if (usb_batch->ep->direction != USB_DIRECTION_IN)
			memcpy(
			    ohci_batch->device_buffer + usb_batch->setup_size,
			    usb_batch->buffer, usb_batch->buffer_size);
        }
	ohci_batch->usb_batch = usb_batch;

        assert(
	   batch_setup[usb_batch->ep->transfer_type][usb_batch->ep->direction]);
        batch_setup[usb_batch->ep->transfer_type][usb_batch->ep->direction](
	    ohci_batch);

	return ohci_batch;
#undef CHECK_NULL_DISPOSE_RET
}
/*----------------------------------------------------------------------------*/
/** Check batch TDs' status.
 *
 * @param[in] instance Batch structure to use.
 * @return False, if there is an active TD, true otherwise.
 *
 * Walk all TDs (usually there is just one). Stop with false if there is an
 * active TD. Stop with true if an error is found. Return true if the walk
 * completes with the last TD.
 */
bool ohci_transfer_batch_is_complete(ohci_transfer_batch_t *ohci_batch)
{
	assert(ohci_batch);
	assert(ohci_batch->usb_batch);

	usb_log_debug("Batch %p checking %zu td(s) for completion.\n",
	    ohci_batch->usb_batch, ohci_batch->td_count);
	usb_log_debug2("ED: %x:%x:%x:%x.\n",
	    ohci_batch->ed->status, ohci_batch->ed->td_head,
	    ohci_batch->ed->td_tail, ohci_batch->ed->next);

	size_t i = 0;
	for (; i < ohci_batch->td_count; ++i) {
		assert(ohci_batch->tds[i] != NULL);
		usb_log_debug("TD %zu: %x:%x:%x:%x.\n", i,
		    ohci_batch->tds[i]->status, ohci_batch->tds[i]->cbp,
		    ohci_batch->tds[i]->next, ohci_batch->tds[i]->be);
		if (!td_is_finished(ohci_batch->tds[i])) {
			return false;
		}
		ohci_batch->usb_batch->error = td_error(ohci_batch->tds[i]);
		if (ohci_batch->usb_batch->error != EOK) {
			usb_log_debug("Batch %p found error TD(%zu):%x.\n",
			    ohci_batch->usb_batch, i,
			    ohci_batch->tds[i]->status);
			/* Make sure TD queue is empty (one TD),
			 * ED should be marked as halted */
			ohci_batch->ed->td_tail =
			    (ohci_batch->ed->td_head & ED_TDTAIL_PTR_MASK);
			++i;
			break;
		}
	}

	assert(i <= ohci_batch->td_count);
	ohci_batch->leave_td = i;

	ohci_endpoint_t *ohci_ep = ohci_endpoint_get(ohci_batch->usb_batch->ep);
	assert(ohci_ep);
	ohci_ep->td = ohci_batch->tds[ohci_batch->leave_td];
	assert(i > 0);
	ohci_batch->usb_batch->transfered_size =
	    ohci_batch->usb_batch->buffer_size;
	for (--i;i < ohci_batch->td_count; ++i)
		ohci_batch->usb_batch->transfered_size
		    -= td_remain_size(ohci_batch->tds[i]);

	/* Clear possible ED HALT */
	ohci_batch->ed->td_head &= ~ED_TDHEAD_HALTED_FLAG;
	/* just make sure that we are leaving the right TD behind */
	const uint32_t pa = addr_to_phys(ohci_ep->td);
	assert(pa == (ohci_batch->ed->td_head & ED_TDHEAD_PTR_MASK));
	assert(pa == (ohci_batch->ed->td_tail & ED_TDTAIL_PTR_MASK));

	return true;
}
/*----------------------------------------------------------------------------*/
/** Starts execution of the TD list
 *
 * @param[in] instance Batch structure to use
 */
void ohci_transfer_batch_commit(ohci_transfer_batch_t *ohci_batch)
{
	assert(ohci_batch);
	ed_set_end_td(ohci_batch->ed, ohci_batch->tds[ohci_batch->td_count]);
}
/*----------------------------------------------------------------------------*/
/** Prepare generic control transfer
 *
 * @param[in] instance Batch structure to use.
 * @param[in] data_dir Direction to use for data stage.
 * @param[in] status_dir Direction to use for status stage.
 *
 * Setup stage with toggle 0 and direction BOTH(SETUP_PID)
 * Data stage with alternating toggle and direction supplied by parameter.
 * Status stage with toggle 1 and direction supplied by parameter.
 */
static void batch_control(ohci_transfer_batch_t *ohci_batch,
    usb_direction_t data_dir, usb_direction_t status_dir)
{
	assert(ohci_batch);
	assert(ohci_batch->usb_batch);
	usb_log_debug("Using ED(%p): %x:%x:%x:%x.\n", ohci_batch->ed,
	    ohci_batch->ed->status, ohci_batch->ed->td_tail,
	    ohci_batch->ed->td_head, ohci_batch->ed->next);

	int toggle = 0;
	char* buffer = ohci_batch->device_buffer;

	/* setup stage */
	td_init(ohci_batch->tds[0], USB_DIRECTION_BOTH, buffer,
		ohci_batch->usb_batch->setup_size, toggle);
	td_set_next(ohci_batch->tds[0], ohci_batch->tds[1]);
	usb_log_debug("Created CONTROL SETUP TD: %x:%x:%x:%x.\n",
	    ohci_batch->tds[0]->status, ohci_batch->tds[0]->cbp,
	    ohci_batch->tds[0]->next, ohci_batch->tds[0]->be);
	buffer += ohci_batch->usb_batch->setup_size;

	/* data stage */
	size_t td_current = 1;
	size_t remain_size = ohci_batch->usb_batch->buffer_size;
	while (remain_size > 0) {
		const size_t transfer_size =
		    remain_size > OHCI_TD_MAX_TRANSFER ?
		    OHCI_TD_MAX_TRANSFER : remain_size;
		toggle = 1 - toggle;

		td_init(ohci_batch->tds[td_current], data_dir, buffer,
		    transfer_size, toggle);
		td_set_next(ohci_batch->tds[td_current],
		    ohci_batch->tds[td_current + 1]);
		usb_log_debug("Created CONTROL DATA TD: %x:%x:%x:%x.\n",
		    ohci_batch->tds[td_current]->status,
		    ohci_batch->tds[td_current]->cbp,
		    ohci_batch->tds[td_current]->next,
		    ohci_batch->tds[td_current]->be);

		buffer += transfer_size;
		remain_size -= transfer_size;
		assert(td_current < ohci_batch->td_count - 1);
		++td_current;
	}

	/* status stage */
	assert(td_current == ohci_batch->td_count - 1);
	td_init(ohci_batch->tds[td_current], status_dir, NULL, 0, 1);
	td_set_next(ohci_batch->tds[td_current],
	    ohci_batch->tds[td_current + 1]);
	usb_log_debug("Created CONTROL STATUS TD: %x:%x:%x:%x.\n",
	    ohci_batch->tds[td_current]->status,
	    ohci_batch->tds[td_current]->cbp,
	    ohci_batch->tds[td_current]->next,
	    ohci_batch->tds[td_current]->be);
}
/*----------------------------------------------------------------------------*/
#define LOG_BATCH_INITIALIZED(batch, name, dir) \
	usb_log_debug2("Batch %p %s %s " USB_TRANSFER_BATCH_FMT " initialized.\n", \
	    (batch), (name), (dir), USB_TRANSFER_BATCH_ARGS(*(batch)))

/** Prepare generic data transfer
 *
 * @param[in] instance Batch structure to use.
 *
 * Direction is supplied by the associated ep and toggle is maintained by the
 * OHCI hw in ED.
 */
static void batch_data(ohci_transfer_batch_t *ohci_batch)
{
	assert(ohci_batch);
	usb_log_debug("Using ED(%p): %x:%x:%x:%x.\n", ohci_batch->ed,
	    ohci_batch->ed->status, ohci_batch->ed->td_tail,
	    ohci_batch->ed->td_head, ohci_batch->ed->next);

	const usb_direction_t direction = ohci_batch->usb_batch->ep->direction;
	size_t td_current = 0;
	size_t remain_size = ohci_batch->usb_batch->buffer_size;
	char *buffer = ohci_batch->device_buffer;
	while (remain_size > 0) {
		const size_t transfer_size = remain_size > OHCI_TD_MAX_TRANSFER
		    ? OHCI_TD_MAX_TRANSFER : remain_size;

		td_init(ohci_batch->tds[td_current], direction,
		    buffer, transfer_size, -1);
		td_set_next(ohci_batch->tds[td_current],
		    ohci_batch->tds[td_current + 1]);
		usb_log_debug("Created DATA TD: %x:%x:%x:%x.\n",
		    ohci_batch->tds[td_current]->status,
		    ohci_batch->tds[td_current]->cbp,
		    ohci_batch->tds[td_current]->next,
		    ohci_batch->tds[td_current]->be);

		buffer += transfer_size;
		remain_size -= transfer_size;
		assert(td_current < ohci_batch->td_count);
		++td_current;
	}
	LOG_BATCH_INITIALIZED(ohci_batch->usb_batch,
	    usb_str_transfer_type(ohci_batch->usb_batch->ep->transfer_type),
	    usb_str_direction(ohci_batch->usb_batch->ep->direction));
}
/*----------------------------------------------------------------------------*/
static void setup_control(ohci_transfer_batch_t *ohci_batch)
{
        // TODO Find a better way to do this
        if (ohci_batch->device_buffer[0] & (1 << 7)) {
		batch_control(ohci_batch, USB_DIRECTION_IN, USB_DIRECTION_OUT);
		LOG_BATCH_INITIALIZED(ohci_batch->usb_batch, "control", "write");
        } else {
		batch_control(ohci_batch, USB_DIRECTION_OUT, USB_DIRECTION_IN);
		LOG_BATCH_INITIALIZED(ohci_batch->usb_batch, "control", "write");
	}
}
/*----------------------------------------------------------------------------*/
void (*const batch_setup[4][3])(ohci_transfer_batch_t*) =
{
        { NULL, NULL, setup_control },
        { NULL, NULL, NULL },
        { batch_data, batch_data, NULL },
        { batch_data, batch_data, NULL },
};
/**
 * @}
 */
