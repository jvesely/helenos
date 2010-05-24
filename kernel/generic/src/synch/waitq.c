/*
 * Copyright (c) 2001-2004 Jakub Jermar
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

/** @addtogroup sync
 * @{
 */

/**
 * @file
 * @brief Wait queue.
 *
 * Wait queue is the basic synchronization primitive upon which all
 * other synchronization primitives build.
 *
 * It allows threads to wait for an event in first-come, first-served
 * fashion. Conditional operation as well as timeouts and interruptions
 * are supported.
 *
 */

#include <synch/waitq.h>
#include <synch/synch.h>
#include <synch/spinlock.h>
#include <proc/thread.h>
#include <proc/scheduler.h>
#include <arch/asm.h>
#include <typedefs.h>
#include <time/timeout.h>
#include <arch.h>
#include <context.h>
#include <adt/list.h>
#include <arch/cycle.h>

static void waitq_sleep_timed_out(void *);

/** Initialize wait queue
 *
 * Initialize wait queue.
 *
 * @param wq Pointer to wait queue to be initialized.
 *
 */
void waitq_initialize(waitq_t *wq)
{
	irq_spinlock_initialize(&wq->lock, "wq.lock");
	list_initialize(&wq->head);
	wq->missed_wakeups = 0;
}

/** Handle timeout during waitq_sleep_timeout() call
 *
 * This routine is called when waitq_sleep_timeout() times out.
 * Interrupts are disabled.
 *
 * It is supposed to try to remove 'its' thread from the wait queue;
 * it can eventually fail to achieve this goal when these two events
 * overlap. In that case it behaves just as though there was no
 * timeout at all.
 *
 * @param data Pointer to the thread that called waitq_sleep_timeout().
 *
 */
void waitq_sleep_timed_out(void *data)
{
	thread_t *thread = (thread_t *) data;
	bool do_wakeup = false;
	DEADLOCK_PROBE_INIT(p_wqlock);
	
	irq_spinlock_lock(&threads_lock, false);
	if (!thread_exists(thread))
		goto out;
	
grab_locks:
	irq_spinlock_lock(&thread->lock, false);
	
	waitq_t *wq;
	if ((wq = thread->sleep_queue)) {  /* Assignment */
		if (!irq_spinlock_trylock(&wq->lock)) {
			irq_spinlock_unlock(&thread->lock, false);
			DEADLOCK_PROBE(p_wqlock, DEADLOCK_THRESHOLD);
			/* Avoid deadlock */
			goto grab_locks;
		}
		
		list_remove(&thread->wq_link);
		thread->saved_context = thread->sleep_timeout_context;
		do_wakeup = true;
		thread->sleep_queue = NULL;
		irq_spinlock_unlock(&wq->lock, false);
	}
	
	thread->timeout_pending = false;
	irq_spinlock_unlock(&thread->lock, false);
	
	if (do_wakeup)
		thread_ready(thread);
	
out:
	irq_spinlock_unlock(&threads_lock, false);
}

/** Interrupt sleeping thread.
 *
 * This routine attempts to interrupt a thread from its sleep in a waitqueue.
 * If the thread is not found sleeping, no action is taken.
 *
 * @param thread Thread to be interrupted.
 *
 */
