/*
 * Copyright © 2015 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libudev.h>

#include <libinput.h>
#include <libinput-version.h>

#include "shared.h"

static int
open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);
	if (fd < 0)
		fprintf(stderr, "Failed to open %s (%s)\n",
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

static inline const char*
bool_to_str(bool b)
{
	if (b)
		return "yes";
	else
		return "no";
}

static const char *
tap_default(struct libinput_device *device)
{
	if (!libinput_device_config_tap_get_finger_count(device))
		return "n/a";

	if (libinput_device_config_tap_get_default_enabled(device))
		return "enabled";
	else
		return "disabled";
}

static const char*
left_handed_default(struct libinput_device *device)
{
	if (!libinput_device_config_left_handed_is_available(device))
		return "n/a";

	if (libinput_device_config_left_handed_get_default(device))
		return "enabled";
	else
		return "disabled";
}

static const char *
nat_scroll_default(struct libinput_device *device)
{
	if (!libinput_device_config_scroll_has_natural_scroll(device))
		return "n/a";

	if (libinput_device_config_scroll_get_default_natural_scroll_enabled(device))
		return "enabled";
	else
		return "disabled";
}

static char *
calibration_default(struct libinput_device *device)
{
	char *str;
	float calibration[6];

	if (!libinput_device_config_calibration_has_matrix(device)) {
		asprintf(&str, "n/a");
		return str;
	}

	if (libinput_device_config_calibration_get_default_matrix(device,
						  calibration) == 0) {
		asprintf(&str, "identity matrix");
		return str;
	}

	asprintf(&str,
		 "%.2f %.2f %.2f %.2f %.2f %.2f",
		 calibration[0],
		 calibration[1],
		 calibration[2],
		 calibration[3],
		 calibration[4],
		 calibration[5]);
	return str;
}

static char *
scroll_defaults(struct libinput_device *device)
{
	uint32_t scroll_methods;
	char *str;
	enum libinput_config_scroll_method method;

	scroll_methods = libinput_device_config_scroll_get_methods(device);
	if (scroll_methods == LIBINPUT_CONFIG_SCROLL_NO_SCROLL) {
		asprintf(&str, "none");
		return str;
	}

	method = libinput_device_config_scroll_get_default_method(device);

	asprintf(&str,
		 "%s%s%s%s%s%s",
		 (method == LIBINPUT_CONFIG_SCROLL_2FG) ? "*" : "",
		 (scroll_methods & LIBINPUT_CONFIG_SCROLL_2FG) ? "two-finger " : "",
		 (method == LIBINPUT_CONFIG_SCROLL_EDGE) ? "*" : "",
		 (scroll_methods & LIBINPUT_CONFIG_SCROLL_EDGE) ? "edge " : "",
		 (method == LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN) ? "*" : "",
		 (scroll_methods & LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN) ? "button" : "");
	return str;
}

static char*
click_defaults(struct libinput_device *device)
{
	uint32_t click_methods;
	char *str;
	enum libinput_config_click_method method;

	click_methods = libinput_device_config_click_get_methods(device);
	if (click_methods == LIBINPUT_CONFIG_CLICK_METHOD_NONE) {
		asprintf(&str, "none");
		return str;
	}

	method = libinput_device_config_click_get_default_method(device);
	asprintf(&str,
		 "%s%s%s%s",
		 (method == LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS) ? "*" : "",
		 (click_methods & LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS) ? "button-areas " : "",
		 (method == LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER) ? "*" : "",
		 (click_methods & LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER) ? "clickfinger " : "");
	return str;
}

static void
print_device_notify(struct libinput_event *ev)
{
	struct libinput_device *dev = libinput_event_get_device(ev);
	struct libinput_seat *seat = libinput_device_get_seat(dev);
	struct libinput_device_group *group;
	double w, h;
	static int next_group_id = 0;
	intptr_t group_id;
	const char *devnode;
	char *str;

	group = libinput_device_get_device_group(dev);
	group_id = (intptr_t)libinput_device_group_get_user_data(group);
	if (!group_id) {
		group_id = ++next_group_id;
		libinput_device_group_set_user_data(group, (void*)group_id);
	}

	devnode = udev_device_get_devnode(
				  libinput_device_get_udev_device(dev));

	printf("Device:         %s\n"
	       "Kernel:         %s\n"
	       "Group:          %d\n"
	       "Seat:           %s, %s\n",
	       libinput_device_get_name(dev),
	       devnode,
	       (int)group_id,
	       libinput_seat_get_physical_name(seat),
	       libinput_seat_get_logical_name(seat));

	if (libinput_device_get_size(dev, &w, &h) == 0)
		printf("Size:           %.2fx%.2fmm\n", w, h);
	printf("Capabilities:   ");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_KEYBOARD))
		printf("keyboard ");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_POINTER))
		printf("pointer ");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_TOUCH))
		printf("touch");
	printf("\n");

	printf("Tap-to-click:   %s\n", tap_default(dev));
	printf("Left-handed:    %s\n", left_handed_default(dev));
	printf("Nat.scrolling:  %s\n", nat_scroll_default(dev));
	str = calibration_default(dev);
	printf("Calibration:    %s\n", str);
	free(str);

	str = scroll_defaults(dev);
	printf("Scroll methods: %s\n", str);
	free(str);

	str = click_defaults(dev);
	printf("Click methods:  %s\n", str);
	free(str);

	printf("\n");
}

static inline void
usage(void)
{
	printf("Usage: %s [--help|--version]\n"
	       "\n"
	       "This tool creates a libinput context on the default seat \"seat0\"\n"
	       "and lists all devices recognized by libinput and the configuration options.\n"
	       "Where multiple options are possible, the default is prefixed with \"*\".\n"
	       "\n"
	       "Options:\n"
	       "--help ...... show this help\n"
	       "--version ... show version information\n"
	       "\n"
	       "This tool requires access to the /dev/input/eventX nodes.\n",
	       program_invocation_short_name);
}

int
main(int argc, char **argv)
{
	struct libinput *li;
	struct tools_options options;
	struct libinput_event *ev;

	if (argc > 1) {
		if (strcmp(argv[1], "--help") == 0) {
			usage();
			return 0;
		} else if (strcmp(argv[1], "--version") == 0) {
			printf("%s\n", LIBINPUT_VERSION);
			return 0;
		} else {
			usage();
			return 1;
		}
	}

	tools_init_options(&options);

	li = tools_open_backend(&options, NULL, &interface);
	if (!li)
		return 1;

	libinput_dispatch(li);
	while ((ev = libinput_get_event(li))) {

		if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED)
			print_device_notify(ev);

		libinput_event_destroy(ev);
		libinput_dispatch(li);
	}

	libinput_unref(li);

	return 0;
}
