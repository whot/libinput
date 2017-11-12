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

#include <stdio.h>
#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <math.h>
#include <unistd.h>
#include <values.h>

#include "libinput-util.h"
#include "litest.h"

static void
test_relative_event(struct litest_device *dev, int dx, int dy)
{
	struct libinput *li = dev->libinput;
	struct libinput_event_pointer *ptrev;
	struct libinput_event *event;
	double ev_dx, ev_dy;
	double expected_dir;
	double expected_length;
	double actual_dir;
	double actual_length;

	litest_event(dev, EV_REL, REL_X, dx);
	litest_event(dev, EV_REL, REL_Y, dy);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ptrev = litest_is_motion_event(event);

	expected_length = sqrt(4 * dx*dx + 4 * dy*dy);
	expected_dir = atan2(dx, dy);

	ev_dx = libinput_event_pointer_get_dx(ptrev);
	ev_dy = libinput_event_pointer_get_dy(ptrev);
	actual_length = sqrt(ev_dx*ev_dx + ev_dy*ev_dy);
	actual_dir = atan2(ev_dx, ev_dy);

	/* Check the length of the motion vector (tolerate 1.0 indifference). */
	litest_assert(fabs(expected_length) >= actual_length);

	/* Check the direction of the motion vector (tolerate 2π/4 radians
	 * indifference). */
	litest_assert(fabs(expected_dir - actual_dir) < M_PI_2);

	libinput_event_destroy(event);

	litest_drain_events(dev->libinput);
}

static void
disable_button_scrolling(struct litest_device *device)
{
	struct libinput_device *dev = device->libinput_device;
	enum libinput_config_status status,
				    expected;

	status = libinput_device_config_scroll_set_method(dev,
					LIBINPUT_CONFIG_SCROLL_NO_SCROLL);

	expected = LIBINPUT_CONFIG_STATUS_SUCCESS;
	litest_assert_int_eq(status, expected);
}

START_TEST(pointer_motion_relative)
{
	struct litest_device *dev = litest_current_device();

	/* send a single event, the first movement
	   is always decelerated by 0.3 */
	litest_event(dev, EV_REL, REL_X, 1);
	litest_event(dev, EV_REL, REL_Y, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(dev->libinput);

	litest_drain_events(dev->libinput);

	test_relative_event(dev, 1, 0);
	test_relative_event(dev, 1, 1);
	test_relative_event(dev, 1, -1);
	test_relative_event(dev, 0, 1);

	test_relative_event(dev, -1, 0);
	test_relative_event(dev, -1, 1);
	test_relative_event(dev, -1, -1);
	test_relative_event(dev, 0, -1);
}
END_TEST

START_TEST(pointer_motion_relative_zero)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;

	/* NOTE: this test does virtually nothing. The kernel should not
	 * allow 0/0 events to be passed to userspace. If it ever happens,
	 * let's hope this test fails if we do the wrong thing.
	 */
	litest_drain_events(li);

	for (i = 0; i < 5; i++) {
		litest_event(dev, EV_REL, REL_X, 0);
		litest_event(dev, EV_REL, REL_Y, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}
	litest_assert_empty_queue(li);

	/* send a single event, the first movement
	   is always decelerated by 0.3 */
	litest_event(dev, EV_REL, REL_X, 1);
	litest_event(dev, EV_REL, REL_Y, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	libinput_event_destroy(libinput_get_event(li));
	litest_assert_empty_queue(li);

	for (i = 0; i < 5; i++) {
		litest_event(dev, EV_REL, REL_X, 0);
		litest_event(dev, EV_REL, REL_Y, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(dev->libinput);
	}
	litest_assert_empty_queue(li);

}
END_TEST

START_TEST(pointer_motion_relative_min_decel)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_pointer *ptrev;
	struct libinput_event *event;
	double evx, evy;
	int dx, dy;
	int cardinal = _i; /* ranged test */
	double len;

	int deltas[8][2] = {
		/* N, NE, E, ... */
		{ 0, 1 },
		{ 1, 1 },
		{ 1, 0 },
		{ 1, -1 },
		{ 0, -1 },
		{ -1, -1 },
		{ -1, 0 },
		{ -1, 1 },
	};

	litest_drain_events(dev->libinput);

	dx = deltas[cardinal][0];
	dy = deltas[cardinal][1];

	litest_event(dev, EV_REL, REL_X, dx);
	litest_event(dev, EV_REL, REL_Y, dy);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	ptrev = litest_is_motion_event(event);
	evx = libinput_event_pointer_get_dx(ptrev);
	evy = libinput_event_pointer_get_dy(ptrev);

	ck_assert((evx == 0.0) == (dx == 0));
	ck_assert((evy == 0.0) == (dy == 0));

	len = hypot(evx, evy);
	ck_assert(fabs(len) >= 0.3);

	libinput_event_destroy(event);
}
END_TEST

static void
test_absolute_event(struct litest_device *dev, double x, double y)
{
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	double ex, ey;
	enum libinput_event_type type = LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE;

	litest_touch_down(dev, 0, x, y);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_assert_notnull(event);
	litest_assert_int_eq(libinput_event_get_type(event), type);

	ptrev = libinput_event_get_pointer_event(event);
	litest_assert(ptrev != NULL);

	ex = libinput_event_pointer_get_absolute_x_transformed(ptrev, 100);
	ey = libinput_event_pointer_get_absolute_y_transformed(ptrev, 100);
	litest_assert_int_eq((int)(ex + 0.5), (int)x);
	litest_assert_int_eq((int)(ey + 0.5), (int)y);

	libinput_event_destroy(event);
}

START_TEST(pointer_motion_absolute)
{
	struct litest_device *dev = litest_current_device();

	litest_drain_events(dev->libinput);

	test_absolute_event(dev, 0, 100);
	test_absolute_event(dev, 100, 0);
	test_absolute_event(dev, 50, 50);
}
END_TEST

START_TEST(pointer_absolute_initial_state)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *libinput1, *libinput2;
	struct libinput_event *ev1, *ev2;
	struct libinput_event_pointer *p1, *p2;
	int axis = _i; /* looped test */

	libinput1 = dev->libinput;
	litest_touch_down(dev, 0, 40, 60);
	litest_touch_up(dev, 0);

	/* device is now on some x/y value */
	litest_drain_events(libinput1);

	libinput2 = litest_create_context();
	libinput_path_add_device(libinput2,
				 libevdev_uinput_get_devnode(dev->uinput));
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

		ck_assert_int_eq(libinput_event_get_type(ev1),
				 LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE);
		ck_assert_int_eq(libinput_event_get_type(ev1),
				 libinput_event_get_type(ev2));

		p1 = libinput_event_get_pointer_event(ev1);
		p2 = libinput_event_get_pointer_event(ev2);

		ck_assert_int_eq(libinput_event_pointer_get_absolute_x(p1),
				 libinput_event_pointer_get_absolute_x(p2));
		ck_assert_int_eq(libinput_event_pointer_get_absolute_y(p1),
				 libinput_event_pointer_get_absolute_y(p2));

		libinput_event_destroy(ev1);
		libinput_event_destroy(ev2);
	}

	libinput_unref(libinput2);
}
END_TEST

static void
test_unaccel_event(struct litest_device *dev, int dx, int dy)
{
      struct libinput *li = dev->libinput;
      struct libinput_event *event;
      struct libinput_event_pointer *ptrev;
      double ev_dx, ev_dy;

      litest_event(dev, EV_REL, REL_X, dx);
      litest_event(dev, EV_REL, REL_Y, dy);
      litest_event(dev, EV_SYN, SYN_REPORT, 0);

      libinput_dispatch(li);

      event = libinput_get_event(li);
      ptrev = litest_is_motion_event(event);

      ev_dx = libinput_event_pointer_get_dx_unaccelerated(ptrev);
      ev_dy = libinput_event_pointer_get_dy_unaccelerated(ptrev);

      litest_assert_int_eq(dx, ev_dx);
      litest_assert_int_eq(dy, ev_dy);

      libinput_event_destroy(event);

      litest_drain_events(dev->libinput);
}

