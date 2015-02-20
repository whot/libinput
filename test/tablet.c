/*
 * Copyright © 2014 Red Hat, Inc.
 * Copyright © 2014 Stephen Chandler "Lyude" Paul
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
#include <unistd.h>
#include <stdbool.h>

#include "libinput-util.h"
#include "evdev-tablet.h"
#include "litest.h"

START_TEST(proximity_in_out)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet *tablet_event;
	struct libinput_event *event;
	bool have_tool_update = false,
	     have_proximity_out = false;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ -1, -1 }
	};

	litest_drain_events(dev->libinput);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) ==
		    LIBINPUT_EVENT_TABLET_PROXIMITY) {
			struct libinput_tool * tool;

			have_tool_update++;
			tablet_event = libinput_event_get_tablet_event(event);
			tool = libinput_event_tablet_get_tool(tablet_event);
			ck_assert_int_eq(libinput_tool_get_type(tool),
					 LIBINPUT_TOOL_PEN);
		}
		libinput_event_destroy(event);
	}
	ck_assert(have_tool_update);

	litest_tablet_proximity_out(dev);
	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) ==
		    LIBINPUT_EVENT_TABLET_PROXIMITY) {
			struct libinput_event_tablet *t =
				libinput_event_get_tablet_event(event);

			if (libinput_event_tablet_get_proximity_state(t) ==
			    LIBINPUT_TOOL_PROXIMITY_OUT)
				have_proximity_out = true;
		}

		libinput_event_destroy(event);
	}
	ck_assert(have_proximity_out);

	/* Proximity out must not emit axis events */
	litest_tablet_proximity_out(dev);
	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type = libinput_event_get_type(event);

		ck_assert(type != LIBINPUT_EVENT_TABLET_AXIS);

		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(proximity_out_clear_buttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet *tablet_event;
	struct libinput_event *event;
	uint32_t button;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ -1, -1 }
	};

	litest_drain_events(dev->libinput);

	/* Test that proximity out events send button releases for any currently
	 * pressed stylus buttons
	 */
	for (button = BTN_TOUCH; button <= BTN_STYLUS2; button++) {
		bool button_released = false;
		uint32_t event_button;
		enum libinput_button_state state;

		litest_tablet_proximity_in(dev, 10, 10, axes);
		litest_event(dev, EV_KEY, button, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		litest_tablet_proximity_out(dev);

		libinput_dispatch(li);

		while ((event = libinput_get_event(li))) {
			tablet_event = libinput_event_get_tablet_event(event);

			if (libinput_event_get_type(event) ==
			    LIBINPUT_EVENT_TABLET_BUTTON) {

				event_button = libinput_event_tablet_get_button(tablet_event);
				state = libinput_event_tablet_get_button_state(tablet_event);

				if (event_button == button &&
				    state == LIBINPUT_BUTTON_STATE_RELEASED)
					button_released = true;
			}

			libinput_event_destroy(event);
		}

		ck_assert_msg(button_released,
			      "Button %d was not released.",
			      event_button);
	}
}
END_TEST

