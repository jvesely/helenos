/*
 * Copyright (c) 2009 Lukas Mejdrech
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

/** @addtogroup libc
 * @{
 */

/** @file
 * Generic module functions implementation. 
 *
 * @todo MAKE IT POSSIBLE TO REMOVE THIS FILE VIA EITHER REPLACING PART OF ITS
 * FUNCTIONALITY OR VIA INTEGRATING ITS FUNCTIONALITY MORE TIGHTLY WITH THE REST
 * OF THE SYSTEM.
 */

#include <async.h>
#include <malloc.h>
#include <err.h>
#include <sys/time.h>

#include <ipc/ipc.h>
#include <ipc/services.h>

#include <net/modules.h>

/** The time between connect requests in microseconds. */
#define MODULE_WAIT_TIME	(10 * 1000)

/** Answers the call.
 *
 * @param[in] callid	The call identifier.
 * @param[in] result	The message processing result.
 * @param[in] answer	The message processing answer.
 * @param[in] answer_count The number of answer parameters.
 */
void
answer_call(ipc_callid_t callid, int result, ipc_call_t *answer,
    int answer_count)
{
	// choose the most efficient answer function
	if (answer || (!answer_count)) {
		switch (answer_count) {
		case 0:
			ipc_answer_0(callid, (ipcarg_t) result);
			break;
		case 1:
			ipc_answer_1(callid, (ipcarg_t) result,
			    IPC_GET_ARG1(*answer));
			break;
		case 2:
			ipc_answer_2(callid, (ipcarg_t) result,
			    IPC_GET_ARG1(*answer), IPC_GET_ARG2(*answer));
			break;
		case 3:
			ipc_answer_3(callid, (ipcarg_t) result,
			    IPC_GET_ARG1(*answer), IPC_GET_ARG2(*answer),
			    IPC_GET_ARG3(*answer));
			break;
		case 4:
			ipc_answer_4(callid, (ipcarg_t) result,
			    IPC_GET_ARG1(*answer), IPC_GET_ARG2(*answer),
			    IPC_GET_ARG3(*answer), IPC_GET_ARG4(*answer));
			break;
		case 5:
		default:
			ipc_answer_5(callid, (ipcarg_t) result,
			    IPC_GET_ARG1(*answer), IPC_GET_ARG2(*answer),
			    IPC_GET_ARG3(*answer), IPC_GET_ARG4(*answer),
			    IPC_GET_ARG5(*answer));
			break;
		}
	}
}

/** Create bidirectional connection with the needed module service and registers
 * the message receiver.
 *
 * @param[in] need	The needed module service.
 * @param[in] arg1	The first parameter.
 * @param[in] arg2	The second parameter.
 * @param[in] arg3	The third parameter.
 * @param[in] client_receiver The message receiver.
 *
 * @return		The phone of the needed service.
 * @return		Other error codes as defined for the ipc_connect_to_me()
 *			function.
 */
int bind_service(services_t need, ipcarg_t arg1, ipcarg_t arg2, ipcarg_t arg3,
    async_client_conn_t client_receiver)
{
	return bind_service_timeout(need, arg1, arg2, arg3, client_receiver, 0);
}

/** Create bidirectional connection with the needed module service and registers
 * the message receiver.
 *
 * @param[in] need	The needed module service.
 * @param[in] arg1	The first parameter.
 * @param[in] arg2	The second parameter.
 * @param[in] arg3	The third parameter.
 * @param[in] client_receiver The message receiver.
 * @param[in] timeout	The connection timeout in microseconds. No timeout if
 * 			set to zero (0).
 *
 * @return		The phone of the needed service.
 * @return		ETIMEOUT if the connection timeouted.
 * @return		Other error codes as defined for the ipc_connect_to_me()
 *			function.
 *
 */
int bind_service_timeout(services_t need, ipcarg_t arg1, ipcarg_t arg2,
    ipcarg_t arg3, async_client_conn_t client_receiver, suseconds_t timeout)
{
	ERROR_DECLARE;
	
	/* Connect to the needed service */
	int phone = connect_to_service_timeout(need, timeout);
	if (phone >= 0) {
		/* Request the bidirectional connection */
		ipcarg_t phonehash;
		if (ERROR_OCCURRED(ipc_connect_to_me(phone, arg1, arg2, arg3,
		    &phonehash))) {
			ipc_hangup(phone);
			return ERROR_CODE;
		}
		async_new_connection(phonehash, 0, NULL, client_receiver);
	}
	
	return phone;
}

