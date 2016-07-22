/*
 * Copyright © 2013 Red Hat, Inc.
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
#include <libevdev/libevdev.h>
#include <unistd.h>

#include "libinput-util.h"
#include "litest.h"

START_TEST(touch_frame_events)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int have_frame_event = 0;

	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 0, 10, 10);
	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_FRAME)
			have_frame_event++;
		libinput_event_destroy(event);
	}
	ck_assert_int_eq(have_frame_event, 1);

	litest_touch_down(dev, 1, 10, 10);
	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_FRAME)
			have_frame_event++;
		libinput_event_destroy(event);
	}
	ck_assert_int_eq(have_frame_event, 2);
}
END_TEST

START_TEST(touch_abs_transform)
{
	struct litest_device *dev;
	struct libinput *libinput;
	struct libinput_event *ev;
	struct libinput_event_touch *tev;
	double fx, fy;
	bool tested = false;

	struct input_absinfo abs[] = {
		{ ABS_X, 0, 32767, 75, 0, 10 },
		{ ABS_Y, 0, 32767, 129, 0, 9 },
		{ ABS_MT_POSITION_X, 0, 32767, 0, 0, 10 },
		{ ABS_MT_POSITION_Y, 0, 32767, 0, 0, 9 },
		{ .value = -1 },
	};

	dev = litest_create_device_with_overrides(LITEST_WACOM_TOUCH,
						  "litest Highres touch device",
						  NULL, abs, NULL);

	libinput = dev->libinput;

	litest_touch_down(dev, 0, 100, 100);

	libinput_dispatch(libinput);

	while ((ev = libinput_get_event(libinput))) {
		if (libinput_event_get_type(ev) != LIBINPUT_EVENT_TOUCH_DOWN) {
			libinput_event_destroy(ev);
			continue;
		}

		tev = libinput_event_get_touch_event(ev);
		fx = libinput_event_touch_get_x_transformed(tev, 1920);
		ck_assert_int_eq(fx, 1919.0);
		fy = libinput_event_touch_get_y_transformed(tev, 720);
		ck_assert_int_eq(fy, 719.0);

		tested = true;

		libinput_event_destroy(ev);
	}

	ck_assert(tested);

	litest_delete_device(dev);
}
END_TEST

START_TEST(touch_many_slots)
{
	struct libinput *libinput;
	struct litest_device *dev;
	struct libinput_event *ev;
	int slot;
	const int num_tps = 100;
	int slot_count = 0;
	enum libinput_event_type type;

	struct input_absinfo abs[] = {
		{ ABS_MT_SLOT, 0, num_tps - 1, 0, 0, 0 },
		{ .value = -1 },
	};

	dev = litest_create_device_with_overrides(LITEST_WACOM_TOUCH,
						  "litest Multi-touch device",
						  NULL, abs, NULL);
	libinput = dev->libinput;

	for (slot = 0; slot < num_tps; ++slot)
		litest_touch_down(dev, slot, 0, 0);
	for (slot = 0; slot < num_tps; ++slot)
		litest_touch_up(dev, slot);

	libinput_dispatch(libinput);
	while ((ev = libinput_get_event(libinput))) {
		type = libinput_event_get_type(ev);

		if (type == LIBINPUT_EVENT_TOUCH_DOWN)
			slot_count++;
		else if (type == LIBINPUT_EVENT_TOUCH_UP)
			break;

		libinput_event_destroy(ev);
		libinput_dispatch(libinput);
	}

	ck_assert_notnull(ev);
	ck_assert_int_gt(slot_count, 0);

	libinput_dispatch(libinput);
	do {
		type = libinput_event_get_type(ev);
		ck_assert_int_ne(type, LIBINPUT_EVENT_TOUCH_DOWN);
		if (type == LIBINPUT_EVENT_TOUCH_UP)
			slot_count--;

		libinput_event_destroy(ev);
		libinput_dispatch(libinput);
	} while ((ev = libinput_get_event(libinput)));

	ck_assert_int_eq(slot_count, 0);

	litest_delete_device(dev);
}
END_TEST

START_TEST(touch_double_touch_down_up)
{
	struct libinput *libinput;
	struct litest_device *dev;
	struct libinput_event *ev;
	bool got_down = false;
	bool got_up = false;

	dev = litest_current_device();
	libinput = dev->libinput;

	/* note: this test is a false negative, libevdev will filter
	 * tracking IDs re-used in the same slot. */

	litest_touch_down(dev, 0, 0, 0);
	litest_touch_down(dev, 0, 0, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(libinput);

	while ((ev = libinput_get_event(libinput))) {
		switch (libinput_event_get_type(ev)) {
		case LIBINPUT_EVENT_TOUCH_DOWN:
			ck_assert(!got_down);
			got_down = true;
			break;
		case LIBINPUT_EVENT_TOUCH_UP:
			ck_assert(got_down);
			ck_assert(!got_up);
			got_up = true;
			break;
		default:
			break;
		}

		libinput_event_destroy(ev);
		libinput_dispatch(libinput);
	}

	ck_assert(got_down);
	ck_assert(got_up);
}
END_TEST