START_TEST(proximity_has_axes)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet *tablet_event;
	struct libinput_event *event;
	struct libinput_tool *tool;
	double x, y,
	       distance;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_TILT_X, 10 },
		{ ABS_TILT_Y, 10 },
		{ -1, -1}
	};

	litest_drain_events(dev->libinput);

	litest_tablet_proximity_in(dev, 10, 10, axes);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TABLET_PROXIMITY, -1);

	event = libinput_get_event(li);

	tablet_event = libinput_event_get_tablet_event(event);
	tool = libinput_event_tablet_get_tool(tablet_event);

	ck_assert(libinput_event_tablet_axis_has_changed(
			tablet_event, LIBINPUT_TABLET_AXIS_X));
	ck_assert(libinput_event_tablet_axis_has_changed(
			tablet_event, LIBINPUT_TABLET_AXIS_Y));

	x = libinput_event_tablet_get_axis_value(tablet_event,
						 LIBINPUT_TABLET_AXIS_X);
	y = libinput_event_tablet_get_axis_value(tablet_event,
						 LIBINPUT_TABLET_AXIS_Y);

	litest_assert_double_ne(x, 0);
	litest_assert_double_ne(y, 0);

	if (libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_DISTANCE)) {
		ck_assert(libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_DISTANCE));

		distance = libinput_event_tablet_get_axis_value(
			tablet_event,
			LIBINPUT_TABLET_AXIS_DISTANCE);
		litest_assert_double_ne(distance, 0);
	}

	if (libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_TILT_X) &&
	    libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_TILT_Y)) {
		ck_assert(libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_TILT_X));
		ck_assert(libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_TILT_Y));

		x = libinput_event_tablet_get_axis_value(
			tablet_event,
			LIBINPUT_TABLET_AXIS_TILT_X);
		y = libinput_event_tablet_get_axis_value(
			tablet_event,
			LIBINPUT_TABLET_AXIS_TILT_Y);

		litest_assert_double_ne(x, 0);
		litest_assert_double_ne(y, 0);
	}

	litest_assert_empty_queue(li);
	libinput_event_destroy(event);

	/* Make sure that the axes are still present on proximity out */
	litest_tablet_proximity_out(dev);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TABLET_PROXIMITY, -1);
	event = libinput_get_event(li);

	tablet_event = libinput_event_get_tablet_event(event);
	tool = libinput_event_tablet_get_tool(tablet_event);

	ck_assert(!libinput_event_tablet_axis_has_changed(
			tablet_event, LIBINPUT_TABLET_AXIS_X));
	ck_assert(!libinput_event_tablet_axis_has_changed(
			tablet_event, LIBINPUT_TABLET_AXIS_Y));

	x = libinput_event_tablet_get_axis_value(tablet_event,
						 LIBINPUT_TABLET_AXIS_X);
	y = libinput_event_tablet_get_axis_value(tablet_event,
						 LIBINPUT_TABLET_AXIS_Y);

	litest_assert_double_ne(x, 0);
	litest_assert_double_ne(y, 0);

	if (libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_DISTANCE)) {
		ck_assert(!libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_DISTANCE));

		distance = libinput_event_tablet_get_axis_value(
			tablet_event,
			LIBINPUT_TABLET_AXIS_DISTANCE);
		litest_assert_double_ne(distance, 0);
	}

	if (libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_TILT_X) &&
	    libinput_tool_has_axis(tool, LIBINPUT_TABLET_AXIS_TILT_Y)) {
		ck_assert(!libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_TILT_X));
		ck_assert(!libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_TILT_Y));

		x = libinput_event_tablet_get_axis_value(
			tablet_event,
			LIBINPUT_TABLET_AXIS_TILT_X);
		y = libinput_event_tablet_get_axis_value(
			tablet_event,
			LIBINPUT_TABLET_AXIS_TILT_Y);

		litest_assert_double_ne(x, 0);
		litest_assert_double_ne(y, 0);
	}

	litest_assert_empty_queue(li);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet *tablet_event;
	struct libinput_event *event;
	int test_x, test_y;
	double last_reported_x = 0, last_reported_y = 0;
	enum libinput_event_type type;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 5, 100, axes);
	libinput_dispatch(li);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_PROXIMITY,
				      -1);

	while ((event = libinput_get_event(li))) {
		bool x_changed, y_changed;
		double reported_x, reported_y;

		tablet_event = libinput_event_get_tablet_event(event);
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_TABLET_PROXIMITY);

		x_changed = libinput_event_tablet_axis_has_changed(
		    tablet_event, LIBINPUT_TABLET_AXIS_X);
		y_changed = libinput_event_tablet_axis_has_changed(
		    tablet_event, LIBINPUT_TABLET_AXIS_Y);

		ck_assert(x_changed);
		ck_assert(y_changed);

		reported_x = libinput_event_tablet_get_axis_value(
		    tablet_event, LIBINPUT_TABLET_AXIS_X);
		reported_y = libinput_event_tablet_get_axis_value(
		    tablet_event, LIBINPUT_TABLET_AXIS_Y);

		litest_assert_double_lt(reported_x, reported_y);

		last_reported_x = reported_x;
		last_reported_y = reported_y;

		libinput_event_destroy(event);
	}

	for (test_x = 10, test_y = 90;
	     test_x <= 100;
	     test_x += 10, test_y -= 10) {
		bool x_changed, y_changed;
		double reported_x, reported_y;

		litest_tablet_motion(dev, test_x, test_y, axes);
		libinput_dispatch(li);

		while ((event = libinput_get_event(li))) {
			tablet_event = libinput_event_get_tablet_event(event);
			type = libinput_event_get_type(event);

			if (type == LIBINPUT_EVENT_TABLET_AXIS) {
				x_changed = libinput_event_tablet_axis_has_changed(
				    tablet_event, LIBINPUT_TABLET_AXIS_X);
				y_changed = libinput_event_tablet_axis_has_changed(
				    tablet_event, LIBINPUT_TABLET_AXIS_Y);

				ck_assert(x_changed);
				ck_assert(y_changed);

				reported_x = libinput_event_tablet_get_axis_value(
				    tablet_event, LIBINPUT_TABLET_AXIS_X);
				reported_y = libinput_event_tablet_get_axis_value(
				    tablet_event, LIBINPUT_TABLET_AXIS_Y);

				litest_assert_double_gt(reported_x,
							last_reported_x);
				litest_assert_double_lt(reported_y,
							last_reported_y);

				last_reported_x = reported_x;
				last_reported_y = reported_y;
			}

			libinput_event_destroy(event);
		}
	}
}
END_TEST