START_TEST(pointer_motion_unaccel)
{
      struct litest_device *dev = litest_current_device();

      litest_drain_events(dev->libinput);

      test_unaccel_event(dev, 10, 0);
      test_unaccel_event(dev, 10, 10);
      test_unaccel_event(dev, 10, -10);
      test_unaccel_event(dev, 0, 10);

      test_unaccel_event(dev, -10, 0);
      test_unaccel_event(dev, -10, 10);
      test_unaccel_event(dev, -10, -10);
      test_unaccel_event(dev, 0, -10);
}
END_TEST

static void
test_button_event(struct litest_device *dev, unsigned int button, int state)
{
	struct libinput *li = dev->libinput;

	litest_button_click_debounced(dev, li, button, state);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li, button,
				   state ?  LIBINPUT_BUTTON_STATE_PRESSED :
					   LIBINPUT_BUTTON_STATE_RELEASED);
}

START_TEST(pointer_button)
{
	struct litest_device *dev = litest_current_device();

	disable_button_scrolling(dev);

	litest_drain_events(dev->libinput);

	test_button_event(dev, BTN_LEFT, 1);
	test_button_event(dev, BTN_LEFT, 0);

	/* press it twice for good measure */
	test_button_event(dev, BTN_LEFT, 1);
	test_button_event(dev, BTN_LEFT, 0);

	if (libinput_device_pointer_has_button(dev->libinput_device,
					       BTN_RIGHT)) {
		test_button_event(dev, BTN_RIGHT, 1);
		test_button_event(dev, BTN_RIGHT, 0);
	}

	/* Skip middle button test on trackpoints (used for scrolling) */
	if (libinput_device_pointer_has_button(dev->libinput_device,
					       BTN_MIDDLE)) {
		test_button_event(dev, BTN_MIDDLE, 1);
		test_button_event(dev, BTN_MIDDLE, 0);
	}
}
END_TEST

START_TEST(pointer_button_auto_release)
{
	struct libinput *libinput;
	struct litest_device *dev;
	struct libinput_event *event;
	enum libinput_event_type type;
	struct libinput_event_pointer *pevent;
	struct {
		int code;
		int released;
	} buttons[] = {
		{ .code = BTN_LEFT, },
		{ .code = BTN_MIDDLE, },
		{ .code = BTN_EXTRA, },
		{ .code = BTN_SIDE, },
		{ .code = BTN_BACK, },
		{ .code = BTN_FORWARD, },
		{ .code = BTN_4, },
	};
	int events[2 * (ARRAY_LENGTH(buttons) + 1)];
	unsigned i;
	int button;
	int valid_code;

	/* Enable all tested buttons on the device */
	for (i = 0; i < 2 * ARRAY_LENGTH(buttons);) {
		button = buttons[i / 2].code;
		events[i++] = EV_KEY;
		events[i++] = button;
	}
	events[i++] = -1;
	events[i++] = -1;

	libinput = litest_create_context();
	dev = litest_add_device_with_overrides(libinput,
					       LITEST_MOUSE,
					       "Generic mouse",
					       NULL, NULL, events);

	litest_drain_events(libinput);

	/* Send pressed events, without releasing */
	for (i = 0; i < ARRAY_LENGTH(buttons); ++i) {
		test_button_event(dev, buttons[i].code, 1);
	}

	litest_drain_events(libinput);

	/* "Disconnect" device */
	litest_delete_device(dev);

	/* Mark all released buttons until device is removed */
	while (1) {
		event = libinput_get_event(libinput);
		ck_assert_notnull(event);
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_DEVICE_REMOVED) {
			libinput_event_destroy(event);
			break;
		}

		ck_assert_int_eq(type, LIBINPUT_EVENT_POINTER_BUTTON);
		pevent = libinput_event_get_pointer_event(event);
		ck_assert_int_eq(libinput_event_pointer_get_button_state(pevent),
				 LIBINPUT_BUTTON_STATE_RELEASED);
		button = libinput_event_pointer_get_button(pevent);

		valid_code = 0;
		for (i = 0; i < ARRAY_LENGTH(buttons); ++i) {
			if (buttons[i].code == button) {
				ck_assert_int_eq(buttons[i].released, 0);
				buttons[i].released = 1;
				valid_code = 1;
			}
		}
		ck_assert_int_eq(valid_code, 1);
		libinput_event_destroy(event);
	}

	/* Check that all pressed buttons has been released. */
	for (i = 0; i < ARRAY_LENGTH(buttons); ++i) {
		ck_assert_int_eq(buttons[i].released, 1);
	}

	libinput_unref(libinput);
}
END_TEST

START_TEST(pointer_button_has_no_button)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	unsigned int code;

	ck_assert(!libinput_device_has_capability(device,
					  LIBINPUT_DEVICE_CAP_POINTER));

	for (code = BTN_LEFT; code < KEY_OK; code++)
		ck_assert_int_eq(-1,
			 libinput_device_pointer_has_button(device, code));
}
END_TEST