START_TEST(touch_calibration_scale)
{
	struct libinput *li;
	struct litest_device *dev;
	struct libinput_event *ev;
	struct libinput_event_touch *tev;
	float matrix[6] = {
		1, 0, 0,
		0, 1, 0
	};

	float calibration;
	double x, y;
	const int width = 640, height = 480;

	dev = litest_current_device();
	li = dev->libinput;

	for (calibration = 0.1; calibration < 1; calibration += 0.1) {
		libinput_device_config_calibration_set_matrix(dev->libinput_device,
							      matrix);
		litest_drain_events(li);

		litest_touch_down(dev, 0, 100, 100);
		litest_touch_up(dev, 0);

		litest_wait_for_event(li);
		ev = libinput_get_event(li);
		tev = litest_is_touch_event(ev, LIBINPUT_EVENT_TOUCH_DOWN);

		x = libinput_event_touch_get_x_transformed(tev, width);
		y = libinput_event_touch_get_y_transformed(tev, height);

		ck_assert_int_eq(round(x), round(width * matrix[0]));
		ck_assert_int_eq(round(y), round(height * matrix[4]));

		libinput_event_destroy(ev);
		litest_drain_events(li);

		matrix[0] = calibration;
		matrix[4] = 1 - calibration;
	}
}
END_TEST

START_TEST(touch_calibration_rotation)
{
	struct libinput *li;
	struct litest_device *dev;
	struct libinput_event *ev;
	struct libinput_event_touch *tev;
	float matrix[6];
	int i;
	double x, y;
	int width = 1024, height = 480;

	dev = litest_current_device();
	li = dev->libinput;

	for (i = 0; i < 4; i++) {
		float angle = i * M_PI/2;

		/* [ cos -sin  tx ]
		   [ sin  cos  ty ]
		   [  0    0   1  ] */
		matrix[0] = cos(angle);
		matrix[1] = -sin(angle);
		matrix[3] = sin(angle);
		matrix[4] = cos(angle);

		switch(i) {
		case 0: /* 0 deg */
			matrix[2] = 0;
			matrix[5] = 0;
			break;
		case 1: /* 90 deg cw */
			matrix[2] = 1;
			matrix[5] = 0;
			break;
		case 2: /* 180 deg cw */
			matrix[2] = 1;
			matrix[5] = 1;
			break;
		case 3: /* 270 deg cw */
			matrix[2] = 0;
			matrix[5] = 1;
			break;
		}

		libinput_device_config_calibration_set_matrix(dev->libinput_device,
							      matrix);
		litest_drain_events(li);

		litest_touch_down(dev, 0, 80, 20);
		litest_touch_up(dev, 0);
		litest_wait_for_event(li);
		ev = libinput_get_event(li);
		tev = litest_is_touch_event(ev, LIBINPUT_EVENT_TOUCH_DOWN);

		x = libinput_event_touch_get_x_transformed(tev, width);
		y = libinput_event_touch_get_y_transformed(tev, height);

		/* rounding errors... */
#define almost_equal(a_, b_) \
		{ ck_assert_int_ge((a_) + 0.5, (b_) - 1); \
		  ck_assert_int_le((a_) + 0.5, (b_) + 1); }
		switch(i) {
		case 0: /* 0 deg */
			almost_equal(x, width * 0.8);
			almost_equal(y, height * 0.2);
			break;
		case 1: /* 90 deg cw */
			almost_equal(x, width * 0.8);
			almost_equal(y, height * 0.8);
			break;
		case 2: /* 180 deg cw */
			almost_equal(x, width * 0.2);
			almost_equal(y, height * 0.8);
			break;
		case 3: /* 270 deg cw */
			almost_equal(x, width * 0.2);
			almost_equal(y, height * 0.2);
			break;
		}
#undef almost_equal

		libinput_event_destroy(ev);
		litest_drain_events(li);
	}
}
END_TEST