START_TEST(motion_delta)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet *tablet_event;
	struct libinput_event *event;
	double x1, y1, x2, y2, dist1, dist2;
	double delta;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 5, 100, axes);
	libinput_dispatch(li);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_PROXIMITY,
				      -1);

	event = libinput_get_event(li);
	tablet_event = libinput_event_get_tablet_event(event);
	x1 = libinput_event_tablet_get_axis_value(tablet_event,
						  LIBINPUT_TABLET_AXIS_X);
	y1 = libinput_event_tablet_get_axis_value(tablet_event,
						  LIBINPUT_TABLET_AXIS_Y);
	dist1 = libinput_event_tablet_get_axis_value(tablet_event,
					  LIBINPUT_TABLET_AXIS_DISTANCE);
	libinput_event_destroy(event);

	axes[0].value = 40;
	litest_tablet_motion(dev, 40, 100, axes);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_AXIS,
				      -1);
	event = libinput_get_event(li);
	tablet_event = libinput_event_get_tablet_event(event);
	x2 = libinput_event_tablet_get_axis_value(tablet_event,
						  LIBINPUT_TABLET_AXIS_X);
	y2 = libinput_event_tablet_get_axis_value(tablet_event,
						  LIBINPUT_TABLET_AXIS_Y);
	dist2 = libinput_event_tablet_get_axis_value(tablet_event,
					  LIBINPUT_TABLET_AXIS_DISTANCE);

	delta = libinput_event_tablet_get_axis_delta(tablet_event,
						  LIBINPUT_TABLET_AXIS_X);
	litest_assert_double_eq(delta, x2 - x1);
	delta = libinput_event_tablet_get_axis_delta(tablet_event,
						  LIBINPUT_TABLET_AXIS_Y);
	litest_assert_double_eq(delta, y2 - y1);
	delta = libinput_event_tablet_get_axis_delta(tablet_event,
						  LIBINPUT_TABLET_AXIS_DISTANCE);
	litest_assert_double_eq(delta, dist2 - dist1);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(left_handed)
{
#if HAVE_LIBWACOM
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet *tablet_event;
	double libinput_max_x, libinput_max_y;
	double last_x, last_y;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ -1, -1 }
	};

	ck_assert(libinput_device_config_left_handed_is_available(dev->libinput_device));

	libinput_device_get_size (dev->libinput_device,
				  &libinput_max_x,
				  &libinput_max_y);

	/* Test that left-handed mode doesn't go into effect until the tool has
	 * left proximity of the tablet. In order to test this, we have to bring
	 * the tool into proximity and make sure libinput processes the
	 * proximity events so that it updates it's internal tablet state, and
	 * then try setting it to left-handed mode. */
	litest_tablet_proximity_in(dev, 0, 100, axes);
	libinput_dispatch(li);
	libinput_device_config_left_handed_set(dev->libinput_device, 1);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TABLET_PROXIMITY, -1);

	while ((event = libinput_get_event(li))) {
		tablet_event = libinput_event_get_tablet_event(event);

		last_x = libinput_event_tablet_get_axis_value(
				tablet_event, LIBINPUT_TABLET_AXIS_X);
		last_y = libinput_event_tablet_get_axis_value(
				tablet_event, LIBINPUT_TABLET_AXIS_Y);

		litest_assert_double_eq(last_x, 0);
		litest_assert_double_eq(last_y, libinput_max_y);

		libinput_event_destroy(event);
	}

	litest_tablet_motion(dev, 100, 0, axes);
	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TABLET_AXIS, -1);

	while ((event = libinput_get_event(li))) {
		double x, y;
		tablet_event = libinput_event_get_tablet_event(event);

		x = libinput_event_tablet_get_axis_value(
			tablet_event, LIBINPUT_TABLET_AXIS_X);
		y = libinput_event_tablet_get_axis_value(
			tablet_event, LIBINPUT_TABLET_AXIS_Y);

		litest_assert_double_eq(x, libinput_max_x);
		litest_assert_double_eq(y, 0);

		litest_assert_double_gt(x, last_x);
		litest_assert_double_lt(y, last_y);

		libinput_event_destroy(event);
	}

	litest_tablet_proximity_out(dev);
	litest_drain_events(li);

	/* Since we've drained the events and libinput's aware the tool is out
	 * of proximity, it should have finally transitioned into left-handed
	 * mode, so the axes should be inverted once we bring it back into
	 * proximity */
	litest_tablet_proximity_in(dev, 0, 100, axes);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TABLET_PROXIMITY, -1);

	while ((event = libinput_get_event(li))) {
		tablet_event = libinput_event_get_tablet_event(event);

		last_x = libinput_event_tablet_get_axis_value(
				tablet_event, LIBINPUT_TABLET_AXIS_X);
		last_y = libinput_event_tablet_get_axis_value(
				tablet_event, LIBINPUT_TABLET_AXIS_Y);

		litest_assert_double_eq(last_x, libinput_max_x);
		litest_assert_double_eq(last_y, 0);

		libinput_event_destroy(event);
	}

	litest_tablet_motion(dev, 100, 0, axes);
	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_TABLET_AXIS, -1);

	while ((event = libinput_get_event(li))) {
		double x, y;
		tablet_event = libinput_event_get_tablet_event(event);

		x = libinput_event_tablet_get_axis_value(
			tablet_event, LIBINPUT_TABLET_AXIS_X);
		y = libinput_event_tablet_get_axis_value(
			tablet_event, LIBINPUT_TABLET_AXIS_Y);

		litest_assert_double_eq(x, 0);
		litest_assert_double_eq(y, libinput_max_y);

		litest_assert_double_lt(x, last_x);
		litest_assert_double_gt(y, last_y);

		libinput_event_destroy(event);
	}
