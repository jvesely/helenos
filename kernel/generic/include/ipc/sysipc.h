/*
 * Copyright (c) 2006 Ondrej Palkovsky
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
/** @file
 */

#ifndef KERN_SYSIPC_H_
#define KERN_SYSIPC_H_

#include <ipc/ipc.h>
#include <ipc/irq.h>
#include <arch/types.h>

unative_t sys_ipc_call_sync_fast(unative_t phoneid, unative_t method, 
    unative_t arg1, unative_t arg2, unative_t arg3, ipc_data_t *data);
unative_t sys_ipc_call_sync_slow(unative_t phoneid, ipc_data_t *question,
    ipc_data_t *reply);
unative_t sys_ipc_call_async_fast(unative_t phoneid, unative_t method, 
    unative_t arg1, unative_t arg2, unative_t arg3, unative_t arg4);
unative_t sys_ipc_call_async_slow(unative_t phoneid, ipc_data_t *data);
unative_t sys_ipc_answer_fast(unative_t callid, unative_t retval, 
    unative_t arg1, unative_t arg2, unative_t arg3, unative_t arg4);
unative_t sys_ipc_answer_slow(unative_t callid, ipc_data_t *data);
unative_t sys_ipc_wait_for_call(ipc_data_t *calldata, uint32_t usec,
    int nonblocking);
unative_t sys_ipc_poke(void);
unative_t sys_ipc_forward_fast(unative_t callid, unative_t phoneid,
    unative_t method, unative_t arg1, unative_t arg2, int mode);
unative_t sys_ipc_forward_slow(unative_t callid, unative_t phoneid,
    ipc_data_t *data, int mode);
unative_t sys_ipc_hangup(int phoneid);
unative_t sys_ipc_register_irq(inr_t inr, devno_t devno, unative_t method,
    irq_code_t *ucode);
unative_t sys_ipc_unregister_irq(inr_t inr, devno_t devno);
unative_t sys_ipc_connect_kbox(sysarg64_t *task_id);

#endif

/** @}
 */
