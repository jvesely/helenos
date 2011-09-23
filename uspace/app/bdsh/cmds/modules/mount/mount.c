/*
 * Copyright (c) 2009 Jakub Jermar
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

#include <stdio.h>
#include <stdlib.h>
#include <vfs/vfs.h>
#include <errno.h>
#include <getopt.h>
#include "config.h"
#include "util.h"
#include "errors.h"
#include "entry.h"
#include "mount.h"
#include "cmds.h"

static const char *cmdname = "mount";

static struct option const long_options[] = {
	{ "help", no_argument, 0, 'h' },
	{ "instance", required_argument, 0, 'i' },
	{ 0, 0, 0, 0 }
};


/* Displays help for mount in various levels */
void help_cmd_mount(unsigned int level)
{
	static char helpfmt[] =
	    "Usage:  %s <fstype> <mp> [dev] [<moptions>]\n";
	if (level == HELP_SHORT) {
		printf("'%s' mounts a file system.\n", cmdname);
	} else {
		help_cmd_mount(HELP_SHORT);
		printf(helpfmt, cmdname);
	}
	return;
}

/* Main entry point for mount, accepts an array of arguments */
int cmd_mount(char **argv)
{
	unsigned int argc;
	const char *mopts = "";
	const char *dev = "";
	int rc, c, opt_ind;
	unsigned int instance = 0;
	bool instance_set = false;
	char **t_argv;

	argc = cli_count_args(argv);

	for (c = 0, optind = 0, opt_ind = 0; c != -1;) {
		c = getopt_long(argc, argv, "i:h", long_options, &opt_ind);
		switch (c) {
		case 'h':
			help_cmd_mount(HELP_LONG);
			return CMD_SUCCESS;
		case 'i':
			instance = (unsigned int) strtol(optarg, NULL, 10);
			instance_set = true;
			break;
		}
	}

	if (instance_set) {
		argc -= 2;
		t_argv = &argv[2];
	} else
		t_argv = &argv[0];

	if ((argc < 3) || (argc > 5)) {
		printf("%s: invalid number of arguments. Try `mount --help'\n",
		    cmdname);
		return CMD_FAILURE;
	}
	if (argc > 3)
		dev = t_argv[3];
	if (argc == 5)
		mopts = t_argv[4];

	rc = mount(t_argv[1], t_argv[2], dev, mopts, 0, instance);
	if (rc != EOK) {
		printf("Unable to mount %s filesystem to %s on %s (rc=%d)\n",
		    t_argv[1], t_argv[2], t_argv[3], rc);
		return CMD_FAILURE;
	}

	return CMD_SUCCESS;
}

