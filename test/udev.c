/*
 * Copyright © 2013 Red Hat, Inc.
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


START_TEST(udev_create_NULL)
{
	struct libinput *li;
	const struct libinput_interface interface;
	struct udev *udev = (struct udev*)0xdeadbeef;
	const char *seat = (const char*)0xdeaddead;

	li = libinput_udev_create_for_seat(NULL, NULL, NULL, NULL);
	ck_assert(li == NULL);

	li = libinput_udev_create_for_seat(&interface, NULL, NULL, NULL);
	ck_assert(li == NULL);
	li = libinput_udev_create_for_seat(NULL, NULL, udev, NULL);
	ck_assert(li == NULL);
	li = libinput_udev_create_for_seat(NULL, NULL, NULL, seat);
	ck_assert(li == NULL);

	li = libinput_udev_create_for_seat(&interface, NULL, udev, NULL);
	ck_assert(li == NULL);
	li = libinput_udev_create_for_seat(NULL, NULL, udev, seat);
	ck_assert(li == NULL);

	li = libinput_udev_create_for_seat(&interface, NULL, NULL, seat);
	ck_assert(li == NULL);
}
END_TEST

START_TEST(udev_create_seat0)
{
	struct libinput *li;
	struct libinput_event *event;
	struct udev *udev;
	int fd;

	udev = udev_new();
	ck_assert(udev != NULL);

	li = libinput_udev_create_for_seat(&simple_interface, NULL, udev, "seat0");
	ck_assert(li != NULL);

	fd = libinput_get_fd(li);
	ck_assert_int_ge(fd, 0);

	/* expect at least one event */
	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event != NULL);

	libinput_event_destroy(event);
	libinput_destroy(li);
	udev_unref(udev);
}
END_TEST

START_TEST(udev_create_empty_seat)
{
	struct libinput *li;
	struct libinput_event *event;
	struct udev *udev;
	int fd;

	udev = udev_new();
	ck_assert(udev != NULL);

	/* expect a libinput reference, but no events */
	li = libinput_udev_create_for_seat(&simple_interface, NULL, udev, "seatdoesntexist");
	ck_assert(li != NULL);

	fd = libinput_get_fd(li);
	ck_assert_int_ge(fd, 0);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event == NULL);

	libinput_event_destroy(event);
	libinput_destroy(li);
	udev_unref(udev);
}
END_TEST

/**
 * This test only works if there's at least one device in the system that is
 * assigned the default seat. Should cover the 99% case.
 */
START_TEST(udev_added_seat_default)
{
	struct libinput *li;
	struct libinput_event *event;
	struct udev *udev;
	struct libinput_device *device;
	struct libinput_seat *seat;
	const char *seat_name;
	enum libinput_event_type type;
	int default_seat_found = 0;

	udev = udev_new();
	ck_assert(udev != NULL);

	li = libinput_udev_create_for_seat(&simple_interface, NULL, udev, "seat0");
	ck_assert(li != NULL);
	libinput_dispatch(li);

	while (!default_seat_found && (event = libinput_get_event(li))) {
		type = libinput_event_get_type(event);
		if (type != LIBINPUT_EVENT_DEVICE_ADDED) {
			libinput_event_destroy(event);
			continue;
		}

		device = libinput_event_get_device(event);
		seat = libinput_device_get_seat(device);
		ck_assert(seat != NULL);

		seat_name = libinput_seat_get_logical_name(seat);
		default_seat_found = !strcmp(seat_name, "default");
		libinput_event_destroy(event);
	}

	ck_assert(default_seat_found);

	libinput_destroy(li);
	udev_unref(udev);
}
END_TEST

START_TEST(udev_double_suspend)
{
	struct libinput *li;
	struct libinput_event *event;
	struct udev *udev;
	int fd;

	udev = udev_new();
	ck_assert(udev != NULL);

	li = libinput_udev_create_for_seat(&simple_interface, NULL, udev, "seat0");
	ck_assert(li != NULL);

	fd = libinput_get_fd(li);
	ck_assert_int_ge(fd, 0);

	/* expect at least one event */
	ck_assert_int_ge(libinput_dispatch(li), 0);
	event = libinput_get_event(li);
	ck_assert(event != NULL);

	libinput_suspend(li);
	libinput_suspend(li);
	libinput_resume(li);

	libinput_event_destroy(event);
	libinput_destroy(li);
	udev_unref(udev);
}
END_TEST