#endif
}
END_TEST

START_TEST(no_left_handed)
{
	struct litest_device *dev = litest_current_device();

	ck_assert(!libinput_device_config_left_handed_is_available(dev->libinput_device));
}
END_TEST

START_TEST(motion_event_state)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet *tablet_event;
	int test_x, test_y;
	double last_x, last_y;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ -1, -1 }
	};

	litest_drain_events(li);
	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_drain_events(li);

	/* couple of events that go left/bottom to right/top */
	for (test_x = 0, test_y = 100; test_x < 100; test_x += 10, test_y -= 10)
		litest_tablet_motion(dev, test_x, test_y, axes);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) == LIBINPUT_EVENT_TABLET_AXIS)
			break;
		libinput_event_destroy(event);
	}

	/* pop the first event off */
	ck_assert_notnull(event);
	tablet_event = libinput_event_get_tablet_event(event);
	ck_assert_notnull(tablet_event);

	last_x = libinput_event_tablet_get_axis_value(tablet_event,
						      LIBINPUT_TABLET_AXIS_X);
	last_y = libinput_event_tablet_get_axis_value(tablet_event,
						      LIBINPUT_TABLET_AXIS_Y);

	/* mark with a button event, then go back to bottom/left */
	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (test_x = 100, test_y = 0; test_x > 0; test_x -= 10, test_y += 10)
		litest_tablet_motion(dev, test_x, test_y, axes);

	libinput_event_destroy(event);
	libinput_dispatch(li);
	ck_assert_int_eq(libinput_next_event_type(li),
			 LIBINPUT_EVENT_TABLET_AXIS);

	/* we expect all events up to the button event to go from
	   bottom/left to top/right */
	while ((event = libinput_get_event(li))) {
		double x, y;

		if (libinput_event_get_type(event) != LIBINPUT_EVENT_TABLET_AXIS)
			break;

		tablet_event = libinput_event_get_tablet_event(event);
		ck_assert_notnull(tablet_event);

		x = libinput_event_tablet_get_axis_value(tablet_event,
							 LIBINPUT_TABLET_AXIS_X);
		y = libinput_event_tablet_get_axis_value(tablet_event,
							 LIBINPUT_TABLET_AXIS_Y);

		ck_assert(x > last_x);
		ck_assert(y < last_y);

		last_x = x;
		last_y = y;
		libinput_event_destroy(event);
	}

	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_TABLET_BUTTON);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(bad_distance_events)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	const struct input_absinfo *absinfo;
	struct axis_replacement axes[] = {
		{ -1, -1 },
	};

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_tablet_proximity_out(dev);
	litest_drain_events(dev->libinput);

	absinfo = libevdev_get_abs_info(dev->evdev, ABS_DISTANCE);
	ck_assert(absinfo != NULL);

	litest_event(dev, EV_ABS, ABS_DISTANCE, absinfo->maximum);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_ABS, ABS_DISTANCE, absinfo->minimum);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(normalization)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet *tablet_event;
	struct libinput_event *event;
	double pressure,
	       tilt_vertical,
	       tilt_horizontal;
	const struct input_absinfo *pressure_absinfo,
                                   *tilt_vertical_absinfo,
                                   *tilt_horizontal_absinfo;

	litest_drain_events(dev->libinput);

	pressure_absinfo = libevdev_get_abs_info(dev->evdev, ABS_PRESSURE);
	tilt_vertical_absinfo = libevdev_get_abs_info(dev->evdev, ABS_TILT_X);
	tilt_horizontal_absinfo = libevdev_get_abs_info(dev->evdev, ABS_TILT_Y);

	/* Test minimum */
	if (pressure_absinfo != NULL)
		litest_event(dev,
			     EV_ABS,
			     ABS_PRESSURE,
			     pressure_absinfo->minimum);

	if (tilt_vertical_absinfo != NULL)
		litest_event(dev,
			     EV_ABS,
			     ABS_TILT_X,
			     tilt_vertical_absinfo->minimum);

	if (tilt_horizontal_absinfo != NULL)
		litest_event(dev,
			     EV_ABS,
			     ABS_TILT_Y,
			     tilt_horizontal_absinfo->minimum);

	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) == LIBINPUT_EVENT_TABLET_AXIS) {
			tablet_event = libinput_event_get_tablet_event(event);

			if (libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_PRESSURE)) {
				pressure = libinput_event_tablet_get_axis_value(
				    tablet_event, LIBINPUT_TABLET_AXIS_PRESSURE);

				litest_assert_double_eq(pressure, 0);
			}

			if (libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_TILT_X)) {
				tilt_vertical =
					libinput_event_tablet_get_axis_value(
					    tablet_event,
					    LIBINPUT_TABLET_AXIS_TILT_X);

				litest_assert_double_eq(tilt_vertical, -1);
			}

			if (libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_TILT_Y)) {
				tilt_horizontal =
					libinput_event_tablet_get_axis_value(
					    tablet_event,
					    LIBINPUT_TABLET_AXIS_TILT_Y);

				litest_assert_double_eq(tilt_horizontal, -1);
			}
		}

		libinput_event_destroy(event);
	}

	/* Test maximum */
	if (pressure_absinfo != NULL)
		litest_event(dev,
			     EV_ABS,
			     ABS_PRESSURE,
			     pressure_absinfo->maximum);

	if (tilt_vertical_absinfo != NULL)
		litest_event(dev,
			     EV_ABS,
			     ABS_TILT_X,
			     tilt_vertical_absinfo->maximum);

	if (tilt_horizontal_absinfo != NULL)
		litest_event(dev,
			     EV_ABS,
			     ABS_TILT_Y,
			     tilt_horizontal_absinfo->maximum);

	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) == LIBINPUT_EVENT_TABLET_AXIS) {
			tablet_event = libinput_event_get_tablet_event(event);

			if (libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_PRESSURE)) {
				pressure = libinput_event_tablet_get_axis_value(
				    tablet_event, LIBINPUT_TABLET_AXIS_PRESSURE);

				litest_assert_double_eq(pressure, 1);
			}

			if (libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_TILT_X)) {
				tilt_vertical =
					libinput_event_tablet_get_axis_value(
					    tablet_event,
					    LIBINPUT_TABLET_AXIS_TILT_X);

				litest_assert_double_eq(tilt_vertical, 1);
			}

			if (libinput_event_tablet_axis_has_changed(
				tablet_event,
				LIBINPUT_TABLET_AXIS_TILT_Y)) {
				tilt_horizontal =
					libinput_event_tablet_get_axis_value(
					    tablet_event,
					    LIBINPUT_TABLET_AXIS_TILT_Y);

				litest_assert_double_eq(tilt_horizontal, 1);
			}
		}

		libinput_event_destroy(event);
	}

}
END_TEST