START_TEST(touch_calibration_translation)
{
	struct libinput *li;
	struct litest_device *dev;
	struct libinput_event *ev;
	struct libinput_event_touch *tev;
	float matrix[6] = {
		1, 0, 0,
		0, 1, 0
	};

	float translate;
	double x, y;
	const int width = 640, height = 480;

	dev = litest_current_device();
	li = dev->libinput;

	/* translating from 0 up to 1 device width/height */
	for (translate = 0.1; translate <= 1; translate += 0.1) {
		libinput_device_config_calibration_set_matrix(dev->libinput_device,
							      matrix);
		litest_drain_events(li);

		litest_touch_down(dev, 0, 100, 100);
		litest_touch_up(dev, 0);

		litest_wait_for_event(li);
		ev = libinput_get_event(li);
		tev = litest_is_touch_event(ev, LIBINPUT_EVENT_TOUCH_DOWN);

		x = libinput_event_touch_get_x_transformed(tev, width);
		y = libinput_event_touch_get_y_transformed(tev, height);

		/* sigh. rounding errors */
		ck_assert_int_ge(round(x), width + round(width * matrix[2]) - 1);
		ck_assert_int_ge(round(y), height + round(height * matrix[5]) - 1);
		ck_assert_int_le(round(x), width + round(width * matrix[2]) + 1);
		ck_assert_int_le(round(y), height + round(height * matrix[5]) + 1);

		libinput_event_destroy(ev);
		litest_drain_events(li);

		matrix[2] = translate;
		matrix[5] = 1 - translate;
	}
}
END_TEST

START_TEST(touch_no_left_handed)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	enum libinput_config_status status;
	int rc;

	rc = libinput_device_config_left_handed_is_available(d);
	ck_assert_int_eq(rc, 0);

	rc = libinput_device_config_left_handed_get(d);
	ck_assert_int_eq(rc, 0);

	rc = libinput_device_config_left_handed_get_default(d);
	ck_assert_int_eq(rc, 0);

	status = libinput_device_config_left_handed_set(d, 0);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
}
END_TEST

START_TEST(fake_mt_exists)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_device *device;

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_DEVICE_ADDED, -1);
	event = libinput_get_event(li);
	device = libinput_event_get_device(event);

	ck_assert(!libinput_device_has_capability(device,
						  LIBINPUT_DEVICE_CAP_TOUCH));

	/* This test may need fixing if we add other fake-mt devices that
	 * have different capabilities */
	ck_assert(libinput_device_has_capability(device,
						 LIBINPUT_DEVICE_CAP_POINTER));

	libinput_event_destroy(event);
}
END_TEST

START_TEST(fake_mt_no_touch_events)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 70, 70, 5, 10);
	litest_touch_up(dev, 0);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 70);
	litest_touch_move_to(dev, 0, 50, 50, 90, 40, 10, 10);
	litest_touch_move_to(dev, 0, 70, 70, 40, 50, 10, 10);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE);
}
END_TEST

START_TEST(touch_protocol_a_init)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device = dev->libinput_device;

	ck_assert_int_ne(libinput_next_event_type(li),
			 LIBINPUT_EVENT_NONE);

	ck_assert(libinput_device_has_capability(device,
						 LIBINPUT_DEVICE_CAP_TOUCH));
}
END_TEST

