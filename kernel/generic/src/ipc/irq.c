/*
 * Copyright (c) 2006 Ondrej Palkovsky
 * Copyright (c) 2006 Jakub Jermar
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

/** @addtogroup genericipc
 * @{
 */

/**
 * @file
 * @brief IRQ notification framework.
 *
 * This framework allows applications to register to receive a notification
 * when interrupt is detected. The application may provide a simple 'top-half'
 * handler as part of its registration, which can perform simple operations
 * (read/write port/memory, add information to notification ipc message).
 *
 * The structure of a notification message is as follows:
 * - METHOD: method as registered by the SYS_IPC_REGISTER_IRQ syscall
 * - ARG1: payload modified by a 'top-half' handler
 * - ARG2: payload modified by a 'top-half' handler
 * - ARG3: payload modified by a 'top-half' handler
 * - ARG4: payload modified by a 'top-half' handler
 * - ARG5: payload modified by a 'top-half' handler
 * - in_phone_hash: interrupt counter (may be needed to assure correct order
 *         in multithreaded drivers)
 *
 * Note on synchronization for ipc_irq_register(), ipc_irq_unregister(),
 * ipc_irq_cleanup() and IRQ handlers:
 *
 *   By always taking all of the uspace IRQ hash table lock, IRQ structure lock
 *   and answerbox lock, we can rule out race conditions between the
 *   registration functions and also the cleanup function. Thus the observer can
 *   either see the IRQ structure present in both the hash table and the
 *   answerbox list or absent in both. Views in which the IRQ structure would be
 *   linked in the hash table but not in the answerbox list, or vice versa, are
 *   not possible.
 *
 *   By always taking the hash table lock and the IRQ structure lock, we can
 *   rule out a scenario in which we would free up an IRQ structure, which is
 *   still referenced by, for example, an IRQ handler. The locking scheme forces
 *   us to lock the IRQ structure only after any progressing IRQs on that
 *   structure are finished. Because we hold the hash table lock, we prevent new
 *   IRQs from taking new references to the IRQ structure.
 *
 */

#include <arch.h>
#include <mm/slab.h>
#include <errno.h>
#include <ddi/irq.h>
#include <ipc/ipc.h>
#include <ipc/irq.h>
#include <syscall/copy.h>
#include <console/console.h>
#include <print.h>

/** Free the top-half pseudocode.
 *
 * @param code Pointer to the top-half pseudocode.
 *
 */
static void code_free(irq_code_t *code)
{
	if (code) {
		free(code->cmds);
		free(code);
	}
}

/** Copy the top-half pseudocode from userspace into the kernel.
 *
 * @param ucode Userspace address of the top-half pseudocode.
 *
 * @return Kernel address of the copied pseudocode.
 *
 */
static irq_code_t *code_from_uspace(irq_code_t *ucode)
{
	irq_code_t *code = malloc(sizeof(*code), 0);
	int rc = copy_from_uspace(code, ucode, sizeof(*code));
	if (rc != 0) {
		free(code);
		return NULL;
	}
	
	if (code->cmdcount > IRQ_MAX_PROG_SIZE) {
		free(code);
		return NULL;
	}
	
	irq_cmd_t *ucmds = code->cmds;
	code->cmds = malloc(sizeof(code->cmds[0]) * code->cmdcount, 0);
	rc = copy_from_uspace(code->cmds, ucmds,
	    sizeof(code->cmds[0]) * code->cmdcount);
	if (rc != 0) {
		free(code->cmds);
		free(code);
		return NULL;
	}
	
	return code;
}

/** Register an answerbox as a receiving end for IRQ notifications.
 *
 * @param box    Receiving answerbox.
 * @param inr    IRQ number.
 * @param devno  Device number.
 * @param method Method to be associated with the notification.
 * @param ucode  Uspace pointer to top-half pseudocode.
 *
 * @return EBADMEM, ENOENT or EEXISTS on failure or 0 on success.
 *
 */