START_TEST(tool_serial)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet *tablet_event;
	struct libinput_event *event;
	struct libinput_tool *tool;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_PROXIMITY,
				      -1);
	event = libinput_get_event(li);
	tablet_event = libinput_event_get_tablet_event(event);
	tool = libinput_event_tablet_get_tool(tablet_event);
	ck_assert_uint_eq(libinput_tool_get_serial(tool), 1000);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(serial_changes_tool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet *tablet_event;
	struct libinput_event *event;
	struct libinput_tool *tool;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 2000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_PROXIMITY,
				      -1);
	event = libinput_get_event(li);
	tablet_event = libinput_event_get_tablet_event(event);
	tool = libinput_event_tablet_get_tool(tablet_event);

	ck_assert_uint_eq(libinput_tool_get_serial(tool), 2000);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(invalid_serials)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet *tablet_event;
	struct libinput_tool *tool;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, -1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) ==
		    LIBINPUT_EVENT_TABLET_PROXIMITY) {
			tablet_event = libinput_event_get_tablet_event(event);
			tool = libinput_event_tablet_get_tool(tablet_event);

			ck_assert_uint_eq(libinput_tool_get_serial(tool), 1000);
		}

		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(tool_ref)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet *tablet_event;
	struct libinput_event *event;
	struct libinput_tool *tool;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_PROXIMITY,
				      -1);
	event = libinput_get_event(li);
	tablet_event = libinput_event_get_tablet_event(event);
	tool = libinput_event_tablet_get_tool(tablet_event);

	ck_assert_notnull(tool);
	ck_assert(tool == libinput_tool_ref(tool));
	ck_assert(tool == libinput_tool_unref(tool));
	ck_assert(libinput_tool_unref(tool) == NULL);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(pad_buttons_ignored)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ -1, -1 }
	};
	int button;

	litest_drain_events(li);

	for (button = BTN_0; button < BTN_MOUSE; button++) {
		litest_event(dev, EV_KEY, button, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		litest_event(dev, EV_KEY, button, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}

	while ((event = libinput_get_event(li))) {
		ck_assert_int_ne(libinput_event_get_type(event),
				 LIBINPUT_EVENT_TABLET_BUTTON);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}

	/* same thing while in prox */
	litest_tablet_proximity_in(dev, 10, 10, axes);
	for (button = BTN_0; button < BTN_MOUSE; button++) {
		litest_event(dev, EV_KEY, button, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		litest_event(dev, EV_KEY, button, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}
	litest_tablet_proximity_out(dev);

	libinput_dispatch(li);
	while ((event = libinput_get_event(li))) {
		ck_assert_int_ne(libinput_event_get_type(event),
				 LIBINPUT_EVENT_TABLET_BUTTON);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}
}
END_TEST

START_TEST(tools_with_serials)
{
	struct libinput *li = litest_create_context();
	struct litest_device *dev[2];
	struct libinput_tool *tool[2] = {0};
	struct libinput_event *event;
	int i;

	for (i = 0; i < 2; i++) {
		dev[i] = litest_add_device_with_overrides(li,
							  LITEST_WACOM_INTUOS,
							  NULL,
							  NULL,
							  NULL,
							  NULL);
		/* WARNING: this test fails if UI_GET_SYSNAME isn't
		 * available or isn't used by libevdev (1.3, commit 2ff45c73).
		 * Put a sleep(1) here and that usually fixes it.
		 */

		litest_event(dev[i], EV_KEY, BTN_TOOL_PEN, 1);
		litest_event(dev[i], EV_MSC, MSC_SERIAL, 100);
		litest_event(dev[i], EV_SYN, SYN_REPORT, 0);

		libinput_dispatch(li);
		while ((event = libinput_get_event(li))) {
			if (libinput_event_get_type(event) ==
			    LIBINPUT_EVENT_TABLET_PROXIMITY) {
				struct libinput_event_tablet *t =
					libinput_event_get_tablet_event(event);

				tool[i] = libinput_event_tablet_get_tool(t);
			}

			libinput_event_destroy(event);
		}
	}

	/* We should get the same object for both devices */
	ck_assert_notnull(tool[0]);
	ck_assert_notnull(tool[1]);
	ck_assert_ptr_eq(tool[0], tool[1]);

	litest_delete_device(dev[0]);
	litest_delete_device(dev[1]);
	libinput_unref(li);
}
END_TEST

START_TEST(tools_without_serials)
{
	struct libinput *li = litest_create_context();
	struct litest_device *dev[2];
	struct libinput_tool *tool[2] = {0};
	struct libinput_event *event;
	int i;

	for (i = 0; i < 2; i++) {
		dev[i] = litest_add_device_with_overrides(li,
							  LITEST_WACOM_ISDV4,
							  NULL,
							  NULL,
							  NULL,
							  NULL);

		/* WARNING: this test fails if UI_GET_SYSNAME isn't
		 * available or isn't used by libevdev (1.3, commit 2ff45c73).
		 * Put a sleep(1) here and that usually fixes it.
		 */

		litest_event(dev[i], EV_KEY, BTN_TOOL_PEN, 1);
		litest_event(dev[i], EV_SYN, SYN_REPORT, 0);

		libinput_dispatch(li);
		while ((event = libinput_get_event(li))) {
			if (libinput_event_get_type(event) ==
			    LIBINPUT_EVENT_TABLET_PROXIMITY) {
				struct libinput_event_tablet *t =
					libinput_event_get_tablet_event(event);

				tool[i] = libinput_event_tablet_get_tool(t);
			}

			libinput_event_destroy(event);
		}
	}

	/* We should get different tool objects for each device */
	ck_assert_notnull(tool[0]);
	ck_assert_notnull(tool[1]);
	ck_assert_ptr_ne(tool[0], tool[1]);

	litest_delete_device(dev[0]);
	litest_delete_device(dev[1]);
	libinput_unref(li);
}
END_TEST

START_TEST(tool_capabilities)
{
	struct libinput *li = litest_create_context();
	struct litest_device *intuos;
	struct litest_device *bamboo;
	struct libinput_event *event;

	/* The axis capabilities of a tool can differ depending on the type of
	 * tablet the tool is being used with */
	bamboo = litest_create_device_with_overrides(LITEST_WACOM_BAMBOO,
						     NULL,
						     NULL,
						     NULL,
						     NULL);
	intuos = litest_create_device_with_overrides(LITEST_WACOM_INTUOS,
						     NULL,
						     NULL,
						     NULL,
						     NULL);

	litest_event(bamboo, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(bamboo, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) ==
		    LIBINPUT_EVENT_TABLET_PROXIMITY) {
			struct libinput_event_tablet *t =
				libinput_event_get_tablet_event(event);
			struct libinput_tool *tool =
				libinput_event_tablet_get_tool(t);

			ck_assert(libinput_tool_has_axis(tool,
							 LIBINPUT_TABLET_AXIS_PRESSURE));
			ck_assert(libinput_tool_has_axis(tool,
							 LIBINPUT_TABLET_AXIS_DISTANCE));
			ck_assert(!libinput_tool_has_axis(tool,
							  LIBINPUT_TABLET_AXIS_TILT_X));
			ck_assert(!libinput_tool_has_axis(tool,
							  LIBINPUT_TABLET_AXIS_TILT_Y));
		}

		libinput_event_destroy(event);
	}

	litest_event(intuos, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(intuos, EV_SYN, SYN_REPORT, 0);

	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) ==
		    LIBINPUT_EVENT_TABLET_PROXIMITY) {
			struct libinput_event_tablet *t =
				libinput_event_get_tablet_event(event);
			struct libinput_tool *tool =
				libinput_event_tablet_get_tool(t);

			ck_assert(libinput_tool_has_axis(tool,
							 LIBINPUT_TABLET_AXIS_PRESSURE));
			ck_assert(libinput_tool_has_axis(tool,
							 LIBINPUT_TABLET_AXIS_DISTANCE));
			ck_assert(libinput_tool_has_axis(tool,
							 LIBINPUT_TABLET_AXIS_TILT_X));
			ck_assert(libinput_tool_has_axis(tool,
							 LIBINPUT_TABLET_AXIS_TILT_Y));
		}

		libinput_event_destroy(event);
	}

	litest_delete_device(bamboo);
	litest_delete_device(intuos);
	libinput_unref(li);
}
END_TEST

