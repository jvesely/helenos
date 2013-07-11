/*
 * Copyright (c) 2006 Ondrej Palkovsky
 * Copyright (c) 2012 Jakub Jermar 
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

#include <ipc/sysipc_ops.h>
#include <ipc/ipc.h>
#include <ipc/ipcrsc.h>
#include <abi/errno.h>
#include <arch.h>

static int request_process(call_t *call, answerbox_t *box)
{
	int phoneid = phone_alloc(TASK);

	IPC_SET_ARG5(call->data, phoneid);
	
	return EOK;
}

static int answer_cleanup(call_t *answer, ipc_data_t *olddata)
{
	int phoneid = (int) IPC_GET_ARG5(*olddata);

	if (phoneid >= 0)
		phone_dealloc(phoneid);

	return EOK;
}

static int answer_preprocess(call_t *answer, ipc_data_t *olddata)
{
	int phoneid = (int) IPC_GET_ARG5(*olddata);

	if (IPC_GET_RETVAL(answer->data) != EOK) {
		/* The connection was not accepted */
		answer_cleanup(answer, olddata);
	} else if (phoneid >= 0) {
		/* The connection was accepted */
		if (phone_connect(phoneid, &answer->sender->answerbox)) {
			/* Set 'phone hash' as arg5 of response */
			IPC_SET_ARG5(answer->data,
			    (sysarg_t) &TASK->phones[phoneid]);
		} else {
			/* The answerbox is shutting down. */
			IPC_SET_RETVAL(answer->data, ENOENT);
			answer_cleanup(answer, olddata);
		}
	} else {
		IPC_SET_RETVAL(answer->data, ELIMIT);
	}

	return EOK;
}


sysipc_ops_t ipc_m_connect_to_me_ops = {
	.request_preprocess = null_request_preprocess,
	.request_forget = null_request_forget,
	.request_process = request_process,
	.answer_cleanup = answer_cleanup,
	.answer_preprocess = answer_preprocess,
	.answer_process = null_answer_process,
};

/** @}
 */
