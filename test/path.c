
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
	return open(path, flags);
}
static void close_restricted(int fd, void *data)
{
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

	li = libinput_create_from_path(NULL, NULL, NULL);
	ck_assert(li == NULL);

	li = libinput_create_from_path(&interface, NULL, NULL);
	ck_assert(li == NULL);
	li = libinput_create_from_path(NULL, NULL, path);
	ck_assert(li == NULL);
}
END_TEST

START_TEST(path_create_invalid)
{
	struct libinput *li;
	const char *path = "/tmp";

	li = libinput_create_from_path(&simple_interface, NULL, path);
	ck_assert(li == NULL);
}
END_TEST

START_TEST(path_seat_added)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_added_seat *seat_event;
	struct libinput_seat *seat;
	const char *seat_name;
	enum libinput_event_type type;

	ck_assert_int_ge(libinput_dispatch(li), -EAGAIN);
	event = libinput_get_event(li);
	ck_assert(event != NULL);

	type = libinput_event_get_type(event);
	ck_assert_int_eq(type, LIBINPUT_EVENT_ADDED_SEAT);

	seat_event = (struct libinput_event_added_seat*)event;
	seat = libinput_event_added_seat_get_seat(seat_event);
	ck_assert(seat != NULL);

	seat_name = libinput_seat_get_name(seat);
	ck_assert_int_eq(strcmp(seat_name, "default"), 0);

	libinput_seat_unref(seat);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(path_device_added)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_added_device *device_event = NULL;
	struct libinput_device *device;

	ck_assert_int_ge(libinput_dispatch(li), -EAGAIN);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_ADDED_DEVICE) {
			device_event = (struct libinput_event_added_device*)event;
			break;;
		}

		libinput_event_destroy(event);
	}

	ck_assert(device_event != NULL);

	device = libinput_event_added_device_get_device(device_event);
	ck_assert(device != NULL);

	libinput_event_destroy(event);
	libinput_device_unref(device);
}
END_TEST

int main (int argc, char **argv) {

	litest_add("path:create", path_create_NULL, LITEST_ANY, LITEST_ANY);
	litest_add("path:create", path_create_invalid, LITEST_ANY, LITEST_ANY);
	litest_add("path:seat events", path_seat_added, LITEST_ANY, LITEST_ANY);
	litest_add("path:device events", path_device_added, LITEST_ANY, LITEST_ANY);

	return litest_run(argc, argv);
}