START_TEST(mouse_tool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet *tev;
	struct libinput_tool *tool;

	if (!libevdev_has_event_code(dev->evdev,
				    EV_KEY,
				    BTN_TOOL_MOUSE))
		return;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_PROXIMITY,
				      -1);
	event = libinput_get_event(li);
	tev = libinput_event_get_tablet_event(event);
	tool = libinput_event_tablet_get_tool(tev);
	ck_assert_notnull(tool);
	ck_assert_int_eq(libinput_tool_get_type(tool),
			 LIBINPUT_TOOL_MOUSE);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(mouse_buttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet *tev;
	struct libinput_tool *tool;
	int code;

	if (!libevdev_has_event_code(dev->evdev,
				    EV_KEY,
				    BTN_TOOL_MOUSE))
		return;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_event(dev, EV_ABS, ABS_MISC, 0x806); /* 5-button mouse tool_id */
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_PROXIMITY,
				      -1);
	event = libinput_get_event(li);
	tev = libinput_event_get_tablet_event(event);
	tool = libinput_event_tablet_get_tool(tev);
	ck_assert_notnull(tool);
	libinput_tool_ref(tool);

	libinput_event_destroy(event);

	for (code = BTN_LEFT; code <= BTN_TASK; code++) {
		bool has_button = libevdev_has_event_code(dev->evdev,
							  EV_KEY,
							  code);
		ck_assert_int_eq(!!has_button,
				 !!libinput_tool_has_button(tool, code));

		if (!has_button)
			continue;

		litest_event(dev, EV_KEY, code, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
		litest_event(dev, EV_KEY, code, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);

		litest_assert_tablet_button_event(li,
					  code,
					  LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_tablet_button_event(li,
					  code,
					  LIBINPUT_BUTTON_STATE_RELEASED);
	}

	libinput_tool_unref(tool);
}
END_TEST

START_TEST(mouse_rotation)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet *tev;
	int angle;
	int tilt_center_x, tilt_center_y;
	const struct input_absinfo *abs;
	double val, old_val = 0;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_TILT_X, 0 },
		{ ABS_TILT_Y, 0 },
		{ -1, -1 }
	};

	if (!libevdev_has_event_code(dev->evdev,
				    EV_KEY,
				    BTN_TOOL_MOUSE))
		return;

	abs = libevdev_get_abs_info(dev->evdev, ABS_TILT_X);
	ck_assert_notnull(abs);
	tilt_center_x = (abs->maximum - abs->minimum + 1) / 2;

	abs = libevdev_get_abs_info(dev->evdev, ABS_TILT_Y);
	ck_assert_notnull(abs);
	tilt_center_y = (abs->maximum - abs->minimum + 1) / 2;

	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_pop_event_frame(dev);

	litest_drain_events(li);

	/* cos/sin are 90 degrees offset from the north-is-zero that
	   libinput uses. 175 is the CCW offset in the mouse HW */
	for (angle = 5; angle < 360; angle += 5) {
		double a = (angle - 90 - 175)/180.0 * M_PI;
		int x, y;

		x = cos(a) * 20 + tilt_center_x;
		y = sin(a) * 20 + tilt_center_y;

		litest_event(dev, EV_ABS, ABS_TILT_X, x);
		litest_event(dev, EV_ABS, ABS_TILT_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);

		litest_wait_for_event_of_type(li,
					      LIBINPUT_EVENT_TABLET_AXIS,
					      -1);
		event = libinput_get_event(li);
		tev = libinput_event_get_tablet_event(event);
		ck_assert(libinput_event_tablet_axis_has_changed(tev,
					 LIBINPUT_TABLET_AXIS_ROTATION_Z));
		val = libinput_event_tablet_get_axis_value(tev,
					 LIBINPUT_TABLET_AXIS_ROTATION_Z);

		/* rounding error galore, we can't test for anything more
		   precise than these */
		litest_assert_double_lt(val, 360.0);
		litest_assert_double_gt(val, old_val);
		litest_assert_double_lt(val, angle + 5);

		old_val = val;
		libinput_event_destroy(event);
		litest_assert_empty_queue(li);
	}
}
END_TEST

