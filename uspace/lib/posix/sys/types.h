/*
 * Copyright (c) 2011 Jiri Zarevucky
 * Copyright (c) 2011 Petr Koupy
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

/** @addtogroup libposix
 * @{
 */
/** @file
 */

#ifndef POSIX_SYS_TYPES_H_
#define POSIX_SYS_TYPES_H_

#include "../libc/sys/types.h"
#include <ipc/devmap.h>
#include <task.h>

typedef task_id_t posix_pid_t;
typedef devmap_handle_t posix_dev_t;
typedef unsigned int posix_ino_t;
typedef unsigned int posix_nlink_t;
typedef unsigned int posix_uid_t;
typedef unsigned int posix_gid_t;
typedef aoff64_t posix_off_t;
typedef unsigned int posix_blksize_t;
typedef unsigned int posix_blkcnt_t;

#ifndef POSIX_INTERNAL
	#define pid_t posix_pid_t
	#define dev_t posix_dev_t
	#define nlink_t posix_nlink_t
	#define uid_t posix_uid_t
	#define gid_t posix_gid_t
	#define off_t posix_off_t
	#define blksize_t posix_blksize_t
	#define blkcnt_t posix_blkcnt_t
#endif

#endif /* POSIX_SYS_TYPES_H_ */

/** @}
 */