void waitq_interrupt_sleep(thread_t *thread)
{
	bool do_wakeup = false;
	DEADLOCK_PROBE_INIT(p_wqlock);
	
	irq_spinlock_lock(&threads_lock, true);
	if (!thread_exists(thread))
		goto out;
	
grab_locks:
	irq_spinlock_lock(&thread->lock, false);
	
	waitq_t *wq;
	if ((wq = thread->sleep_queue)) {  /* Assignment */
		if (!(thread->sleep_interruptible)) {
			/*
			 * The sleep cannot be interrupted.
			 *
			 */
			irq_spinlock_unlock(&thread->lock, false);
			goto out;
		}
		
		if (!irq_spinlock_trylock(&wq->lock)) {
			irq_spinlock_unlock(&thread->lock, false);
			DEADLOCK_PROBE(p_wqlock, DEADLOCK_THRESHOLD);
			/* Avoid deadlock */
			goto grab_locks;
		}
		
		if ((thread->timeout_pending) &&
		    (timeout_unregister(&thread->sleep_timeout)))
			thread->timeout_pending = false;
		
		list_remove(&thread->wq_link);
		thread->saved_context = thread->sleep_interruption_context;
		do_wakeup = true;
		thread->sleep_queue = NULL;
		irq_spinlock_unlock(&wq->lock, false);
	}
	irq_spinlock_unlock(&thread->lock, false);
	
	if (do_wakeup)
		thread_ready(thread);
	
out:
	irq_spinlock_unlock(&threads_lock, true);
}

/** Interrupt the first thread sleeping in the wait queue.
 *
 * Note that the caller somehow needs to know that the thread to be interrupted
 * is sleeping interruptibly.
 *
 * @param wq Pointer to wait queue.
 *
 */
void waitq_unsleep(waitq_t *wq)
{
	irq_spinlock_lock(&wq->lock, true);
	
	if (!list_empty(&wq->head)) {
		thread_t *thread = list_get_instance(wq->head.next, thread_t, wq_link);
		
		irq_spinlock_lock(&thread->lock, false);
		
		ASSERT(thread->sleep_interruptible);
		
		if ((thread->timeout_pending) &&
		    (timeout_unregister(&thread->sleep_timeout)))
			thread->timeout_pending = false;
		
		list_remove(&thread->wq_link);
		thread->saved_context = thread->sleep_interruption_context;
		thread->sleep_queue = NULL;
		
		irq_spinlock_unlock(&thread->lock, false);
		thread_ready(thread);
	}
	
	irq_spinlock_unlock(&wq->lock, true);
}

#define PARAM_NON_BLOCKING(flags, usec) \
	(((flags) & SYNCH_FLAGS_NON_BLOCKING) && ((usec) == 0))

/** Sleep until either wakeup, timeout or interruption occurs
 *
 * This is a sleep implementation which allows itself to time out or to be
 * interrupted from the sleep, restoring a failover context.
 *
 * Sleepers are organised in a FIFO fashion in a structure called wait queue.
 *
 * This function is really basic in that other functions as waitq_sleep()
 * and all the *_timeout() functions use it.
 *
 * @param wq    Pointer to wait queue.
 * @param usec  Timeout in microseconds.
 * @param flags Specify mode of the sleep.
 *
 * The sleep can be interrupted only if the
 * SYNCH_FLAGS_INTERRUPTIBLE bit is specified in flags.
 *
 * If usec is greater than zero, regardless of the value of the
 * SYNCH_FLAGS_NON_BLOCKING bit in flags, the call will not return until either
 * timeout, interruption or wakeup comes.
 *
 * If usec is zero and the SYNCH_FLAGS_NON_BLOCKING bit is not set in flags,
 * the call will not return until wakeup or interruption comes.
 *
 * If usec is zero and the SYNCH_FLAGS_NON_BLOCKING bit is set in flags, the
 * call will immediately return, reporting either success or failure.
 *
 * @return ESYNCH_WOULD_BLOCK, meaning that the sleep failed because at the
 *         time of the call there was no pending wakeup
 * @return ESYNCH_TIMEOUT, meaning that the sleep timed out.
 * @return ESYNCH_INTERRUPTED, meaning that somebody interrupted the sleeping
 *         thread.
 * @return ESYNCH_OK_ATOMIC, meaning that the sleep succeeded and that there
 *         was a pending wakeup at the time of the call. The caller was not put
 *         asleep at all.
 * @return ESYNCH_OK_BLOCKED, meaning that the sleep succeeded; the full sleep
 *         was attempted.
 *
 */