START_TEST(airbrush_tool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet *tev;
	struct libinput_tool *tool;

	if (!libevdev_has_event_code(dev->evdev,
				    EV_KEY,
				    BTN_TOOL_AIRBRUSH))
		return;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_AIRBRUSH, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_PROXIMITY,
				      -1);
	event = libinput_get_event(li);
	tev = libinput_event_get_tablet_event(event);
	tool = libinput_event_tablet_get_tool(tev);
	ck_assert_notnull(tool);
	ck_assert_int_eq(libinput_tool_get_type(tool),
			 LIBINPUT_TOOL_AIRBRUSH);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(airbrush_wheel)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet *tev;
	const struct input_absinfo *abs;
	double val;
	double scale;
	int v;

	if (!libevdev_has_event_code(dev->evdev,
				    EV_KEY,
				    BTN_TOOL_AIRBRUSH))
		return;

	litest_drain_events(li);

	abs = libevdev_get_abs_info(dev->evdev, ABS_WHEEL);
	ck_assert_notnull(abs);

	litest_event(dev, EV_KEY, BTN_TOOL_AIRBRUSH, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	/* start with non-zero */
	litest_event(dev, EV_ABS, ABS_WHEEL, 10);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_drain_events(li);

	scale = abs->maximum - abs->minimum;
	for (v = abs->minimum; v < abs->maximum; v += 8) {
		litest_event(dev, EV_ABS, ABS_WHEEL, v);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);

		litest_wait_for_event_of_type(li,
					      LIBINPUT_EVENT_TABLET_AXIS,
					      -1);
		event = libinput_get_event(li);
		tev = libinput_event_get_tablet_event(event);
		ck_assert(libinput_event_tablet_axis_has_changed(tev,
					 LIBINPUT_TABLET_AXIS_SLIDER));
		val = libinput_event_tablet_get_axis_value(tev,
					 LIBINPUT_TABLET_AXIS_SLIDER);

		ck_assert_int_eq(val, (v - abs->minimum)/scale);
		libinput_event_destroy(event);
		litest_assert_empty_queue(li);
	}
}
END_TEST

