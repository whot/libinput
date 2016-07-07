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

#include "libinput-util.h"
#include "litest.h"

static void
test_2fg_scroll(struct litest_device *dev, double dx, double dy, int want_sleep)
{
	struct libinput *li = dev->libinput;

	litest_touch_down(dev, 0, 49, 50);
	litest_touch_down(dev, 1, 51, 50);

	litest_touch_move_two_touches(dev, 49, 50, 51, 50, dx, dy, 10, 0);

	/* Avoid a small scroll being seen as a tap */
	if (want_sleep) {
		libinput_dispatch(li);
		litest_timeout_tap();
		libinput_dispatch(li);
	}

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
}

START_TEST(touchpad_2fg_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	test_2fg_scroll(dev, 0.1, 40, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 10);
	test_2fg_scroll(dev, 0.1, -40, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -10);
	test_2fg_scroll(dev, 40, 0.1, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 10);
	test_2fg_scroll(dev, -40, 0.1, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -10);

	/* 2fg scroll smaller than the threshold should not generate events */
	test_2fg_scroll(dev, 0.1, 0.1, 1);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_scroll_diagonal)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	int i;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 45, 30);
	litest_touch_down(dev, 1, 55, 30);

	litest_touch_move_two_touches(dev, 45, 30, 55, 30, 10, 10, 10, 0);
	libinput_dispatch(li);
	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_POINTER_AXIS,
				      -1);
	litest_drain_events(li);

	/* get rid of any touch history still adding x deltas sideways */
	for (i = 0; i < 5; i++)
		litest_touch_move(dev, 0, 55, 41 + i);
	litest_drain_events(li);

	for (i = 6; i < 10; i++) {
		litest_touch_move(dev, 0, 55, 41 + i);
		libinput_dispatch(li);

		event = libinput_get_event(li);
		ptrev = litest_is_axis_event(event,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
				LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
		ck_assert(!libinput_event_pointer_has_axis(ptrev,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL));
		libinput_event_destroy(event);
	}

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
}
END_TEST

START_TEST(touchpad_2fg_scroll_slow_distance)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	double width, height;
	double y_move = 100;

	if (!litest_has_2fg_scroll(dev))
		return;

	/* We want to move > 5 mm. */
	ck_assert_int_eq(libinput_device_get_size(dev->libinput_device,
						  &width,
						  &height), 0);
	y_move = 100.0/height * 7;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 49, 50);
	litest_touch_down(dev, 1, 51, 50);
	litest_touch_move_two_touches(dev, 49, 50, 51, 50, 0, y_move, 100, 10);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert_notnull(event);

	/* last event is value 0, tested elsewhere */
	while (libinput_next_event_type(li) != LIBINPUT_EVENT_NONE) {
		double axisval;
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_AXIS);
		ptrev = libinput_event_get_pointer_event(event);

		axisval = libinput_event_pointer_get_axis_value(ptrev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		ck_assert(axisval > 0.0);

		/* this is to verify we test the right thing, if the value
		   is greater than scroll.threshold we triggered the wrong
		   condition */
		ck_assert(axisval < 5.0);

		libinput_event_destroy(event);
		event = libinput_get_event(li);
	}

	litest_assert_empty_queue(li);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(touchpad_2fg_scroll_source)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	test_2fg_scroll(dev, 0, 30, 0);
	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_POINTER_AXIS, -1);

	while ((event = libinput_get_event(li))) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_AXIS);
		ptrev = libinput_event_get_pointer_event(event);
		ck_assert_int_eq(libinput_event_pointer_get_axis_source(ptrev),
				 LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(touchpad_2fg_scroll_semi_mt)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 20);
	litest_touch_down(dev, 1, 30, 20);
	libinput_dispatch(li);
	litest_touch_move_two_touches(dev,
				      20, 20,
				      30, 20,
				      30, 40,
				      10, 1);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_2fg_scroll_return_to_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	/* start with motion */
	litest_touch_down(dev, 0, 70, 70);
	litest_touch_move_to(dev, 0, 70, 70, 49, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* 2fg scroll */
	litest_touch_down(dev, 1, 51, 50);
	litest_touch_move_two_touches(dev, 49, 50, 51, 50, 0, 20, 5, 0);
	litest_touch_up(dev, 1);
	libinput_dispatch(li);
	litest_timeout_finger_switch();
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_touch_move_to(dev, 0, 49, 70, 49, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* back to 2fg scroll, lifting the other finger */
	litest_touch_down(dev, 1, 51, 50);
	litest_touch_move_two_touches(dev, 49, 50, 51, 50, 0, 20, 5, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_finger_switch();
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* move with second finger */
	litest_touch_move_to(dev, 1, 51, 70, 51, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 1);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_scroll_natural_defaults)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_ge(libinput_device_config_scroll_has_natural_scroll(dev->libinput_device), 1);
	ck_assert_int_eq(libinput_device_config_scroll_get_natural_scroll_enabled(dev->libinput_device), 0);
	ck_assert_int_eq(libinput_device_config_scroll_get_default_natural_scroll_enabled(dev->libinput_device), 0);
}
END_TEST

START_TEST(touchpad_scroll_natural_enable_config)
{
	struct litest_device *dev = litest_current_device();
	enum libinput_config_status status;

	status = libinput_device_config_scroll_set_natural_scroll_enabled(dev->libinput_device, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	ck_assert_int_eq(libinput_device_config_scroll_get_natural_scroll_enabled(dev->libinput_device), 1);

	status = libinput_device_config_scroll_set_natural_scroll_enabled(dev->libinput_device, 0);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	ck_assert_int_eq(libinput_device_config_scroll_get_natural_scroll_enabled(dev->libinput_device), 0);
}
END_TEST

START_TEST(touchpad_scroll_natural_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!litest_has_2fg_scroll(dev))
		return;

	litest_enable_2fg_scroll(dev);
	litest_drain_events(li);

	libinput_device_config_scroll_set_natural_scroll_enabled(dev->libinput_device, 1);

	test_2fg_scroll(dev, 0.1, 40, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -10);
	test_2fg_scroll(dev, 0.1, -40, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 10);
	test_2fg_scroll(dev, 40, 0.1, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -10);
	test_2fg_scroll(dev, -40, 0.1, 0);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 10);

}
END_TEST

