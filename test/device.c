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

#include <config.h>

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <unistd.h>

#include "litest.h"

static int open_restricted(const char *path, int flags, void *data)
{
	int fd;
	fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}
static void close_restricted(int fd, void *data)
{
	close(fd);
}

const struct libinput_interface simple_interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};


START_TEST(device_suspend)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	struct libinput_event *event;

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event != NULL);
	device = libinput_event_get_device(event);
	libinput_device_ref(device);
	ck_assert(device != NULL);
	libinput_event_destroy(event);

	litest_drain_events(li);

	/* no event from suspending */
	libinput_device_suspend(device);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event == NULL);

	litest_event(dev, EV_REL, REL_X, 10);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	/* no event from suspended device */
	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event == NULL);

	/* no event from suspending */
	libinput_device_resume(device);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event == NULL);

	libinput_device_unref(device);
}
END_TEST

START_TEST(device_double_suspend)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	struct libinput_event *event;

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event != NULL);
	device = libinput_event_get_device(event);
	libinput_device_ref(device);
	ck_assert(device != NULL);
	libinput_event_destroy(event);

	litest_drain_events(li);

	libinput_device_suspend(device);
	libinput_device_suspend(device);

	libinput_device_unref(device);
}
END_TEST

START_TEST(device_double_resume)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	struct libinput_event *event;

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event != NULL);
	device = libinput_event_get_device(event);
	libinput_device_ref(device);
	ck_assert(device != NULL);
	libinput_event_destroy(event);

	litest_drain_events(li);

	libinput_device_suspend(device);
	libinput_device_resume(device);
	libinput_device_resume(device);

	libinput_device_unref(device);
}
END_TEST

START_TEST(device_resume_invalid) {
	struct libinput *li;
	struct libinput_event *event;
	struct libinput_device *device;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	int rc;
	void *userdata = &rc;
	char *devnode;

	evdev = libevdev_new();
	ck_assert(evdev != NULL);

	libevdev_set_name(evdev, "test device");
	libevdev_enable_event_code(evdev, EV_KEY, BTN_LEFT, NULL);
	libevdev_enable_event_code(evdev, EV_KEY, BTN_RIGHT, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_X, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_Y, NULL);

	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput);
	ck_assert_int_eq(rc, 0);
	devnode = strdup(libevdev_uinput_get_devnode(uinput));

	li = libinput_path_create_from_device(&simple_interface, userdata,
					      libevdev_uinput_get_devnode(uinput));
	ck_assert(li != NULL);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event != NULL);
	device = libinput_event_get_device(event);
	libinput_device_ref(device);
	ck_assert(device != NULL);
	libinput_event_destroy(event);

	libinput_device_suspend(device);

	litest_drain_events(li);
	/* now destroy the original device and re-create it */
	libevdev_uinput_destroy(uinput);
	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput);
	ck_assert_int_eq(rc, 0);
	/* make sure it came back with the same device node */
	ck_assert_int_eq(strcmp(devnode, libevdev_uinput_get_devnode(uinput)), 0);

	rc = libinput_device_resume(device);
	ck_assert_int_eq(rc, -ENODEV);

	libinput_device_unref(device);
	libinput_unref(li);
	libevdev_free(evdev);
	free(devnode);
}
END_TEST

int main (int argc, char **argv)
{
	litest_add("device:suspend", device_suspend, LITEST_POINTER, LITEST_ANY);
	litest_add("device:suspend", device_double_suspend, LITEST_ANY, LITEST_ANY);
	litest_add("device:suspend", device_double_resume, LITEST_ANY, LITEST_ANY);
	litest_add_no_device("device:suspend", device_resume_invalid);

	return litest_run(argc, argv);
}