START_TEST(udev_double_resume)
{
	struct libinput *li;
	struct libinput_event *event;
	struct udev *udev;
	int fd;

	udev = udev_new();
	ck_assert(udev != NULL);

	li = libinput_udev_create_for_seat(&simple_interface, NULL, udev, "seat0");
	ck_assert(li != NULL);

	fd = libinput_get_fd(li);
	ck_assert_int_ge(fd, 0);

	/* expect at least one event */
	ck_assert_int_ge(libinput_dispatch(li), 0);
	event = libinput_get_event(li);
	ck_assert(event != NULL);

	libinput_suspend(li);
	libinput_resume(li);
	libinput_resume(li);

	libinput_event_destroy(event);
	libinput_destroy(li);
	udev_unref(udev);
}
END_TEST

static void
process_events_count_devices(struct libinput *li, int *device_count)
{
	struct libinput_event *event;

	while ((event = libinput_get_event(li))) {
		switch (libinput_event_get_type(event)) {
		case LIBINPUT_EVENT_DEVICE_ADDED:
			(*device_count)++;
			break;
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			(*device_count)--;
			break;
		default:
			break;
		}
		libinput_event_destroy(event);
	}
}

START_TEST(udev_suspend_resume)
{
	struct libinput *li;
	struct udev *udev;
	int fd;
	int num_devices = 0;

	udev = udev_new();
	ck_assert(udev != NULL);

	li = libinput_udev_create_for_seat(&simple_interface, NULL, udev, "seat0");
	ck_assert(li != NULL);

	fd = libinput_get_fd(li);
	ck_assert_int_ge(fd, 0);

	/* Check that at least one device was discovered after creation. */
	ck_assert_int_ge(libinput_dispatch(li), 0);
	process_events_count_devices(li, &num_devices);
	ck_assert_int_gt(num_devices, 0);

	/* Check that after a suspend, no devices are left. */
	libinput_suspend(li);
	ck_assert_int_ge(libinput_dispatch(li), 0);
	process_events_count_devices(li, &num_devices);
	ck_assert_int_eq(num_devices, 0);

	/* Check that after a resume, at least one device is discovered. */
	libinput_resume(li);
	ck_assert_int_ge(libinput_dispatch(li), 0);
	process_events_count_devices(li, &num_devices);
	ck_assert_int_gt(num_devices, 0);

	libinput_destroy(li);
	udev_unref(udev);
}
END_TEST

START_TEST(udev_device_sysname)
{
	struct libinput *li;
	struct libinput_event *ev;
	struct libinput_device *device;
	const char *sysname;
	struct udev *udev;

	udev = udev_new();
	ck_assert(udev != NULL);

	li = libinput_udev_create_for_seat(&simple_interface, NULL, udev, "seat0");
	ck_assert(li != NULL);

	libinput_dispatch(li);

	while ((ev = libinput_get_event(li))) {
		if (libinput_event_get_type(ev) != LIBINPUT_EVENT_DEVICE_ADDED)
			continue;

		device = libinput_event_get_device(ev);
		sysname = libinput_device_get_sysname(device);
		ck_assert(sysname != NULL && strlen(sysname) > 1);
		ck_assert(strchr(sysname, '/') == NULL);
		ck_assert_int_eq(strncmp(sysname, "event", 5), 0);
		libinput_event_destroy(ev);
	}

	libinput_destroy(li);
	udev_unref(udev);
}
END_TEST

int main (int argc, char **argv) {

	litest_add_no_device("udev:create", udev_create_NULL);
	litest_add_no_device("udev:create", udev_create_seat0);
	litest_add_no_device("udev:create", udev_create_empty_seat);

	litest_add_no_device("udev:seat events", udev_added_seat_default);

	litest_add("udev:suspend", udev_double_suspend, LITEST_ANY, LITEST_ANY);
	litest_add("udev:suspend", udev_double_resume, LITEST_ANY, LITEST_ANY);
	litest_add("udev:suspend", udev_suspend_resume, LITEST_ANY, LITEST_ANY);
	litest_add("udev:device events", udev_device_sysname, LITEST_ANY, LITEST_ANY);

	return litest_run(argc, argv);
}