START_TEST(pointer_recover_from_lost_button_count)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libevdev *evdev = dev->evdev;

	disable_button_scrolling(dev);

	litest_drain_events(dev->libinput);

	litest_button_click_debounced(dev, li, BTN_LEFT, 1);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	/* Grab for the release to make libinput lose count */
	libevdev_grab(evdev, LIBEVDEV_GRAB);
	litest_button_click_debounced(dev, li, BTN_LEFT, 0);
	libevdev_grab(evdev, LIBEVDEV_UNGRAB);

	litest_assert_empty_queue(li);

	litest_button_click_debounced(dev, li, BTN_LEFT, 1);
	litest_assert_empty_queue(li);

	litest_button_click_debounced(dev, li, BTN_LEFT, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

static inline double
wheel_click_count(struct litest_device *dev, int which)
{
	struct udev_device *d;
	const char *prop = NULL;
	int count;
	double angle = 0.0;

	d = libinput_device_get_udev_device(dev->libinput_device);
	litest_assert_ptr_notnull(d);

	if (which == REL_HWHEEL)
		prop = udev_device_get_property_value(d, "MOUSE_WHEEL_CLICK_COUNT_HORIZONTAL");
	if (!prop)
		prop = udev_device_get_property_value(d, "MOUSE_WHEEL_CLICK_COUNT");
	if (!prop)
		goto out;

	count = parse_mouse_wheel_click_count_property(prop);
	litest_assert_int_ne(count, 0);
	angle = 360.0/count;

out:
	udev_device_unref(d);
	return angle;
}

static inline double
wheel_click_angle(struct litest_device *dev, int which)
{
	struct udev_device *d;
	const char *prop = NULL;
	const int default_angle = 15;
	double angle;

	angle = wheel_click_count(dev, which);
	if (angle != 0.0)
		return angle;

	angle = default_angle;
	d = libinput_device_get_udev_device(dev->libinput_device);
	litest_assert_ptr_notnull(d);

	if (which == REL_HWHEEL)
		prop = udev_device_get_property_value(d, "MOUSE_WHEEL_CLICK_ANGLE_HORIZONTAL");
	if (!prop)
		prop = udev_device_get_property_value(d, "MOUSE_WHEEL_CLICK_ANGLE");
	if (!prop)
		goto out;

	angle = parse_mouse_wheel_click_angle_property(prop);
	if (angle == 0.0)
		angle = default_angle;

out:
	udev_device_unref(d);
	return angle;
}

static enum libinput_pointer_axis_source
wheel_source(struct litest_device *dev, int which)
{
	struct udev_device *d;
	bool is_tilt = false;

	d = libinput_device_get_udev_device(dev->libinput_device);
	litest_assert_ptr_notnull(d);

	switch(which) {
	case REL_WHEEL:
		is_tilt = !!udev_device_get_property_value(d, "MOUSE_WHEEL_TILT_VERTICAL");
		break;
	case REL_HWHEEL:
		is_tilt = !!udev_device_get_property_value(d, "MOUSE_WHEEL_TILT_HORIZONTAL");
		break;
	default:
		litest_abort_msg("Invalid source axis %d\n", which);
		break;
	}

	udev_device_unref(d);
	return is_tilt ?
		LIBINPUT_POINTER_AXIS_SOURCE_WHEEL_TILT :
		LIBINPUT_POINTER_AXIS_SOURCE_WHEEL;
}

static void
test_wheel_event(struct litest_device *dev, int which, int amount)
{
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	enum libinput_pointer_axis axis;
	enum libinput_pointer_axis_source source;

	double scroll_step, expected, discrete;

	scroll_step = wheel_click_angle(dev, which);
	source = wheel_source(dev, which);
	expected = amount * scroll_step;
	discrete = amount;

	if (libinput_device_config_scroll_get_natural_scroll_enabled(dev->libinput_device)) {
		expected *= -1;
		discrete *= -1;
	}

	/* mouse scroll wheels are 'upside down' */
	if (which == REL_WHEEL)
		amount *= -1;
	litest_event(dev, EV_REL, which, amount);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	axis = (which == REL_WHEEL) ?
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL :
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL;

	event = libinput_get_event(li);
	ptrev = litest_is_axis_event(event, axis, source);

	litest_assert_double_eq(
			libinput_event_pointer_get_axis_value(ptrev, axis),
			expected);
	litest_assert_double_eq(
			libinput_event_pointer_get_axis_value_discrete(ptrev, axis),
			discrete);
	libinput_event_destroy(event);
}

START_TEST(pointer_scroll_wheel)
{
	struct litest_device *dev = litest_current_device();

	litest_drain_events(dev->libinput);

	/* make sure we hit at least one of the below two conditions */
	ck_assert(libevdev_has_event_code(dev->evdev, EV_REL, REL_WHEEL) ||
		  libevdev_has_event_code(dev->evdev, EV_REL, REL_HWHEEL));

	if (libevdev_has_event_code(dev->evdev, EV_REL, REL_WHEEL)) {
		test_wheel_event(dev, REL_WHEEL, -1);
		test_wheel_event(dev, REL_WHEEL, 1);

		test_wheel_event(dev, REL_WHEEL, -5);
		test_wheel_event(dev, REL_WHEEL, 6);
	}

	if (libevdev_has_event_code(dev->evdev, EV_REL, REL_HWHEEL)) {
		test_wheel_event(dev, REL_HWHEEL, -1);
		test_wheel_event(dev, REL_HWHEEL, 1);

		test_wheel_event(dev, REL_HWHEEL, -5);
		test_wheel_event(dev, REL_HWHEEL, 6);
	}
}
END_TEST

START_TEST(pointer_scroll_natural_defaults)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_ge(libinput_device_config_scroll_has_natural_scroll(dev->libinput_device), 1);
	ck_assert_int_eq(libinput_device_config_scroll_get_natural_scroll_enabled(dev->libinput_device), 0);
	ck_assert_int_eq(libinput_device_config_scroll_get_default_natural_scroll_enabled(dev->libinput_device), 0);
}
END_TEST

START_TEST(pointer_scroll_natural_defaults_noscroll)
{
	struct litest_device *dev = litest_current_device();

	if (libinput_device_config_scroll_has_natural_scroll(dev->libinput_device))
		return;

	ck_assert_int_eq(libinput_device_config_scroll_get_natural_scroll_enabled(dev->libinput_device), 0);
	ck_assert_int_eq(libinput_device_config_scroll_get_default_natural_scroll_enabled(dev->libinput_device), 0);
}
END_TEST

START_TEST(pointer_scroll_natural_enable_config)
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

START_TEST(pointer_scroll_natural_wheel)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;

	litest_drain_events(dev->libinput);

	libinput_device_config_scroll_set_natural_scroll_enabled(device, 1);

	/* make sure we hit at least one of the below two conditions */
	ck_assert(libevdev_has_event_code(dev->evdev, EV_REL, REL_WHEEL) ||
		  libevdev_has_event_code(dev->evdev, EV_REL, REL_HWHEEL));

	if (libevdev_has_event_code(dev->evdev, EV_REL, REL_WHEEL)) {
		test_wheel_event(dev, REL_WHEEL, -1);
		test_wheel_event(dev, REL_WHEEL, 1);

		test_wheel_event(dev, REL_WHEEL, -5);
		test_wheel_event(dev, REL_WHEEL, 6);
	}

	if (libevdev_has_event_code(dev->evdev, EV_REL, REL_HWHEEL)) {
		test_wheel_event(dev, REL_HWHEEL, -1);
		test_wheel_event(dev, REL_HWHEEL, 1);

		test_wheel_event(dev, REL_HWHEEL, -5);
		test_wheel_event(dev, REL_HWHEEL, 6);
	}
}
END_TEST

START_TEST(pointer_scroll_has_axis_invalid)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *pev;

	litest_drain_events(dev->libinput);

	if (!libevdev_has_event_code(dev->evdev, EV_REL, REL_WHEEL))
		return;

	litest_event(dev, EV_REL, REL_WHEEL, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	pev = litest_is_axis_event(event,
				   LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
				   0);

	ck_assert_int_eq(libinput_event_pointer_has_axis(pev, -1), 0);
	ck_assert_int_eq(libinput_event_pointer_has_axis(pev, 2), 0);
	ck_assert_int_eq(libinput_event_pointer_has_axis(pev, 3), 0);
	ck_assert_int_eq(libinput_event_pointer_has_axis(pev, 0xffff), 0);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(pointer_seat_button_count)
{
	const int num_devices = 4;
	struct litest_device *devices[num_devices];
	struct libinput *libinput;
	struct libinput_event *ev;
	struct libinput_event_pointer *tev;
	int i;
	int seat_button_count = 0;
	int expected_seat_button_count = 0;
	char device_name[255];

	libinput = litest_create_context();
	for (i = 0; i < num_devices; ++i) {
		sprintf(device_name, "litest Generic mouse (%d)", i);
		devices[i] = litest_add_device_with_overrides(libinput,
							      LITEST_MOUSE,
							      device_name,
							      NULL, NULL, NULL);
	}

	for (i = 0; i < num_devices; ++i)
		litest_button_click_debounced(devices[i],
					      libinput,
					      BTN_LEFT,
					      true);

	libinput_dispatch(libinput);
	while ((ev = libinput_get_event(libinput))) {
		if (libinput_event_get_type(ev) !=
		    LIBINPUT_EVENT_POINTER_BUTTON) {
			libinput_event_destroy(ev);
			libinput_dispatch(libinput);
			continue;
		}

		tev = libinput_event_get_pointer_event(ev);
		ck_assert_notnull(tev);
		ck_assert_int_eq(libinput_event_pointer_get_button(tev),
				 BTN_LEFT);
		ck_assert_int_eq(libinput_event_pointer_get_button_state(tev),
				 LIBINPUT_BUTTON_STATE_PRESSED);

		++expected_seat_button_count;
		seat_button_count =
			libinput_event_pointer_get_seat_button_count(tev);
		ck_assert_int_eq(expected_seat_button_count, seat_button_count);

		libinput_event_destroy(ev);
		libinput_dispatch(libinput);
	}

	ck_assert_int_eq(seat_button_count, num_devices);

	for (i = 0; i < num_devices; ++i)
		litest_button_click_debounced(devices[i],
					      libinput,
					      BTN_LEFT,
					      false);

	libinput_dispatch(libinput);
	while ((ev = libinput_get_event(libinput))) {
		if (libinput_event_get_type(ev) !=
		    LIBINPUT_EVENT_POINTER_BUTTON) {
			libinput_event_destroy(ev);
			libinput_dispatch(libinput);
			continue;
		}

		tev = libinput_event_get_pointer_event(ev);
		ck_assert_notnull(tev);
		ck_assert_int_eq(libinput_event_pointer_get_button(tev),
				 BTN_LEFT);
		ck_assert_int_eq(libinput_event_pointer_get_button_state(tev),
				 LIBINPUT_BUTTON_STATE_RELEASED);

		--expected_seat_button_count;
		seat_button_count =
			libinput_event_pointer_get_seat_button_count(tev);
		ck_assert_int_eq(expected_seat_button_count, seat_button_count);

		libinput_event_destroy(ev);
		libinput_dispatch(libinput);
	}

	ck_assert_int_eq(seat_button_count, 0);

	for (i = 0; i < num_devices; ++i)
		litest_delete_device(devices[i]);
	libinput_unref(libinput);
}
END_TEST

START_TEST(pointer_no_calibration)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	enum libinput_config_status status;
	int rc;
	float calibration[6] = {0};

	rc = libinput_device_config_calibration_has_matrix(d);
	ck_assert_int_eq(rc, 0);
	rc = libinput_device_config_calibration_get_matrix(d, calibration);
	ck_assert_int_eq(rc, 0);
	rc = libinput_device_config_calibration_get_default_matrix(d,
								   calibration);
	ck_assert_int_eq(rc, 0);

	status = libinput_device_config_calibration_set_matrix(d,
							       calibration);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
}
END_TEST

