/*
 * Copyright © 2015 Red Hat, Inc.
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
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libudev.h>

#include <libinput.h>
#include <libinput-util.h>
#include <libinput-version.h>

#include "shared.h"

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

static const char *
draglock_default(struct libinput_device *device)
{
	if (!libinput_device_config_tap_get_finger_count(device))
		return "n/a";

	if (libinput_device_config_tap_get_default_drag_lock_enabled(device))
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

static const char *
middle_emulation_default(struct libinput_device *device)
{
	if (!libinput_device_config_middle_emulation_is_available(device))
		return "n/a";

	if (libinput_device_config_middle_emulation_get_default_enabled(device))
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
		xasprintf(&str, "n/a");
		return str;
	}

	if (libinput_device_config_calibration_get_default_matrix(device,
						  calibration) == 0) {
		xasprintf(&str, "identity matrix");
		return str;
	}

	xasprintf(&str,
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
		xasprintf(&str, "none");
		return str;
	}

	method = libinput_device_config_scroll_get_default_method(device);

	xasprintf(&str,
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
		xasprintf(&str, "none");
		return str;
	}

	method = libinput_device_config_click_get_default_method(device);
	xasprintf(&str,
		 "%s%s%s%s",
		 (method == LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS) ? "*" : "",
		 (click_methods & LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS) ? "button-areas " : "",
		 (method == LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER) ? "*" : "",
		 (click_methods & LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER) ? "clickfinger " : "");
	return str;
}

static char*
accel_profiles(struct libinput_device *device)
{
	uint32_t profiles;
	char *str;
	enum libinput_config_accel_profile profile;

	if (!libinput_device_config_accel_is_available(device)) {
		xasprintf(&str, "n/a");
		return str;
	}

	profiles = libinput_device_config_accel_get_profiles(device);
	if (profiles == LIBINPUT_CONFIG_ACCEL_PROFILE_NONE) {
		xasprintf(&str, "none");
		return str;
	}

	profile = libinput_device_config_accel_get_default_profile(device);
	xasprintf(&str,
		  "%s%s%s%s",
		  (profile == LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT) ? "*" : "",
		  (profiles & LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT) ? "flat" : "",
		  (profile == LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE) ? "*" : "",
		  (profiles & LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE) ? "adaptive" : "");

	return str;
}

static const char *
dwt_default(struct libinput_device *device)
{
	if (!libinput_device_config_dwt_is_available(device))
		return "n/a";

	if (libinput_device_config_dwt_get_default_enabled(device))
		return "enabled";
	else
		return "disabled";
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

	printf("Device:           %s\n"
	       "Kernel:           %s\n"
	       "Group:            %d\n"
	       "Seat:             %s, %s\n",
	       libinput_device_get_name(dev),
	       devnode,
	       (int)group_id,
	       libinput_seat_get_physical_name(seat),
	       libinput_seat_get_logical_name(seat));

	if (libinput_device_get_size(dev, &w, &h) == 0)
		printf("Size:             %.2fx%.2fmm\n", w, h);
	printf("Capabilities:     ");
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

	printf("Tap-to-click:     %s\n", tap_default(dev));
	printf("Tap drag lock:    %s\n", draglock_default(dev));
	printf("Left-handed:      %s\n", left_handed_default(dev));
	printf("Nat.scrolling:    %s\n", nat_scroll_default(dev));
	printf("Middle emulation: %s\n", middle_emulation_default(dev));
	str = calibration_default(dev);
	printf("Calibration:      %s\n", str);
	free(str);

	str = scroll_defaults(dev);
	printf("Scroll methods:   %s\n", str);
	free(str);

	str = click_defaults(dev);
	printf("Click methods:    %s\n", str);
	free(str);

	printf("Disable-w-typing: %s\n", dwt_default(dev));

	str = accel_profiles(dev);
	printf("Accel profiles:   %s\n", str);
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
	struct tools_context context;
	struct libinput_event *ev;

	if (argc > 1) {
		if (streq(argv[1], "--help")) {
			usage();
			return 0;
		} else if (streq(argv[1], "--version")) {
			printf("%s\n", LIBINPUT_VERSION);
			return 0;
		} else {
			usage();
			return 1;
		}
	}

	tools_init_context(&context);

	li = tools_open_backend(&context);
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
