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

/** @addtogroup libnet
 * @{
 */

#ifndef LIBNET_IP_REMOTE_H_
#define LIBNET_IP_REMOTE_H_

#include <ipc/services.h>

#include <net/ip_codes.h>
#include <net/inet.h>
#include <net/in.h>
#include <net/packet.h>
#include <net/device.h>
#include <net/socket.h>

extern int ip_set_gateway_req_remote(int, device_id_t, in_addr_t);
extern int ip_packet_size_req_remote(int, device_id_t, packet_dimension_t *);
extern int ip_received_error_msg_remote(int, device_id_t, packet_t *, services_t,
    services_t);
extern int ip_device_req_remote(int, device_id_t, services_t);
extern int ip_add_route_req_remote(int, device_id_t, in_addr_t, in_addr_t,
    in_addr_t);
extern int ip_send_msg_remote(int, device_id_t, packet_t *, services_t,
    services_t);
extern int ip_get_route_req_remote(int, ip_protocol_t, const struct sockaddr *,
    socklen_t, device_id_t *, void **, size_t *);

#endif

/** @}
 */