START_TEST(touchpad_scroll_natural_edge)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	libinput_device_config_scroll_set_natural_scroll_enabled(dev->libinput_device, 1);

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -4);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 99, 80);
	litest_touch_move_to(dev, 0, 99, 80, 99, 20, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 4);
	litest_assert_empty_queue(li);

}
END_TEST

START_TEST(touchpad_edge_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 4);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 99, 80);
	litest_touch_move_to(dev, 0, 99, 80, 99, 20, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -4);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 20, 99);
	litest_touch_move_to(dev, 0, 20, 99, 70, 99, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 4);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 70, 99);
	litest_touch_move_to(dev, 0, 70, 99, 20, 99, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -4);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_scroll_defaults)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libevdev *evdev = dev->evdev;
	enum libinput_config_scroll_method method, expected;
	enum libinput_config_status status;

	method = libinput_device_config_scroll_get_methods(device);
	ck_assert(method & LIBINPUT_CONFIG_SCROLL_EDGE);
	if (libevdev_get_num_slots(evdev) > 1)
		ck_assert(method & LIBINPUT_CONFIG_SCROLL_2FG);
	else
		ck_assert((method & LIBINPUT_CONFIG_SCROLL_2FG) == 0);

	if (libevdev_get_num_slots(evdev) > 1)
		expected = LIBINPUT_CONFIG_SCROLL_2FG;
	else
		expected = LIBINPUT_CONFIG_SCROLL_EDGE;

	method = libinput_device_config_scroll_get_method(device);
	ck_assert_int_eq(method, expected);
	method = libinput_device_config_scroll_get_default_method(device);
	ck_assert_int_eq(method, expected);

	status = libinput_device_config_scroll_set_method(device,
					  LIBINPUT_CONFIG_SCROLL_EDGE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_scroll_set_method(device,
					  LIBINPUT_CONFIG_SCROLL_2FG);

	if (libevdev_get_num_slots(evdev) > 1)
		ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	else
		ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
}
END_TEST

