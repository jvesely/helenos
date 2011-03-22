/* Copyright (c) 2008, Tim Post <tinkertim@gmail.com>
 * Copyright (c) 2011, Martin Sucha
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the original program's authors nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <str.h>
#include <fcntl.h>
#include <io/console.h>
#include <io/color.h>
#include <io/style.h>
#include <errno.h>
#include <vfs/vfs.h>
#include <assert.h>

#include "config.h"
#include "util.h"
#include "errors.h"
#include "entry.h"
#include "cat.h"
#include "cmds.h"

static const char *cmdname = "cat";
#define CAT_VERSION "0.0.1"
#define CAT_DEFAULT_BUFLEN 1024

static const char *cat_oops = "That option is not yet supported\n";
static const char *hexchars = "0123456789abcdef";

static size_t chars_per_screen = 0;
static size_t chars_remaining = 0;

static struct option const long_options[] = {
	{ "help", no_argument, 0, 'h' },
	{ "version", no_argument, 0, 'v' },
	{ "head", required_argument, 0, 'H' },
	{ "tail", required_argument, 0, 't' },
	{ "buffer", required_argument, 0, 'b' },
	{ "more", no_argument, 0, 'm' },
	{ "hex", no_argument, 0, 'x' },
	{ 0, 0, 0, 0 }
};

/* Dispays help for cat in various levels */
void help_cmd_cat(unsigned int level)
{
	if (level == HELP_SHORT) {
		printf("`%s' shows the contents of files\n", cmdname);
	} else {
		help_cmd_cat(HELP_SHORT);
		printf(
		"Usage:  %s [options] <file1> [file2] [...]\n"
		"Options:\n"
		"  -h, --help       A short option summary\n"
		"  -v, --version    Print version information and exit\n"
		"  -H, --head ##    Print only the first ## bytes\n"
		"  -t, --tail ##    Print only the last ## bytes\n"
		"  -b, --buffer ##  Set the read buffer size to ##\n"
		"  -m, --more       Pause after each screen full\n"
		"  -x, --hex        Print bytes as hex values\n"
		"Currently, %s is under development, some options don't work.\n",
		cmdname, cmdname);
	}

	return;
}

static void waitprompt()
{
	sysarg_t rows, cols;
	if (console_get_size(fphone(stdout), &cols, &rows) == EOK) {
		console_set_pos(fphone(stdout), 0, rows-1);
	}
	console_set_color(fphone(stdout), COLOR_BLUE, COLOR_WHITE, 0);
	printf("Press any key to continue");
	fflush(stdout);
	console_set_style(fphone(stdout), STYLE_NORMAL);
}

static void waitkey()
{
	console_event_t ev;
	
	while (true) {
		if (!console_get_event(fphone(stdin), &ev)) {
			return;
		}
		if (ev.type == KEY_PRESS) {
			return;
		}
	}
	assert(false);
}

static void newpage()
{
	console_clear(fphone(stdout));
	chars_remaining = chars_per_screen;
}

static void paged_char(wchar_t c)
{
	putchar(c);
	if (chars_per_screen > 0) {
		chars_remaining--;
		if (chars_remaining == 0) {
			fflush(stdout);
			waitprompt();
			waitkey();
			newpage();
		}
	}
}

static unsigned int cat_file(const char *fname, size_t blen, bool hex)
{
	int fd, bytes = 0, count = 0, reads = 0;
	off64_t total = 0;
	char *buff = NULL;
	int i;
	size_t offset = 0;

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		printf("Unable to open %s\n", fname);
		return 1;
	}

	total = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	if (NULL == (buff = (char *) malloc(blen + 1))) {
		close(fd);
		printf("Unable to allocate enough memory to read %s\n",
			fname);
		return 1;
	}

	do {
		bytes = read(fd, buff, blen);
		if (bytes > 0) {
			count += bytes;
			buff[bytes] = '\0';
			offset = 0;
			for (i = 0; i < bytes; i++) {
				if (hex) {
					paged_char(hexchars[((uint8_t)buff[i])/16]);
					paged_char(hexchars[((uint8_t)buff[i])%16]);
				}
				else {
					wchar_t c = str_decode(buff, &offset, bytes);
					if (c == 0) {
						// reached end of string
						break;
					}
					paged_char(c);
				}
				
			}
			reads++;
		}
	} while (bytes > 0);

	close(fd);
	if (bytes == -1) {
		printf("Error reading %s\n", fname);
		free(buff);
		return 1;
	}

	free(buff);

	return 0;
}

/* Main entry point for cat, accepts an array of arguments */
int cmd_cat(char **argv)
{
	unsigned int argc, i, ret = 0, buffer = 0;
	int c, opt_ind;
	bool hex = false;
	bool more = false;
	sysarg_t rows, cols;
	int rc;

	argc = cli_count_args(argv);

	for (c = 0, optind = 0, opt_ind = 0; c != -1;) {
		c = getopt_long(argc, argv, "xhvmH:t:b:", long_options, &opt_ind);
		switch (c) {
		case 'h':
			help_cmd_cat(HELP_LONG);
			return CMD_SUCCESS;
		case 'v':
			printf("%s\n", CAT_VERSION);
			return CMD_SUCCESS;
		case 'H':
			printf("%s", cat_oops);
			return CMD_FAILURE;
		case 't':
			printf("%s", cat_oops);
			return CMD_FAILURE;
		case 'b':
			printf("%s", cat_oops);
			break;
		case 'm':
			more = true;
			break;
		case 'x':
			hex = true;
			break;
		}
	}

	argc -= optind;

	if (argc < 1) {
		printf("%s - incorrect number of arguments. Try `%s --help'\n",
			cmdname, cmdname);
		return CMD_FAILURE;
	}

	if (buffer <= 0)
		buffer = CAT_DEFAULT_BUFLEN;
	
	if (more) {
		rc = console_get_size(fphone(stdout), &cols, &rows);
		if (rc != EOK) {
			printf("%s - cannot get console size\n", cmdname);
			return CMD_FAILURE;
		}
		chars_per_screen = cols * (rows-1);
		newpage();
	}

	for (i = optind; argv[i] != NULL; i++)
		ret += cat_file(argv[i], buffer, hex);

	if (ret)
		return CMD_FAILURE;
	else
		return CMD_SUCCESS;
}