int ipc_irq_register(answerbox_t *box, inr_t inr, devno_t devno,
    unative_t method, irq_code_t *ucode)
{
	unative_t key[] = {
		(unative_t) inr,
		(unative_t) devno
	};
	
	irq_code_t *code;
	if (ucode) {
		code = code_from_uspace(ucode);
		if (!code)
			return EBADMEM;
	} else
		code = NULL;
	
	/*
	 * Allocate and populate the IRQ structure.
	 */
	irq_t *irq = malloc(sizeof(irq_t), 0);
	
	irq_initialize(irq);
	irq->devno = devno;
	irq->inr = inr;
	irq->claim = ipc_irq_top_half_claim;
	irq->handler = ipc_irq_top_half_handler;
	irq->notif_cfg.notify = true;
	irq->notif_cfg.answerbox = box;
	irq->notif_cfg.method = method;
	irq->notif_cfg.code = code;
	irq->notif_cfg.counter = 0;
	
	/*
	 * Enlist the IRQ structure in the uspace IRQ hash table and the
	 * answerbox's list.
	 */
	irq_spinlock_lock(&irq_uspace_hash_table_lock, true);
	
	link_t *hlp = hash_table_find(&irq_uspace_hash_table, key);
	if (hlp) {
		irq_t *hirq = hash_table_get_instance(hlp, irq_t, link);
		
		/* hirq is locked */
		irq_spinlock_unlock(&hirq->lock, false);
		code_free(code);
		irq_spinlock_unlock(&irq_uspace_hash_table_lock, true);
		
		free(irq);
		return EEXISTS;
	}
	
	/* Locking is not really necessary, but paranoid */
	irq_spinlock_lock(&irq->lock, false);
	irq_spinlock_lock(&box->irq_lock, false);
	
	hash_table_insert(&irq_uspace_hash_table, key, &irq->link);
	list_append(&irq->notif_cfg.link, &box->irq_head);
	
	irq_spinlock_unlock(&box->irq_lock, false);
	irq_spinlock_unlock(&irq->lock, false);
	irq_spinlock_unlock(&irq_uspace_hash_table_lock, true);
	
	return EOK;
}

/** Unregister task from IRQ notification.
 *
 * @param box   Answerbox associated with the notification.
 * @param inr   IRQ number.
 * @param devno Device number.
 *
 */
int ipc_irq_unregister(answerbox_t *box, inr_t inr, devno_t devno)
{
	unative_t key[] = {
		(unative_t) inr,
		(unative_t) devno
	};
	
	irq_spinlock_lock(&irq_uspace_hash_table_lock, true);
	link_t *lnk = hash_table_find(&irq_uspace_hash_table, key);
	if (!lnk) {
		irq_spinlock_unlock(&irq_uspace_hash_table_lock, true);
		return ENOENT;
	}
	
	irq_t *irq = hash_table_get_instance(lnk, irq_t, link);
	
	/* irq is locked */
	irq_spinlock_lock(&box->irq_lock, false);
	
	ASSERT(irq->notif_cfg.answerbox == box);
	
	/* Free up the pseudo code and associated structures. */
	code_free(irq->notif_cfg.code);
	
	/* Remove the IRQ from the answerbox's list. */
	list_remove(&irq->notif_cfg.link);
	
	/*
	 * We need to drop the IRQ lock now because hash_table_remove() will try
	 * to reacquire it. That basically violates the natural locking order,
	 * but a deadlock in hash_table_remove() is prevented by the fact that
	 * we already held the IRQ lock and didn't drop the hash table lock in
	 * the meantime.
	 */
	irq_spinlock_unlock(&irq->lock, false);
	
	/* Remove the IRQ from the uspace IRQ hash table. */
	hash_table_remove(&irq_uspace_hash_table, key, 2);
	
	irq_spinlock_unlock(&box->irq_lock, false);
	irq_spinlock_unlock(&irq_uspace_hash_table_lock, true);
	
	/* Free up the IRQ structure. */
	free(irq);
	
	return EOK;
}

