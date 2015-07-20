/*
 * Copyright © 2015 Red Hat, Inc.
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
#include <libinput.h>

#include "libinput-util.h"
#include "litest.h"

START_TEST(gestures_cap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;

	ck_assert(libinput_device_has_capability(device,
						 LIBINPUT_DEVICE_CAP_GESTURE));
}
END_TEST

START_TEST(gestures_nocap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;

	ck_assert(!libinput_device_has_capability(device,
						  LIBINPUT_DEVICE_CAP_GESTURE));
}
END_TEST

START_TEST(gestures_swipe_3fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_gesture *gevent;
	double dx, dy;
	int cardinal = _i; /* ranged test */
	double dir_x, dir_y;
	int cardinals[8][2] = {
		{ 0, 30 },
		{ 30, 30 },
		{ 30, 0 },
		{ 30, -30 },
		{ 0, -30 },
		{ -30, -30 },
		{ -30, 0 },
		{ -30, 30 },
	};

	if (libevdev_get_num_slots(dev->evdev) < 3)
		return;

	dir_x = cardinals[cardinal][0];
	dir_y = cardinals[cardinal][1];

	litest_drain_events(li);

	litest_touch_down(dev, 0, 40, 40);
	litest_touch_down(dev, 1, 40, 50);
	litest_touch_down(dev, 2, 40, 60);
	libinput_dispatch(li);
	litest_touch_move_three_touches(dev,
					40, 40,
					40, 50,
					40, 60,
					dir_x, dir_y,
					10, 2);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	gevent = litest_is_gesture_event(event,
					 LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN,
					 3);
	dx = libinput_event_gesture_get_dx(gevent);
	dy = libinput_event_gesture_get_dy(gevent);
	ck_assert(dx == 0.0);
	ck_assert(dy == 0.0);
	libinput_event_destroy(event);

	while ((event = libinput_get_event(li)) != NULL) {
		gevent = litest_is_gesture_event(event,
						 LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE,
						 3);

		dx = libinput_event_gesture_get_dx(gevent);
		dy = libinput_event_gesture_get_dy(gevent);
		debug_trace("delta: %.2f/%.2f\n", dx, dy);
		if (dir_x == 0.0)
			ck_assert(dx == 0.0);
		else if (dir_x < 0.0)
			ck_assert(dx < 0.0);
		else if (dir_x > 0.0)
			ck_assert(dx > 0.0);

		if (dir_y == 0.0)
			ck_assert(dy == 0.0);
		else if (dir_y < 0.0)
			ck_assert(dy < 0.0);
		else if (dir_y > 0.0)
			ck_assert(dy > 0.0);

		dx = libinput_event_gesture_get_dx_unaccelerated(gevent);
		dy = libinput_event_gesture_get_dy_unaccelerated(gevent);
		if (dir_x == 0.0)
			ck_assert(dx == 0.0);
		else if (dir_x < 0.0)
			ck_assert(dx < 0.0);
		else if (dir_x > 0.0)
			ck_assert(dx > 0.0);

		if (dir_y == 0.0)
			ck_assert(dy == 0.0);
		else if (dir_y < 0.0)
			ck_assert(dy < 0.0);
		else if (dir_y > 0.0)
			ck_assert(dy > 0.0);

		libinput_event_destroy(event);
	}

	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 2);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	gevent = litest_is_gesture_event(event,
					 LIBINPUT_EVENT_GESTURE_SWIPE_END,
					 3);
	ck_assert(!libinput_event_gesture_get_cancelled(gevent));
	libinput_event_destroy(event);
}
END_TEST