START_TEST(touchpad_edge_scroll_timeout)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	double width = 0, height = 0;
	int nevents = 0;
	double mm; /* one mm in percent of the device */

	ck_assert_int_eq(libinput_device_get_size(dev->libinput_device,
						  &width,
						  &height), 0);
	mm = 100.0/height;

	/* timeout-based scrolling is disabled when software buttons are
	 * active, so switch to clickfinger. Not all test devices support
	 * that, hence the extra check. */
	if (libinput_device_config_click_get_methods(dev->libinput_device) &
	    LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER)
		litest_enable_clickfinger(dev);

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	/* move 0.5mm, enough to load up the motion history, but less than
	 * the scroll threshold of 2mm */
	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 20 + mm/2, 8, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_timeout_edgescroll();
	libinput_dispatch(li);

	litest_assert_empty_queue(li);

	/* now move slowly up to the 2mm scroll threshold. we expect events */
	litest_touch_move_to(dev, 0, 99, 20 + mm/2, 99, 20 + mm * 2, 20, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_POINTER_AXIS, -1);

	while ((event = libinput_get_event(li))) {
		double value;

		ptrev = litest_is_axis_event(event,
					     LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
					     0);
		value = libinput_event_pointer_get_axis_value(ptrev,
							      LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		ck_assert_double_lt(value, 5.0);
		libinput_event_destroy(event);
		nevents++;
	}

	/* we sent 20 events but allow for some to be swallowed by rounding
	 * errors, the hysteresis, etc. */
	ck_assert_int_ge(nevents, 10);

	litest_assert_empty_queue(li);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(touchpad_edge_scroll_no_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 99, 10);
	litest_touch_move_to(dev, 0, 99, 10, 99, 70, 12, 0);
	/* moving outside -> no motion event */
	litest_touch_move_to(dev, 0, 99, 70, 20, 80, 12, 0);
	/* moving down outside edge once scrolling had started -> scroll */
	litest_touch_move_to(dev, 0, 20, 80, 40, 99, 12, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 4);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_edge_scroll_no_edge_after_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	/* moving into the edge zone must not trigger scroll events */
	litest_touch_down(dev, 0, 20, 20);
	litest_touch_move_to(dev, 0, 20, 20, 99, 20, 12, 0);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 12, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_edge_scroll_source)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 10, 0);
	litest_touch_up(dev, 0);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_POINTER_AXIS, -1);

	while ((event = libinput_get_event(li))) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_AXIS);
		ptrev = libinput_event_get_pointer_event(event);
		ck_assert_int_eq(libinput_event_pointer_get_axis_source(ptrev),
				 LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(touchpad_edge_scroll_no_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);
	litest_enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 49, 50);
	litest_touch_down(dev, 1, 51, 50);
	litest_touch_move_two_touches(dev, 49, 50, 51, 50, 20, 30, 5, 0);
	libinput_dispatch(li);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	libinput_dispatch(li);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_edge_scroll_into_buttonareas)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_buttonareas(dev);
	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 40);
	litest_touch_move_to(dev, 0, 99, 40, 99, 95, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
	/* in the button zone now, make sure we still get events */
	litest_touch_move_to(dev, 0, 99, 95, 99, 100, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* and out of the zone again */
	litest_touch_move_to(dev, 0, 99, 100, 99, 70, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* still out of the zone */
	litest_touch_move_to(dev, 0, 99, 70, 99, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_edge_scroll_within_buttonareas)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_buttonareas(dev);
	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 99);

	/* within left button */
	litest_touch_move_to(dev, 0, 20, 99, 40, 99, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* over to right button */
	litest_touch_move_to(dev, 0, 40, 99, 60, 99, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* within right button */
	litest_touch_move_to(dev, 0, 60, 99, 80, 99, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_edge_scroll_buttonareas_click_stops_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	double val;

	litest_enable_buttonareas(dev);
	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 95);
	litest_touch_move_to(dev, 0, 20, 95, 70, 95, 10, 5);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_button_click(dev, BTN_LEFT, true);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	ptrev = litest_is_axis_event(event,
				     LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL,
				     LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
	val = libinput_event_pointer_get_axis_value(ptrev,
				    LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
	ck_assert(val == 0.0);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_RIGHT,
				       LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_event_destroy(event);

	/* within button areas -> no movement */
	litest_touch_move_to(dev, 0, 70, 95, 90, 95, 10, 0);
	litest_assert_empty_queue(li);

	litest_button_click(dev, BTN_LEFT, false);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_edge_scroll_clickfinger_click_stops_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	double val;

	litest_enable_clickfinger(dev);
	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 95);
	litest_touch_move_to(dev, 0, 20, 95, 70, 95, 10, 5);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_button_click(dev, BTN_LEFT, true);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	ptrev = litest_is_axis_event(event,
				     LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL,
				     LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
	val = libinput_event_pointer_get_axis_value(ptrev,
				    LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
	ck_assert(val == 0.0);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_event_destroy(event);

	/* clickfinger releases pointer -> expect movement */
	litest_touch_move_to(dev, 0, 70, 95, 90, 95, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
	litest_assert_empty_queue(li);

	litest_button_click(dev, BTN_LEFT, false);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_edge_scroll_into_area)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_edge_scroll(dev);
	litest_drain_events(li);

	/* move into area, move vertically, move back to edge */

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 50, 15, 2);
	litest_touch_move_to(dev, 0, 99, 50, 20, 50, 15, 2);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_AXIS);
	litest_touch_move_to(dev, 0, 20, 50, 20, 20, 15, 2);
	litest_touch_move_to(dev, 0, 20, 20, 99, 20, 15, 2);
	litest_assert_empty_queue(li);

	litest_touch_move_to(dev, 0, 99, 20, 99, 50, 15, 2);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

void
litest_setup_tests(void)
{
	litest_add("touchpad:scroll", touchpad_2fg_scroll, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_diagonal, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_slow_distance, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_return_to_motion, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_source, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_semi_mt, LITEST_SEMI_MT, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_scroll_natural_defaults, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_scroll_natural_enable_config, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_scroll_natural_2fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_scroll_natural_edge, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_scroll_defaults, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_no_motion, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_no_edge_after_motion, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_timeout, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_source, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_no_2fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_edge_scroll_into_buttonareas, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_within_buttonareas, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_buttonareas_click_stops_scroll, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_clickfinger_click_stops_scroll, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_into_area, LITEST_TOUCHPAD, LITEST_ANY);
}
