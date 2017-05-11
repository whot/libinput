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

#include <sys/types.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <libudev.h>

#include <libevdev/libevdev.h>

#include <libinput.h>
#include <libinput-util.h>
#include <libinput-version.h>

#include "libinput-tool.h"

#define DEV_INPUT_EVENT "/dev/input"
#define EVENT_DEV_NAME "event"

static bool use_color = true;

struct dimensions {
	int top, bottom, left, right;
};

static int
is_event_device(const struct dirent *dir) {
	return strncmp(EVENT_DEV_NAME, dir->d_name, 5) == 0;
}

static bool
find_trackpoint(char *path, size_t sz)
{
	struct dirent **namelist;
	int ndev;
	bool rc, found;

	ndev = scandir(DEV_INPUT_EVENT, &namelist, is_event_device, versionsort);
	if (ndev <= 0)
		return false;

	for (int i = 0; i < ndev; i++)
	{
		struct libevdev *evdev;
		char fname[PATH_MAX];
		int fd;

		snprintf(fname, sizeof(fname),
			 "%s/%s",
			 DEV_INPUT_EVENT,
			 namelist[i]->d_name);

		fd = open(fname, O_RDONLY);
		if (fd < 0)
			continue;

		if (libevdev_new_from_fd(fd, &evdev) == 0) {
			if (libevdev_has_property(evdev,
						  INPUT_PROP_POINTING_STICK)) {
				if (!found) {
					snprintf(path, sz, "%s", fname);
					found = true;
					rc = true;
				} else {
					fprintf(stderr,
						"Error: multiple trackpoint devices found.\n");
					rc = false;
				}
			}
			libevdev_free(evdev);
		}
		close(fd);
	}

	for (int i = 0; i < ndev; i++) {
		free(namelist[i]);
	}

	return rc;
}

static void
print_field(const char *header, const char *value, bool highlight)
{
	printf("%s%23s: %s%s%s\n",
	       (highlight && use_color) ? ANSI_HIGHLIGHT : "",
	       header,
	       (highlight && !use_color) ? "**" : "",
	       value ? value : "n/a",
	       (highlight) ?
		       (use_color ? ANSI_NORMAL : "**" ) : "");
}

static void
print_checkbox(const char *header, bool available, bool expected)
{
	bool highlight = (available != expected);

	print_field(header, available ? "yes" : "no", highlight);
}

#define want_yes(cond, msg) \
	print_checkbox(msg, (cond), true)

#define want_no(cond, msg) \
	print_checkbox(msg, (cond), false)

#define want_yes_or_no(cond, msg) \
	print_checkbox(msg, (cond), (cond))

static void
check_evdev_device(struct libevdev *evdev)
{
	bool highlight;
	const char *bustype;
	bool have_rel = libevdev_has_event_code(evdev, EV_REL, REL_X) &&
			libevdev_has_event_code(evdev, EV_REL, REL_Y);
	bool have_prop = libevdev_has_property(evdev,
					       INPUT_PROP_POINTING_STICK);
	bool have_pressure = libevdev_has_event_code(evdev, EV_ABS,
						     ABS_PRESSURE);

	want_yes(have_rel, "relative x/y");
	want_yes(have_prop, "property");
	want_no(have_pressure, "pressure");

	switch (libevdev_get_id_bustype(evdev)) {
	case BUS_I2C:
		bustype = "i2c";
		highlight = false;
		break;
	case BUS_I8042:
		bustype = "i8042";
		highlight = false;
		break;
	case BUS_USB:
		bustype = "usb";
		highlight = true;
		break;
	default:
		bustype = "unknown";
		highlight = true;
		break;
	}
	print_field("bustype", bustype, highlight);
}

static void
check_udev_device(const char *devnode)
{
	struct udev *udev;
	struct udev_device *udev_device;
	struct stat st;
	const char *prop;
	bool have_id_input = true;
	bool have_id_input_pointingstick = true;

	if (stat(devnode, &st) < 0) {
		perror("Error: failed to check udev device");
		return;
	}

	udev = udev_new();
	udev_device = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

	prop = udev_device_get_property_value(udev_device, "ID_INPUT");
	if (prop == NULL ||
	    (!streq(prop, "1") && !streq(prop, "0")))
	    have_id_input = false;

	prop = udev_device_get_property_value(udev_device, "ID_INPUT");
	if (prop == NULL ||
	    (!streq(prop, "1") && !streq(prop, "0")))
	    have_id_input_pointingstick = false;

	want_yes(have_id_input, "ID_INPUT");
	want_yes(have_id_input_pointingstick, "ID_INPUT_POINTINGSTICK");

	prop = udev_device_get_property_value(udev_device, "POINTINGSTICK_CONST_ACCEL");
	print_field("const accel", prop, prop != NULL);

	udev_device_unref(udev_device);
	udev_unref(udev);
}

