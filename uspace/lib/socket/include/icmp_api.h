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

/** @addtogroup icmp
 *  @{
 */

/** @file
 *  ICMP module application interface.
 */

#ifndef __NET_ICMP_API_H__
#define __NET_ICMP_API_H__

#include <net/socket_codes.h>
#include <sys/types.h>

#include <net_device.h>
#include <adt/measured_strings.h>
#include <packet/packet.h>
#include <inet.h>
#include <ip_codes.h>
#include <icmp_codes.h>
#include <icmp_common.h>

/** Miliseconds type definition.
 */
typedef size_t	mseconds_t;

/** @name ICMP module application interface
 *  This interface is used by other application modules.
 */
/*@{*/

/** Requests an echo message.
 *  Sends a packet with specified parameters to the target host and waits for the reply upto the given timeout.
 *  Blocks the caller until the reply or the timeout occurres.
 *  @param[in] icmp_phone The ICMP module phone used for (semi)remote calls.
 *  @param[in] size The message data length in bytes.
 *  @param[in] timeout The timeout in miliseconds.
 *  @param[in] ttl The time to live.
 *  @param[in] tos The type of service.
 *  @param[in] dont_fragment The value indicating whether the datagram must not be fragmented. Is used as a MTU discovery.
 *  @param[in] addr The target host address.
 *  @param[in] addrlen The torget host address length.
 *  @returns ICMP_ECHO on success.
 *  @returns ETIMEOUT if the reply has not arrived before the timeout.
 *  @returns ICMP type of the received error notification. 
 *  @returns EINVAL if the addrlen parameter is less or equal to zero (<=0).
 *  @returns ENOMEM if there is not enough memory left.
 *  @returns EPARTY if there was an internal error.
 */
extern int icmp_echo_msg(int icmp_phone, size_t size, mseconds_t timeout, ip_ttl_t ttl, ip_tos_t tos, int dont_fragment, const struct sockaddr * addr, socklen_t addrlen);

/*@}*/

#endif

/** @}
 */
