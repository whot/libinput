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
		ck_assert_int_eq(libinput_event_get_type(ev),
				 LIBINPUT_EVENT_TOUCH_DOWN);
		tev = libinput_event_get_touch_event(ev);

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
		ck_assert_int_eq(libinput_event_get_type(ev),
				 LIBINPUT_EVENT_TOUCH_DOWN);
		tev = libinput_event_get_touch_event(ev);

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
		ck_assert_int_eq(libinput_event_get_type(ev),
				 LIBINPUT_EVENT_TOUCH_DOWN);
		tev = libinput_event_get_touch_event(ev);

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

int
main(int argc, char **argv)
{
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

	return litest_run(argc, argv);
}
