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

static int log_handler_called;
static void *log_handler_userdata;

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

static void
simple_log_handler(enum libinput_log_priority priority,
		   void *userdata,
		   const char *format,
		   va_list args)
{
	log_handler_called++;
	ck_assert(userdata == log_handler_userdata);
	ck_assert(format != NULL);
}

START_TEST(log_default_priority)
{
	enum libinput_log_priority pri;

	pri = libinput_log_get_priority();

	ck_assert_int_eq(pri, LIBINPUT_LOG_PRIORITY_ERROR);
}
END_TEST

START_TEST(log_handler_invoked)
{
	struct libinput *li;

	libinput_log_set_priority(LIBINPUT_LOG_PRIORITY_DEBUG);
	libinput_log_set_handler(simple_log_handler, NULL);
	log_handler_userdata = NULL;

	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, "/tmp");

	ck_assert_int_gt(log_handler_called, 0);
	log_handler_called = 0;
}
END_TEST

START_TEST(log_userdata_NULL)
{
	struct libinput *li;

	libinput_log_set_priority(LIBINPUT_LOG_PRIORITY_DEBUG);
	libinput_log_set_handler(simple_log_handler, NULL);
	log_handler_userdata = NULL;

	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, "/tmp");

	ck_assert_int_gt(log_handler_called, 0);
	log_handler_called = 0;
}
END_TEST

START_TEST(log_userdata)
{
	struct libinput *li;

	libinput_log_set_priority(LIBINPUT_LOG_PRIORITY_DEBUG);
	libinput_log_set_handler(simple_log_handler, &li);
	log_handler_userdata = &li;

	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, "/tmp");

	ck_assert_int_gt(log_handler_called, 0);
	log_handler_called = 0;
}
END_TEST

START_TEST(log_handler_NULL)
{
	struct libinput *li;

	libinput_log_set_priority(LIBINPUT_LOG_PRIORITY_DEBUG);
	libinput_log_set_handler(NULL, NULL);
	log_handler_userdata = NULL;

	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, "/tmp");

	ck_assert_int_eq(log_handler_called, 0);
	log_handler_called = 0;
	libinput_log_set_handler(simple_log_handler, NULL);
}
END_TEST

START_TEST(log_priority)
{
	struct libinput *li;

	libinput_log_set_priority(LIBINPUT_LOG_PRIORITY_ERROR);
	libinput_log_set_handler(simple_log_handler, NULL);
	log_handler_userdata = NULL;

	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, "/tmp");

	ck_assert_int_eq(log_handler_called, 0);

	libinput_log_set_priority(LIBINPUT_LOG_PRIORITY_INFO);
	libinput_path_add_device(li, "/tmp");
	ck_assert_int_gt(log_handler_called, 0);

	log_handler_called = 0;
}
END_TEST

int main (int argc, char **argv) {
	litest_add_no_device("log:defaults", log_default_priority);
	litest_add_no_device("log:logging", log_handler_invoked);
	litest_add_no_device("log:logging", log_handler_NULL);
	litest_add_no_device("log:logging", log_userdata);
	litest_add_no_device("log:logging", log_userdata_NULL);
	litest_add_no_device("log:logging", log_priority);

	return litest_run(argc, argv);
}
