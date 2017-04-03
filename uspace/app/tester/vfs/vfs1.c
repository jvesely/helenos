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
#include <str.h>
#include <vfs/vfs.h>
#include <unistd.h>
#include <dirent.h>
#include <loc.h>
#include <sys/types.h>
#include "../tester.h"

#define TEST_DIRECTORY  "/tmp/testdir"
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
	aoff64_t pos = 0;
	int rc;

	rc = vfs_link_path(TEST_DIRECTORY, KIND_DIRECTORY, NULL);
	if (rc != EOK) {
		TPRINTF("rc=%d\n", rc);
		return "vfs_link_path() failed";
	}
	TPRINTF("Created directory %s\n", TEST_DIRECTORY);
	
	int fd0 = vfs_lookup_open(TEST_FILE, WALK_REGULAR | WALK_MAY_CREATE,
	    MODE_READ | MODE_WRITE);
	if (fd0 < 0)
		return "vfs_lookup_open() failed";
	TPRINTF("Created file %s (fd=%d)\n", TEST_FILE, fd0);
	
	size_t size = sizeof(text);
	ssize_t cnt = vfs_write(fd0, &pos, text, size);
	if (cnt < 0)
		return "write() failed";
	TPRINTF("Written %zd bytes\n", cnt);

	pos = 0;
	
	char buf[BUF_SIZE];
	TPRINTF("read..\n");
	while ((cnt = vfs_read(fd0, &pos, buf, BUF_SIZE))) {
		TPRINTF("read returns %zd\n", cnt);
		if (cnt < 0)
			return "read() failed";
		
		int _cnt = (int) cnt;
		if (_cnt != cnt) {
			/* Count overflow, just to be sure. */
			TPRINTF("Read %zd bytes\n", cnt);
		} else {
			TPRINTF("Read %zd bytes: \"%.*s\"\n", cnt, _cnt, buf);
		}
	}
	
	vfs_put(fd0);
	
	const char *rv = read_root();
	if (rv != NULL)
		return rv;
	
	if (vfs_rename_path(TEST_FILE, TEST_FILE2) != EOK)
		return "vfs_rename_path() failed";
	TPRINTF("Renamed %s to %s\n", TEST_FILE, TEST_FILE2);
	
	if (vfs_unlink_path(TEST_FILE2) != EOK)
		return "vfs_unlink_path() failed";
	TPRINTF("Unlinked %s\n", TEST_FILE2);
	
	if (vfs_unlink_path(TEST_DIRECTORY) != EOK)
		return "vfs_unlink_path() failed";
	TPRINTF("Removed directory %s\n", TEST_DIRECTORY);
	
	rv = read_root();
	if (rv != NULL)
		return rv;
	
	return NULL;
}
