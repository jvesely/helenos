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

/** @addtogroup netecho
 * @{
 */

/** @file
 * Network echo server.
 *
 * Sockets-based server that echoes incomming messages. If stream mode
 * is selected, accepts incoming connections.
 */

#include <malloc.h>
#include <stdio.h>
#include <str.h>
#include <task.h>
#include <arg_parse.h>

#include <net/in.h>
#include <net/in6.h>
#include <net/inet.h>
#include <net/socket.h>
#include <net/socket_parse.h>

#include "print_error.h"

#define NAME "netecho"

static void echo_print_help(void)
{
	printf(
		"Network echo server\n" \
		"Usage: " NAME " [options]\n" \
		"Where options are:\n" \
		"-b backlog | --backlog=size\n" \
		"\tThe size of the accepted sockets queue. Only for SOCK_STREAM. The default is 3.\n" \
		"\n" \
		"-c count | --count=count\n" \
		"\tThe number of received messages to handle. A negative number means infinity. The default is infinity.\n" \
		"\n" \
		"-f protocol_family | --family=protocol_family\n" \
		"\tThe listenning socket protocol family. Only the PF_INET and PF_INET6 are supported.\n"
		"\n" \
		"-h | --help\n" \
		"\tShow this application help.\n"
		"\n" \
		"-p port_number | --port=port_number\n" \
		"\tThe port number the application should listen at. The default is 7.\n" \
		"\n" \
		"-r reply_string | --reply=reply_string\n" \
		"\tThe constant reply string. The default is the original data received.\n" \
		"\n" \
		"-s receive_size | --size=receive_size\n" \
		"\tThe maximum receive data size the application should accept. The default is 1024 bytes.\n" \
		"\n" \
		"-t socket_type | --type=socket_type\n" \
		"\tThe listenning socket type. Only the SOCK_DGRAM and the SOCK_STREAM are supported.\n" \
		"\n" \
		"-v | --verbose\n" \
		"\tShow all output messages.\n"
	);
}

