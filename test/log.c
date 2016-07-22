/*
 * Copyright © 2014 Red Hat, Inc.
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

#include <config.h>

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <unistd.h>

#include "litest.h"

static int log_handler_called;
static struct libinput *log_handler_context;

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

static const struct libinput_interface simple_interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

static void
simple_log_handler(struct libinput *libinput,
		   enum libinput_log_priority priority,
		   const char *format,
		   va_list args)
{
	log_handler_called++;
	if (log_handler_context)
		litest_assert_ptr_eq(libinput, log_handler_context);
	litest_assert_notnull(format);
}

START_TEST(log_default_priority)
{
	enum libinput_log_priority pri;
	struct libinput *li;

	li = libinput_path_create_context(&simple_interface, NULL);
	pri = libinput_log_get_priority(li);

	ck_assert_int_eq(pri, LIBINPUT_LOG_PRIORITY_ERROR);

	libinput_unref(li);
}
END_TEST

START_TEST(log_handler_invoked)
{
	struct libinput *li;

	li = libinput_path_create_context(&simple_interface, NULL);

	libinput_log_set_priority(li, LIBINPUT_LOG_PRIORITY_DEBUG);
	libinput_log_set_handler(li, simple_log_handler);
	log_handler_context = li;

	libinput_path_add_device(li, "/tmp");

	ck_assert_int_gt(log_handler_called, 0);
	log_handler_called = 0;

	libinput_unref(li);

	log_handler_context = NULL;
}
END_TEST

START_TEST(log_handler_NULL)
{
	struct libinput *li;

	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_log_set_priority(li, LIBINPUT_LOG_PRIORITY_DEBUG);
	libinput_log_set_handler(li, NULL);

	libinput_path_add_device(li, "/tmp");

	ck_assert_int_eq(log_handler_called, 0);
	log_handler_called = 0;

	libinput_unref(li);
}
END_TEST

START_TEST(log_priority)
{
	struct libinput *li;

	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_log_set_priority(li, LIBINPUT_LOG_PRIORITY_ERROR);
	libinput_log_set_handler(li, simple_log_handler);
	log_handler_context = li;

	libinput_path_add_device(li, "/tmp");

	ck_assert_int_eq(log_handler_called, 1);

	libinput_log_set_priority(li, LIBINPUT_LOG_PRIORITY_INFO);
	/* event0 is usually Lid Switch which prints an info that
	   we don't handle it */
	libinput_path_add_device(li, "/dev/input/event0");
	ck_assert_int_gt(log_handler_called, 1);

	log_handler_called = 0;

	libinput_unref(li);
	log_handler_context = NULL;
}
END_TEST

void
litest_setup_tests_log(void)
{
	litest_add_no_device("log:defaults", log_default_priority);
	litest_add_no_device("log:logging", log_handler_invoked);
	litest_add_no_device("log:logging", log_handler_NULL);
	litest_add_no_device("log:logging", log_priority);
}
