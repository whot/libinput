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
#include <linux/input.h>
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

static void
usage(void)
{
	printf("Usage: %s [--udev [<seat>]|--device /dev/input/event0]\n"
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
	int clockid = CLOCK_MONOTONIC;

	if (fd >= 0 && ioctl(fd, EVIOCSCLOCKID, &clockid) < 0)
		fprintf(stderr, "Changing clock on %s failed, timestamps "
				"will be off\n", path);

	return fd < 0 ? -errno : fd;
}

static void
close_restricted(int fd, void *user_data)
{
	close(fd);
}

static void get_current_screen_dimensions(struct libinput_device *device,
					  int *width,
					  int *height,
					  void *user_data)
{
	/* display absdata in % of the screen */
	*width = 100;
	*height = 100;
}

const static struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
	.get_current_screen_dimensions = get_current_screen_dimensions,
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
	case LIBINPUT_EVENT_TOUCH_TOUCH:
		type = "TOUCH_TOUCH";
		break;
	case LIBINPUT_EVENT_TOUCH_FRAME:
		type = "TOUCH_FRAME";
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

	printf("%s	%s\n",
	       libinput_seat_get_physical_name(seat),
	       libinput_seat_get_logical_name(seat));
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
	li_fixed_t x =	libinput_event_pointer_get_dx(p),
		   y = libinput_event_pointer_get_dy(p);

	print_event_time(libinput_event_pointer_get_time(p));

	printf("%6.2f/%6.2f\n",
	       li_fixed_to_double(x),
	       li_fixed_to_double(y));
}

static void
print_absmotion_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	li_fixed_t x = libinput_event_pointer_get_absolute_x(p),
		   y = libinput_event_pointer_get_absolute_y(p);

	print_event_time(libinput_event_pointer_get_time(p));
	printf("%6.2f/%6.2f\n",
	       li_fixed_to_double(x),
	       li_fixed_to_double(y));
}

static void
print_button_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	enum libinput_pointer_button_state state;

	print_event_time(libinput_event_pointer_get_time(p));

	state = libinput_event_pointer_get_button_state(p);
	printf("%3d %s\n",
	       libinput_event_pointer_get_button(p),
	       state == LIBINPUT_POINTER_BUTTON_STATE_PRESSED ? "pressed" : "released");
}

static void
print_axis_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	enum libinput_pointer_axis axis = libinput_event_pointer_get_axis(p);
	const char *ax;
	li_fixed_t val;

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
	printf("%s %.2f\n",
	       ax, li_fixed_to_double(val));
}

static void
print_touch_frame_event(struct libinput_event *ev)
{
	struct libinput_event_touch *t = libinput_event_get_touch_event(ev);

	print_event_time(libinput_event_touch_get_time(t));
	printf("\n");
}

static void
print_touch_event(struct libinput_event *ev)
{
	struct libinput_event_touch *t = libinput_event_get_touch_event(ev);
	li_fixed_t x = libinput_event_touch_get_x(t),
		   y = libinput_event_touch_get_y(t);
	const char *type;

	switch (libinput_event_touch_get_touch_type(t)) {
	case LIBINPUT_TOUCH_TYPE_DOWN: type = "down"; break;
	case LIBINPUT_TOUCH_TYPE_UP: type = "up"; break;
	case LIBINPUT_TOUCH_TYPE_MOTION: type = "motion"; break;
	case LIBINPUT_TOUCH_TYPE_CANCEL: type = "cancel"; break;
	default:
		abort();
	}

	print_event_time(libinput_event_touch_get_time(t));

	printf("%6s %u %5.2f/%5.2f\n",
	       type,
	       libinput_event_touch_get_slot(t),
	       li_fixed_to_double(x),
	       li_fixed_to_double(y));
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
			print_button_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_AXIS:
			print_axis_event(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_TOUCH:
			print_touch_event(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_FRAME:
			print_touch_frame_event(ev);
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

int
main(int argc, char **argv)
{
	struct libinput *li;
	struct timespec tp;

	if (parse_args(argc, argv))
		return 1;

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

	if (udev)
		udev_unref(udev);

	return 0;
}
