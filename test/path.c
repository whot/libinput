
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

static int open_func_count = 0;
static int close_func_count = 0;

static int open_restricted(const char *path, int flags, void *data)
{
	int fd;
	open_func_count++;
	fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}
static void close_restricted(int fd, void *data)
{
	close_func_count++;
	close(fd);
}

const struct libinput_interface simple_interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};


START_TEST(path_create_NULL)
{
	struct libinput *li;
	const struct libinput_interface interface;
	const char *path = "foo";

	open_func_count = 0;
	close_func_count = 0;

	li = libinput_create_from_path(NULL, NULL, NULL);
	ck_assert(li == NULL);
	li = libinput_create_from_path(&interface, NULL, NULL);
	ck_assert(li == NULL);
	li = libinput_create_from_path(NULL, NULL, path);
	ck_assert(li == NULL);

	ck_assert_int_eq(open_func_count, 0);
	ck_assert_int_eq(close_func_count, 0);
}
END_TEST

START_TEST(path_create_invalid)
{
	struct libinput *li;
	const char *path = "/tmp";

	open_func_count = 0;
	close_func_count = 0;

	li = libinput_create_from_path(&simple_interface, NULL, path);
	ck_assert(li == NULL);

	ck_assert_int_eq(open_func_count, 1);
	ck_assert_int_eq(close_func_count, 0);

	libinput_destroy(li);
	ck_assert_int_eq(close_func_count, 0);
}
END_TEST

START_TEST(path_create_destroy)
{
	struct libinput *li;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	int rc;
	void *userdata = &rc;

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
	libevdev_free(evdev);

	li = libinput_create_from_path(&simple_interface, userdata,
				       libevdev_uinput_get_devnode(uinput));
	ck_assert(li != NULL);
	ck_assert(libinput_get_user_data(li) == userdata);
	ck_assert_int_eq(open_func_count, 1);

	libevdev_uinput_destroy(uinput);
	libinput_destroy(li);
	ck_assert_int_eq(close_func_count, 1);
}
END_TEST

START_TEST(path_added_seat)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_seat *seat;
	const char *seat_name;
	enum libinput_event_type type;

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert(event != NULL);

	type = libinput_event_get_type(event);
	ck_assert_int_eq(type, LIBINPUT_EVENT_DEVICE_ADDED);

	seat = libinput_event_get_seat(event);
	ck_assert(seat != NULL);

	seat_name = libinput_seat_get_logical_name(seat);
	ck_assert_int_eq(strcmp(seat_name, "default"), 0);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(path_added_device)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_device *device;

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
			break;
		}

		libinput_event_destroy(event);
	}

	ck_assert(event != NULL);

	device = libinput_event_get_device(event);
	ck_assert(device != NULL);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(path_suspend)
{
	struct libinput *li;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	int rc;
	void *userdata = &rc;

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
	libevdev_free(evdev);

	li = libinput_create_from_path(&simple_interface, userdata,
				       libevdev_uinput_get_devnode(uinput));
	ck_assert(li != NULL);

	libinput_suspend(li);
	libinput_resume(li);

	libevdev_uinput_destroy(uinput);
	libinput_destroy(li);
}
END_TEST

START_TEST(path_double_suspend)
{
	struct libinput *li;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	int rc;
	void *userdata = &rc;

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
	libevdev_free(evdev);

	li = libinput_create_from_path(&simple_interface, userdata,
				       libevdev_uinput_get_devnode(uinput));
	ck_assert(li != NULL);

	libinput_suspend(li);
	libinput_suspend(li);
	libinput_resume(li);

	libevdev_uinput_destroy(uinput);
	libinput_destroy(li);
}
END_TEST

START_TEST(path_double_resume)
{
	struct libinput *li;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	int rc;
	void *userdata = &rc;

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
	libevdev_free(evdev);

	li = libinput_create_from_path(&simple_interface, userdata,
				       libevdev_uinput_get_devnode(uinput));
	ck_assert(li != NULL);

	libinput_suspend(li);
	libinput_resume(li);
	libinput_resume(li);

	libevdev_uinput_destroy(uinput);
	libinput_destroy(li);
}
END_TEST

int main (int argc, char **argv) {

	litest_add("path:create", path_create_NULL, LITEST_ANY, LITEST_ANY);
	litest_add("path:create", path_create_invalid, LITEST_ANY, LITEST_ANY);
	litest_add("path:create", path_create_destroy, LITEST_ANY, LITEST_ANY);
	litest_add("path:suspend", path_suspend, LITEST_ANY, LITEST_ANY);
	litest_add("path:suspend", path_double_suspend, LITEST_ANY, LITEST_ANY);
	litest_add("path:suspend", path_double_resume, LITEST_ANY, LITEST_ANY);
	litest_add("path:seat events", path_added_seat, LITEST_ANY, LITEST_ANY);
	litest_add("path:device events", path_added_device, LITEST_ANY, LITEST_ANY);

	return litest_run(argc, argv);
}