START_TEST(touch_protocol_a_touch)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *ev;
	struct libinput_event_touch *tev;
	double x, y, oldx, oldy;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 5, 95);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TOUCH_DOWN, -1);

	ev = libinput_get_event(li);
	tev = litest_is_touch_event(ev, LIBINPUT_EVENT_TOUCH_DOWN);

	oldx = libinput_event_touch_get_x(tev);
	oldy = libinput_event_touch_get_y(tev);

	libinput_event_destroy(ev);

	litest_touch_move_to(dev, 0, 10, 90, 90, 10, 20, 1);
	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TOUCH_MOTION, -1);

	while ((ev = libinput_get_event(li))) {
		if (libinput_event_get_type(ev) ==
		    LIBINPUT_EVENT_TOUCH_FRAME) {
			libinput_event_destroy(ev);
			continue;
		}
		ck_assert_int_eq(libinput_event_get_type(ev),
				 LIBINPUT_EVENT_TOUCH_MOTION);

		tev = libinput_event_get_touch_event(ev);
		x = libinput_event_touch_get_x(tev);
		y = libinput_event_touch_get_y(tev);

		ck_assert_int_gt(x, oldx);
		ck_assert_int_lt(y, oldy);

		oldx = x;
		oldy = y;

		libinput_event_destroy(ev);
	}

	litest_touch_up(dev, 0);
	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TOUCH_UP, -1);
}
END_TEST

START_TEST(touch_protocol_a_2fg_touch)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *ev;
	struct libinput_event_touch *tev;
	int pos;

	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_touch_down(dev, 0, 5, 95);
	litest_touch_down(dev, 0, 95, 5);
	litest_pop_event_frame(dev);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TOUCH_DOWN, -1);

	ev = libinput_get_event(li);
	libinput_event_destroy(ev);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TOUCH_DOWN, -1);

	ev = libinput_get_event(li);
	libinput_event_destroy(ev);

	for (pos = 10; pos < 100; pos += 10) {
		litest_push_event_frame(dev);
		litest_touch_move_to(dev, 0, pos, 100 - pos, pos, 100 - pos, 1, 1);
		litest_touch_move_to(dev, 0, 100 - pos, pos, 100 - pos, pos, 1, 1);
		litest_pop_event_frame(dev);
		litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TOUCH_MOTION, -1);
		ev = libinput_get_event(li);
		tev = libinput_event_get_touch_event(ev);
		ck_assert_int_eq(libinput_event_touch_get_slot(tev),
				0);
		libinput_event_destroy(ev);

		litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TOUCH_MOTION, -1);
		ev = libinput_get_event(li);
		tev = libinput_event_get_touch_event(ev);
		ck_assert_int_eq(libinput_event_touch_get_slot(tev),
				1);
		libinput_event_destroy(ev);
	}

	litest_event(dev, EV_SYN, SYN_MT_REPORT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TOUCH_UP, -1);
	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TOUCH_UP, -1);
}
END_TEST

START_TEST(touch_initial_state)
{
	struct litest_device *dev;
	struct libinput *libinput1, *libinput2;
	struct libinput_event *ev1 = NULL;
	struct libinput_event *ev2 = NULL;
	struct libinput_event_touch *t1, *t2;
	struct libinput_device *device1, *device2;
	int axis = _i; /* looped test */

	dev = litest_current_device();
	device1 = dev->libinput_device;
	libinput_device_config_tap_set_enabled(device1,
					       LIBINPUT_CONFIG_TAP_DISABLED);

	libinput1 = dev->libinput;
	litest_touch_down(dev, 0, 40, 60);
	litest_touch_up(dev, 0);

	/* device is now on some x/y value */
	litest_drain_events(libinput1);

	libinput2 = litest_create_context();
	device2 = libinput_path_add_device(libinput2,
					   libevdev_uinput_get_devnode(
							       dev->uinput));
	libinput_device_config_tap_set_enabled(device2,
					       LIBINPUT_CONFIG_TAP_DISABLED);
	litest_drain_events(libinput2);

	if (axis == ABS_X)
		litest_touch_down(dev, 0, 40, 70);
	else
		litest_touch_down(dev, 0, 70, 60);
	litest_touch_up(dev, 0);

	litest_wait_for_event(libinput1);
	litest_wait_for_event(libinput2);

	while (libinput_next_event_type(libinput1)) {
		ev1 = libinput_get_event(libinput1);
		ev2 = libinput_get_event(libinput2);

		t1 = litest_is_touch_event(ev1, 0);
		t2 = litest_is_touch_event(ev2, 0);

		ck_assert_int_eq(libinput_event_get_type(ev1),
				 libinput_event_get_type(ev2));

		if (libinput_event_get_type(ev1) == LIBINPUT_EVENT_TOUCH_UP ||
		    libinput_event_get_type(ev1) == LIBINPUT_EVENT_TOUCH_FRAME)
			break;

		ck_assert_int_eq(libinput_event_touch_get_x(t1),
				 libinput_event_touch_get_x(t2));
		ck_assert_int_eq(libinput_event_touch_get_y(t1),
				 libinput_event_touch_get_y(t2));

		libinput_event_destroy(ev1);
		libinput_event_destroy(ev2);
	}

	libinput_event_destroy(ev1);
	libinput_event_destroy(ev2);

	libinput_unref(libinput2);
}
END_TEST

