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

#include <stdio.h>
#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <math.h>
#include <unistd.h>

#include "libinput-util.h"
#include "litest.h"

static void
test_relative_event(struct litest_device *dev, int dx, int dy)
{
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	double ev_dx, ev_dy;
	double expected_dir;
	double expected_length;
	double actual_dir;
	double actual_length;

	/* Send two deltas, as the first one may be eaten up by an
	 * acceleration filter. */
	litest_event(dev, EV_REL, REL_X, dx);
	litest_event(dev, EV_REL, REL_Y, dy);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_REL, REL_X, dx);
	litest_event(dev, EV_REL, REL_Y, dy);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert(event != NULL);
	ck_assert_int_eq(libinput_event_get_type(event), LIBINPUT_EVENT_POINTER_MOTION);

	ptrev = libinput_event_get_pointer_event(event);
	ck_assert(ptrev != NULL);

	expected_length = sqrt(4 * dx*dx + 4 * dy*dy);
	expected_dir = atan2(dx, dy);

	ev_dx = libinput_event_pointer_get_dx(ptrev);
	ev_dy = libinput_event_pointer_get_dy(ptrev);
	actual_length = sqrt(ev_dx*ev_dx + ev_dy*ev_dy);
	actual_dir = atan2(ev_dx, ev_dy);

	/* Check the length of the motion vector (tolerate 1.0 indifference). */
	ck_assert(fabs(expected_length) >= actual_length);

	/* Check the direction of the motion vector (tolerate 2π/4 radians
	 * indifference). */
	ck_assert(fabs(expected_dir - actual_dir) < M_PI_2);

	libinput_event_destroy(event);

	litest_drain_events(dev->libinput);
}

START_TEST(pointer_motion_relative)
{
	struct litest_device *dev = litest_current_device();

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

static void
test_button_event(struct litest_device *dev, unsigned int button, int state)
{
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	litest_event(dev, EV_KEY, button, state);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert(event != NULL);
	ck_assert_int_eq(libinput_event_get_type(event), LIBINPUT_EVENT_POINTER_BUTTON);

	ptrev = libinput_event_get_pointer_event(event);
	ck_assert(ptrev != NULL);
	ck_assert_int_eq(libinput_event_pointer_get_button(ptrev), button);
	ck_assert_int_eq(libinput_event_pointer_get_button_state(ptrev),
			 state ?
				LIBINPUT_BUTTON_STATE_PRESSED :
				LIBINPUT_BUTTON_STATE_RELEASED);
	libinput_event_destroy(event);
}

START_TEST(pointer_button)
{
	struct litest_device *dev = litest_current_device();

	litest_drain_events(dev->libinput);

	test_button_event(dev, BTN_LEFT, 1);
	test_button_event(dev, BTN_LEFT, 0);

	/* press it twice for good measure */
	test_button_event(dev, BTN_LEFT, 1);
	test_button_event(dev, BTN_LEFT, 0);

	if (libevdev_has_event_code(dev->evdev, EV_KEY, BTN_RIGHT)) {
		test_button_event(dev, BTN_RIGHT, 1);
		test_button_event(dev, BTN_RIGHT, 0);
	}

	if (libevdev_has_event_code(dev->evdev, EV_KEY, BTN_MIDDLE)) {
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

static void
test_wheel_event(struct litest_device *dev, int which, int amount)
{
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	/* the current evdev implementation scales the scroll wheel events
	   up by a factor 10 */
	const int scroll_step = 10;
	int expected = amount * scroll_step;

	/* mouse scroll wheels are 'upside down' */
	if (which == REL_WHEEL)
		amount *= -1;
	litest_event(dev, EV_REL, which, amount);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert(event != NULL);
	ck_assert_int_eq(libinput_event_get_type(event),
			  LIBINPUT_EVENT_POINTER_AXIS);

	ptrev = libinput_event_get_pointer_event(event);
	ck_assert(ptrev != NULL);
	ck_assert_int_eq(libinput_event_pointer_get_axis(ptrev),
			 which == REL_WHEEL ?
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL :
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
	ck_assert_int_eq(libinput_event_pointer_get_axis_value(ptrev), expected);
	libinput_event_destroy(event);
}

START_TEST(pointer_scroll_wheel)
{
	struct litest_device *dev = litest_current_device();

	litest_drain_events(dev->libinput);

	test_wheel_event(dev, REL_WHEEL, -1);
	test_wheel_event(dev, REL_WHEEL, 1);

	test_wheel_event(dev, REL_WHEEL, -5);
	test_wheel_event(dev, REL_WHEEL, 6);

	if (libevdev_has_event_code(dev->evdev, EV_REL, REL_HWHEEL)) {
		test_wheel_event(dev, REL_HWHEEL, -1);
		test_wheel_event(dev, REL_HWHEEL, 1);

		test_wheel_event(dev, REL_HWHEEL, -5);
		test_wheel_event(dev, REL_HWHEEL, 6);
	}
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
	int seat_button_count;
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
		litest_button_click(devices[i], BTN_LEFT, true);

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
		litest_button_click(devices[i], BTN_LEFT, false);

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

int main (int argc, char **argv) {

	litest_add("pointer:motion", pointer_motion_relative, LITEST_POINTER, LITEST_ANY);
	litest_add("pointer:button", pointer_button, LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add_no_device("pointer:button_auto_release", pointer_button_auto_release);
	litest_add("pointer:scroll", pointer_scroll_wheel, LITEST_WHEEL, LITEST_ANY);
	litest_add_no_device("pointer:seat button count", pointer_seat_button_count);

	litest_add("pointer:calibration", pointer_no_calibration, LITEST_ANY, LITEST_TOUCH|LITEST_SINGLE_TOUCH);

	return litest_run(argc, argv);
}