int main(int argc, char *argv[])
{
	size_t size = 1024;
	int verbose = 0;
	char *reply = NULL;
	sock_type_t type = SOCK_DGRAM;
	int count = -1;
	int family = PF_INET;
	uint16_t port = 7;
	int backlog = 3;

	socklen_t max_length = sizeof(struct sockaddr_in6);
	uint8_t address_data[max_length];
	struct sockaddr *address = (struct sockaddr *) address_data;
	struct sockaddr_in *address_in = (struct sockaddr_in *) address;
	struct sockaddr_in6 *address_in6 = (struct sockaddr_in6 *) address;
	socklen_t addrlen;
	char address_string[INET6_ADDRSTRLEN];
	uint8_t *address_start;
	int socket_id;
	int listening_id;
	char *data;
	size_t length;
	int index;
	size_t reply_length;
	int value;
	int rc;

	// parse the command line arguments
	for (index = 1; index < argc; ++ index) {
		if (argv[index][0] == '-') {
			switch (argv[index][1]) {
			case 'b':
				rc = arg_parse_int(argc, argv, &index, &backlog, 0);
				if (rc != EOK)
					return rc;
				break;
			case 'c':
				rc = arg_parse_int(argc, argv, &index, &count, 0);
				if (rc != EOK)
					return rc;
				break;
			case 'f':
				rc = arg_parse_name_int(argc, argv, &index, &family, 0, socket_parse_protocol_family);
				if (rc != EOK)
					return rc;
				break;
			case 'h':
				echo_print_help();
				return EOK;
				break;
			case 'p':
				rc = arg_parse_int(argc, argv, &index, &value, 0);
				if (rc != EOK)
					return rc;
				port = (uint16_t) value;
				break;
			case 'r':
				rc = arg_parse_string(argc, argv, &index, &reply, 0);
				if (rc != EOK)
					return rc;
				break;
			case 's':
				rc = arg_parse_int(argc, argv, &index, &value, 0);
				if (rc != EOK)
					return rc;
				size = (value >= 0) ? (size_t) value : 0;
				break;
			case 't':
				rc = arg_parse_name_int(argc, argv, &index, &value, 0, socket_parse_socket_type);
				if (rc != EOK)
					return rc;
				type = (sock_type_t) value;
				break;
			case 'v':
				verbose = 1;
				break;
			// long options with the double minus sign ('-')
			case '-':
				if (str_lcmp(argv[index] + 2, "backlog=", 6) == 0) {
					rc = arg_parse_int(argc, argv, &index, &backlog, 8);
					if (rc != EOK)
						return rc;
				} else if (str_lcmp(argv[index] + 2, "count=", 6) == 0) {
					rc = arg_parse_int(argc, argv, &index, &count, 8);
					if (rc != EOK)
						return rc;
				} else if (str_lcmp(argv[index] + 2, "family=", 7) == 0) {
					rc = arg_parse_name_int(argc, argv, &index, &family, 9, socket_parse_protocol_family);
					if (rc != EOK)
						return rc;
				} else if (str_lcmp(argv[index] + 2, "help", 5) == 0) {
					echo_print_help();
					return EOK;
				} else if (str_lcmp(argv[index] + 2, "port=", 5) == 0) {
					rc = arg_parse_int(argc, argv, &index, &value, 7);
					if (rc != EOK)
						return rc;
					port = (uint16_t) value;
				} else if (str_lcmp(argv[index] + 2, "reply=", 6) == 0) {
					rc = arg_parse_string(argc, argv, &index, &reply, 8);
					if (rc != EOK)
						return rc;
				} else if (str_lcmp(argv[index] + 2, "size=", 5) == 0) {
					rc = arg_parse_int(argc, argv, &index, &value, 7);
					if (rc != EOK)
						return rc;
					size = (value >= 0) ? (size_t) value : 0;
				} else if (str_lcmp(argv[index] + 2, "type=", 5) == 0) {
					rc = arg_parse_name_int(argc, argv, &index, &value, 7, socket_parse_socket_type);
					if (rc != EOK)
						return rc;
					type = (sock_type_t) value;
				} else if (str_lcmp(argv[index] + 2, "verbose", 8) == 0) {
					verbose = 1;
				} else {
					echo_print_help();
					return EINVAL;
				}
				break;
			default:
				echo_print_help();
				return EINVAL;
			}
		} else {
			echo_print_help();
			return EINVAL;
		}
	}

	// check the buffer size
	if (size <= 0) {
		fprintf(stderr, "Receive size too small (%zu). Using 1024 bytes instead.\n", size);
		size = 1024;
	}
	// size plus the terminating null (\0)
	data = (char *) malloc(size + 1);
	if (!data) {
		fprintf(stderr, "Failed to allocate receive buffer.\n");
		return ENOMEM;
	}

	// set the reply size if set
	reply_length = reply ? str_length(reply) : 0;

	// prepare the address buffer
	bzero(address_data, max_length);
	switch (family) {
	case PF_INET:
		address_in->sin_family = AF_INET;
		address_in->sin_port = htons(port);
		addrlen = sizeof(struct sockaddr_in);
		break;
	case PF_INET6:
		address_in6->sin6_family = AF_INET6;
		address_in6->sin6_port = htons(port);
		addrlen = sizeof(struct sockaddr_in6);
		break;
	default:
		fprintf(stderr, "Protocol family is not supported\n");
		return EAFNOSUPPORT;
	}

	// get a listening socket
	listening_id = socket(family, type, 0);
	if (listening_id < 0) {
		socket_print_error(stderr, listening_id, "Socket create: ", "\n");
		return listening_id;
	}

	// if the stream socket is used
	if (type == SOCK_STREAM) {
		// check the backlog
		if (backlog <= 0) {
			fprintf(stderr, "Accepted sockets queue size too small (%zu). Using 3 instead.\n", size);
			backlog = 3;
		}
		// set the backlog
		rc = listen(listening_id, backlog);
		if (rc != EOK) {
			socket_print_error(stderr, rc, "Socket listen: ", "\n");
			return rc;
		}
	}

	// bind the listenning socket
	rc = bind(listening_id, address, addrlen);
	if (rc != EOK) {
		socket_print_error(stderr, rc, "Socket bind: ", "\n");
		return rc;
	}

	if (verbose)
		printf("Socket %d listenning at %d\n", listening_id, port);

	socket_id = listening_id;

	// do count times
	// or indefinitely if set to a negative value
	while (count) {

		addrlen = max_length;
		if (type == SOCK_STREAM) {
			// acceept a socket if the stream socket is used
			socket_id = accept(listening_id, address, &addrlen);
			if (socket_id <= 0) {
				socket_print_error(stderr, socket_id, "Socket accept: ", "\n");
			} else {
				if (verbose)
					printf("Socket %d accepted\n", socket_id);
			}
		}

		// if the datagram socket is used or the stream socked was accepted
		if (socket_id > 0) {

			// receive an echo request
			value = recvfrom(socket_id, data, size, 0, address, &addrlen);
			if (value < 0) {
				socket_print_error(stderr, value, "Socket receive: ", "\n");
			} else {
				length = (size_t) value;
				if (verbose) {
					// print the header

					// get the source port and prepare the address buffer
					address_start = NULL;
					switch (address->sa_family) {
					case AF_INET:
						port = ntohs(address_in->sin_port);
						address_start = (uint8_t *) &address_in->sin_addr.s_addr;
						break;
					case AF_INET6:
						port = ntohs(address_in6->sin6_port);
						address_start = (uint8_t *) &address_in6->sin6_addr.s6_addr;
						break;
					default:
						fprintf(stderr, "Address family %u (%#x) is not supported.\n",
						    address->sa_family, address->sa_family);
					}
					// parse the source address
					if (address_start) {
						rc = inet_ntop(address->sa_family, address_start, address_string, sizeof(address_string));
						if (rc != EOK) {
							fprintf(stderr, "Received address error %d\n", rc);
						} else {
							data[length] = '\0';
							printf("Socket %d received %zu bytes from %s:%d\n%s\n",
							    socket_id, length, address_string, port, data);
						}
					}
				}

				// answer the request either with the static reply or the original data
				rc = sendto(socket_id, reply ? reply : data, reply ? reply_length : length, 0, address, addrlen);
				if (rc != EOK)
					socket_print_error(stderr, rc, "Socket send: ", "\n");
			}

			// close the accepted stream socket
			if (type == SOCK_STREAM) {
				rc = closesocket(socket_id);
				if (rc != EOK)
					socket_print_error(stderr, rc, "Close socket: ", "\n");
			}

		}

		// decrease the count if positive
		if (count > 0) {
			count--;
			if (verbose)
				printf("Waiting for next %d packet(s)\n", count);
		}
	}

	if (verbose)
		printf("Closing the socket\n");

	// close the listenning socket
	rc = closesocket(listening_id);
	if (rc != EOK) {
		socket_print_error(stderr, rc, "Close socket: ", "\n");
		return rc;
	}

	if (verbose)
		printf("Exiting\n");

	return EOK;
}

/** @}
 */
