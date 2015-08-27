/*
 * Copyright © 2014 Red Hat, Inc.
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

#define _GNU_SOURCE
#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libudev.h>

#include <libevdev/libevdev.h>
#include <libinput-util.h>

#include "shared.h"

enum options {
	OPT_DEVICE,
	OPT_UDEV,
	OPT_GRAB,
	OPT_HELP,
	OPT_VERBOSE,
	OPT_TAP_ENABLE,
	OPT_TAP_DISABLE,
	OPT_DRAG_LOCK_ENABLE,
	OPT_DRAG_LOCK_DISABLE,
	OPT_NATURAL_SCROLL_ENABLE,
	OPT_NATURAL_SCROLL_DISABLE,
	OPT_LEFT_HANDED_ENABLE,
	OPT_LEFT_HANDED_DISABLE,
	OPT_MIDDLEBUTTON_ENABLE,
	OPT_MIDDLEBUTTON_DISABLE,
	OPT_DWT_ENABLE,
	OPT_DWT_DISABLE,
	OPT_CLICK_METHOD,
	OPT_SCROLL_METHOD,
	OPT_SCROLL_BUTTON,
	OPT_SPEED,
	OPT_PROFILE,
};

static void
log_handler(struct libinput *li,
	    enum libinput_log_priority priority,
	    const char *format,
	    va_list args)
{
	vprintf(format, args);
}

void
tools_usage()
{
	printf("Usage: %s [options] [--udev [<seat>]|--device /dev/input/event0]\n"
	       "--udev <seat>.... Use udev device discovery (default).\n"
	       "		  Specifying a seat ID is optional.\n"
	       "--device /path/to/device .... open the given device only\n"
	       "\n"
	       "Features:\n"
	       "--enable-tap\n"
	       "--disable-tap.... enable/disable tapping\n"
	       "--enable-drag-lock\n"
	       "--disable-drag-lock.... enable/disable tapping drag lock\n"
	       "--enable-natural-scrolling\n"
	       "--disable-natural-scrolling.... enable/disable natural scrolling\n"
	       "--enable-left-handed\n"
	       "--disable-left-handed.... enable/disable left-handed button configuration\n"
	       "--enable-middlebutton\n"
	       "--disable-middlebutton.... enable/disable middle button emulation\n"
	       "--enable-dwt\n"
	       "--disable-dwt..... enable/disable disable-while-typing\n"
	       "--set-click-method=[none|clickfinger|buttonareas] .... set the desired click method\n"
	       "--set-scroll-method=[none|twofinger|edge|button] ... set the desired scroll method\n"
	       "--set-scroll-button=BTN_MIDDLE ... set the button to the given button code\n"
	       "--set-profile=[adaptive|flat].... set pointer acceleration profile\n"
	       "--set-speed=<value>.... set pointer acceleration speed\n"
	       "\n"
	       "These options apply to all applicable devices, if a feature\n"
	       "is not explicitly specified it is left at each device's default.\n"
	       "\n"
	       "Other options:\n"
	       "--grab .......... Exclusively grab all openend devices\n"
	       "--verbose ....... Print debugging output.\n"
	       "--help .......... Print this help.\n",
		program_invocation_short_name);
}

void
tools_init_context(struct tools_context *context)
{
	struct tools_options *options = &context->options;

	context->user_data = NULL;

	memset(options, 0, sizeof(*options));
	options->tapping = -1;
	options->drag_lock = -1;
	options->natural_scroll = -1;
	options->left_handed = -1;
	options->middlebutton = -1;
	options->dwt = -1;
	options->click_method = -1;
	options->scroll_method = -1;
	options->scroll_button = -1;
	options->backend = BACKEND_UDEV;
	options->seat = "seat0";
	options->speed = 0.0;
	options->profile = LIBINPUT_CONFIG_ACCEL_PROFILE_NONE;
}

int
tools_parse_args(int argc, char **argv, struct tools_context *context)
{
	struct tools_options *options = &context->options;

	while (1) {
		int c;
		int option_index = 0;
		static struct option opts[] = {
			{ "device", 1, 0, OPT_DEVICE },
			{ "udev", 0, 0, OPT_UDEV },
			{ "grab", 0, 0, OPT_GRAB },
			{ "help", 0, 0, OPT_HELP },
			{ "verbose", 0, 0, OPT_VERBOSE },
			{ "enable-tap", 0, 0, OPT_TAP_ENABLE },
			{ "disable-tap", 0, 0, OPT_TAP_DISABLE },
			{ "enable-drag-lock", 0, 0, OPT_DRAG_LOCK_ENABLE },
			{ "disable-drag-lock", 0, 0, OPT_DRAG_LOCK_DISABLE },
			{ "enable-natural-scrolling", 0, 0, OPT_NATURAL_SCROLL_ENABLE },
			{ "disable-natural-scrolling", 0, 0, OPT_NATURAL_SCROLL_DISABLE },
			{ "enable-left-handed", 0, 0, OPT_LEFT_HANDED_ENABLE },
			{ "disable-left-handed", 0, 0, OPT_LEFT_HANDED_DISABLE },
			{ "enable-middlebutton", 0, 0, OPT_MIDDLEBUTTON_ENABLE },
			{ "disable-middlebutton", 0, 0, OPT_MIDDLEBUTTON_DISABLE },
			{ "enable-dwt", 0, 0, OPT_DWT_ENABLE },
			{ "disable-dwt", 0, 0, OPT_DWT_DISABLE },
			{ "set-click-method", 1, 0, OPT_CLICK_METHOD },
			{ "set-scroll-method", 1, 0, OPT_SCROLL_METHOD },
			{ "set-scroll-button", 1, 0, OPT_SCROLL_BUTTON },
			{ "set-profile", 1, 0, OPT_PROFILE },
			{ "speed", 1, 0, OPT_SPEED },
			{ 0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "h", opts, &option_index);
		if (c == -1)
			break;

		switch(c) {
		case 'h':
		case OPT_HELP:
			tools_usage();
			exit(0);
		case OPT_DEVICE:
			options->backend = BACKEND_DEVICE;
			if (!optarg) {
				tools_usage();
				return 1;
			}
			options->device = optarg;
			break;
		case OPT_UDEV:
			options->backend = BACKEND_UDEV;
			if (optarg)
				options->seat = optarg;
			break;
		case OPT_GRAB:
			options->grab = 1;
			break;
		case OPT_VERBOSE:
			options->verbose = 1;
			break;
		case OPT_TAP_ENABLE:
			options->tapping = 1;
			break;
		case OPT_TAP_DISABLE:
			options->tapping = 0;
			break;
		case OPT_DRAG_LOCK_ENABLE:
			options->drag_lock = 1;
			break;
		case OPT_DRAG_LOCK_DISABLE:
			options->drag_lock = 0;
			break;
		case OPT_NATURAL_SCROLL_ENABLE:
			options->natural_scroll = 1;
			break;
		case OPT_NATURAL_SCROLL_DISABLE:
			options->natural_scroll = 0;
			break;
		case OPT_LEFT_HANDED_ENABLE:
			options->left_handed = 1;
			break;
		case OPT_LEFT_HANDED_DISABLE:
			options->left_handed = 0;
			break;
		case OPT_MIDDLEBUTTON_ENABLE:
			options->middlebutton = 1;
			break;
		case OPT_MIDDLEBUTTON_DISABLE:
			options->middlebutton = 0;
			break;
		case OPT_DWT_ENABLE:
			options->dwt = LIBINPUT_CONFIG_DWT_ENABLED;
			break;
		case OPT_DWT_DISABLE:
			options->dwt = LIBINPUT_CONFIG_DWT_DISABLED;
			break;
		case OPT_CLICK_METHOD:
			if (!optarg) {
				tools_usage();
				return 1;
			}
			if (streq(optarg, "none")) {
				options->click_method =
				LIBINPUT_CONFIG_CLICK_METHOD_NONE;
			} else if (streq(optarg, "clickfinger")) {
				options->click_method =
				LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
			} else if (streq(optarg, "buttonareas")) {
				options->click_method =
				LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
			} else {
				tools_usage();
				return 1;
			}
			break;
		case OPT_SCROLL_METHOD:
			if (!optarg) {
				tools_usage();
				return 1;
			}
			if (streq(optarg, "none")) {
				options->scroll_method =
				LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
			} else if (streq(optarg, "twofinger")) {
				options->scroll_method =
				LIBINPUT_CONFIG_SCROLL_2FG;
			} else if (streq(optarg, "edge")) {
				options->scroll_method =
				LIBINPUT_CONFIG_SCROLL_EDGE;
			} else if (streq(optarg, "button")) {
				options->scroll_method =
				LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
			} else {
				tools_usage();
				return 1;
			}
			break;
		case OPT_SCROLL_BUTTON:
			if (!optarg) {
				tools_usage();
				return 1;
			}
			options->scroll_button =
			libevdev_event_code_from_name(EV_KEY,
						      optarg);
			if (options->scroll_button == -1) {
				fprintf(stderr,
					"Invalid button %s\n",
					optarg);
				return 1;
			}
			break;
		case OPT_SPEED:
			if (!optarg) {
				tools_usage();
				return 1;
			}
			options->speed = atof(optarg);
			break;
		case OPT_PROFILE:
			if (!optarg) {
				tools_usage();
				return 1;
			}
			if (streq(optarg, "adaptive")) {
				options->profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
			} else if (streq(optarg, "flat")) {
				options->profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
			} else {
				tools_usage();
				return 1;
			}
			break;
		default:
			tools_usage();
			return 1;
		}

	}

	if (optind < argc) {
		tools_usage();
		return 1;
	}

	return 0;
}

static struct libinput *
open_udev(const struct libinput_interface *interface,
	  void *userdata,
	  const char *seat,
	  int verbose)
{
	struct libinput *li;
	struct udev *udev = udev_new();

	if (!udev) {
		fprintf(stderr, "Failed to initialize udev\n");
		return NULL;
	}

	li = libinput_udev_create_context(interface, userdata, udev);
	if (!li) {
		fprintf(stderr, "Failed to initialize context from udev\n");
		goto out;
	}

	if (verbose) {
		libinput_log_set_handler(li, log_handler);
		libinput_log_set_priority(li, LIBINPUT_LOG_PRIORITY_DEBUG);
	}

	if (libinput_udev_assign_seat(li, seat)) {
		fprintf(stderr, "Failed to set seat\n");
		libinput_unref(li);
		li = NULL;
		goto out;
	}

out:
	udev_unref(udev);
	return li;
}

static struct libinput *
open_device(const struct libinput_interface *interface,
	    void *userdata,
	    const char *path,
	    int verbose)
{
	struct libinput_device *device;
	struct libinput *li;

	li = libinput_path_create_context(interface, userdata);
	if (!li) {
		fprintf(stderr, "Failed to initialize context from %s\n", path);
		return NULL;
	}

	if (verbose) {
		libinput_log_set_handler(li, log_handler);
		libinput_log_set_priority(li, LIBINPUT_LOG_PRIORITY_DEBUG);
	}

	device = libinput_path_add_device(li, path);
	if (!device) {
		fprintf(stderr, "Failed to initialized device %s\n", path);
		libinput_unref(li);
		li = NULL;
	}

	return li;
}

static int
open_restricted(const char *path, int flags, void *user_data)
{
	const struct tools_context *context = user_data;
	int fd = open(path, flags);

	if (fd < 0)
		fprintf(stderr, "Failed to open %s (%s)\n",
			path, strerror(errno));
	else if (context->options.grab &&
		 ioctl(fd, EVIOCGRAB, (void*)1) == -1)
		fprintf(stderr, "Grab requested, but failed for %s (%s)\n",
			path, strerror(errno));

	return fd < 0 ? -errno : fd;
}

static void
close_restricted(int fd, void *user_data)
{
	close(fd);
}

static const struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

struct libinput *
tools_open_backend(struct tools_context *context)
{
	struct libinput *li = NULL;
	struct tools_options *options = &context->options;

	if (options->backend == BACKEND_UDEV) {
		li = open_udev(&interface, context, options->seat, options->verbose);
	} else if (options->backend == BACKEND_DEVICE) {
		li = open_device(&interface, context, options->device, options->verbose);
	} else
		abort();

	return li;
}

void
tools_device_apply_config(struct libinput_device *device,
			  struct tools_options *options)
{
	if (options->tapping != -1)
		libinput_device_config_tap_set_enabled(device, options->tapping);
	if (options->drag_lock != -1)
		libinput_device_config_tap_set_drag_lock_enabled(device,
								 options->drag_lock);
	if (options->natural_scroll != -1)
		libinput_device_config_scroll_set_natural_scroll_enabled(device,
									 options->natural_scroll);
	if (options->left_handed != -1)
		libinput_device_config_left_handed_set(device, options->left_handed);
	if (options->middlebutton != -1)
		libinput_device_config_middle_emulation_set_enabled(device,
								    options->middlebutton);

	if (options->dwt != -1)
		libinput_device_config_dwt_set_enabled(device, options->dwt);

	if (options->click_method != -1)
		libinput_device_config_click_set_method(device, options->click_method);

	if (options->scroll_method != -1)
		libinput_device_config_scroll_set_method(device,
							 options->scroll_method);
	if (options->scroll_button != -1)
		libinput_device_config_scroll_set_button(device,
							 options->scroll_button);

	if (libinput_device_config_accel_is_available(device)) {
		libinput_device_config_accel_set_speed(device,
						       options->speed);
		if (options->profile != LIBINPUT_CONFIG_ACCEL_PROFILE_NONE)
			libinput_device_config_accel_set_profile(device,
								 options->profile);
	}
}