START_TEST(artpen_tool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet *tev;
	struct libinput_tool *tool;

	if (!libevdev_has_event_code(dev->evdev,
				    EV_ABS,
				    ABS_Z))
		return;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_ABS, ABS_MISC, 0x804); /* Art Pen */
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_PROXIMITY,
				      -1);
	event = libinput_get_event(li);
	tev = libinput_event_get_tablet_event(event);
	tool = libinput_event_tablet_get_tool(tev);
	ck_assert_notnull(tool);
	ck_assert_int_eq(libinput_tool_get_type(tool),
			 LIBINPUT_TOOL_PEN);
	ck_assert(libinput_tool_has_axis(tool,
					 LIBINPUT_TABLET_AXIS_ROTATION_Z));

	libinput_event_destroy(event);
}
END_TEST

START_TEST(artpen_rotation)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet *tev;
	const struct input_absinfo *abs;
	double val;
	double scale;
	int angle;

	if (!libevdev_has_event_code(dev->evdev,
				    EV_ABS,
				    ABS_Z))
		return;

	litest_drain_events(li);

	abs = libevdev_get_abs_info(dev->evdev, ABS_Z);
	ck_assert_notnull(abs);
	scale = (abs->maximum - abs->minimum + 1)/360.0;

	litest_event(dev, EV_KEY, BTN_TOOL_BRUSH, 1);
	litest_event(dev, EV_ABS, ABS_MISC, 0x804); /* Art Pen */
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_event(dev, EV_ABS, ABS_Z, abs->minimum);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_drain_events(li);

	for (angle = 8; angle < 360; angle += 8) {
		int a = angle * scale + abs->minimum;

		litest_event(dev, EV_ABS, ABS_Z, a);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);

		litest_wait_for_event_of_type(li,
					      LIBINPUT_EVENT_TABLET_AXIS,
					      -1);
		event = libinput_get_event(li);
		tev = libinput_event_get_tablet_event(event);
		ck_assert(libinput_event_tablet_axis_has_changed(tev,
					 LIBINPUT_TABLET_AXIS_ROTATION_Z));
		val = libinput_event_tablet_get_axis_value(tev,
					 LIBINPUT_TABLET_AXIS_ROTATION_Z);

		/* artpen has a 90 deg offset cw */
		ck_assert_int_eq(round(val), (angle + 90) % 360);

		val = libinput_event_tablet_get_axis_delta(tev,
					 LIBINPUT_TABLET_AXIS_ROTATION_Z);
		ck_assert_int_eq(val, 8);

		libinput_event_destroy(event);
		litest_assert_empty_queue(li);

	}
}
END_TEST

int
main(int argc, char **argv)
{
	litest_add("tablet:tool", tool_ref, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add_no_device("tablet:tool", tool_capabilities);
	litest_add("tablet:tool_serial", tool_serial, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add("tablet:tool_serial", serial_changes_tool, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add("tablet:tool_serial", invalid_serials, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add_no_device("tablet:tool_serial", tools_with_serials);
	litest_add_no_device("tablet:tool_serial", tools_without_serials);
	litest_add("tablet:proximity", proximity_out_clear_buttons, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:proximity", proximity_in_out, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:proximity", proximity_has_axes, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:proximity", bad_distance_events, LITEST_TABLET | LITEST_DISTANCE, LITEST_ANY);
	litest_add("tablet:motion", motion, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:motion", motion_delta, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:motion", motion_event_state, LITEST_TABLET, LITEST_ANY);
	litest_add_for_device("tablet:left_handed", left_handed, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:left_handed", no_left_handed, LITEST_WACOM_CINTIQ);
	litest_add("tablet:normalization", normalization, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:pad", pad_buttons_ignored, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:mouse", mouse_tool, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:mouse", mouse_buttons, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:mouse", mouse_rotation, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:airbrush", airbrush_tool, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:airbrush", airbrush_wheel, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:artpen", artpen_tool, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:artpen", artpen_rotation, LITEST_TABLET, LITEST_ANY);

	return litest_run(argc, argv);
}