/** Disconnect all IRQ notifications from an answerbox.
 *
 * This function is effective because the answerbox contains
 * list of all irq_t structures that are registered to
 * send notifications to it.
 *
 * @param box Answerbox for which we want to carry out the cleanup.
 *
 */
void ipc_irq_cleanup(answerbox_t *box)
{
loop:
	irq_spinlock_lock(&irq_uspace_hash_table_lock, true);
	irq_spinlock_lock(&box->irq_lock, false);
	
	while (box->irq_head.next != &box->irq_head) {
		DEADLOCK_PROBE_INIT(p_irqlock);
		
		irq_t *irq = list_get_instance(box->irq_head.next, irq_t,
		    notif_cfg.link);
		
		if (!irq_spinlock_trylock(&irq->lock)) {
			/*
			 * Avoid deadlock by trying again.
			 */
			irq_spinlock_unlock(&box->irq_lock, false);
			irq_spinlock_unlock(&irq_uspace_hash_table_lock, true);
			DEADLOCK_PROBE(p_irqlock, DEADLOCK_THRESHOLD);
			goto loop;
		}
		
		unative_t key[2];
		key[0] = irq->inr;
		key[1] = irq->devno;
		
		ASSERT(irq->notif_cfg.answerbox == box);
		
		/* Unlist from the answerbox. */
		list_remove(&irq->notif_cfg.link);
		
		/* Free up the pseudo code and associated structures. */
		code_free(irq->notif_cfg.code);
		
		/*
		 * We need to drop the IRQ lock now because hash_table_remove()
		 * will try to reacquire it. That basically violates the natural
		 * locking order, but a deadlock in hash_table_remove() is
		 * prevented by the fact that we already held the IRQ lock and
		 * didn't drop the hash table lock in the meantime.
		 */
		irq_spinlock_unlock(&irq->lock, false);
		
		/* Remove from the hash table. */
		hash_table_remove(&irq_uspace_hash_table, key, 2);
		
		free(irq);
	}
	
	irq_spinlock_unlock(&box->irq_lock, false);
	irq_spinlock_unlock(&irq_uspace_hash_table_lock, true);
}

/** Add a call to the proper answerbox queue.
 *
 * Assume irq->lock is locked and interrupts disabled.
 *
 * @param irq  IRQ structure referencing the target answerbox.
 * @param call IRQ notification call.
 *
 */
static void send_call(irq_t *irq, call_t *call)
{
	irq_spinlock_lock(&irq->notif_cfg.answerbox->irq_lock, false);
	list_append(&call->link, &irq->notif_cfg.answerbox->irq_notifs);
	irq_spinlock_unlock(&irq->notif_cfg.answerbox->irq_lock, false);
	
	waitq_wakeup(&irq->notif_cfg.answerbox->wq, WAKEUP_FIRST);
}

/** Apply the top-half pseudo code to find out whether to accept the IRQ or not.
 *
 * @param irq IRQ structure.
 *
 * @return IRQ_ACCEPT if the interrupt is accepted by the
 *         pseudocode, IRQ_DECLINE otherwise.
 *
 */