START_TEST(pointer_left_handed_defaults)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	int rc;

	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_APPLE &&
	    libevdev_get_id_product(dev->evdev) == PRODUCT_ID_APPLE_APPLETOUCH)
		return;

	rc = libinput_device_config_left_handed_is_available(d);
	ck_assert_int_ne(rc, 0);

	rc = libinput_device_config_left_handed_get(d);
	ck_assert_int_eq(rc, 0);

	rc = libinput_device_config_left_handed_get_default(d);
	ck_assert_int_eq(rc, 0);
}
END_TEST

START_TEST(pointer_left_handed)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, 1);
	litest_button_click_debounced(dev, li, BTN_LEFT, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_button_click_debounced(dev, li, BTN_RIGHT, 1);
	litest_button_click_debounced(dev, li, BTN_RIGHT, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	if (libinput_device_pointer_has_button(d, BTN_MIDDLE)) {
		litest_button_click_debounced(dev, li, BTN_MIDDLE, 1);
		litest_button_click_debounced(dev, li, BTN_MIDDLE, 0);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);
	}
}
END_TEST

START_TEST(pointer_left_handed_during_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	litest_drain_events(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, 1);
	libinput_dispatch(li);

	/* Change while button is down, expect correct release event */
	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_button_click_debounced(dev, li, BTN_LEFT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(pointer_left_handed_during_click_multiple_buttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	if (!libinput_device_pointer_has_button(d, BTN_MIDDLE))
		return;

	litest_disable_middleemu(dev);

	litest_drain_events(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, 1);
	libinput_dispatch(li);

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* No left-handed until all buttons were down */
	litest_button_click_debounced(dev, li, BTN_RIGHT, 1);
	litest_button_click_debounced(dev, li, BTN_RIGHT, 0);
	litest_button_click_debounced(dev, li, BTN_LEFT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(pointer_scroll_button)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	/* Make left button switch to scrolling mode */
	libinput_device_config_scroll_set_method(dev->libinput_device,
					LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	libinput_device_config_scroll_set_button(dev->libinput_device,
					BTN_LEFT);

	litest_drain_events(li);

	litest_button_scroll(dev, BTN_LEFT, 1, 6);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 6);
	litest_button_scroll(dev, BTN_LEFT, 1, -7);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -7);
	litest_button_scroll(dev, BTN_LEFT, 8, 1);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 8);
	litest_button_scroll(dev, BTN_LEFT, -9, 1);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -9);

	/* scroll smaller than the threshold should not generate axis events */
	litest_button_scroll(dev, BTN_LEFT, 1, 1);

	litest_button_scroll(dev, BTN_LEFT, 0, 0);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);

	/* Restore default scroll behavior */
	libinput_device_config_scroll_set_method(dev->libinput_device,
		libinput_device_config_scroll_get_default_method(
			dev->libinput_device));
	libinput_device_config_scroll_set_button(dev->libinput_device,
		libinput_device_config_scroll_get_default_button(
			dev->libinput_device));
}
END_TEST