int waitq_sleep_timeout(waitq_t *wq, uint32_t usec, unsigned int flags)
{
	ASSERT((!PREEMPTION_DISABLED) || (PARAM_NON_BLOCKING(flags, usec)));
	
	ipl_t ipl = waitq_sleep_prepare(wq);
	int rc = waitq_sleep_timeout_unsafe(wq, usec, flags);
	waitq_sleep_finish(wq, rc, ipl);
	return rc;
}

/** Prepare to sleep in a waitq.
 *
 * This function will return holding the lock of the wait queue
 * and interrupts disabled.
 *
 * @param wq Wait queue.
 *
 * @return Interrupt level as it existed on entry to this function.
 *
 */
ipl_t waitq_sleep_prepare(waitq_t *wq)
{
	ipl_t ipl;
	
restart:
	ipl = interrupts_disable();
	
	if (THREAD) {  /* Needed during system initiailzation */
		/*
		 * Busy waiting for a delayed timeout.
		 * This is an important fix for the race condition between
		 * a delayed timeout and a next call to waitq_sleep_timeout().
		 * Simply, the thread is not allowed to go to sleep if
		 * there are timeouts in progress.
		 *
		 */
		irq_spinlock_lock(&THREAD->lock, false);
		
		if (THREAD->timeout_pending) {
			irq_spinlock_unlock(&THREAD->lock, false);
			interrupts_restore(ipl);
			goto restart;
		}
		
		irq_spinlock_unlock(&THREAD->lock, false);
	}
	
	irq_spinlock_lock(&wq->lock, false);
	return ipl;
}

/** Finish waiting in a wait queue.
 *
 * This function restores interrupts to the state that existed prior
 * to the call to waitq_sleep_prepare(). If necessary, the wait queue
 * lock is released.
 *
 * @param wq  Wait queue.
 * @param rc  Return code of waitq_sleep_timeout_unsafe().
 * @param ipl Interrupt level returned by waitq_sleep_prepare().
 *
 */
void waitq_sleep_finish(waitq_t *wq, int rc, ipl_t ipl)
{
	switch (rc) {
	case ESYNCH_WOULD_BLOCK:
	case ESYNCH_OK_ATOMIC:
		irq_spinlock_unlock(&wq->lock, false);
		break;
	default:
		break;
	}
	
	interrupts_restore(ipl);
}

/** Internal implementation of waitq_sleep_timeout().
 *
 * This function implements logic of sleeping in a wait queue.
 * This call must be preceded by a call to waitq_sleep_prepare()
 * and followed by a call to waitq_sleep_finish().
 *
 * @param wq    See waitq_sleep_timeout().
 * @param usec  See waitq_sleep_timeout().
 * @param flags See waitq_sleep_timeout().
 *
 * @return See waitq_sleep_timeout().
 *
 */
int waitq_sleep_timeout_unsafe(waitq_t *wq, uint32_t usec, unsigned int flags)
{
	/* Checks whether to go to sleep at all */
	if (wq->missed_wakeups) {
		wq->missed_wakeups--;
		return ESYNCH_OK_ATOMIC;
	} else {
		if (PARAM_NON_BLOCKING(flags, usec)) {
			/* Return immediatelly instead of going to sleep */
			return ESYNCH_WOULD_BLOCK;
		}
	}
	
	/*
	 * Now we are firmly decided to go to sleep.
	 *
	 */
	irq_spinlock_lock(&THREAD->lock, false);
	
	if (flags & SYNCH_FLAGS_INTERRUPTIBLE) {
		/*
		 * If the thread was already interrupted,
		 * don't go to sleep at all.
		 *
		 */
		if (THREAD->interrupted) {
			irq_spinlock_unlock(&THREAD->lock, false);
			irq_spinlock_unlock(&wq->lock, false);
			return ESYNCH_INTERRUPTED;
		}
		
		/*
		 * Set context that will be restored if the sleep
		 * of this thread is ever interrupted.
		 *
		 */
		THREAD->sleep_interruptible = true;
		if (!context_save(&THREAD->sleep_interruption_context)) {
			/* Short emulation of scheduler() return code. */
			THREAD->last_cycle = get_cycle();
			irq_spinlock_unlock(&THREAD->lock, false);
			return ESYNCH_INTERRUPTED;
		}
	} else
		THREAD->sleep_interruptible = false;
	
	if (usec) {
		/* We use the timeout variant. */
		if (!context_save(&THREAD->sleep_timeout_context)) {
			/* Short emulation of scheduler() return code. */
			THREAD->last_cycle = get_cycle();
			irq_spinlock_unlock(&THREAD->lock, false);
			return ESYNCH_TIMEOUT;
		}
		
		THREAD->timeout_pending = true;
		timeout_register(&THREAD->sleep_timeout, (uint64_t) usec,
		    waitq_sleep_timed_out, THREAD);
	}
	
	list_append(&THREAD->wq_link, &wq->head);
	
	/*
	 * Suspend execution.
	 *
	 */
	THREAD->state = Sleeping;
	THREAD->sleep_queue = wq;
	
	irq_spinlock_unlock(&THREAD->lock, false);
	
	/* wq->lock is released in scheduler_separated_stack() */
	scheduler();
	
	return ESYNCH_OK_BLOCKED;
}