static const char *
get_attr(struct udev_device *udev_device, const char *attr)
{
	struct udev_device *parent = udev_device;

	while (parent) {
		const char *val;
		val = udev_device_get_sysattr_value(parent, attr);

		if (val) {
			return val;
		}

		parent = udev_device_get_parent(parent);
	}

	return NULL;
}

static void
check_attrs(const char *devnode)
{
	struct udev *udev;
	struct udev_device *udev_device;
	struct stat st;
	const char *attr;
	int sensitivity = -1;
	bool highlight;

	if (stat(devnode, &st) < 0) {
		perror("Error: failed to check udev device");
		return;
	}

	udev = udev_new();
	udev_device = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

	/* 0x80 is kernel default for sensitivity */
	highlight = true;
	attr = get_attr(udev_device, "sensitivity");
	if (attr && safe_atoi(attr, &sensitivity) && sensitivity == 0x80)
		highlight = false;
	print_field("sensitivity", attr, highlight);

	/* 050 is kernel default for drift_time */
	highlight = true;
	attr = get_attr(udev_device, "drift_time");
	if (attr && safe_atoi(attr, &sensitivity) && sensitivity == 0x5)
		highlight = false;
	print_field("drift_time", attr, highlight);

	udev_device_unref(udev_device);
	udev_unref(udev);
}

static int
print_current_values(const struct dimensions *d)
{
	static int progress;
	char status = 0;

	switch (progress) {
		case 0: status = '|'; break;
		case 1: status = '/'; break;
		case 2: status = '-'; break;
		case 3: status = '\\'; break;
	}

	progress = (progress + 1) % 4;

	printf("\rTrackpoint sends:	x [%3d..%3d], y [%3d..%3d] %c",
			d->left, d->right, d->top, d->bottom, status);
	return 0;
}

static int
handle_event(struct dimensions *d, const struct input_event *ev)
{
	if (ev->type == EV_SYN) {
		return print_current_values(d);
	} else if (ev->type != EV_REL)
		return 0;

	switch(ev->code) {
		case REL_X:
			d->left = min(d->left, ev->value);
			d->right = max(d->right, ev->value);
			break;
		case REL_Y:
			d->top = min(d->top, ev->value);
			d->bottom = max(d->bottom, ev->value);
			break;
	}

	return 0;
}

static void
read_range(struct libevdev *dev)
{
	struct dimensions dim = {0};
	struct pollfd fds[2];
	sigset_t mask;

	fds[0].fd = libevdev_get_fd(dev);
	fds[0].events = POLLIN;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	fds[1].fd = signalfd(-1, &mask, SFD_NONBLOCK);
	fds[1].events = POLLIN;

	sigprocmask(SIG_BLOCK, &mask, NULL);

	printf("\n"
	       "Push the trackpoint in all four cardinal directions.\n"
	       "Movements should emulate the fastest reasonable pointer movement on the screen.\n"
	       "Do not hold the trackpoint down in one direction for longer than two seconds\n");

	while (poll(fds, 2, -1)) {
		struct input_event ev;
		int rc;

		if (fds[1].revents)
			break;

		do {
			rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
			if (rc == LIBEVDEV_READ_STATUS_SYNC) {
				fprintf(stderr, "Error: cannot keep up\n");
				return;
			} else if (rc != -EAGAIN && rc < 0) {
				fprintf(stderr, "Error: %s\n", strerror(-rc));
				return;
			} else if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
				handle_event(&dim, &ev);
			}
		} while (rc != -EAGAIN);
	}

	printf("\n");
}

int
libinput_analyze_trackpoint(struct global_options *opts,
			    int argc,
			    char **argv)
{
	char path[PATH_MAX];
	const char *device = path;
	struct libevdev *evdev = NULL;
	int fd = -1;

	if (!isatty(STDOUT_FILENO))
		use_color = false;

	if (argc > 1) {
		device = argv[1];
	} else if (!find_trackpoint(path, sizeof(path))) {
		fprintf(stderr, "Error: Unable to find the trackpoint device, please specify path\n");
		return EXIT_FAILURE;
	}

	fd = open(device, O_RDONLY|O_NONBLOCK);
	if (fd < 0) {
		perror("Error: failed to open device");
		goto out;
	}

	errno = -libevdev_new_from_fd(fd, &evdev);
	if (errno != 0) {
		perror("Error: failed to init context");
		goto out;
	}

	printf("# libinput version: %s\n", LIBINPUT_VERSION);
	printf("Device name: %s\n", libevdev_get_name(evdev));

	check_evdev_device(evdev);
	check_udev_device(device);
	check_attrs(device);

	read_range(evdev);

	printf("\nItems highlighed indicate unexpected or user-set values\n");

	return EXIT_SUCCESS;
out:
	if (evdev)
		libevdev_free(evdev);
	close(fd);

	return EXIT_FAILURE;
}
