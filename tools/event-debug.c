/*
 * Copyright © 2014 Red Hat, Inc.
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

#include "shared.h"

uint32_t start_time;
static const uint32_t screen_width = 100;
static const uint32_t screen_height = 100;
struct tools_options options;
static unsigned int stop = 0;

static int
open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);
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
	case LIBINPUT_EVENT_TABLET_AXIS:
		type = "TABLET_AXIS";
		break;
	case LIBINPUT_EVENT_TABLET_PROXIMITY:
		type = "TABLET_PROXIMITY";
		break;
	case LIBINPUT_EVENT_TABLET_BUTTON:
		type = "TABLET_BUTTON";
		break;
	}

	printf("%-7s	%-16s ", libinput_device_get_sysname(dev), type);
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
	uint32_t scroll_methods;
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
					   LIBINPUT_DEVICE_CAP_TABLET))
		printf("T");

	if (libinput_device_get_size(dev, &w, &h) == 0)
		printf("\tsize %.2f/%.2fmm", w, h);

	if (libinput_device_config_tap_get_finger_count((dev)))
	    printf(" tap");
	if (libinput_device_config_left_handed_is_available((dev)))
	    printf(" left");
	if (libinput_device_config_scroll_has_natural_scroll((dev)))
	    printf(" scroll-nat");
	if (libinput_device_config_calibration_has_matrix((dev)))
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

	printf("\n");

}

static void
print_key_event(struct libinput_event *ev)
{
	struct libinput_event_keyboard *k = libinput_event_get_keyboard_event(ev);
	enum libinput_key_state state;

	print_event_time(libinput_event_keyboard_get_time(k));
	state = libinput_event_keyboard_get_key_state(k);
	printf("%d %s\n",
	       libinput_event_keyboard_get_key(k),
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
print_pointer_button_event(struct libinput_event *ev)
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
print_tablet_button_event(struct libinput_event *ev)
{
	struct libinput_event_tablet *p = libinput_event_get_tablet_event(ev);
	enum libinput_button_state state;

	print_event_time(libinput_event_tablet_get_time(p));

	state = libinput_event_tablet_get_button_state(p);
	printf("%3d %s, seat count: %u\n",
	       libinput_event_tablet_get_button(p),
	       state == LIBINPUT_BUTTON_STATE_PRESSED ? "pressed" : "released",
	       libinput_event_tablet_get_seat_button_count(p));
}

static void
print_pointer_axis_event(struct libinput_event *ev)
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

static const char*
tablet_axis_changed_sym(struct libinput_event_tablet *t,
			enum libinput_tablet_axis axis)
{
	if (libinput_event_tablet_axis_has_changed(t, axis))
		return "*";
	else
		return "";
}

static void
print_tablet_axis_event(struct libinput_event *ev)
{
	struct libinput_event_tablet *t = libinput_event_get_tablet_event(ev);
	double x, y;
	double dist, pressure;
	double rotation, slider;

	print_event_time(libinput_event_tablet_get_time(t));

	x = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_X);
	y = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_Y);
	printf("\t%.2f%s/%.2f%s",
	       x, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_X),
	       y, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_Y));

	x = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_TILT_X);
	y = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_TILT_Y);
	printf("\ttilt: %.2f%s/%.2f%s ",
	       x, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_TILT_X),
	       y, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_TILT_Y));

	dist = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_DISTANCE);
	pressure = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_PRESSURE);
	if (dist)
		printf("distance: %.2f%s",
		       dist, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_DISTANCE));
	else
		printf("pressure: %.2f%s",
		       pressure, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_PRESSURE));

	rotation = libinput_event_tablet_get_axis_value(t,
					LIBINPUT_TABLET_AXIS_ROTATION_Z);
	printf(" rotation: %.2f%s",
	       rotation,
	       tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_ROTATION_Z));

	slider = libinput_event_tablet_get_axis_value(t,
					LIBINPUT_TABLET_AXIS_SLIDER);
	printf(" slider: %.2f%s",
	       slider,
	       tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_SLIDER));

	printf("\n");
}

static void
print_touch_event_without_coords(struct libinput_event *ev)
{
	struct libinput_event_touch *t = libinput_event_get_touch_event(ev);

	print_event_time(libinput_event_touch_get_time(t));
	printf("\n");
}

static void
print_proximity_event(struct libinput_event *ev)
{
	struct libinput_event_tablet *t = libinput_event_get_tablet_event(ev);
	struct libinput_tool *tool = libinput_event_tablet_get_tool(t);
	enum libinput_tool_proximity_state state;
	const char *tool_str,
	           *state_str;
	double x, y;
	double dist, pressure;

	switch (libinput_tool_get_type(tool)) {
	case LIBINPUT_TOOL_PEN:
		tool_str = "pen";
		break;
	case LIBINPUT_TOOL_ERASER:
		tool_str = "eraser";
		break;
	case LIBINPUT_TOOL_BRUSH:
		tool_str = "brush";
		break;
	case LIBINPUT_TOOL_PENCIL:
		tool_str = "pencil";
		break;
	case LIBINPUT_TOOL_AIRBRUSH:
		tool_str = "airbrush";
		break;
	case LIBINPUT_TOOL_FINGER:
		tool_str = "finger";
		break;
	case LIBINPUT_TOOL_MOUSE:
		tool_str = "mouse";
		break;
	case LIBINPUT_TOOL_LENS:
		tool_str = "lens";
		break;
	default:
		abort();
	}

	state = libinput_event_tablet_get_proximity_state(t);

	print_event_time(libinput_event_tablet_get_time(t));

	if (state == LIBINPUT_TOOL_PROXIMITY_IN) {
		x = libinput_event_tablet_get_axis_value(
					t, LIBINPUT_TABLET_AXIS_X);
		y = libinput_event_tablet_get_axis_value(
					t, LIBINPUT_TABLET_AXIS_Y);
		printf("\t%.2f/%.2f", x, y);

		x = libinput_event_tablet_get_axis_value(
					t, LIBINPUT_TABLET_AXIS_TILT_X);
		y = libinput_event_tablet_get_axis_value(
					t, LIBINPUT_TABLET_AXIS_TILT_Y);
		printf("\ttilt: %.2f/%.2f ", x, y);

		dist = libinput_event_tablet_get_axis_value(
					t, LIBINPUT_TABLET_AXIS_DISTANCE);
		pressure = libinput_event_tablet_get_axis_value(
					t, LIBINPUT_TABLET_AXIS_PRESSURE);

		if (dist)
			printf("\tdistance: %.2f ", dist);
		else
			printf("\tpressure: %.2f ", pressure);

		state_str = "proximity-in";
	} else if (state == LIBINPUT_TOOL_PROXIMITY_OUT) {
		state_str = "proximity-out";
		printf("\t");
	} else {
		abort();
	}

	printf("%s (%#x) %s",
	       tool_str, libinput_tool_get_serial(tool), state_str);

	printf("\taxes:");
	if (libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_DISTANCE))
		printf("d");
	if (libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_PRESSURE))
		printf("p");
	if (libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_TILT_X) ||
	    libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_TILT_Y))
		printf("t");
	if (libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_ROTATION_Z))
		printf("r");
	if (libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_SLIDER))
		printf("s");

	printf("\tbtn:");
	if (libinput_tool_has_button(tool, BTN_TOUCH))
		printf("T");
	if (libinput_tool_has_button(tool, BTN_STYLUS))
		printf("S");
	if (libinput_tool_has_button(tool, BTN_STYLUS2))
		printf("S2");
	if (libinput_tool_has_button(tool, BTN_LEFT))
		printf("L");
	if (libinput_tool_has_button(tool, BTN_MIDDLE))
		printf("M");
	if (libinput_tool_has_button(tool, BTN_RIGHT))
		printf("R");
	if (libinput_tool_has_button(tool, BTN_SIDE))
		printf("Sd");
	if (libinput_tool_has_button(tool, BTN_EXTRA))
		printf("Ex");

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
						  &options);
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
			print_pointer_button_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_AXIS:
			print_pointer_axis_event(ev);
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
		case LIBINPUT_EVENT_TABLET_AXIS:
			print_tablet_axis_event(ev);
			break;
		case LIBINPUT_EVENT_TABLET_PROXIMITY:
			print_proximity_event(ev);
			break;
		case LIBINPUT_EVENT_TABLET_BUTTON:
			print_tablet_button_event(ev);
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

	tools_init_options(&options);

	if (tools_parse_args(argc, argv, &options))
		return 1;

	li = tools_open_backend(&options, NULL, &interface);
	if (!li)
		return 1;

	clock_gettime(CLOCK_MONOTONIC, &tp);
	start_time = tp.tv_sec * 1000 + tp.tv_nsec / 1000000;

	mainloop(li);

	libinput_unref(li);

	return 0;
}