/** Connects to the needed module.
 *
 * @param[in] need	The needed module service.
 * @returns		The phone of the needed service.
 */
int connect_to_service(services_t need)
{
	return connect_to_service_timeout(need, 0);
}

/** Connects to the needed module.
 *
 *  @param[in] need	The needed module service.
 *  @param[in] timeout	The connection timeout in microseconds. No timeout if
 *			set to zero (0).
 *  @returns		The phone of the needed service.
 *  @returns		ETIMEOUT if the connection timeouted.
 */
int connect_to_service_timeout(services_t need, suseconds_t timeout)
{
	int phone;

	// if no timeout is set
	if (timeout <= 0)
		return async_connect_me_to_blocking(PHONE_NS, need, 0, 0);

	while (true) {
		phone = async_connect_me_to(PHONE_NS, need, 0, 0);
		if ((phone >= 0) || (phone != ENOENT))
			return phone;

		// end if no time is left
		if (timeout <= 0)
			return ETIMEOUT;

		// wait the minimum of the module wait time and the timeout
		usleep((timeout <= MODULE_WAIT_TIME) ?
		    timeout : MODULE_WAIT_TIME);
		timeout -= MODULE_WAIT_TIME;
	}
}

/** Receives data from the other party.
 *
 * The received data buffer is allocated and returned.
 *
 * @param[out] data	The data buffer to be filled.
 * @param[out] length	The buffer length.
 * @returns		EOK on success.
 * @returns		EBADMEM if the data or the length parameter is NULL.
 * @returns		EINVAL if the client does not send data.
 * @returns		ENOMEM if there is not enough memory left.
 * @returns		Other error codes as defined for the
 *			async_data_write_finalize() function.
 */
int data_receive(void **data, size_t *length)
{
	ERROR_DECLARE;

	ipc_callid_t callid;

	if (!data || !length)
		return EBADMEM;

	// fetch the request
	if (!async_data_write_receive(&callid, length))
		return EINVAL;

	// allocate the buffer
	*data = malloc(*length);
	if (!*data)
		return ENOMEM;

	// fetch the data
	if (ERROR_OCCURRED(async_data_write_finalize(callid, *data, *length))) {
		free(data);
		return ERROR_CODE;
	}

	return EOK;
}

/** Replies the data to the other party.
 *
 * @param[in] data	The data buffer to be sent.
 * @param[in] data_length The buffer length.
 * @returns		EOK on success.
 * @returns		EINVAL if the client does not expect the data.
 * @returns		EOVERFLOW if the client does not expect all the data.
 *			Only partial data are transfered.
 * @returns		Other error codes as defined for the
 *			async_data_read_finalize() function.
 */
int data_reply(void *data, size_t data_length)
{
	size_t length;
	ipc_callid_t callid;

	// fetch the request
	if (!async_data_read_receive(&callid, &length))
		return EINVAL;

	// check the requested data size
	if (length < data_length) {
		async_data_read_finalize(callid, data, length);
		return EOVERFLOW;
	}

	// send the data
	return async_data_read_finalize(callid, data, data_length);
}

/** Refreshes answer structure and parameters count.
 *
 * Erases all attributes.
 *
 * @param[in,out] answer The message processing answer structure.
 * @param[in,out] answer_count The number of answer parameters.
 */
void refresh_answer(ipc_call_t *answer, int *answer_count)
{
	if (answer_count)
		*answer_count = 0;

	if (answer) {
		IPC_SET_RETVAL(*answer, 0);
		// just to be precize
		IPC_SET_METHOD(*answer, 0);
		IPC_SET_ARG1(*answer, 0);
		IPC_SET_ARG2(*answer, 0);
		IPC_SET_ARG3(*answer, 0);
		IPC_SET_ARG4(*answer, 0);
		IPC_SET_ARG5(*answer, 0);
	}
}

/** @}
 */
