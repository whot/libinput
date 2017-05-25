/*
 * Copyright © 2017 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libudev.h>

#include <libinput.h>
#include <libinput-util.h>
#include <libinput-version.h>

static void
usage(void)
{
	printf("Usage: libinput [GLOBAL OPTIONS] [COMMAND] [ARGS]\n"
	       "\n"
	       "This tool creates a libinput context and interacts with that context.\n"
	       "For detailed information about the options below, see the"
	       "libinput(1) man page.\n"
	       "\n"
	       "This tool usually requires access to the /dev/input/eventX nodes.\n"
	       "\n"
	       "Global options:\n"
	       "  --help ...... show this help\n"
	       "  --version ... show version information\n"
	       "  --verbose ... enable verbose output for debugging\n"
	       "  --quiet ..... reduce output (may be used with --verbose)\n"
	       "\n"
	       "Commands:\n"
	       "  list-devices\n"
	       "	List all devices with their default configuration options\n"
	       "\n"
	       "  debug-events\n"
	       "	Print events to stdout\n"
	       "\n"
	       "  debug-gui\n"
	       "	Display a simple GUI to visualize libinput's events.\n"
	       "\n");
}

enum global_opts {
	GOPT_HELP = 1,
	GOPT_VERSION,
};

static inline void
setup_path(void)
{
	const char *path = getenv("PATH");
	char new_path[PATH_MAX];

	snprintf(new_path,
		 sizeof(new_path),
		 "%s:%s",
		 LIBINPUT_TOOL_PATH,
		 path ? path : "");
	setenv("PATH", new_path, 1);
}

static int
exec_command(int real_argc, char **real_argv)
{
	char *argv[64] = {NULL};
	char executable[128];
	const char *command;
	int rc;

	assert((size_t)real_argc < ARRAY_LENGTH(argv));

	command = real_argv[0];

	rc = snprintf(executable, sizeof(executable), "libinput-%s", command);
	if (rc >= (int)sizeof(executable)) {
		usage();
		return EXIT_FAILURE;
	}

	argv[0] = executable;
	for (int i = 1; i < real_argc; i++)
		argv[i] = real_argv[i];

	setup_path();

	rc = execvp(executable, argv);
	fprintf(stderr,
		"Failed to execute '%s' (%s)\n",
		command,
		strerror(errno));
	return EXIT_FAILURE;
}

int
main(int argc, char **argv)
{
	int option_index = 0;

	while (1) {
		int c;
		static struct option opts[] = {
			{ "help",	no_argument,	0, GOPT_HELP },
			{ "version",	no_argument,	0, GOPT_VERSION },
			{ 0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "+h", opts, &option_index);
		if (c == -1)
			break;

		switch(c) {
		case 'h':
		case GOPT_HELP:
			usage();
			return EXIT_SUCCESS;
		case GOPT_VERSION:
			printf("%s\n", LIBINPUT_VERSION);
			return EXIT_SUCCESS;
		default:
			usage();
			return EXIT_FAILURE;
		}
	}

	if (optind >= argc) {
		usage();
		return EXIT_FAILURE;
	}

	argv += optind;
	argc -= optind;

	return exec_command(argc, argv);
}
