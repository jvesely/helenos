/*
 * Copyright (c) 2010 Martin Decky
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

/** @addtogroup libnet
 * @{
 */

#ifndef LIBNET_TL_SKEL_H_
#define LIBNET_TL_SKEL_H_

/** @file
 * Transport layer module skeleton.
 * The skeleton has to be part of each transport layer module.
 */

#include <async.h>
#include <fibril_synch.h>
#include <ipc/services.h>

#include <adt/measured_strings.h>
#include <net/device.h>
#include <net/packet.h>

/** Module initialization.
 *
 * This has to be implemented in user code.
 *
 * @param[in] net_phone Networking module phone.
 *
 * @return EOK on success.
 * @return Other error codes as defined for each specific module
 *         initialize function.
 *
 */
extern int tl_initialize(int net_phone);

/** Per-connection module initialization.
 *
 * This has to be implemented in user code.
 *
 */
extern void tl_connection(void);

/** Process the transport layer module message.
 *
 * This has to be implemented in user code.
 *
 * @param[in]  callid Message identifier.
 * @param[in]  call   Message parameters.
 * @param[out] answer Answer.
 * @param[out] count  Number of arguments of the answer.
 *
 * @return EOK on success.
 * @return Other error codes as defined for each specific module.
 *
 */
extern int tl_message(ipc_callid_t, ipc_call_t *,
    ipc_call_t *, size_t *);

extern int tl_module_start(int);

#endif

/** @}
 */