START_TEST(gestures_pinch)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_gesture *gevent;
	double dx, dy;
	int cardinal = _i; /* ranged test */
	double dir_x, dir_y;
	int i;
	double scale, oldscale;
	double angle;
	int cardinals[8][2] = {
		{ 0, 30 },
		{ 30, 30 },
		{ 30, 0 },
		{ 30, -30 },
		{ 0, -30 },
		{ -30, -30 },
		{ -30, 0 },
		{ -30, 30 },
	};

	if (libevdev_get_num_slots(dev->evdev) < 3)
		return;

	dir_x = cardinals[cardinal][0];
	dir_y = cardinals[cardinal][1];

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50 + dir_x, 50 + dir_y);
	litest_touch_down(dev, 1, 50 - dir_x, 50 - dir_y);
	libinput_dispatch(li);

	for (i = 0; i < 8; i++) {
		litest_push_event_frame(dev);
		if (dir_x > 0.0)
			dir_x -= 3;
		else if (dir_x < 0.0)
			dir_x += 3;
		if (dir_y > 0.0)
			dir_y -= 3;
		else if (dir_y < 0.0)
			dir_y += 3;
		litest_touch_move(dev,
				  0,
				  50 + dir_x,
				  50 + dir_y);
		litest_touch_move(dev,
				  1,
				  50 - dir_x,
				  50 - dir_y);
		litest_pop_event_frame(dev);
		libinput_dispatch(li);
	}

	event = libinput_get_event(li);
	gevent = litest_is_gesture_event(event,
					 LIBINPUT_EVENT_GESTURE_PINCH_BEGIN,
					 2);
	dx = libinput_event_gesture_get_dx(gevent);
	dy = libinput_event_gesture_get_dy(gevent);
	scale = libinput_event_gesture_get_scale(gevent);
	ck_assert(dx == 0.0);
	ck_assert(dy == 0.0);
	ck_assert(scale == 1.0);

	libinput_event_destroy(event);

	while ((event = libinput_get_event(li)) != NULL) {
		gevent = litest_is_gesture_event(event,
						 LIBINPUT_EVENT_GESTURE_PINCH_UPDATE,
						 2);

		oldscale = scale;
		scale = libinput_event_gesture_get_scale(gevent);

		ck_assert(scale < oldscale);

		angle = libinput_event_gesture_get_angle_delta(gevent);
		ck_assert_double_le(fabs(angle), 1.0);

		libinput_event_destroy(event);
		libinput_dispatch(li);
	}

	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	gevent = litest_is_gesture_event(event,
					 LIBINPUT_EVENT_GESTURE_PINCH_END,
					 2);
	ck_assert(!libinput_event_gesture_get_cancelled(gevent));
	libinput_event_destroy(event);
}
END_TEST

START_TEST(gestures_spread)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_gesture *gevent;
	double dx, dy;
	int cardinal = _i; /* ranged test */
	double dir_x, dir_y;
	int i;
	double scale, oldscale;
	double angle;
	int cardinals[8][2] = {
		{ 0, 1 },
		{ 1, 1 },
		{ 1, 0 },
		{ 1, -1 },
		{ 0, -1 },
		{ -1, -1 },
		{ -1, 0 },
		{ -1, 1 },
	};

	if (libevdev_get_num_slots(dev->evdev) < 3)
		return;

	dir_x = cardinals[cardinal][0];
	dir_y = cardinals[cardinal][1];

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50 + dir_x, 50 + dir_y);
	litest_touch_down(dev, 1, 50 - dir_x, 50 - dir_y);
	libinput_dispatch(li);

	for (i = 0; i < 8; i++) {
		litest_push_event_frame(dev);
		if (dir_x > 0.0)
			dir_x += 3;
		else if (dir_x < 0.0)
			dir_x -= 3;
		if (dir_y > 0.0)
			dir_y += 3;
		else if (dir_y < 0.0)
			dir_y -= 3;
		litest_touch_move(dev,
				  0,
				  50 + dir_x,
				  50 + dir_y);
		litest_touch_move(dev,
				  1,
				  50 - dir_x,
				  50 - dir_y);
		litest_pop_event_frame(dev);
		libinput_dispatch(li);
	}

	event = libinput_get_event(li);
	gevent = litest_is_gesture_event(event,
					 LIBINPUT_EVENT_GESTURE_PINCH_BEGIN,
					 2);
	dx = libinput_event_gesture_get_dx(gevent);
	dy = libinput_event_gesture_get_dy(gevent);
	scale = libinput_event_gesture_get_scale(gevent);
	ck_assert(dx == 0.0);
	ck_assert(dy == 0.0);
	ck_assert(scale == 1.0);

	libinput_event_destroy(event);

	while ((event = libinput_get_event(li)) != NULL) {
		gevent = litest_is_gesture_event(event,
						 LIBINPUT_EVENT_GESTURE_PINCH_UPDATE,
						 2);
		oldscale = scale;
		scale = libinput_event_gesture_get_scale(gevent);
		ck_assert(scale > oldscale);

		angle = libinput_event_gesture_get_angle_delta(gevent);
		ck_assert_double_le(fabs(angle), 1.0);

		libinput_event_destroy(event);
		libinput_dispatch(li);
	}

	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	gevent = litest_is_gesture_event(event,
					 LIBINPUT_EVENT_GESTURE_PINCH_END,
					 2);
	ck_assert(!libinput_event_gesture_get_cancelled(gevent));
	libinput_event_destroy(event);
}
END_TEST

void
litest_setup_tests(void)
{
	/* N, NE, ... */
	struct range cardinals = { 0, 8 };

	litest_add("gestures:cap", gestures_cap, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("gestures:cap", gestures_nocap, LITEST_ANY, LITEST_TOUCHPAD);

	litest_add_ranged("gestures:swipe", gestures_swipe_3fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH, &cardinals);
	litest_add_ranged("gestures:pinch", gestures_pinch, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH, &cardinals);
	litest_add_ranged("gestures:pinch", gestures_spread, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH, &cardinals);
}
