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

#include "libinput-tool.h"

void
libinput_tool_usage(void)
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
	       "\n");
}

enum command {
	COMMAND_NONE,
	COMMAND_LIST_DEVICES,
	COMMAND_DEBUG_EVENTS,
};

enum global_opts {
	GOPT_HELP = 1,
	GOPT_VERSION,
	GOPT_QUIET,
	GOPT_VERBOSE,
};

static bool
parse_args_cmd(enum command cmd,
	       struct global_options *global_options,
	       int argc, char *argv[])
{
	optind = 0;

	switch (cmd) {
	case COMMAND_NONE:
		break;
	case COMMAND_LIST_DEVICES:
		return libinput_list_devices(global_options, argc, argv);
	case COMMAND_DEBUG_EVENTS:
		return libinput_debug_events(global_options, argc, argv);
	}
	return true;
}

int
main(int argc, char **argv)
{
	enum command cmd = COMMAND_NONE;
	const char *command;
	int option_index = 0;
	struct global_options global_options = {0};

	if (argc == 1) {
		libinput_tool_usage();
		return false;
	}

	while (1) {
		int c;
		static struct option opts[] = {
			{ "help",	no_argument,	0, GOPT_HELP },
			{ "version",	no_argument,	0, GOPT_VERSION },
			{ "quiet",	no_argument,	0, GOPT_QUIET },
			{ "verbose",	no_argument,	0, GOPT_VERBOSE },
			{ 0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "+h", opts, &option_index);
		if (c == -1)
			break;

		switch(c) {
		case 'h':
		case GOPT_HELP:
			libinput_tool_usage();
			exit(0);
		case GOPT_VERSION:
			printf("%s\n", LIBINPUT_VERSION);
			exit(0);
		case GOPT_VERBOSE:
			global_options.verbose = true;
			break;
		case GOPT_QUIET:
			global_options.quiet = true;
			break;
		default:
			libinput_tool_usage();
			return false;
		}
	}

	if (optind > argc) {
		libinput_tool_usage();
		return false;
	}

	command = argv[optind];

	if (streq(command, "list-devices")) {
		cmd = COMMAND_LIST_DEVICES;
	} else if (streq(command, "debug-events")) {
		cmd = COMMAND_DEBUG_EVENTS;
	} else {
		fprintf(stderr, "Invalid command '%s'\n", command);
		return EXIT_FAILURE;
	}

	return parse_args_cmd(cmd, &global_options, argc - optind, &argv[optind]);
}