START_TEST(pointer_scroll_button_noscroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	uint32_t methods, button;
	enum libinput_config_status status;

	methods = libinput_device_config_scroll_get_method(device);
	ck_assert_int_eq((methods & LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN), 0);
	button = libinput_device_config_scroll_get_button(device);
	ck_assert_int_eq(button, 0);
	button = libinput_device_config_scroll_get_default_button(device);
	ck_assert_int_eq(button, 0);

	status = libinput_device_config_scroll_set_method(device,
					LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = libinput_device_config_scroll_set_button(device, BTN_LEFT);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
}
END_TEST

START_TEST(pointer_scroll_button_no_event_before_timeout)
{
	struct litest_device *device = litest_current_device();
	struct libinput *li = device->libinput;
	int i;

	if (!libinput_device_pointer_has_button(device->libinput_device,
						BTN_MIDDLE))
		return;

	litest_disable_middleemu(device);
	disable_button_scrolling(device);

	libinput_device_config_scroll_set_method(device->libinput_device,
					LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	libinput_device_config_scroll_set_button(device->libinput_device,
						 BTN_LEFT);
	litest_drain_events(li);

	litest_button_click_debounced(device, li, BTN_LEFT, true);
	litest_assert_empty_queue(li);

	for (i = 0; i < 10; i++) {
		litest_event(device, EV_REL, REL_Y, 1);
		litest_event(device, EV_SYN, SYN_REPORT, 0);
	}
	litest_assert_empty_queue(li);

	litest_timeout_buttonscroll();
	libinput_dispatch(li);
	litest_button_click_debounced(device, li, BTN_LEFT, false);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(pointer_scroll_button_middle_emulation)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;
	int i;

	status = libinput_device_config_middle_emulation_set_enabled(device,
				LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);

	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	status = libinput_device_config_scroll_set_method(device,
				 LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_scroll_set_button(device, BTN_MIDDLE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_button_click_debounced(dev, li, BTN_LEFT, 1);
	litest_button_click_debounced(dev, li, BTN_RIGHT, 1);
	libinput_dispatch(li);
	litest_timeout_buttonscroll();
	libinput_dispatch(li);

	for (i = 0; i < 10; i++) {
		litest_event(dev, EV_REL, REL_Y, -1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	libinput_dispatch(li);

	litest_button_click_debounced(dev, li, BTN_LEFT, 0);
	litest_button_click_debounced(dev, li, BTN_RIGHT, 0);
	libinput_dispatch(li);

	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -1);
	litest_assert_empty_queue(li);

	/* Restore default scroll behavior */
	libinput_device_config_scroll_set_method(dev->libinput_device,
		libinput_device_config_scroll_get_default_method(
			dev->libinput_device));
	libinput_device_config_scroll_set_button(dev->libinput_device,
		libinput_device_config_scroll_get_default_button(
			dev->libinput_device));
}
END_TEST

START_TEST(pointer_scroll_nowheel_defaults)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_scroll_method method, expected;
	uint32_t button;

	/* button scrolling is only enabled if there is a
	   middle button present */
	if (libinput_device_pointer_has_button(device, BTN_MIDDLE))
		expected = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
	else
		expected = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;

	method = libinput_device_config_scroll_get_method(device);
	ck_assert_int_eq(method, expected);

	method = libinput_device_config_scroll_get_default_method(device);
	ck_assert_int_eq(method, expected);

	if (method == LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN) {
		button = libinput_device_config_scroll_get_button(device);
		ck_assert_int_eq(button, BTN_MIDDLE);
		button = libinput_device_config_scroll_get_default_button(device);
		ck_assert_int_eq(button, BTN_MIDDLE);
	}
}
END_TEST

START_TEST(pointer_scroll_defaults_logitech_marble)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_scroll_method method;
	uint32_t button;

	method = libinput_device_config_scroll_get_method(device);
	ck_assert_int_eq(method, LIBINPUT_CONFIG_SCROLL_NO_SCROLL);
	method = libinput_device_config_scroll_get_default_method(device);
	ck_assert_int_eq(method, LIBINPUT_CONFIG_SCROLL_NO_SCROLL);

	button = libinput_device_config_scroll_get_button(device);
	ck_assert_int_eq(button, BTN_SIDE);
}
END_TEST

START_TEST(pointer_accel_defaults)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	double speed;

	ck_assert(libinput_device_config_accel_is_available(device));
	ck_assert_double_eq(libinput_device_config_accel_get_default_speed(device),
			    0.0);
	ck_assert_double_eq(libinput_device_config_accel_get_speed(device),
			    0.0);

	for (speed = -2.0; speed < -1.0; speed += 0.2) {
		status = libinput_device_config_accel_set_speed(device,
								speed);
		ck_assert_int_eq(status,
				 LIBINPUT_CONFIG_STATUS_INVALID);
		ck_assert_double_eq(libinput_device_config_accel_get_speed(device),
				    0.0);
	}

	for (speed = -1.0; speed <= 1.0; speed += 0.2) {
		status = libinput_device_config_accel_set_speed(device,
								speed);
		ck_assert_int_eq(status,
				 LIBINPUT_CONFIG_STATUS_SUCCESS);
		ck_assert_double_eq(libinput_device_config_accel_get_speed(device),
				    speed);
	}

	for (speed = 1.2; speed <= 2.0; speed += 0.2) {
		status = libinput_device_config_accel_set_speed(device,
								speed);
		ck_assert_int_eq(status,
				 LIBINPUT_CONFIG_STATUS_INVALID);
		ck_assert_double_eq(libinput_device_config_accel_get_speed(device),
				    1.0);
	}

}
END_TEST

START_TEST(pointer_accel_invalid)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;

	ck_assert(libinput_device_config_accel_is_available(device));

	status = libinput_device_config_accel_set_speed(device,
							NAN);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
	status = libinput_device_config_accel_set_speed(device,
							INFINITY);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

START_TEST(pointer_accel_defaults_absolute)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	double speed;

	ck_assert(!libinput_device_config_accel_is_available(device));
	ck_assert_double_eq(libinput_device_config_accel_get_default_speed(device),
			    0.0);
	ck_assert_double_eq(libinput_device_config_accel_get_speed(device),
			    0.0);

	for (speed = -2.0; speed <= 2.0; speed += 0.2) {
		status = libinput_device_config_accel_set_speed(device,
								speed);
		if (speed >= -1.0 && speed <= 1.0)
			ck_assert_int_eq(status,
					 LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
		else
			ck_assert_int_eq(status,
					 LIBINPUT_CONFIG_STATUS_INVALID);
		ck_assert_double_eq(libinput_device_config_accel_get_speed(device),
				    0.0);
	}
}
END_TEST

START_TEST(pointer_accel_defaults_absolute_relative)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;

	ck_assert(libinput_device_config_accel_is_available(device));
	ck_assert_double_eq(libinput_device_config_accel_get_default_speed(device),
			    0.0);
	ck_assert_double_eq(libinput_device_config_accel_get_speed(device),
			    0.0);
}
END_TEST

START_TEST(pointer_accel_direction_change)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *pev;
	int i;
	double delta;

	litest_drain_events(li);

	for (i = 0; i < 10; i++) {
		litest_event(dev, EV_REL, REL_X, -1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}
	litest_event(dev, EV_REL, REL_X, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	do {
		pev = libinput_event_get_pointer_event(event);

		delta = libinput_event_pointer_get_dx(pev);
		ck_assert_double_le(delta, 0.0);
		libinput_event_destroy(event);
		event = libinput_get_event(li);
	} while (libinput_next_event_type(li) != LIBINPUT_EVENT_NONE);

	pev = libinput_event_get_pointer_event(event);
	delta = libinput_event_pointer_get_dx(pev);
	ck_assert_double_gt(delta, 0.0);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(pointer_accel_profile_defaults)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	enum libinput_config_accel_profile profile;
	uint32_t profiles;

	ck_assert(libinput_device_config_accel_is_available(device));

	profile = libinput_device_config_accel_get_default_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);

	profile = libinput_device_config_accel_get_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);

	profiles = libinput_device_config_accel_get_profiles(device);
	ck_assert(profiles & LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
	ck_assert(profiles & LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);

	status = libinput_device_config_accel_set_profile(device,
							  LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	profile = libinput_device_config_accel_get_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);

	profile = libinput_device_config_accel_get_default_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);

	status = libinput_device_config_accel_set_profile(device,
							  LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	profile = libinput_device_config_accel_get_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
}
END_TEST

START_TEST(pointer_accel_profile_defaults_noprofile)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	enum libinput_config_accel_profile profile;
	uint32_t profiles;

	ck_assert(libinput_device_config_accel_is_available(device));

	profile = libinput_device_config_accel_get_default_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);

	profile = libinput_device_config_accel_get_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);

	profiles = libinput_device_config_accel_get_profiles(device);
	ck_assert_int_eq(profiles, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);

	status = libinput_device_config_accel_set_profile(device,
							  LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	profile = libinput_device_config_accel_get_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);

	status = libinput_device_config_accel_set_profile(device,
							  LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	profile = libinput_device_config_accel_get_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);
}
END_TEST

