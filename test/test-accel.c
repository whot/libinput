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

#include <config.h>

#include <stdio.h>
#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>

#include "libinput-util.h"
#include "litest.h"
#include "evfd-seat.h"

static void
write_event(int fd,
	    uint64_t us,
	    unsigned int type,
	    unsigned int code,
	    int value)
{
	struct input_event ev = {
		.time.tv_sec = us / 1000000,
		.time.tv_usec = us % 1000000,
		.type = type,
		.code = code,
		.value = value,
	};
	ssize_t rc;

	rc = write(fd, &ev, sizeof ev);
	litest_assert_int_eq(rc, (ssize_t)sizeof ev);
}

static void
touch_down(int fd, uint64_t us, int x, int y)
{
	static int tracking_id = 0;

	write_event(fd, us, EV_ABS, ABS_MT_SLOT, 0);
	write_event(fd, us, EV_ABS, ABS_X, x);
	write_event(fd, us, EV_ABS, ABS_Y, y);
	write_event(fd, us, EV_ABS, ABS_MT_POSITION_X, x);
	write_event(fd, us, EV_ABS, ABS_MT_POSITION_Y, y);
	write_event(fd, us, EV_ABS, ABS_MT_TRACKING_ID, ++tracking_id);
	write_event(fd, us, EV_KEY, BTN_TOOL_FINGER, 1);
	write_event(fd, us, EV_KEY, BTN_TOUCH, 1);
	write_event(fd, us, EV_SYN, SYN_REPORT, 0);
}

static void
touch_move(int fd, uint64_t us, int x, int y)
{
	write_event(fd, us, EV_ABS, ABS_X, x);
	write_event(fd, us, EV_ABS, ABS_Y, y);
	write_event(fd, us, EV_ABS, ABS_MT_POSITION_X, x);
	write_event(fd, us, EV_ABS, ABS_MT_POSITION_Y, y);
	write_event(fd, us, EV_SYN, SYN_REPORT, 0);
}

static void
touch_up(int fd, uint64_t us)
{
	write_event(fd, us, EV_ABS, ABS_MT_TRACKING_ID, -1);
	write_event(fd, us, EV_KEY, BTN_TOOL_FINGER, 0);
	write_event(fd, us, EV_KEY, BTN_TOUCH, 0);
	write_event(fd, us, EV_SYN, SYN_REPORT, 0);
}

static uint64_t
now(void)
{
	uint64_t us;
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	us = s2us(ts.tv_sec) + ns2us(ts.tv_sec);

	return us;
}

static void
finger_motion(struct libinput *li,
	      int fd, double mmps, int distance,
	      int xstart, int ystart,
	      int xres, int yres)
{
	int x;
	uint64_t us = now();

	for (x = xstart; x < xstart + distance * xres; x++) {
		/* constant movement with
		 *   speed: N mm/s
		 *   resolution: R u/mm
		 *
		 * next unit takes 1/(NR)s to hit
		 *    or 1000/(NR) millis
		 *    or 1000000/(NR) µs
		 */
		us += s2us(1)/(mmps * xres);
		touch_move(fd, us, x, 50);
		libinput_dispatch(li);
	}
}

static struct libevdev_uinput*
create_touchpad(int width, int height, int xres, int yres)
{
	struct libevdev_uinput *uinput;
	const int w = xres * width,
		  h = yres * height;

	struct input_id id = { .bustype = BUS_I8042,
				.vendor = 0x1,
				.product = 0x2,
				.version = 0x3 };

	struct input_absinfo abs[] = {
		{ ABS_X, 0, w, 0, 0, xres },
		{ ABS_Y, 0, h, 0, 0, yres },
		{ ABS_MT_POSITION_X, 0, w, 0, 0, xres },
		{ ABS_MT_POSITION_Y, 0, h, 0, 0, yres },
		{ ABS_MT_SLOT, 0, 1, 0, 0, 0 },
		{ ABS_MT_TRACKING_ID, 0, 0xffff, 0, 0, 0 },
		{ .value = -1 },
	};

	int events[] = {
		EV_KEY, BTN_TOUCH,
		EV_KEY, BTN_LEFT,
		EV_KEY, BTN_RIGHT,
		EV_KEY, BTN_TOOL_FINGER,
		EV_KEY, BTN_TOOL_DOUBLETAP,
		EV_KEY, BTN_TOOL_TRIPLETAP,
		INPUT_PROP_MAX, INPUT_PROP_POINTER,
		-1, -1
	};

	uinput = litest_create_uinput_device_from_description(
					      "litest resolution touchpad",
					      &id,
					      abs,
					      events);

	return uinput;
}

START_TEST(accel_touchpad)
{
	struct libinput *li;
	struct libevdev_uinput *uinput;
	int pipefd[2];
	int rc, fd, us;
	int resolution = _i; /* ranged test */
	int xres = resolution,
	    yres = resolution;

	double dx = 0, dy = 0;
	double speed = 80.0; /* mm/s */
	int distance = 30; /* mm */

	struct libinput_event *ev;
	struct libinput_event_pointer *pev;

	rc = pipe2(pipefd, O_NONBLOCK);
	ck_assert_int_eq(rc, 0);

	uinput  = create_touchpad(100, 100, xres, yres);
	li = litest_create_evfd_context();
	libinput_evfd_add_device(li,
				 libevdev_uinput_get_devnode(uinput),
				 pipefd[0]);

	fd = pipefd[1];
	us = now();
	touch_down(fd, us, 500, 500);
	libinput_dispatch(li);
	litest_drain_events(li);

	us += ms2us(12);

	finger_motion(li, fd, speed, distance, 50, 500, xres, yres);
	libinput_dispatch(li);

	ev = libinput_get_event(li);
	do {
		pev = litest_is_motion_event(ev);

		dx += libinput_event_pointer_get_dx(pev);
		dy += libinput_event_pointer_get_dy(pev);

		libinput_event_destroy(ev);
		ev = libinput_get_event(li);
	} while (ev);

	/* these numbers depend on the accel function, change when the
	 * function changes.
	 * This only tests that regardless of the device resolution, the
	 * deltas are always the same */
	ck_assert_double_gt(dx, 428.0);
	ck_assert_double_lt(dx, 430.0);

	touch_up(fd, us);
	libinput_dispatch(li);

	libevdev_uinput_destroy(uinput);
	libinput_unref(li);
}
END_TEST

void
litest_setup_tests_accel(void)
{
	struct range resolutions = { 12, 75 };

	litest_add_ranged_no_device("accel:touchpad", accel_touchpad, &resolutions);
}
