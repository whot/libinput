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
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libudev.h>
#include "linux/input.h"
#include <sys/ioctl.h>

#include <libinput.h>
#include <libevdev/libevdev.h>

#include "shared.h"

uint32_t start_time;
static const uint32_t screen_width = 100;
static const uint32_t screen_height = 100;
struct tools_context context;
static unsigned int stop = 0;

static void
print_event_header(struct libinput_event *ev)
{
	struct libinput_device *dev = libinput_event_get_device(ev);
	const char *type = NULL;

	switch(libinput_event_get_type(ev)) {
	case LIBINPUT_EVENT_NONE:
		abort();
	case LIBINPUT_EVENT_DEVICE_ADDED:
		type = "DEVICE_ADDED";
		break;
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		type = "DEVICE_REMOVED";
		break;
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		type = "KEYBOARD_KEY";
		break;
	case LIBINPUT_EVENT_POINTER_MOTION:
		type = "POINTER_MOTION";
		break;
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		type = "POINTER_MOTION_ABSOLUTE";
		break;
	case LIBINPUT_EVENT_POINTER_BUTTON:
		type = "POINTER_BUTTON";
		break;
	case LIBINPUT_EVENT_POINTER_AXIS:
		type = "POINTER_AXIS";
		break;
	case LIBINPUT_EVENT_TOUCH_DOWN:
		type = "TOUCH_DOWN";
		break;
	case LIBINPUT_EVENT_TOUCH_MOTION:
		type = "TOUCH_MOTION";
		break;
	case LIBINPUT_EVENT_TOUCH_UP:
		type = "TOUCH_UP";
		break;
	case LIBINPUT_EVENT_TOUCH_CANCEL:
		type = "TOUCH_CANCEL";
		break;
	case LIBINPUT_EVENT_TOUCH_FRAME:
		type = "TOUCH_FRAME";
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
		type = "GESTURE_SWIPE_BEGIN";
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
		type = "GESTURE_SWIPE_UPDATE";
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_END:
		type = "GESTURE_SWIPE_END";
		break;
	}

	printf("%-7s	%s	", libinput_device_get_sysname(dev), type);
}

static void
print_event_time(uint32_t time)
{
	printf("%+6.2fs	", (time - start_time) / 1000.0);
}

static void
print_device_notify(struct libinput_event *ev)
{
	struct libinput_device *dev = libinput_event_get_device(ev);
	struct libinput_seat *seat = libinput_device_get_seat(dev);
	struct libinput_device_group *group;
	double w, h;
	uint32_t scroll_methods, click_methods;
	static int next_group_id = 0;
	intptr_t group_id;

	group = libinput_device_get_device_group(dev);
	group_id = (intptr_t)libinput_device_group_get_user_data(group);
	if (!group_id) {
		group_id = ++next_group_id;
		libinput_device_group_set_user_data(group, (void*)group_id);
	}

	printf("%-33s %5s %7s group%d",
	       libinput_device_get_name(dev),
	       libinput_seat_get_physical_name(seat),
	       libinput_seat_get_logical_name(seat),
	       (int)group_id);

	printf(" cap:");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_KEYBOARD))
		printf("k");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_POINTER))
		printf("p");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_TOUCH))
		printf("t");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_GESTURE))
		printf("g");

	if (libinput_device_get_size(dev, &w, &h) == 0)
		printf("\tsize %.2f/%.2fmm", w, h);

	if (libinput_device_config_tap_get_finger_count(dev)) {
	    printf(" tap");
	    if (libinput_device_config_tap_get_drag_lock_enabled(dev))
		    printf("(dl on)");
	    else
		    printf("(dl off)");
	}
	if (libinput_device_config_left_handed_is_available(dev))
	    printf(" left");
	if (libinput_device_config_scroll_has_natural_scroll(dev))
	    printf(" scroll-nat");
	if (libinput_device_config_calibration_has_matrix(dev))
	    printf(" calib");

	scroll_methods = libinput_device_config_scroll_get_methods(dev);
	if (scroll_methods != LIBINPUT_CONFIG_SCROLL_NO_SCROLL) {
		printf(" scroll");
		if (scroll_methods & LIBINPUT_CONFIG_SCROLL_2FG)
			printf("-2fg");
		if (scroll_methods & LIBINPUT_CONFIG_SCROLL_EDGE)
			printf("-edge");
		if (scroll_methods & LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN)
			printf("-button");
	}

	click_methods = libinput_device_config_click_get_methods(dev);
	if (click_methods != LIBINPUT_CONFIG_CLICK_METHOD_NONE) {
		printf(" click");
		if (click_methods & LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS)
			printf("-buttonareas");
		if (click_methods & LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER)
			printf("-clickfinger");
	}

	printf("\n");

}

static void
print_key_event(struct libinput_event *ev)
{
	struct libinput_event_keyboard *k = libinput_event_get_keyboard_event(ev);
	enum libinput_key_state state;
	uint32_t key;
	const char *keyname;

	print_event_time(libinput_event_keyboard_get_time(k));
	state = libinput_event_keyboard_get_key_state(k);

	key = libinput_event_keyboard_get_key(k);
	keyname = libevdev_event_code_get_name(EV_KEY, key);
	printf("%s (%d) %s\n",
	       keyname ? keyname : "???",
	       key,
	       state == LIBINPUT_KEY_STATE_PRESSED ? "pressed" : "released");
}

static void
print_motion_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	double x = libinput_event_pointer_get_dx(p);
	double y = libinput_event_pointer_get_dy(p);

	print_event_time(libinput_event_pointer_get_time(p));

	printf("%6.2f/%6.2f\n", x, y);
}

