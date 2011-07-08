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

#ifndef POSIX_UNISTD_H_
#define POSIX_UNISTD_H_

#include "libc/unistd.h"
#include "sys/types.h"

/* Process Termination */
#define _exit exit

/* Option Arguments */
extern char *optarg;
extern int optind, opterr, optopt;
extern int getopt(int, char * const [], const char *);

/* Environment */
extern char **posix_environ;

extern char *posix_getlogin(void);
extern int posix_getlogin_r(char *name, size_t namesize);

/* Identifying Terminals */
extern int posix_isatty(int fd);

/* Query Memory Parameters */
extern int posix_getpagesize(void);

/* Process Identification */
extern posix_pid_t posix_getpid(void);
extern posix_uid_t posix_getuid(void);
extern posix_gid_t posix_getgid(void);

/* Standard Streams */
#undef STDIN_FILENO
#define STDIN_FILENO (fileno(stdin))
#undef STDOUT_FILENO
#define STDOUT_FILENO (fileno(stdout))
#undef STDERR_FILENO
#define STDERR_FILENO (fileno(stderr))

/* File Accessibility */
#undef F_OK
#undef X_OK
#undef W_OK
#undef R_OK
#define	F_OK 0 /* Test for existence. */
#define	X_OK 1 /* Test for execute permission. */
#define	W_OK 2 /* Test for write permission. */
#define	R_OK 4 /* Test for read permission. */
extern int posix_access(const char *path, int amode);

/* System Parameters */
enum {
	_SC_PHYS_PAGES,
	_SC_AVPHYS_PAGES,
	_SC_PAGESIZE,
	_SC_CLK_TCK
};
extern long posix_sysconf(int name);

/* Path Configuration Parameters */
enum {
	_PC_2_SYMLINKS,
	_PC_ALLOC_SIZE_MIN,
	_PC_ASYNC_IO,
	_PC_CHOWN_RESTRICTED,
	_PC_FILESIZEBITS,
	_PC_LINK_MAX,
	_PC_MAX_CANON,
	_PC_MAX_INPUT,
	_PC_NAME_MAX,
	_PC_NO_TRUNC,
	_PC_PATH_MAX,
	_PC_PIPE_BUF,
	_PC_PRIO_IO,
	_PC_REC_INCR_XFER_SIZE,
	_PC_REC_MIN_XFER_SIZE,
	_PC_REC_XFER_ALIGN,
	_PC_SYMLINK_MAX,
	_PC_SYNC_IO,
	_PC_VDISABLE
};
extern long posix_pathconf(const char *path, int name);

/* Creating a Process */
extern posix_pid_t posix_fork(void);

/* Executing a File */
extern int posix_execv(const char *path, char *const argv[]);
extern int posix_execvp(const char *file, char *const argv[]);

/* Creating a Pipe */
extern int posix_pipe(int fildes[2]);

#ifndef LIBPOSIX_INTERNAL
	#define environ posix_environ
	#define getlogin posix_getlogin
	#define getlogin_r posix_getlogin_r

	#define isatty posix_isatty

	#undef getpagesize
	#define getpagesize posix_getpagesize

	#define getpid posix_getpid
	#define getuid posix_getuid
	#define getgid posix_getgid

	#define access posix_access

	#define sysconf posix_sysconf

	#define pathconf posix_pathconf

	#define fork posix_fork

	#define execv posix_execv
	#define execvp posix_execvp

	#define pipe posix_pipe
#endif

#endif /* POSIX_UNISTD_H_ */

/** @}
 */