START_TEST(pointer_accel_profile_invalid)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;

	ck_assert(libinput_device_config_accel_is_available(device));

	status = libinput_device_config_accel_set_profile(device,
					   LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);

	status = libinput_device_config_accel_set_profile(device,
					   LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE + 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);

	status = libinput_device_config_accel_set_profile(device,
			   LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE |LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

START_TEST(pointer_accel_profile_noaccel)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	enum libinput_config_accel_profile profile;

	ck_assert(!libinput_device_config_accel_is_available(device));

	profile = libinput_device_config_accel_get_default_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);

	profile = libinput_device_config_accel_get_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);

	status = libinput_device_config_accel_set_profile(device,
					   LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);

	status = libinput_device_config_accel_set_profile(device,
					   LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE + 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);

	status = libinput_device_config_accel_set_profile(device,
			   LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE |LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

START_TEST(pointer_accel_profile_flat_motion_relative)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;

	libinput_device_config_accel_set_profile(device,
						 LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
	litest_drain_events(dev->libinput);

	test_relative_event(dev, 1, 0);
	test_relative_event(dev, 1, 1);
	test_relative_event(dev, 1, -1);
	test_relative_event(dev, 0, 1);

	test_relative_event(dev, -1, 0);
	test_relative_event(dev, -1, 1);
	test_relative_event(dev, -1, -1);
	test_relative_event(dev, 0, -1);
}
END_TEST

START_TEST(middlebutton)
{
	struct litest_device *device = litest_current_device();
	struct libinput *li = device->libinput;
	enum libinput_config_status status;
	unsigned int i;
	const int btn[][4] = {
		{ BTN_LEFT, BTN_RIGHT, BTN_LEFT, BTN_RIGHT },
		{ BTN_LEFT, BTN_RIGHT, BTN_RIGHT, BTN_LEFT },
		{ BTN_RIGHT, BTN_LEFT, BTN_LEFT, BTN_RIGHT },
		{ BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_LEFT },
	};

	disable_button_scrolling(device);

	status = libinput_device_config_middle_emulation_set_enabled(
					    device->libinput_device,
					    LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	litest_drain_events(li);

	for (i = 0; i < ARRAY_LENGTH(btn); i++) {
		litest_button_click_debounced(device, li, btn[i][0], true);
		litest_button_click_debounced(device, li, btn[i][1], true);

		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_empty_queue(li);

		litest_button_click_debounced(device, li, btn[i][2], false);
		litest_button_click_debounced(device, li, btn[i][3], false);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_assert_empty_queue(li);
	}
}
END_TEST

START_TEST(middlebutton_nostart_while_down)
{
	struct litest_device *device = litest_current_device();
	struct libinput *li = device->libinput;
	enum libinput_config_status status;
	unsigned int i;
	const int btn[][4] = {
		{ BTN_LEFT, BTN_RIGHT, BTN_LEFT, BTN_RIGHT },
		{ BTN_LEFT, BTN_RIGHT, BTN_RIGHT, BTN_LEFT },
		{ BTN_RIGHT, BTN_LEFT, BTN_LEFT, BTN_RIGHT },
		{ BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_LEFT },
	};

	if (!libinput_device_pointer_has_button(device->libinput_device,
						BTN_MIDDLE))
		return;

	disable_button_scrolling(device);

	status = libinput_device_config_middle_emulation_set_enabled(
					    device->libinput_device,
					    LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	litest_button_click_debounced(device, li, BTN_MIDDLE, true);
	litest_drain_events(li);

	for (i = 0; i < ARRAY_LENGTH(btn); i++) {
		litest_button_click_debounced(device, li, btn[i][0], true);
		litest_assert_button_event(li,
					   btn[i][0],
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_button_click_debounced(device, li, btn[i][1], true);
		litest_assert_button_event(li,
					   btn[i][1],
					   LIBINPUT_BUTTON_STATE_PRESSED);

		litest_assert_empty_queue(li);

		litest_button_click_debounced(device, li, btn[i][2], false);
		litest_assert_button_event(li,
					   btn[i][2],
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_button_click_debounced(device, li, btn[i][3], false);
		litest_assert_button_event(li,
					   btn[i][3],
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_assert_empty_queue(li);
	}

	litest_button_click_debounced(device, li, BTN_MIDDLE, false);
	litest_drain_events(li);
}
END_TEST

START_TEST(middlebutton_timeout)
{
	struct litest_device *device = litest_current_device();
	struct libinput *li = device->libinput;
	enum libinput_config_status status;
	unsigned int button;

	disable_button_scrolling(device);

	status = libinput_device_config_middle_emulation_set_enabled(
					    device->libinput_device,
					    LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	for (button = BTN_LEFT; button <= BTN_RIGHT; button++) {
		litest_drain_events(li);
		litest_button_click_debounced(device, li, button, true);
		litest_assert_empty_queue(li);
		litest_timeout_middlebutton();

		litest_assert_button_event(li,
					   button,
					   LIBINPUT_BUTTON_STATE_PRESSED);

		litest_button_click_debounced(device, li, button, false);
		litest_assert_button_event(li,
					   button,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_assert_empty_queue(li);
	}
}
END_TEST

START_TEST(middlebutton_doubleclick)
{
	struct litest_device *device = litest_current_device();
	struct libinput *li = device->libinput;
	enum libinput_config_status status;
	unsigned int i;
	const int btn[][4] = {
		{ BTN_LEFT, BTN_RIGHT, BTN_LEFT, BTN_RIGHT },
		{ BTN_LEFT, BTN_RIGHT, BTN_RIGHT, BTN_LEFT },
		{ BTN_RIGHT, BTN_LEFT, BTN_LEFT, BTN_RIGHT },
		{ BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_LEFT },
	};

	disable_button_scrolling(device);

	status = libinput_device_config_middle_emulation_set_enabled(
				    device->libinput_device,
				    LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	litest_drain_events(li);

	for (i = 0; i < ARRAY_LENGTH(btn); i++) {
		litest_button_click_debounced(device, li, btn[i][0], true);
		litest_button_click_debounced(device, li, btn[i][1], true);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_empty_queue(li);

		litest_button_click_debounced(device, li, btn[i][2], false);
		litest_button_click_debounced(device, li, btn[i][2], true);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_button_click_debounced(device, li, btn[i][3], false);

		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_assert_empty_queue(li);
	}
}
END_TEST

START_TEST(middlebutton_middleclick)
{
	struct litest_device *device = litest_current_device();
	struct libinput *li = device->libinput;
	enum libinput_config_status status;
	unsigned int button;

	disable_button_scrolling(device);

	if (!libinput_device_pointer_has_button(device->libinput_device,
					       BTN_MIDDLE))
		return;

	status = libinput_device_config_middle_emulation_set_enabled(
					    device->libinput_device,
					    LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	/* one button down, then middle -> release buttons */
	for (button = BTN_LEFT; button <= BTN_RIGHT; button++) {
		/* release button before middle */
		litest_drain_events(li);
		litest_button_click_debounced(device, li, button, true);
		litest_button_click_debounced(device, li, BTN_MIDDLE, true);
		litest_assert_button_event(li,
					   button,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_empty_queue(li);
		litest_button_click_debounced(device, li, button, false);
		litest_assert_button_event(li,
					   button,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_button_click_debounced(device, li, BTN_MIDDLE, false);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_assert_empty_queue(li);

		/* release middle before button */
		litest_button_click_debounced(device, li, button, true);
		litest_button_click_debounced(device, li, BTN_MIDDLE, true);
		litest_assert_button_event(li,
					   button,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_empty_queue(li);
		litest_button_click_debounced(device, li, BTN_MIDDLE, false);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_button_click_debounced(device, li, button, false);
		litest_assert_button_event(li,
					   button,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_assert_empty_queue(li);
	}
}
END_TEST

START_TEST(middlebutton_middleclick_during)
{
	struct litest_device *device = litest_current_device();
	struct libinput *li = device->libinput;
	enum libinput_config_status status;
	unsigned int button;

	disable_button_scrolling(device);

	if (!libinput_device_pointer_has_button(device->libinput_device,
						BTN_MIDDLE))
		return;

	status = libinput_device_config_middle_emulation_set_enabled(
					    device->libinput_device,
					    LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	litest_drain_events(li);

	/* trigger emulation, then real middle */
	for (button = BTN_LEFT; button <= BTN_RIGHT; button++) {
		litest_button_click_debounced(device, li, BTN_LEFT, true);
		litest_button_click_debounced(device, li, BTN_RIGHT, true);

		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);

		litest_button_click_debounced(device, li, BTN_MIDDLE, true);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);

		litest_assert_empty_queue(li);

		/* middle still down, release left/right */
		litest_button_click_debounced(device, li, button, false);
		litest_assert_empty_queue(li);
		litest_button_click_debounced(device, li, button, true);
		litest_assert_button_event(li,
					   button,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_empty_queue(li);

		/* release both */
		litest_button_click_debounced(device, li, BTN_LEFT, false);
		litest_button_click_debounced(device, li, BTN_RIGHT, false);
		litest_assert_button_event(li,
					   button,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_assert_empty_queue(li);

		litest_button_click_debounced(device, li, BTN_MIDDLE, false);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);
		litest_assert_empty_queue(li);
	}
}
END_TEST

START_TEST(middlebutton_default_enabled)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	int available;
	enum libinput_config_middle_emulation_state state;

	if (!libinput_device_pointer_has_button(dev->libinput_device,
						BTN_MIDDLE))
		return;

	available = libinput_device_config_middle_emulation_is_available(device);
	ck_assert(available);

	state = libinput_device_config_middle_emulation_get_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);

	state = libinput_device_config_middle_emulation_get_default_enabled(
					    device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);

	status = libinput_device_config_middle_emulation_set_enabled(device,
					    LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_middle_emulation_set_enabled(device,
					    LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_middle_emulation_set_enabled(device, 3);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

START_TEST(middlebutton_default_clickpad)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	enum libinput_config_middle_emulation_state state;
	int available;

	available = libinput_device_config_middle_emulation_is_available(device);
	ck_assert(available);

	state = libinput_device_config_middle_emulation_get_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
	state = libinput_device_config_middle_emulation_get_default_enabled(
					    device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);

	status = libinput_device_config_middle_emulation_set_enabled(device,
					    LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_middle_emulation_set_enabled(device,
					    LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_middle_emulation_set_enabled(device, 3);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

START_TEST(middlebutton_default_touchpad)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_middle_emulation_state state;
	int available;
	const char *name = libinput_device_get_name(dev->libinput_device);

	if (streq(name, "litest AlpsPS/2 ALPS GlidePoint") ||
	    streq(name, "litest AlpsPS/2 ALPS DualPoint TouchPad"))
	    return;

	available = libinput_device_config_middle_emulation_is_available(device);
	ck_assert(!available);

	if (libinput_device_pointer_has_button(device, BTN_MIDDLE))
		return;

	state = libinput_device_config_middle_emulation_get_enabled(
					    device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
	state = libinput_device_config_middle_emulation_get_default_enabled(
					    device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
}
END_TEST

START_TEST(middlebutton_default_alps)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_middle_emulation_state state;
	int available;

	available = libinput_device_config_middle_emulation_is_available(device);
	ck_assert(available);

	state = libinput_device_config_middle_emulation_get_enabled(
					    device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	state = libinput_device_config_middle_emulation_get_default_enabled(
					    device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
}
END_TEST

START_TEST(middlebutton_default_disabled)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_middle_emulation_state state;
	enum libinput_config_status status;
	int available;

	available = libinput_device_config_middle_emulation_is_available(device);
	ck_assert(!available);
	state = libinput_device_config_middle_emulation_get_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
	state = libinput_device_config_middle_emulation_get_default_enabled(
								    device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
	status = libinput_device_config_middle_emulation_set_enabled(device,
				     LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_middle_emulation_set_enabled(device,
				     LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
}
END_TEST

START_TEST(middlebutton_button_scrolling)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;
	struct libinput_event *ev;
	struct libinput_event_pointer *pev;
	int i;

	status = libinput_device_config_middle_emulation_set_enabled(
				device,
				LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	status = libinput_device_config_scroll_set_method(device,
				LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	status = libinput_device_config_scroll_set_button(device, BTN_LEFT);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	/* middle emulation discards */
	litest_assert_empty_queue(li);

	litest_timeout_middlebutton();
	libinput_dispatch(li);

	/* scroll discards */
	litest_assert_empty_queue(li);
	litest_timeout_buttonscroll();
	libinput_dispatch(li);

	for (i = 0; i < 10; i++) {
		litest_event(dev, EV_REL, REL_Y, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}

	ev = libinput_get_event(li);
	do {
		pev = litest_is_axis_event(ev,
					   LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
					   LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS);
		ck_assert_double_gt(libinput_event_pointer_get_axis_value(pev,
									  LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL),
				    0.0);
		libinput_event_destroy(ev);
		ev = libinput_get_event(li);
	} while (ev);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	ev = libinput_get_event(li);
	pev = litest_is_axis_event(ev,
				   LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
				   LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS);
	ck_assert_double_eq(libinput_event_pointer_get_axis_value(pev,
								  LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL),
			    0.0);
	libinput_event_destroy(ev);

	/* no button release */
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(middlebutton_button_scrolling_middle)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	status = libinput_device_config_middle_emulation_set_enabled(
				device,
				LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	status = libinput_device_config_scroll_set_method(device,
				LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	status = libinput_device_config_scroll_set_button(device, BTN_LEFT);
	if (status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED)
		return;

	litest_drain_events(li);

	/* button scrolling should not stop middle emulation */

	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_RIGHT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_RIGHT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(pointer_time_usec)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_pointer *ptrev;
	struct libinput_event *event;
	uint64_t time_usec;

	litest_drain_events(dev->libinput);

	litest_event(dev, EV_REL, REL_X, 1);
	litest_event(dev, EV_REL, REL_Y, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_wait_for_event(li);

	event = libinput_get_event(li);
	ptrev = litest_is_motion_event(event);

	time_usec = libinput_event_pointer_get_time_usec(ptrev);
	ck_assert_int_eq(libinput_event_pointer_get_time(ptrev),
			 (uint32_t) (time_usec / 1000));

	libinput_event_destroy(event);
	litest_drain_events(dev->libinput);
}
END_TEST

START_TEST(debounce_bounce)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	unsigned int button = _i; /* ranged test */

	if (!libinput_device_pointer_has_button(dev->libinput_device,
						button))
		return;

	litest_disable_middleemu(dev);
	disable_button_scrolling(dev);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, button, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, button, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, button, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_timeout_debounce();
	libinput_dispatch(li);

	litest_assert_button_event(li,
				   button,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, button, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, button, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, button, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_timeout_debounce();
	libinput_dispatch(li);

	litest_assert_button_event(li,
				   button,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(debounce_bounce_check_immediate)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_disable_middleemu(dev);
	disable_button_scrolling(dev);
	litest_drain_events(li);

	/* Press must be sent without delay */
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_debounce();
	litest_assert_empty_queue(li);

	/* held down & past timeout, we expect releases to be immediate */

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_timeout_debounce();
	litest_assert_empty_queue(li);
}
END_TEST

/* Triggers the event sequence that initializes the spurious
 * debouncing behavior */
static inline void
debounce_trigger_spurious(struct litest_device *dev, struct libinput *li)
{
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_timeout_debounce();
	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_timeout_debounce();
	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	/* gets filtered now */
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_timeout_debounce();
	libinput_dispatch(li);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}

START_TEST(debounce_spurious)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	unsigned int button = _i; /* ranged test */

	if (!libinput_device_pointer_has_button(dev->libinput_device,
						button))
		return;

	litest_disable_middleemu(dev);
	disable_button_scrolling(dev);
	litest_drain_events(li);

	debounce_trigger_spurious(dev, li);

	for (int i = 0; i < 3; i++) {
		litest_event(dev, EV_KEY, button, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
		litest_timeout_debounce();
		libinput_dispatch(li);

		/* Not all devices can disable middle button emulation, time out on
		 * middle button here to make sure the initial button press event
		 * was flushed.
		 */
		litest_timeout_middlebutton();
		libinput_dispatch(li);

		litest_assert_button_event(li,
					   button,
					   LIBINPUT_BUTTON_STATE_PRESSED);

		/* bouncy bouncy bouncy */
		litest_event(dev, EV_KEY, button, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		litest_event(dev, EV_KEY, button, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		litest_assert_empty_queue(li);

		litest_event(dev, EV_KEY, button, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
		litest_timeout_debounce();
		libinput_dispatch(li);
		litest_assert_button_event(li,
					   button,
					   LIBINPUT_BUTTON_STATE_RELEASED);

		litest_assert_empty_queue(li);
	}
}
END_TEST

START_TEST(debounce_spurious_multibounce)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_disable_middleemu(dev);
	litest_drain_events(li);

	debounce_trigger_spurious(dev, li);
	litest_drain_events(li);

	/* Let's assume our button has ventricular fibrilation and sends a
	 * lot of clicks. Debouncing is now enabled, ventricular
	 * fibrillation should cause one button down for the first press and
	 * one release for the last release.
	 */

	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_timeout_debounce();

	/* Not all devices can disable middle button emulation, time out on
	 * middle button here to make sure the initial button press event
	 * was flushed.
	 */
	libinput_dispatch(li);
	litest_timeout_middlebutton();
	libinput_dispatch(li);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);
	litest_timeout_debounce();

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(debounce_spurious_dont_enable_on_otherbutton)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;

	if (!libinput_device_config_middle_emulation_is_available(device))
		return;

	litest_disable_middleemu(dev);
	disable_button_scrolling(dev);
	litest_drain_events(li);

	/* Don't trigger spurious debouncing on otherbutton events */
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_timeout_debounce();
	libinput_dispatch(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_RIGHT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_RIGHT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);

	/* Expect release to be immediate */
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_timeout_debounce();
	libinput_dispatch(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(debounce_spurious_cancel_debounce_otherbutton)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;

	if (!libinput_device_config_middle_emulation_is_available(device))
		return;

	litest_disable_middleemu(dev);
	disable_button_scrolling(dev);
	litest_drain_events(li);

	debounce_trigger_spurious(dev, li);

	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_timeout_debounce();
	libinput_dispatch(li);

	/* spurious debouncing is on but the release should get flushed by
	 * the other button */
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_RIGHT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_RIGHT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(debounce_spurious_switch_to_otherbutton)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;

	if (!libinput_device_config_middle_emulation_is_available(device))
		return;

	litest_drain_events(li);
	debounce_trigger_spurious(dev, li);

	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_timeout_debounce();
	libinput_dispatch(li);


	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	/* release is now held back,
	 * other button should flush the release */
	litest_event(dev, EV_KEY, BTN_RIGHT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_RIGHT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	/* bouncing right button triggers debounce */
	litest_event(dev, EV_KEY, BTN_RIGHT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_RIGHT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

void
litest_setup_tests_pointer(void)
{
	struct range axis_range = {ABS_X, ABS_Y + 1};
	struct range compass = {0, 7}; /* cardinal directions */
	struct range buttons = {BTN_LEFT, BTN_TASK + 1};

	litest_add("pointer:motion", pointer_motion_relative, LITEST_RELATIVE, LITEST_POINTINGSTICK);
	litest_add_for_device("pointer:motion", pointer_motion_relative_zero, LITEST_MOUSE);
	litest_add_ranged("pointer:motion", pointer_motion_relative_min_decel, LITEST_RELATIVE, LITEST_POINTINGSTICK, &compass);
	litest_add("pointer:motion", pointer_motion_absolute, LITEST_ABSOLUTE, LITEST_ANY);
	litest_add("pointer:motion", pointer_motion_unaccel, LITEST_RELATIVE, LITEST_ANY);
	litest_add("pointer:button", pointer_button, LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add_no_device("pointer:button", pointer_button_auto_release);
	litest_add_no_device("pointer:button", pointer_seat_button_count);
	litest_add_for_device("pointer:button", pointer_button_has_no_button, LITEST_KEYBOARD);
	litest_add("pointer:button", pointer_recover_from_lost_button_count, LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("pointer:scroll", pointer_scroll_wheel, LITEST_WHEEL, LITEST_TABLET);
	litest_add("pointer:scroll", pointer_scroll_button, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ANY);
	litest_add("pointer:scroll", pointer_scroll_button_noscroll, LITEST_ABSOLUTE|LITEST_BUTTON, LITEST_RELATIVE);
	litest_add("pointer:scroll", pointer_scroll_button_noscroll, LITEST_ANY, LITEST_RELATIVE|LITEST_BUTTON);
	litest_add("pointer:scroll", pointer_scroll_button_no_event_before_timeout, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ANY);
	litest_add("pointer:scroll", pointer_scroll_button_middle_emulation, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ANY);
	litest_add("pointer:scroll", pointer_scroll_nowheel_defaults, LITEST_RELATIVE|LITEST_BUTTON, LITEST_WHEEL);
	litest_add_for_device("pointer:scroll", pointer_scroll_defaults_logitech_marble , LITEST_LOGITECH_TRACKBALL);
	litest_add("pointer:scroll", pointer_scroll_natural_defaults, LITEST_WHEEL, LITEST_TABLET);
	litest_add("pointer:scroll", pointer_scroll_natural_defaults_noscroll, LITEST_ANY, LITEST_WHEEL);
	litest_add("pointer:scroll", pointer_scroll_natural_enable_config, LITEST_WHEEL, LITEST_TABLET);
	litest_add("pointer:scroll", pointer_scroll_natural_wheel, LITEST_WHEEL, LITEST_TABLET);
	litest_add("pointer:scroll", pointer_scroll_has_axis_invalid, LITEST_WHEEL, LITEST_TABLET);

	litest_add("pointer:calibration", pointer_no_calibration, LITEST_ANY, LITEST_TOUCH|LITEST_SINGLE_TOUCH|LITEST_ABSOLUTE|LITEST_PROTOCOL_A|LITEST_TABLET);

									/* tests touchpads too */
	litest_add("pointer:left-handed", pointer_left_handed_defaults, LITEST_BUTTON, LITEST_ANY);
	litest_add("pointer:left-handed", pointer_left_handed, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ANY);
	litest_add("pointer:left-handed", pointer_left_handed_during_click, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ANY);
	litest_add("pointer:left-handed", pointer_left_handed_during_click_multiple_buttons, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ANY);

	litest_add("pointer:accel", pointer_accel_defaults, LITEST_RELATIVE, LITEST_ANY);
	litest_add("pointer:accel", pointer_accel_invalid, LITEST_RELATIVE, LITEST_ANY);
	litest_add("pointer:accel", pointer_accel_defaults_absolute, LITEST_ABSOLUTE, LITEST_RELATIVE);
	litest_add("pointer:accel", pointer_accel_defaults_absolute_relative, LITEST_ABSOLUTE|LITEST_RELATIVE, LITEST_ANY);
	litest_add("pointer:accel", pointer_accel_direction_change, LITEST_RELATIVE, LITEST_POINTINGSTICK);
	litest_add("pointer:accel", pointer_accel_profile_defaults, LITEST_RELATIVE, LITEST_TOUCHPAD);
	litest_add("pointer:accel", pointer_accel_profile_defaults_noprofile, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("pointer:accel", pointer_accel_profile_invalid, LITEST_RELATIVE, LITEST_ANY);
	litest_add("pointer:accel", pointer_accel_profile_noaccel, LITEST_ANY, LITEST_TOUCHPAD|LITEST_RELATIVE|LITEST_TABLET);
	litest_add("pointer:accel", pointer_accel_profile_flat_motion_relative, LITEST_RELATIVE, LITEST_TOUCHPAD);

	litest_add("pointer:middlebutton", middlebutton, LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("pointer:middlebutton", middlebutton_nostart_while_down, LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("pointer:middlebutton", middlebutton_timeout, LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("pointer:middlebutton", middlebutton_doubleclick, LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("pointer:middlebutton", middlebutton_middleclick, LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("pointer:middlebutton", middlebutton_middleclick_during, LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("pointer:middlebutton", middlebutton_default_enabled, LITEST_BUTTON, LITEST_TOUCHPAD|LITEST_POINTINGSTICK);
	litest_add("pointer:middlebutton", middlebutton_default_clickpad, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("pointer:middlebutton", middlebutton_default_touchpad, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("pointer:middlebutton", middlebutton_default_disabled, LITEST_ANY, LITEST_BUTTON);
	litest_add_for_device("pointer:middlebutton", middlebutton_default_alps, LITEST_ALPS_SEMI_MT);
	litest_add("pointer:middlebutton", middlebutton_button_scrolling, LITEST_RELATIVE|LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("pointer:middlebutton", middlebutton_button_scrolling_middle, LITEST_RELATIVE|LITEST_BUTTON, LITEST_CLICKPAD);

	litest_add_ranged("pointer:state", pointer_absolute_initial_state, LITEST_ABSOLUTE, LITEST_ANY, &axis_range);

	litest_add("pointer:time", pointer_time_usec, LITEST_RELATIVE, LITEST_ANY);

	litest_add_ranged("pointer:debounce", debounce_bounce, LITEST_BUTTON, LITEST_TOUCHPAD, &buttons);
	litest_add("pointer:debounce", debounce_bounce_check_immediate, LITEST_BUTTON, LITEST_TOUCHPAD);
	litest_add_ranged("pointer:debounce", debounce_spurious, LITEST_BUTTON, LITEST_TOUCHPAD, &buttons);
	litest_add("pointer:debounce", debounce_spurious_multibounce, LITEST_BUTTON, LITEST_TOUCHPAD);
	litest_add("pointer:debounce_otherbutton", debounce_spurious_dont_enable_on_otherbutton, LITEST_BUTTON, LITEST_TOUCHPAD);
	litest_add("pointer:debounce_otherbutton", debounce_spurious_cancel_debounce_otherbutton, LITEST_BUTTON, LITEST_TOUCHPAD);
	litest_add("pointer:debounce_otherbutton", debounce_spurious_switch_to_otherbutton, LITEST_BUTTON, LITEST_TOUCHPAD);
}
