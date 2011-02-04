/*
 * Copyright (c) 2010 Vojtech Horky
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

/** @addtogroup drvusbvhc
 * @{
 */
/** @file
 * @brief Virtual HC (implementation).
 */

#include <adt/list.h>
#include <bool.h>
#include <async.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <str_error.h>

#include <usbvirt/hub.h>

#include "vhcd.h"
#include "hc.h"
#include "devices.h"
#include "hub.h"

#define USLEEP_BASE (0 * 5 * 1000)

#define USLEEP_VAR 50

#define SHORTENING_VAR 15

#define PROB_OUTCOME_BABBLE 5
#define PROB_OUTCOME_CRCERROR 7

#define PROB_TEST(var, new_value, prob, number) \
	do { \
		if (((number) % (prob)) == 0) { \
			var = (new_value); \
		} \
	} while (0)

static link_t transaction_list;

#define TRANSACTION_FORMAT "T[%d.%d %s/%s (%d)]"
#define TRANSACTION_PRINTF(t) \
	(t).target.address, (t).target.endpoint, \
	usb_str_transfer_type((t).transfer_type), \
	usbvirt_str_transaction_type((t).type), \
	(int)(t).len

#define transaction_get_instance(lnk) \
	list_get_instance(lnk, transaction_t, link)

#define HUB_STATUS_MAX_LEN (HUB_PORT_COUNT + 64)

static inline unsigned int pseudo_random(unsigned int *seed)
{
	*seed = ((*seed) * 873511) % 22348977 + 7;
	return ((*seed) >> 8);
}

/** Call transaction callback.
 * Calling this callback informs the backend that transaction was processed.
 */
static void process_transaction_with_outcome(transaction_t * transaction,
    usb_transaction_outcome_t outcome)
{
	usb_log_debug2("Transaction " TRANSACTION_FORMAT " done: %s.\n",
	    TRANSACTION_PRINTF(*transaction),
	    usb_str_transaction_outcome(outcome));
	
	transaction->callback(transaction->buffer, transaction->actual_len,
	    outcome, transaction->callback_arg);
}

/** Host controller manager main function.
 */
static int hc_manager_fibril(void *arg)
{
	list_initialize(&transaction_list);
	
	static unsigned int seed = 4573;
	
	usb_log_info("Transaction processor ready.\n");
	
	while (true) {
		async_usleep(USLEEP_BASE + (pseudo_random(&seed) % USLEEP_VAR));
		
		if (list_empty(&transaction_list)) {
			continue;
		}
		
		char ports[HUB_STATUS_MAX_LEN + 1];
		virthub_get_status(&virtual_hub_device, ports, HUB_STATUS_MAX_LEN);
		
		link_t *first_transaction_link = transaction_list.next;
		transaction_t *transaction
		    = transaction_get_instance(first_transaction_link);
		list_remove(first_transaction_link);
		
		usb_log_debug("Processing " TRANSACTION_FORMAT " [%s].\n",
		    TRANSACTION_PRINTF(*transaction), ports);

		usb_transaction_outcome_t outcome;
		outcome = virtdev_send_to_all(transaction);
		
		process_transaction_with_outcome(transaction, outcome);

		free(transaction);
	}

	assert(false && "unreachable");
	return EOK;
}

void hc_manager(void)
{
	fid_t fid = fibril_create(hc_manager_fibril, NULL);
	if (fid == 0) {
		usb_log_fatal("Failed to start HC manager fibril.\n");
		return;
	}
	fibril_add_ready(fid);
}

/** Create new transaction
 */
static transaction_t *transaction_create(usbvirt_transaction_type_t type,
    usb_target_t target, usb_transfer_type_t transfer_type,
    void * buffer, size_t len,
    hc_transaction_done_callback_t callback, void * arg)
{
	transaction_t * transaction = malloc(sizeof(transaction_t));
	
	list_initialize(&transaction->link);
	transaction->type = type;
	transaction->transfer_type = transfer_type;
	transaction->target = target;
	transaction->buffer = buffer;
	transaction->len = len;
	transaction->actual_len = len;
	transaction->callback = callback;
	transaction->callback_arg = arg;

	return transaction;
}

static void hc_add_transaction(transaction_t *transaction)
{
	usb_log_debug("Adding transaction " TRANSACTION_FORMAT ".\n",
	    TRANSACTION_PRINTF(*transaction));
	list_append(&transaction->link, &transaction_list);
}

/** Add transaction directioned towards the device.
 */
void hc_add_transaction_to_device(bool setup, usb_target_t target,
    usb_transfer_type_t transfer_type,
    void * buffer, size_t len,
    hc_transaction_done_callback_t callback, void * arg)
{
	transaction_t *transaction = transaction_create(
	    setup ? USBVIRT_TRANSACTION_SETUP : USBVIRT_TRANSACTION_OUT,
	    target, transfer_type,
	    buffer, len, callback, arg);
	hc_add_transaction(transaction);
}

/** Add transaction directioned from the device.
 */
void hc_add_transaction_from_device(usb_target_t target,
    usb_transfer_type_t transfer_type,
    void * buffer, size_t len,
    hc_transaction_done_callback_t callback, void * arg)
{
	transaction_t *transaction = transaction_create(USBVIRT_TRANSACTION_IN,
	    target, transfer_type,
	    buffer, len, callback, arg);
	hc_add_transaction(transaction);
}

/**
 * @}
 */
