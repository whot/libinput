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
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libudev.h>
#include "linux/input.h"
#include <sys/ioctl.h>
#include <sys/signalfd.h>

#include <libinput.h>

static enum {
	MODE_UDEV,
	MODE_DEVICE,
} mode = MODE_UDEV;
static const char *device;
static const char *seat = "seat0";
static struct udev *udev;
uint32_t start_time;
static const uint32_t screen_width = 100;
static const uint32_t screen_height = 100;
static int verbose = 0;

static void
usage(void)
{
	printf("Usage: %s [--verbose] [--udev [<seat>]|--device /dev/input/event0]\n"
	       "--verbose ....... Print debugging output.\n"
	       "--udev <seat>.... Use udev device discovery (default).\n"
	       "		  Specifying a seat ID is optional.\n"
	       "--device /path/to/device .... open the given device only\n",
		program_invocation_short_name);
}

static int
parse_args(int argc, char **argv)
{
	while (1) {
		int c;
		int option_index = 0;
		static struct option opts[] = {
			{ "device", 1, 0, 'd' },
			{ "udev", 0, 0, 'u' },
			{ "help", 0, 0, 'h' },
			{ "verbose", 0, 0, 'v'},
			{ 0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "h", opts, &option_index);
		if (c == -1)
			break;

		switch(c) {
			case 'h': /* --help */
				usage();
				exit(0);
			case 'd': /* --device */
				mode = MODE_DEVICE;
				if (!optarg) {
					usage();
					return 1;
				}
				device = optarg;
				break;
			case 'u': /* --udev */
				mode = MODE_UDEV;
				if (optarg)
					seat = optarg;
				break;
			case 'v': /* --verbose */
				verbose = 1;
				break;
			default:
				usage();
				return 1;
		}

	}

	if (optind < argc) {
		usage();
		return 1;
	}

	return 0;
}

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

static int
open_udev(struct libinput **li)
{
	udev = udev_new();
	if (!udev) {
		fprintf(stderr, "Failed to initialize udev\n");
		return 1;
	}

	*li = libinput_udev_create_for_seat(&interface, NULL, udev, seat);
	if (!*li) {
		fprintf(stderr, "Failed to initialize context from udev\n");
		return 1;
	}

	return 0;
}

static int
open_device(struct libinput **li, const char *path)
{
	struct libinput_device *device;

	*li = libinput_path_create_context(&interface, NULL);
	if (!*li) {
		fprintf(stderr, "Failed to initialize context from %s\n", path);
		return 1;
	}

	device = libinput_path_add_device(*li, path);
	if (!device) {
		fprintf(stderr, "Failed to initialized device %s\n", path);
		libinput_destroy(*li);
		return 1;
	}

	return 0;
}

static void
print_event_header(struct libinput_event *ev)
{
	struct libinput_device *dev = libinput_event_get_device(ev);
	const char *type;

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
	case LIBINPUT_EVENT_TABLET_AXIS_RELATIVE:
		type = "TABLET_AXIS_RELATIVE";
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_UPDATE:
		type = "TABLET_TOOL_UPDATE";
		break;
	case LIBINPUT_EVENT_TABLET_PROXIMITY_OUT:
		type = "TABLET_PROXIMITY_OUT";
		break;
	case LIBINPUT_EVENT_TABLET_BUTTON:
		type = "TABLET_BUTTON";
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
	double w, h;

	printf("%s	%s",
	       libinput_seat_get_physical_name(seat),
	       libinput_seat_get_logical_name(seat));

	if (libinput_device_get_size(dev, &w, &h) == 0)
		printf("\tsize %.2f/%.2fmm", w, h);

	printf("\n");
}

static void
print_key_event(struct libinput_event *ev)
{
	struct libinput_event_keyboard *k = libinput_event_get_keyboard_event(ev);
	enum libinput_keyboard_key_state state;

	print_event_time(libinput_event_keyboard_get_time(k));
	state = libinput_event_keyboard_get_key_state(k);
	printf("%d %s\n",
	       libinput_event_keyboard_get_key(k),
	       state == LIBINPUT_KEYBOARD_KEY_STATE_PRESSED ? "pressed" : "released");
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
	enum libinput_pointer_axis axis = libinput_event_pointer_get_axis(p);
	const char *ax;
	double val;

	switch (axis) {
	case LIBINPUT_POINTER_AXIS_VERTICAL_SCROLL:
		ax = "vscroll";
		break;
	case LIBINPUT_POINTER_AXIS_HORIZONTAL_SCROLL:
		ax = "hscroll";
		break;
	default:
		abort();
	}

	print_event_time(libinput_event_pointer_get_time(p));
	val = libinput_event_pointer_get_axis_value(p);
	printf("%s %.2f\n", ax, val);
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

	print_event_time(libinput_event_tablet_get_time(t));

	x = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_X);
	y = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_Y);
	printf("\t%.2f%s/%.2f%s",
	       x, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_X),
	       y, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_Y));

	x = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_TILT_VERTICAL);
	y = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_TILT_HORIZONTAL);
	printf("\ttilt: %.2f%s/%.2f%s ",
	       x, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_TILT_VERTICAL),
	       y, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_TILT_HORIZONTAL));

	dist = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_DISTANCE);
	pressure = libinput_event_tablet_get_axis_value(t, LIBINPUT_TABLET_AXIS_PRESSURE);
	if (dist)
		printf("distance: %.2f%s",
		       dist, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_DISTANCE));
	else
		printf("pressure: %.2f%s",
		       pressure, tablet_axis_changed_sym(t, LIBINPUT_TABLET_AXIS_PRESSURE));
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
print_tool_update_event(struct libinput_event *ev)
{
	struct libinput_event_tablet *t = libinput_event_get_tablet_event(ev);
	struct libinput_tool *tool = libinput_event_tablet_get_tool(t);
	const char *tool_str;

	switch (libinput_tool_get_type(tool)) {
	case LIBINPUT_TOOL_NONE:
		tool_str = "none";
		break;
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

	print_event_time(libinput_event_tablet_get_time(t));
	printf("%s (%#x)", tool_str, libinput_tool_get_serial(tool));
	printf("\n");
}

static void
print_proximity_out_event(struct libinput_event *ev) {
	struct libinput_event_tablet *t = libinput_event_get_tablet_event(ev);

	print_event_time(libinput_event_tablet_get_time(t));
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
		case LIBINPUT_EVENT_TABLET_AXIS_RELATIVE:
			print_tablet_axis_event(ev);
			break;
		case LIBINPUT_EVENT_TABLET_TOOL_UPDATE:
			print_tool_update_event(ev);
			break;
		case LIBINPUT_EVENT_TABLET_PROXIMITY_OUT:
			print_proximity_out_event(ev);
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

void
mainloop(struct libinput *li)
{
	struct pollfd fds[2];
	sigset_t mask;

	fds[0].fd = libinput_get_fd(li);
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	fds[1].fd = signalfd(-1, &mask, SFD_NONBLOCK);
	fds[1].events = POLLIN;
	fds[1].revents = 0;

	if (fds[1].fd == -1 ||
	    sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		fprintf(stderr, "Failed to set up signal handling (%s)\n",
				strerror(errno));
	}

	/* Handle already-pending device added events */
	if (handle_and_print_events(li))
		fprintf(stderr, "Expected device added events on startup but got none. "
				"Maybe you don't have the right permissions?\n");

	while (poll(fds, 2, -1) > -1) {
		if (fds[1].revents)
			break;

		handle_and_print_events(li);
	}

	close(fds[1].fd);
}

static void
log_handler(enum libinput_log_priority priority,
	    void *user_data,
	    const char *format,
	    va_list args)
{
	vprintf(format, args);
}

int
main(int argc, char **argv)
{
	struct libinput *li;
	struct timespec tp;

	if (parse_args(argc, argv))
		return 1;

	if (verbose) {
		libinput_log_set_handler(log_handler, NULL);
		libinput_log_set_priority(LIBINPUT_LOG_PRIORITY_DEBUG);
	}

	if (mode == MODE_UDEV) {
		if (open_udev(&li))
			return 1;
	} else if (mode == MODE_DEVICE) {
		if (open_device(&li, device))
			return 1;
	} else
		abort();

	clock_gettime(CLOCK_MONOTONIC, &tp);
	start_time = tp.tv_sec * 1000 + tp.tv_nsec / 1000000;

	mainloop(li);

	libinput_destroy(li);
	if (udev)
		udev_unref(udev);

	return 0;
}
