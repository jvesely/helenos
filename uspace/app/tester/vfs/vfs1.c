/*
 * Copyright (c) 2008 Jakub Jermar
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/vfs.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <devmap.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../tester.h"

#define FS_TYPE      "tmpfs"
#define MOUNT_POINT  "/tmp"
#define OPTIONS      ""
#define FLAGS        0

#define TEST_DIRECTORY  MOUNT_POINT "/testdir"
#define TEST_FILE       TEST_DIRECTORY "/testfile"
#define TEST_FILE2      TEST_DIRECTORY "/nextfile"

#define MAX_DEVICE_NAME  32
#define BUF_SIZE         16

static char text[] = "Lorem ipsum dolor sit amet, consectetur adipisicing elit";

static const char *read_root(void)
{
	TPRINTF("Opening the root directory...");
	
	DIR *dirp = opendir("/");
	if (!dirp) {
		TPRINTF("\n");
		return "opendir() failed";
	} else
		TPRINTF("OK\n");
	
	struct dirent *dp;
	while ((dp = readdir(dirp)))
		TPRINTF(" node \"%s\"\n", dp->d_name);
	closedir(dirp);
	
	return NULL;
}

const char *test_vfs1(void)
{
	if (mkdir(MOUNT_POINT, 0) != 0)
		return "mkdir() failed";
	TPRINTF("Created directory %s\n", MOUNT_POINT);
	
	int rc = mount(FS_TYPE, MOUNT_POINT, "", OPTIONS, FLAGS);
	switch (rc) {
	case EOK:
		TPRINTF("Mounted %s on %s\n", FS_TYPE, MOUNT_POINT);
		break;
	case EBUSY:
		TPRINTF("(INFO) Filesystem already mounted on %s\n", MOUNT_POINT);
		break;
	default:
		TPRINTF("(ERR) IPC returned errno %d (is tmpfs loaded?)\n", rc);
		return "mount() failed";
	}
	
	if (mkdir(TEST_DIRECTORY, 0) != 0)
		return "mkdir() failed";
	TPRINTF("Created directory %s\n", TEST_DIRECTORY);
	
	int fd0 = open(TEST_FILE, O_CREAT);
	if (fd0 < 0)
		return "open() failed";
	TPRINTF("Created file %s (fd=%d)\n", TEST_FILE, fd0);
	
	size_t size = sizeof(text);
	ssize_t cnt = write(fd0, text, size);
	if (cnt < 0)
		return "write() failed";
	TPRINTF("Written %d bytes\n", cnt);
	
	if (lseek(fd0, 0, SEEK_SET) != 0)
		return "lseek() failed";
	TPRINTF("Sought to position 0\n");
	
	char buf[BUF_SIZE];
	while ((cnt = read(fd0, buf, BUF_SIZE))) {
		if (cnt < 0)
			return "read() failed";
		
		TPRINTF("Read %d bytes: \".*s\"\n", cnt, cnt, buf);
	}
	
	close(fd0);
	
	const char *rv = read_root();
	if (rv != NULL)
		return rv;
	
	if (rename(TEST_FILE, TEST_FILE2))
		return "rename() failed";
	TPRINTF("Renamed %s to %s\n", TEST_FILE, TEST_FILE2);
	
	if (unlink(TEST_FILE2))
		return "unlink() failed";
	TPRINTF("Unlinked %s\n", TEST_FILE2);
	
	if (rmdir(TEST_DIRECTORY))
		return "rmdir() failed";
	TPRINTF("Removed directory %s\n", TEST_DIRECTORY);
	
	rv = read_root();
	if (rv != NULL)
		return rv;
	
	return NULL;
}