irq_ownership_t ipc_irq_top_half_claim(irq_t *irq)
{
	irq_code_t *code = irq->notif_cfg.code;
	uint32_t *scratch = irq->notif_cfg.scratch;
	
	if (!irq->notif_cfg.notify)
		return IRQ_DECLINE;
	
	if (!code)
		return IRQ_DECLINE;
	
	size_t i;
	for (i = 0; i < code->cmdcount; i++) {
		uint32_t dstval;
		uintptr_t srcarg = code->cmds[i].srcarg;
		uintptr_t dstarg = code->cmds[i].dstarg;
		
		if (srcarg >= IPC_CALL_LEN)
			break;
		
		if (dstarg >= IPC_CALL_LEN)
			break;
	
		switch (code->cmds[i].cmd) {
		case CMD_PIO_READ_8:
			dstval = pio_read_8((ioport8_t *) code->cmds[i].addr);
			if (dstarg)
				scratch[dstarg] = dstval;
			break;
		case CMD_PIO_READ_16:
			dstval = pio_read_16((ioport16_t *) code->cmds[i].addr);
			if (dstarg)
				scratch[dstarg] = dstval;
			break;
		case CMD_PIO_READ_32:
			dstval = pio_read_32((ioport32_t *) code->cmds[i].addr);
			if (dstarg)
				scratch[dstarg] = dstval;
			break;
		case CMD_PIO_WRITE_8:
			pio_write_8((ioport8_t *) code->cmds[i].addr,
			    (uint8_t) code->cmds[i].value);
			break;
		case CMD_PIO_WRITE_16:
			pio_write_16((ioport16_t *) code->cmds[i].addr,
			    (uint16_t) code->cmds[i].value);
			break;
		case CMD_PIO_WRITE_32:
			pio_write_32((ioport32_t *) code->cmds[i].addr,
			    (uint32_t) code->cmds[i].value);
			break;
		case CMD_BTEST:
			if ((srcarg) && (dstarg)) {
				dstval = scratch[srcarg] & code->cmds[i].value;
				scratch[dstarg] = dstval;
			}
			break;
		case CMD_PREDICATE:
			if ((srcarg) && (!scratch[srcarg])) {
				i += code->cmds[i].value;
				continue;
			}
			break;
		case CMD_ACCEPT:
			return IRQ_ACCEPT;
		case CMD_DECLINE:
		default:
			return IRQ_DECLINE;
		}
	}
	
	return IRQ_DECLINE;
}

/* IRQ top-half handler.
 *
 * We expect interrupts to be disabled and the irq->lock already held.
 *
 * @param irq IRQ structure.
 *
 */
void ipc_irq_top_half_handler(irq_t *irq)
{
	ASSERT(irq);
	
	if (irq->notif_cfg.answerbox) {
		call_t *call = ipc_call_alloc(FRAME_ATOMIC);
		if (!call)
			return;
		
		call->flags |= IPC_CALL_NOTIF;
		/* Put a counter to the message */
		call->priv = ++irq->notif_cfg.counter;
		
		/* Set up args */
		IPC_SET_METHOD(call->data, irq->notif_cfg.method);
		IPC_SET_ARG1(call->data, irq->notif_cfg.scratch[1]);
		IPC_SET_ARG2(call->data, irq->notif_cfg.scratch[2]);
		IPC_SET_ARG3(call->data, irq->notif_cfg.scratch[3]);
		IPC_SET_ARG4(call->data, irq->notif_cfg.scratch[4]);
		IPC_SET_ARG5(call->data, irq->notif_cfg.scratch[5]);
		
		send_call(irq, call);
	}
}

/** Send notification message.
 *
 * @param irq IRQ structure.
 * @param a1  Driver-specific payload argument.
 * @param a2  Driver-specific payload argument.
 * @param a3  Driver-specific payload argument.
 * @param a4  Driver-specific payload argument.
 * @param a5  Driver-specific payload argument.
 *
 */
void ipc_irq_send_msg(irq_t *irq, unative_t a1, unative_t a2, unative_t a3,
    unative_t a4, unative_t a5)
{
	irq_spinlock_lock(&irq->lock, true);
	
	if (irq->notif_cfg.answerbox) {
		call_t *call = ipc_call_alloc(FRAME_ATOMIC);
		if (!call) {
			irq_spinlock_unlock(&irq->lock, true);
			return;
		}
		
		call->flags |= IPC_CALL_NOTIF;
		/* Put a counter to the message */
		call->priv = ++irq->notif_cfg.counter;
		
		IPC_SET_METHOD(call->data, irq->notif_cfg.method);
		IPC_SET_ARG1(call->data, a1);
		IPC_SET_ARG2(call->data, a2);
		IPC_SET_ARG3(call->data, a3);
		IPC_SET_ARG4(call->data, a4);
		IPC_SET_ARG5(call->data, a5);
		
		send_call(irq, call);
	}
	
	irq_spinlock_unlock(&irq->lock, true);
}

/** @}
 */