START_TEST(touch_time_usec)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_touch *tev;
	uint64_t time_usec;

	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 0, 10, 10);

	litest_wait_for_event(li);

	event = libinput_get_event(li);
	tev = litest_is_touch_event(event, LIBINPUT_EVENT_TOUCH_DOWN);
	time_usec = libinput_event_touch_get_time_usec(tev);
	ck_assert_int_eq(libinput_event_touch_get_time(tev),
			 (uint32_t) (time_usec / 1000));
	libinput_event_destroy(event);
}
END_TEST

START_TEST(touch_fuzz)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int i;
	int x = 700, y = 300;

	litest_drain_events(dev->libinput);

	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, 30);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_is_touch_event(event, LIBINPUT_EVENT_TOUCH_DOWN);
	libinput_event_destroy(event);
	event = libinput_get_event(li);
	litest_is_touch_event(event, LIBINPUT_EVENT_TOUCH_FRAME);
	libinput_event_destroy(event);

	litest_drain_events(li);

	for (i = 0; i < 50; i++) {
		if (i % 2) {
			x++;
			y--;
		} else {
			x--;
			y++;
		}
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
		litest_assert_empty_queue(li);
	}
}
END_TEST

void
litest_setup_tests_touch(void)
{
	struct range axes = { ABS_X, ABS_Y + 1};

	litest_add("touch:frame", touch_frame_events, LITEST_TOUCH, LITEST_ANY);
	litest_add_no_device("touch:abs-transform", touch_abs_transform);
	litest_add_no_device("touch:many-slots", touch_many_slots);
	litest_add("touch:double-touch-down-up", touch_double_touch_down_up, LITEST_TOUCH, LITEST_ANY);
	litest_add("touch:calibration", touch_calibration_scale, LITEST_TOUCH, LITEST_TOUCHPAD);
	litest_add("touch:calibration", touch_calibration_scale, LITEST_SINGLE_TOUCH, LITEST_TOUCHPAD);
	litest_add("touch:calibration", touch_calibration_rotation, LITEST_TOUCH, LITEST_TOUCHPAD);
	litest_add("touch:calibration", touch_calibration_rotation, LITEST_SINGLE_TOUCH, LITEST_TOUCHPAD);
	litest_add("touch:calibration", touch_calibration_translation, LITEST_TOUCH, LITEST_TOUCHPAD);
	litest_add("touch:calibration", touch_calibration_translation, LITEST_SINGLE_TOUCH, LITEST_TOUCHPAD);

	litest_add("touch:left-handed", touch_no_left_handed, LITEST_TOUCH, LITEST_ANY);

	litest_add("touch:fake-mt", fake_mt_exists, LITEST_FAKE_MT, LITEST_ANY);
	litest_add("touch:fake-mt", fake_mt_no_touch_events, LITEST_FAKE_MT, LITEST_ANY);

	litest_add("touch:protocol a", touch_protocol_a_init, LITEST_PROTOCOL_A, LITEST_ANY);
	litest_add("touch:protocol a", touch_protocol_a_touch, LITEST_PROTOCOL_A, LITEST_ANY);
	litest_add("touch:protocol a", touch_protocol_a_2fg_touch, LITEST_PROTOCOL_A, LITEST_ANY);

	litest_add_ranged("touch:state", touch_initial_state, LITEST_TOUCH, LITEST_PROTOCOL_A, &axes);

	litest_add("touch:time", touch_time_usec, LITEST_TOUCH, LITEST_TOUCHPAD);

	litest_add_for_device("touch:fuzz", touch_fuzz, LITEST_MULTITOUCH_FUZZ_SCREEN);
}
