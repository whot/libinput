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


START_TEST(udev_create_NULL)
{
	struct libinput *li;
	const struct libinput_interface interface;
	struct udev *udev = (struct udev*)0xdeadbeef;
	const char *seat = (const char*)0xdeaddead;

	li = libinput_create_from_udev(NULL, NULL, NULL, NULL);
	ck_assert(li == NULL);

	li = libinput_create_from_udev(&interface, NULL, NULL, NULL);
	ck_assert(li == NULL);
	li = libinput_create_from_udev(NULL, NULL, udev, NULL);
	ck_assert(li == NULL);
	li = libinput_create_from_udev(NULL, NULL, NULL, seat);
	ck_assert(li == NULL);

	li = libinput_create_from_udev(&interface, NULL, udev, NULL);
	ck_assert(li == NULL);
	li = libinput_create_from_udev(NULL, NULL, udev, seat);
	ck_assert(li == NULL);

	li = libinput_create_from_udev(&interface, NULL, NULL, seat);
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

	li = libinput_create_from_udev(&simple_interface, NULL, udev, "seat0");
	ck_assert(li != NULL);

	fd = libinput_get_fd(li);
	ck_assert_int_ge(fd, 0);

	/* expect at least one event */
	ck_assert_int_ge(libinput_dispatch(li), 0);
	event = libinput_get_event(li);
	ck_assert(event != NULL);

	libinput_event_destroy(event);
	libinput_destroy(li);
	udev_unref(udev);
}
END_TEST

START_TEST(udev_create_seat9)
{
	struct libinput *li;
	struct libinput_event *event;
	struct udev *udev;
	int fd;

	udev = udev_new();
	ck_assert(udev != NULL);

	/* Seat 9 - expect a libinput reference, but no events */
	li = libinput_create_from_udev(&simple_interface, NULL, udev, "seat9");
	ck_assert(li != NULL);

	fd = libinput_get_fd(li);
	ck_assert_int_ge(fd, 0);

	ck_assert_int_ge(libinput_dispatch(li), -EAGAIN);
	event = libinput_get_event(li);
	ck_assert(event == NULL);

	libinput_event_destroy(event);
	libinput_destroy(li);
	udev_unref(udev);
}
END_TEST

int main (int argc, char **argv) {

	litest_add("udev:create", udev_create_NULL, LITEST_NO_DEVICE);
	litest_add("udev:create", udev_create_seat0, LITEST_NO_DEVICE);
	litest_add("udev:create", udev_create_seat9, LITEST_NO_DEVICE);

	return litest_run(argc, argv);
}