static void
print_absmotion_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	double x = libinput_event_pointer_get_absolute_x_transformed(
		p, screen_width);
	double y = libinput_event_pointer_get_absolute_y_transformed(
		p, screen_height);

	print_event_time(libinput_event_pointer_get_time(p));
	printf("%6.2f/%6.2f\n", x, y);
}

static void
print_button_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	enum libinput_button_state state;

	print_event_time(libinput_event_pointer_get_time(p));

	state = libinput_event_pointer_get_button_state(p);
	printf("%3d %s, seat count: %u\n",
	       libinput_event_pointer_get_button(p),
	       state == LIBINPUT_BUTTON_STATE_PRESSED ? "pressed" : "released",
	       libinput_event_pointer_get_seat_button_count(p));
}

static void
print_axis_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	double v = 0, h = 0;

	if (libinput_event_pointer_has_axis(p,
				    LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
		v = libinput_event_pointer_get_axis_value(p,
			      LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
	if (libinput_event_pointer_has_axis(p,
				    LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
		h = libinput_event_pointer_get_axis_value(p,
			      LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
	print_event_time(libinput_event_pointer_get_time(p));
	printf("vert %.2f horiz %.2f\n", v, h);
}

static void
print_touch_event_without_coords(struct libinput_event *ev)
{
	struct libinput_event_touch *t = libinput_event_get_touch_event(ev);

	print_event_time(libinput_event_touch_get_time(t));
	printf("\n");
}

static void
print_touch_event_with_coords(struct libinput_event *ev)
{
	struct libinput_event_touch *t = libinput_event_get_touch_event(ev);
	double x = libinput_event_touch_get_x_transformed(t, screen_width);
	double y = libinput_event_touch_get_y_transformed(t, screen_height);
	double xmm = libinput_event_touch_get_x(t);
	double ymm = libinput_event_touch_get_y(t);

	print_event_time(libinput_event_touch_get_time(t));

	printf("%d (%d) %5.2f/%5.2f (%5.2f/%5.2fmm)\n",
	       libinput_event_touch_get_slot(t),
	       libinput_event_touch_get_seat_slot(t),
	       x, y,
	       xmm, ymm);
}

static void
print_gesture_event_without_coords(struct libinput_event *ev)
{
	struct libinput_event_gesture *t = libinput_event_get_gesture_event(ev);

	print_event_time(libinput_event_gesture_get_time(t));
	printf("%d\n", libinput_event_gesture_get_finger_count(t));
}

static void
print_gesture_event_with_coords(struct libinput_event *ev)
{
	struct libinput_event_gesture *t = libinput_event_get_gesture_event(ev);
	double dx = libinput_event_gesture_get_dx(t);
	double dy = libinput_event_gesture_get_dy(t);
	double dx_unaccel = libinput_event_gesture_get_dx_unaccelerated(t);
	double dy_unaccel = libinput_event_gesture_get_dy_unaccelerated(t);

	print_event_time(libinput_event_gesture_get_time(t));

	printf("%d %5.2f/%5.2f (%5.2f/%5.2f unaccelerated)\n",
	       libinput_event_gesture_get_finger_count(t),
	       dx, dy, dx_unaccel, dy_unaccel);
}

static int
handle_and_print_events(struct libinput *li)
{
	int rc = -1;
	struct libinput_event *ev;

	libinput_dispatch(li);
	while ((ev = libinput_get_event(li))) {
		print_event_header(ev);

		switch (libinput_event_get_type(ev)) {
		case LIBINPUT_EVENT_NONE:
			abort();
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			print_device_notify(ev);
			tools_device_apply_config(libinput_event_get_device(ev),
						  &context.options);
			break;
		case LIBINPUT_EVENT_KEYBOARD_KEY:
			print_key_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_MOTION:
			print_motion_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
			print_absmotion_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_BUTTON:
			print_button_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_AXIS:
			print_axis_event(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_DOWN:
			print_touch_event_with_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_MOTION:
			print_touch_event_with_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_UP:
			print_touch_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_CANCEL:
			print_touch_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_FRAME:
			print_touch_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
			print_gesture_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
			print_gesture_event_with_coords(ev);
			break;
		case LIBINPUT_EVENT_GESTURE_SWIPE_END:
			print_gesture_event_without_coords(ev);
			break;
		}

		libinput_event_destroy(ev);
		libinput_dispatch(li);
		rc = 0;
	}
	return rc;
}

static void
sighandler(int signal, siginfo_t *siginfo, void *userdata)
{
	stop = 1;
}

static void
mainloop(struct libinput *li)
{
	struct pollfd fds;
	struct sigaction act;

	fds.fd = libinput_get_fd(li);
	fds.events = POLLIN;
	fds.revents = 0;

	memset(&act, 0, sizeof(act));
	act.sa_sigaction = sighandler;
	act.sa_flags = SA_SIGINFO;

	if (sigaction(SIGINT, &act, NULL) == -1) {
		fprintf(stderr, "Failed to set up signal handling (%s)\n",
				strerror(errno));
		return;
	}

	/* Handle already-pending device added events */
	if (handle_and_print_events(li))
		fprintf(stderr, "Expected device added events on startup but got none. "
				"Maybe you don't have the right permissions?\n");

	while (!stop && poll(&fds, 1, -1) > -1)
		handle_and_print_events(li);
}

int
main(int argc, char **argv)
{
	struct libinput *li;
	struct timespec tp;

	tools_init_context(&context);

	if (tools_parse_args(argc, argv, &context))
		return 1;

	li = tools_open_backend(&context);
	if (!li)
		return 1;

	clock_gettime(CLOCK_MONOTONIC, &tp);
	start_time = tp.tv_sec * 1000 + tp.tv_nsec / 1000000;

	mainloop(li);

	libinput_unref(li);

	return 0;
}