/** Wake up first thread sleeping in a wait queue
 *
 * Wake up first thread sleeping in a wait queue. This is the SMP- and IRQ-safe
 * wrapper meant for general use.
 *
 * Besides its 'normal' wakeup operation, it attempts to unregister possible
 * timeout.
 *
 * @param wq   Pointer to wait queue.
 * @param mode Wakeup mode.
 *
 */
void waitq_wakeup(waitq_t *wq, wakeup_mode_t mode)
{
	irq_spinlock_lock(&wq->lock, true);
	_waitq_wakeup_unsafe(wq, mode);
	irq_spinlock_unlock(&wq->lock, true);
}

/** Internal SMP- and IRQ-unsafe version of waitq_wakeup()
 *
 * This is the internal SMP- and IRQ-unsafe version of waitq_wakeup(). It
 * assumes wq->lock is already locked and interrupts are already disabled.
 *
 * @param wq   Pointer to wait queue.
 * @param mode If mode is WAKEUP_FIRST, then the longest waiting
 *             thread, if any, is woken up. If mode is WAKEUP_ALL, then
 *             all waiting threads, if any, are woken up. If there are
 *             no waiting threads to be woken up, the missed wakeup is
 *             recorded in the wait queue.
 *
 */
void _waitq_wakeup_unsafe(waitq_t *wq, wakeup_mode_t mode)
{
	size_t count = 0;
	
loop:
	if (list_empty(&wq->head)) {
		wq->missed_wakeups++;
		if ((count) && (mode == WAKEUP_ALL))
			wq->missed_wakeups--;
		
		return;
	}
	
	count++;
	thread_t *thread = list_get_instance(wq->head.next, thread_t, wq_link);
	
	/*
	 * Lock the thread prior to removing it from the wq.
	 * This is not necessary because of mutual exclusion
	 * (the link belongs to the wait queue), but because
	 * of synchronization with waitq_sleep_timed_out()
	 * and thread_interrupt_sleep().
	 *
	 * In order for these two functions to work, the following
	 * invariant must hold:
	 *
	 * thread->sleep_queue != NULL <=> thread sleeps in a wait queue
	 *
	 * For an observer who locks the thread, the invariant
	 * holds only when the lock is held prior to removing
	 * it from the wait queue.
	 *
	 */
	irq_spinlock_lock(&thread->lock, false);
	list_remove(&thread->wq_link);
	
	if ((thread->timeout_pending) &&
	    (timeout_unregister(&thread->sleep_timeout)))
		thread->timeout_pending = false;
	
	thread->sleep_queue = NULL;
	irq_spinlock_unlock(&thread->lock, false);
	
	thread_ready(thread);
	
	if (mode == WAKEUP_ALL)
		goto loop;
}

/** @}
 */
