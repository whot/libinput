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
enable_edge_scroll(struct litest_device *dev)
{
	enum libinput_config_status status, expected;
	struct libinput_device *device = dev->libinput_device;

	status = libinput_device_config_scroll_set_method(device,
					  LIBINPUT_CONFIG_SCROLL_EDGE);

	expected = LIBINPUT_CONFIG_STATUS_SUCCESS;
	litest_assert_int_eq(status, expected);
}

static void
enable_clickfinger(struct litest_device *dev)
{
	enum libinput_config_status status, expected;
	struct libinput_device *device = dev->libinput_device;

	status = libinput_device_config_click_set_method(device,
				 LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
	expected = LIBINPUT_CONFIG_STATUS_SUCCESS;
	litest_assert_int_eq(status, expected);
}

static void
enable_buttonareas(struct litest_device *dev)
{
	enum libinput_config_status status, expected;
	struct libinput_device *device = dev->libinput_device;

	status = libinput_device_config_click_set_method(device,
				 LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
	expected = LIBINPUT_CONFIG_STATUS_SUCCESS;
	litest_assert_int_eq(status, expected);
}

START_TEST(touchpad_1fg_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 50, 5, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert(event != NULL);

	while (event) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);

		ptrev = libinput_event_get_pointer_event(event);
		ck_assert_int_ge(libinput_event_pointer_get_dx(ptrev), 0);
		ck_assert_int_eq(libinput_event_pointer_get_dy(ptrev), 0);
		libinput_event_destroy(event);
		event = libinput_get_event(li);
	}
}
END_TEST

START_TEST(touchpad_2fg_no_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_DISABLED);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 20);
	litest_touch_down(dev, 1, 70, 20);
	litest_touch_move_to(dev, 0, 20, 20, 80, 80, 5, 0);
	litest_touch_move_to(dev, 1, 70, 20, 80, 50, 5, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	while (event) {
		ck_assert_int_ne(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);
		libinput_event_destroy(event);
		event = libinput_get_event(li);
	}
}
END_TEST

START_TEST(touchpad_click_defaults_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	uint32_t methods, method;
	enum libinput_config_status status;

	/* call this test for apple touchpads */

	methods = libinput_device_config_click_get_methods(device);
	ck_assert(methods & LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
	ck_assert(methods & LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);

	method = libinput_device_config_click_get_method(device);
	ck_assert_int_eq(method, LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
	method = libinput_device_config_click_get_default_method(device);
	ck_assert_int_eq(method, LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);

	status = libinput_device_config_click_set_method(device,
							 LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_click_set_method(device,
							 LIBINPUT_CONFIG_CLICK_METHOD_NONE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
}
END_TEST

START_TEST(touchpad_click_defaults_btnarea)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	uint32_t methods, method;
	enum libinput_config_status status;

	/* call this test for non-apple clickpads */

	methods = libinput_device_config_click_get_methods(device);
	ck_assert(methods & LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
	ck_assert(methods & LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);

	method = libinput_device_config_click_get_method(device);
	ck_assert_int_eq(method,  LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
	method = libinput_device_config_click_get_default_method(device);
	ck_assert_int_eq(method,  LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);

	status = libinput_device_config_click_set_method(device,
							 LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_click_set_method(device,
							 LIBINPUT_CONFIG_CLICK_METHOD_NONE);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
}
END_TEST

START_TEST(touchpad_click_defaults_none)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	uint32_t methods, method;
	enum libinput_config_status status;

	/* call this test for non-clickpads */

	methods = libinput_device_config_click_get_methods(device);
	ck_assert_int_eq(methods, 0);

	method = libinput_device_config_click_get_method(device);
	ck_assert_int_eq(method, LIBINPUT_CONFIG_CLICK_METHOD_NONE);
	method = libinput_device_config_click_get_default_method(device);
	ck_assert_int_eq(method, LIBINPUT_CONFIG_CLICK_METHOD_NONE);

	status = libinput_device_config_click_set_method(device,
							 LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = libinput_device_config_click_set_method(device,
							 LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
}
END_TEST

START_TEST(touchpad_1fg_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_1fg_clickfinger_no_touch)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	enable_clickfinger(dev);

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_2fg_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 70);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_3fg_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (libevdev_get_num_slots(dev->evdev) < 3)
		return;

	enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 60, 70);
	litest_touch_down(dev, 2, 70, 70);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 2);

	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_3fg_clickfinger_btntool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (libevdev_get_num_slots(dev->evdev) >= 3 ||
	    !libevdev_has_event_code(dev->evdev, EV_KEY, BTN_TOOL_TRIPLETAP))
		return;

	enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 60, 70);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_4fg_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (libevdev_get_num_slots(dev->evdev) < 4)
		return;

	enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 60, 70);
	litest_touch_down(dev, 2, 70, 70);
	litest_touch_down(dev, 3, 80, 70);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 2);
	litest_touch_up(dev, 3);

	libinput_dispatch(li);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_4fg_clickfinger_btntool_2slots)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (libevdev_get_num_slots(dev->evdev) >= 3 ||
	    !libevdev_has_event_code(dev->evdev, EV_KEY, BTN_TOOL_QUADTAP))
		return;

	enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 60, 70);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_QUADTAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_KEY, BTN_TOOL_QUADTAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	libinput_dispatch(li);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_4fg_clickfinger_btntool_3slots)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (libevdev_get_num_slots(dev->evdev) >= 4 ||
	    !libevdev_has_event_code(dev->evdev, EV_KEY, BTN_TOOL_TRIPLETAP))
		return;

	enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 60, 70);
	litest_touch_down(dev, 2, 70, 70);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_QUADTAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
	litest_event(dev, EV_KEY, BTN_TOOL_QUADTAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 2);

	libinput_dispatch(li);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_clickfinger_distance)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	double w, h;
	bool small_touchpad = false;
	unsigned int expected_button;

	if (libinput_device_get_size(dev->libinput_device, &w, &h) == 0 &&
	    h < 50.0)
		small_touchpad = true;

	enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 90, 50);
	litest_touch_down(dev, 1, 10, 50);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 50, 5);
	litest_touch_down(dev, 1, 50, 95);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	/* if the touchpad is small enough, we expect all fingers to count
	 * for clickfinger */
	if (small_touchpad)
		expected_button = BTN_RIGHT;
	else
		expected_button = BTN_LEFT;

	litest_assert_button_event(li,
				   expected_button,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   expected_button,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_2fg_clickfinger_bottom)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	/* this test is run for the T440s touchpad only, makes getting the
	 * mm correct easier */

	libinput_device_config_click_set_method(dev->libinput_device,
						LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
	litest_drain_events(li);

	/* one above, one below the magic line, vert spread ca 27mm */
	litest_touch_down(dev, 0, 40, 60);
	litest_touch_down(dev, 1, 60, 100);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);

	/* both below the magic line */
	litest_touch_down(dev, 0, 40, 100);
	litest_touch_down(dev, 1, 60, 95);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	/* one above, one below the magic line, vert spread 17mm */
	litest_touch_down(dev, 0, 50, 75);
	litest_touch_down(dev, 1, 55, 100);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_clickfinger_to_area_method)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	enable_buttonareas(dev);

	litest_touch_down(dev, 0, 95, 95);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	enable_clickfinger(dev);

	litest_drain_events(li);

	/* use bottom right corner to catch accidental softbutton right */
	litest_touch_down(dev, 0, 95, 95);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

}
END_TEST

START_TEST(touchpad_clickfinger_to_area_method_while_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	enable_buttonareas(dev);

	litest_touch_down(dev, 0, 95, 95);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	enable_clickfinger(dev);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_drain_events(li);

	/* use bottom right corner to catch accidental softbutton right */
	litest_touch_down(dev, 0, 95, 95);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

}
END_TEST

START_TEST(touchpad_area_to_clickfinger_method)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	enable_clickfinger(dev);

	litest_drain_events(li);

	/* use bottom right corner to catch accidental softbutton right */
	litest_touch_down(dev, 0, 95, 95);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	enable_buttonareas(dev);

	litest_touch_down(dev, 0, 95, 95);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

}
END_TEST

START_TEST(touchpad_area_to_clickfinger_method_while_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	enable_clickfinger(dev);

	litest_drain_events(li);

	/* use bottom right corner to catch accidental softbutton right */
	litest_touch_down(dev, 0, 95, 95);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	enable_buttonareas(dev);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_touch_down(dev, 0, 95, 95);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

}
END_TEST

START_TEST(touchpad_btn_left)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(clickpad_btn_left)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	enable_buttonareas(dev);

	litest_drain_events(li);

	/* A clickpad always needs a finger down to tell where the
	   click happens */
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	ck_assert_int_eq(libinput_next_event_type(li), LIBINPUT_EVENT_NONE);
}
END_TEST

START_TEST(clickpad_click_n_drag)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_dispatch(li);
	ck_assert_int_eq(libinput_next_event_type(li), LIBINPUT_EVENT_NONE);

	/* now put a second finger down */
	litest_touch_down(dev, 1, 70, 70);
	litest_touch_move_to(dev, 1, 70, 70, 80, 50, 5, 0);
	litest_touch_up(dev, 1);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(clickpad_finger_pin)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libevdev *evdev = dev->evdev;
	const struct input_absinfo *abs;

	abs = libevdev_get_abs_info(evdev, ABS_MT_POSITION_X);
	ck_assert_notnull(abs);
	if (abs->resolution == 0)
		return;

	litest_drain_events(li);

	/* make sure the movement generates pointer events when
	   not pinned */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 52, 52, 10, 1);
	litest_touch_move_to(dev, 0, 52, 52, 48, 48, 10, 1);
	litest_touch_move_to(dev, 0, 48, 48, 50, 50, 10, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_button_click(dev, BTN_LEFT, true);
	litest_drain_events(li);

	litest_touch_move_to(dev, 0, 50, 50, 51, 51, 10, 1);
	litest_touch_move_to(dev, 0, 51, 51, 49, 49, 10, 1);
	litest_touch_move_to(dev, 0, 49, 49, 50, 50, 10, 1);

	litest_assert_empty_queue(li);

	litest_button_click(dev, BTN_LEFT, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	/* still pinned after release */
	litest_touch_move_to(dev, 0, 50, 50, 51, 51, 10, 1);
	litest_touch_move_to(dev, 0, 51, 51, 49, 49, 10, 1);
	litest_touch_move_to(dev, 0, 49, 49, 50, 50, 10, 1);

	litest_assert_empty_queue(li);

	/* move to unpin */
	litest_touch_move_to(dev, 0, 50, 50, 70, 70, 10, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(clickpad_softbutton_left)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 10, 90);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	libinput_dispatch(li);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_softbutton_right)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 90, 90);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_PRESSED);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_RELEASED);

	libinput_dispatch(li);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_softbutton_left_tap_n_drag)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	/* Tap in left button area, then finger down, button click
		-> expect left button press/release and left button press
	   Release button, finger up
		-> expect right button release
	 */
	litest_touch_down(dev, 0, 20, 90);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 20, 90);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li,
				   BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_softbutton_right_tap_n_drag)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	/* Tap in right button area, then finger down, button click
		-> expect left button press/release and right button press
	   Release button, finger up
		-> expect right button release
	 */
	litest_touch_down(dev, 0, 90, 90);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 90, 90);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_softbutton_left_1st_fg_move)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	double x = 0, y = 0;
	int nevents = 0;

	litest_drain_events(li);

	/* One finger down in the left button area, button press
		-> expect a button event
	   Move finger up out of the area, wait for timeout
	   Move finger around diagonally down left
		-> expect motion events down left
	   Release finger
		-> expect a button event */

	/* finger down, press in left button */
	litest_touch_down(dev, 0, 20, 90);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	/* move out of the area, then wait for softbutton timer */
	litest_touch_move_to(dev, 0, 20, 90, 90, 20, 10, 0);
	libinput_dispatch(li);
	litest_timeout_softbuttons();
	libinput_dispatch(li);
	litest_drain_events(li);

	/* move down left, expect motion */
	litest_touch_move_to(dev, 0, 90, 20, 20, 90, 10, 0);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event != NULL);
	while (event) {
		struct libinput_event_pointer *p;

		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);
		p = libinput_event_get_pointer_event(event);

		/* we moved up/right, now down/left so the pointer accel
		   code may lag behind with the dx/dy vectors. Hence, add up
		   the x/y movements and expect that on average we moved
		   left and down */
		x += libinput_event_pointer_get_dx(p);
		y += libinput_event_pointer_get_dy(p);
		nevents++;

		libinput_event_destroy(event);
		libinput_dispatch(li);
		event = libinput_get_event(li);
	}

	ck_assert(x/nevents < 0);
	ck_assert(y/nevents > 0);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_softbutton_left_2nd_fg_move)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	litest_drain_events(li);

	/* One finger down in the left button area, button press
		-> expect a button event
	   Put a second finger down in the area, move it right, release
		-> expect motion events right
	   Put a second finger down in the area, move it down, release
		-> expect motion events down
	   Release second finger, release first finger
		-> expect a button event */
	litest_touch_down(dev, 0, 20, 90);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 1, 20, 20);
	litest_touch_move_to(dev, 1, 20, 20, 80, 20, 10, 0);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event != NULL);
	while (event) {
		struct libinput_event_pointer *p;
		double x, y;

		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);
		p = libinput_event_get_pointer_event(event);

		x = libinput_event_pointer_get_dx(p);
		y = libinput_event_pointer_get_dy(p);

		/* Ignore events only containing an unaccelerated motion
		 * vector. */
		if (x != 0 || y != 0) {
			ck_assert(x > 0);
			ck_assert(y == 0);
		}

		libinput_event_destroy(event);
		libinput_dispatch(li);
		event = libinput_get_event(li);
	}
	litest_touch_up(dev, 1);

	/* second finger down */
	litest_touch_down(dev, 1, 20, 20);
	litest_touch_move_to(dev, 1, 20, 20, 20, 80, 10, 0);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event != NULL);
	while (event) {
		struct libinput_event_pointer *p;
		double x, y;

		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);
		p = libinput_event_get_pointer_event(event);

		x = libinput_event_pointer_get_dx(p);
		y = libinput_event_pointer_get_dy(p);

		ck_assert(x == 0);
		ck_assert(y > 0);

		libinput_event_destroy(event);
		libinput_dispatch(li);
		event = libinput_get_event(li);
	}

	litest_touch_up(dev, 1);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_softbutton_left_to_right)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	/* One finger down in left software button area,
	   move to right button area immediately, click
		-> expect right button event
	*/

	litest_touch_down(dev, 0, 20, 90);
	litest_touch_move_to(dev, 0, 20, 90, 90, 90, 10, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_softbutton_right_to_left)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	/* One finger down in right software button area,
	   move to left button area immediately, click
		-> expect left button event
	*/

	litest_touch_down(dev, 0, 90, 90);
	litest_touch_move_to(dev, 0, 90, 90, 20, 90, 10, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_topsoftbuttons_left)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 10, 5);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_topsoftbuttons_right)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 90, 5);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_topsoftbuttons_middle)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 5);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_topsoftbuttons_move_out_ignore)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	/* Finger down in top button area, wait past enter timeout
	   Move into main area, wait past leave timeout
	   Click
	     -> expect no events
	 */

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 5);
	libinput_dispatch(li);
	litest_timeout_softbuttons();
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_touch_move_to(dev, 0, 50, 5, 80, 90, 20, 0);
	libinput_dispatch(li);
	litest_timeout_softbuttons();
	libinput_dispatch(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_topsoftbuttons_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	enable_clickfinger(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 90, 5);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 90, 5);
	litest_touch_down(dev, 1, 80, 5);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(clickpad_topsoftbuttons_clickfinger_dev_disabled)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct litest_device *trackpoint = litest_add_device(li,
							     LITEST_TRACKPOINT);

	libinput_device_config_send_events_set_mode(dev->libinput_device,
						    LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	enable_clickfinger(dev);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 90, 5);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 90, 5);
	litest_touch_down(dev, 1, 10, 5);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_delete_device(trackpoint);
}
END_TEST

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
	test_2fg_scroll(dev, 0.1, 0.1, 200);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_scroll_slow_distance)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	const struct input_absinfo *y;
	double y_move;

	/* We want to move > 5 mm. */
	y = libevdev_get_abs_info(dev->evdev, ABS_Y);
	if (y->resolution) {
		y_move = 7.0 * y->resolution /
					(y->maximum - y->minimum) * 100;
	} else {
		y_move = 20.0;
	}

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

	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 20);
	litest_touch_down(dev, 1, 30, 20);
	libinput_dispatch(li);
	litest_touch_move_to(dev, 1, 30, 20, 30, 70, 10, 5);

	litest_assert_empty_queue(li);

	litest_touch_move_to(dev, 0, 20, 20, 20, 70, 10, 5);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_2fg_scroll_return_to_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

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

START_TEST(touchpad_scroll_natural)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

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

START_TEST(touchpad_edge_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);
	enable_edge_scroll(dev);

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
	int y_movement = 30; /* in percent of height */

	/* account for different touchpad heights, let's move 100% on a 15mm
	   high touchpad, less on anything else. This number is picked at
	   random, we just want deltas less than 5.
	   */
	if (libinput_device_get_size(dev->libinput_device,
				     &width,
				     &height) != -1) {
		y_movement = 100 * 15/height;
	}

	litest_drain_events(li);
	enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 99, 20);
	libinput_dispatch(li);
	litest_timeout_edgescroll();
	libinput_dispatch(li);

	litest_touch_move_to(dev, 0, 99, 20, 99, 20 + y_movement, 100, 10);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert_notnull(event);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_POINTER_AXIS, -1);

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

START_TEST(touchpad_edge_scroll_no_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);
	enable_edge_scroll(dev);

	litest_touch_down(dev, 0, 99, 10);
	litest_touch_move_to(dev, 0, 99, 10, 99, 70, 10, 0);
	/* moving outside -> no motion event */
	litest_touch_move_to(dev, 0, 99, 70, 20, 80, 10, 0);
	/* moving down outside edge once scrolling had started -> scroll */
	litest_touch_move_to(dev, 0, 20, 80, 40, 99, 10, 0);
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
	enable_edge_scroll(dev);

	/* moving into the edge zone must not trigger scroll events */
	litest_touch_down(dev, 0, 20, 20);
	litest_touch_move_to(dev, 0, 20, 20, 99, 20, 10, 0);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 10, 0);
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
	enable_edge_scroll(dev);

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
	enable_edge_scroll(dev);

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

	enable_buttonareas(dev);
	enable_edge_scroll(dev);
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

	enable_buttonareas(dev);
	enable_edge_scroll(dev);
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

	enable_buttonareas(dev);
	enable_edge_scroll(dev);
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

	enable_clickfinger(dev);
	enable_edge_scroll(dev);
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

static int
touchpad_has_palm_detect_size(struct litest_device *dev)
{
	double width, height;
	unsigned int vendor;
	int rc;

	vendor = libinput_device_get_id_vendor(dev->libinput_device);
	if (vendor == VENDOR_ID_WACOM)
		return 0;
	if (vendor == VENDOR_ID_APPLE)
		return 1;

	rc = libinput_device_get_size(dev->libinput_device, &width, &height);

	return rc == 0 && width >= 70;
}

START_TEST(touchpad_palm_detect_at_edge)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to(dev, 0, 99, 50, 99, 70, 5, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 50);
	litest_touch_move_to(dev, 0, 5, 50, 5, 70, 5, 0);
	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_no_palm_detect_at_edge_for_edge_scrolling)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	enable_edge_scroll(dev);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to(dev, 0, 99, 50, 99, 70, 5, 0);
	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_palm_detect_at_bottom_corners)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	/* Run for non-clickpads only: make sure the bottom corners trigger
	   palm detection too */
	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 95);
	litest_touch_move_to(dev, 0, 99, 95, 99, 99, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 95);
	litest_touch_move_to(dev, 0, 5, 95, 5, 99, 5, 0);
	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_palm_detect_at_top_corners)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	/* Run for non-clickpads only: make sure the bottom corners trigger
	   palm detection too */
	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 5);
	litest_touch_move_to(dev, 0, 99, 5, 99, 9, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 5);
	litest_touch_move_to(dev, 0, 5, 5, 5, 9, 5, 0);
	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_palm_detect_palm_stays_palm)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 75, 99, 5, 0);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_palm_becomes_pointer)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to(dev, 0, 99, 50, 0, 70, 5, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_no_palm_moving_into_edges)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	/* moving non-palm into the edge does not label it as palm */
	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 99, 50, 5, 0);

	litest_drain_events(li);

	litest_touch_move_to(dev, 0, 99, 50, 99, 90, 5, 0);
	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 95, 5);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 5);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 90);
	litest_touch_up(dev, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 95, 90);
	litest_touch_up(dev, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_left_handed)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_button_click(dev, BTN_RIGHT, 1);
	litest_button_click(dev, BTN_RIGHT, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	if (libevdev_has_event_code(dev->evdev,
				    EV_KEY,
				    BTN_MIDDLE)) {
		litest_button_click(dev, BTN_MIDDLE, 1);
		litest_button_click(dev, BTN_MIDDLE, 0);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_assert_button_event(li,
					   BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);
	}
}
END_TEST

START_TEST(touchpad_left_handed_clickpad)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);
	litest_touch_down(dev, 0, 10, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_drain_events(li);
	litest_touch_down(dev, 0, 90, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_drain_events(li);
	litest_touch_down(dev, 0, 50, 50);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_left_handed_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);
	litest_touch_down(dev, 0, 10, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	/* Clickfinger is unaffected by left-handed setting */
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_drain_events(li);
	litest_touch_down(dev, 0, 10, 90);
	litest_touch_down(dev, 1, 30, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_left_handed_tapping)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	litest_enable_tap(dev->libinput_device);

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);

	/* Tapping is unaffected by left-handed setting */
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_left_handed_tapping_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	litest_enable_tap(dev->libinput_device);

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);

	/* Tapping is unaffected by left-handed setting */
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_left_handed_delayed)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	litest_drain_events(li);
	litest_button_click(dev, BTN_LEFT, 1);
	libinput_dispatch(li);

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_button_click(dev, BTN_LEFT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	/* left-handed takes effect now */
	litest_button_click(dev, BTN_RIGHT, 1);
	libinput_dispatch(li);
	litest_timeout_middlebutton();
	libinput_dispatch(li);
	litest_button_click(dev, BTN_LEFT, 1);
	libinput_dispatch(li);

	status = libinput_device_config_left_handed_set(d, 0);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_button_click(dev, BTN_RIGHT, 0);
	litest_button_click(dev, BTN_LEFT, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

START_TEST(touchpad_left_handed_clickpad_delayed)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	litest_drain_events(li);
	litest_touch_down(dev, 0, 10, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	libinput_dispatch(li);

	status = libinput_device_config_left_handed_set(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	/* left-handed takes effect now */
	litest_drain_events(li);
	litest_touch_down(dev, 0, 90, 90);
	litest_button_click(dev, BTN_LEFT, 1);
	libinput_dispatch(li);

	status = libinput_device_config_left_handed_set(d, 0);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_button_click(dev, BTN_LEFT, 0);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
}
END_TEST

static void
hover_continue(struct litest_device *dev, unsigned int slot,
	       int x, int y)
{
	litest_event(dev, EV_ABS, ABS_MT_SLOT, slot);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
	litest_event(dev, EV_ABS, ABS_X, x);
	litest_event(dev, EV_ABS, ABS_Y, y);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 10);
	litest_event(dev, EV_ABS, ABS_TOOL_WIDTH, 6);
	/* WARNING: no SYN_REPORT! */
}

static void
hover_start(struct litest_device *dev, unsigned int slot,
	    int x, int y)
{
	static unsigned int tracking_id;

	litest_event(dev, EV_ABS, ABS_MT_SLOT, slot);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, ++tracking_id);
	hover_continue(dev, slot, x, y);
	/* WARNING: no SYN_REPORT! */
}

START_TEST(touchpad_semi_mt_hover_noevent)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;
	int x = 2400,
	    y = 2400;

	litest_drain_events(li);

	hover_start(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (i = 0; i < 10; i++) {
		x += 200;
		y -= 200;
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_semi_mt_hover_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int i;
	int x = 2400,
	    y = 2400;

	litest_drain_events(li);

	hover_start(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (i = 0; i < 10; i++) {
		x += 200;
		y -= 200;
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	litest_assert_empty_queue(li);

	litest_event(dev, EV_ABS, ABS_X, x + 100);
	litest_event(dev, EV_ABS, ABS_Y, y + 100);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	for (i = 0; i < 10; i++) {
		x -= 200;
		y += 200;
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	libinput_dispatch(li);

	ck_assert_int_ne(libinput_next_event_type(li),
			 LIBINPUT_EVENT_NONE);
	while ((event = libinput_get_event(li)) != NULL) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}

	/* go back to hover */
	hover_continue(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (i = 0; i < 10; i++) {
		x += 200;
		y -= 200;
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_semi_mt_hover_down_hover_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int i, j;
	int x = 1400,
	    y = 1400;

	litest_drain_events(li);

	/* hover */
	hover_start(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_empty_queue(li);

	for (i = 0; i < 3; i++) {
		/* touch */
		litest_event(dev, EV_ABS, ABS_X, x + 100);
		litest_event(dev, EV_ABS, ABS_Y, y + 100);
		litest_event(dev, EV_KEY, BTN_TOUCH, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);

		for (j = 0; j < 5; j++) {
			x += 200;
			y += 200;
			litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
			litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
			litest_event(dev, EV_ABS, ABS_X, x);
			litest_event(dev, EV_ABS, ABS_Y, y);
			litest_event(dev, EV_SYN, SYN_REPORT, 0);
		}

		libinput_dispatch(li);

		ck_assert_int_ne(libinput_next_event_type(li),
				 LIBINPUT_EVENT_NONE);
		while ((event = libinput_get_event(li)) != NULL) {
			ck_assert_int_eq(libinput_event_get_type(event),
					 LIBINPUT_EVENT_POINTER_MOTION);
			libinput_event_destroy(event);
			libinput_dispatch(li);
		}

		/* go back to hover */
		hover_continue(dev, 0, x, y);
		litest_event(dev, EV_KEY, BTN_TOUCH, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);

		for (j = 0; j < 5; j++) {
			x += 200;
			y += 200;
			litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
			litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
			litest_event(dev, EV_ABS, ABS_X, x);
			litest_event(dev, EV_ABS, ABS_Y, y);
			litest_event(dev, EV_SYN, SYN_REPORT, 0);
		}

		litest_assert_empty_queue(li);
	}

	/* touch */
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);

	/* start a new touch to be sure */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 70, 70, 10, 10);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	ck_assert_int_ne(libinput_next_event_type(li),
			 LIBINPUT_EVENT_NONE);
	while ((event = libinput_get_event(li)) != NULL) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}
}
END_TEST

START_TEST(touchpad_semi_mt_hover_down_up)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;
	int x = 1400,
	    y = 1400;

	litest_drain_events(li);

	/* hover two fingers, then touch */
	hover_start(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_empty_queue(li);

	hover_start(dev, 1, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);

	/* hover first finger, end second in same frame */
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	/* now move the finger */
	for (i = 0; i < 10; i++) {
		litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		x -= 100;
		y -= 100;
	}

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
}
END_TEST

START_TEST(touchpad_semi_mt_hover_2fg_noevent)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;
	int x = 2400,
	    y = 2400;

	litest_drain_events(li);

	hover_start(dev, 0, x, y);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	hover_start(dev, 1, x + 500, y + 500);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (i = 0; i < 10; i++) {
		x += 200;
		y -= 200;
		litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x + 500);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y + 500);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_semi_mt_hover_2fg_1fg_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int i;
	int x = 2400,
	    y = 2400;

	litest_drain_events(li);

	/* two slots active, but BTN_TOOL_FINGER only */
	hover_start(dev, 0, x, y);
	hover_start(dev, 1, x + 500, y + 500);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (i = 0; i < 10; i++) {
		x += 200;
		y -= 200;
		litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
		litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_X, x + 500);
		litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, y + 500);
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, y);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}

	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	ck_assert_int_ne(libinput_next_event_type(li),
			 LIBINPUT_EVENT_NONE);
	while ((event = libinput_get_event(li)) != NULL) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_MOTION);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}
}
END_TEST

START_TEST(touchpad_hover_noevent)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_hover_start(dev, 0, 50, 50);
	litest_hover_move_to(dev, 0, 50, 50, 70, 70, 10, 10);
	litest_hover_end(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_hover_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	/* hover the finger */
	litest_hover_start(dev, 0, 50, 50);

	litest_hover_move_to(dev, 0, 50, 50, 70, 70, 10, 10);

	litest_assert_empty_queue(li);

	/* touch the finger on the sensor */
	litest_touch_move_to(dev, 0, 70, 70, 50, 50, 10, 10);

	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* go back to hover */
	litest_hover_move_to(dev, 0, 50, 50, 70, 70, 10, 10);
	litest_hover_end(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_hover_down_hover_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;

	litest_drain_events(li);

	litest_hover_start(dev, 0, 50, 50);

	for (i = 0; i < 3; i++) {

		/* hover the finger */
		litest_hover_move_to(dev, 0, 50, 50, 70, 70, 10, 10);

		litest_assert_empty_queue(li);

		/* touch the finger */
		litest_touch_move_to(dev, 0, 70, 70, 50, 50, 10, 10);

		libinput_dispatch(li);

		litest_assert_only_typed_events(li,
						LIBINPUT_EVENT_POINTER_MOTION);
	}

	litest_hover_end(dev, 0);

	/* start a new touch to be sure */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 70, 70, 10, 10);
	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_hover_down_up)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	/* hover two fingers, and a touch */
	litest_push_event_frame(dev);
	litest_hover_start(dev, 0, 50, 50);
	litest_hover_start(dev, 1, 50, 50);
	litest_touch_down(dev, 2, 50, 50);
	litest_pop_event_frame(dev);;

	litest_assert_empty_queue(li);

	/* hover first finger, end second and third in same frame */
	litest_push_event_frame(dev);
	litest_hover_move(dev, 0, 70, 70);
	litest_hover_end(dev, 1);
	litest_touch_up(dev, 2);
	litest_pop_event_frame(dev);;

	litest_assert_empty_queue(li);

	/* now move the finger */
	litest_touch_move_to(dev, 0, 50, 50, 70, 70, 10, 10);

	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

START_TEST(touchpad_hover_2fg_noevent)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	/* hover two fingers */
	litest_push_event_frame(dev);
	litest_hover_start(dev, 0, 25, 25);
	litest_hover_start(dev, 1, 50, 50);
	litest_pop_event_frame(dev);;

	litest_hover_move_two_touches(dev, 25, 25, 50, 50, 50, 50, 10, 0);

	litest_push_event_frame(dev);
	litest_hover_end(dev, 0);
	litest_hover_end(dev, 1);
	litest_pop_event_frame(dev);;

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_hover_2fg_1fg_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;

	litest_drain_events(li);

	/* hover two fingers */
	litest_push_event_frame(dev);
	litest_hover_start(dev, 0, 25, 25);
	litest_touch_down(dev, 1, 50, 50);
	litest_pop_event_frame(dev);;

	for (i = 0; i < 10; i++) {
		litest_push_event_frame(dev);
		litest_hover_move(dev, 0, 25 + 5 * i, 25 + 5 * i);
		litest_touch_move(dev, 1, 50 + 5 * i, 50 - 5 * i);
		litest_pop_event_frame(dev);;
	}

	litest_push_event_frame(dev);
	litest_hover_end(dev, 0);
	litest_touch_up(dev, 1);
	litest_pop_event_frame(dev);;

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
}
END_TEST

static void
assert_btnevent_from_device(struct litest_device *device,
			    unsigned int button,
			    enum libinput_button_state state)
{
	struct libinput *li = device->libinput;
	struct libinput_event *e;

	libinput_dispatch(li);
	e = libinput_get_event(li);
	litest_is_button_event(e, button, state);

	litest_assert_ptr_eq(libinput_event_get_device(e), device->libinput_device);
	libinput_event_destroy(e);
}

START_TEST(touchpad_trackpoint_buttons)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;

	const struct buttons {
		unsigned int device_value;
		unsigned int real_value;
	} buttons[] = {
		{ BTN_0, BTN_LEFT },
		{ BTN_1, BTN_RIGHT },
		{ BTN_2, BTN_MIDDLE },
	};
	const struct buttons *b;

	trackpoint = litest_add_device(li,
				       LITEST_TRACKPOINT);
	libinput_device_config_scroll_set_method(trackpoint->libinput_device,
					 LIBINPUT_CONFIG_SCROLL_NO_SCROLL);

	litest_drain_events(li);

	ARRAY_FOR_EACH(buttons, b) {
		litest_button_click(touchpad, b->device_value, true);
		assert_btnevent_from_device(trackpoint,
					    b->real_value,
					    LIBINPUT_BUTTON_STATE_PRESSED);

		litest_button_click(touchpad, b->device_value, false);

		assert_btnevent_from_device(trackpoint,
					    b->real_value,
					    LIBINPUT_BUTTON_STATE_RELEASED);
	}

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(touchpad_trackpoint_mb_scroll)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;

	trackpoint = litest_add_device(li,
				       LITEST_TRACKPOINT);

	litest_drain_events(li);
	litest_button_click(touchpad, BTN_2, true); /* middle */
	libinput_dispatch(li);
	litest_timeout_buttonscroll();
	libinput_dispatch(li);
	litest_event(trackpoint, EV_REL, REL_Y, -2);
	litest_event(trackpoint, EV_SYN, SYN_REPORT, 0);
	litest_event(trackpoint, EV_REL, REL_Y, -2);
	litest_event(trackpoint, EV_SYN, SYN_REPORT, 0);
	litest_event(trackpoint, EV_REL, REL_Y, -2);
	litest_event(trackpoint, EV_SYN, SYN_REPORT, 0);
	litest_event(trackpoint, EV_REL, REL_Y, -2);
	litest_event(trackpoint, EV_SYN, SYN_REPORT, 0);
	litest_button_click(touchpad, BTN_2, false);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(touchpad_trackpoint_mb_click)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;
	enum libinput_config_status status;

	trackpoint = litest_add_device(li,
				       LITEST_TRACKPOINT);
	status = libinput_device_config_scroll_set_method(
				  trackpoint->libinput_device,
				  LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);
	litest_button_click(touchpad, BTN_2, true); /* middle */
	litest_button_click(touchpad, BTN_2, false);

	assert_btnevent_from_device(trackpoint,
				    BTN_MIDDLE,
				    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_btnevent_from_device(trackpoint,
				    BTN_MIDDLE,
				    LIBINPUT_BUTTON_STATE_RELEASED);
	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(touchpad_trackpoint_buttons_softbuttons)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;

	trackpoint = litest_add_device(li,
				       LITEST_TRACKPOINT);

	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 95, 90);
	litest_button_click(touchpad, BTN_LEFT, true);
	litest_button_click(touchpad, BTN_1, true);
	litest_button_click(touchpad, BTN_LEFT, false);
	litest_touch_up(touchpad, 0);
	litest_button_click(touchpad, BTN_1, false);

	assert_btnevent_from_device(touchpad,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_btnevent_from_device(touchpad,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_RELEASED);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_touch_down(touchpad, 0, 95, 90);
	litest_button_click(touchpad, BTN_LEFT, true);
	litest_button_click(touchpad, BTN_1, true);
	litest_button_click(touchpad, BTN_1, false);
	litest_button_click(touchpad, BTN_LEFT, false);
	litest_touch_up(touchpad, 0);

	assert_btnevent_from_device(touchpad,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_RELEASED);
	assert_btnevent_from_device(touchpad,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(touchpad_trackpoint_buttons_2fg_scroll)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;
	struct libinput_event *e;
	struct libinput_event_pointer *pev;
	double val;

	trackpoint = litest_add_device(li,
				       LITEST_TRACKPOINT);

	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 49, 70);
	litest_touch_down(touchpad, 1, 51, 70);
	litest_touch_move_two_touches(touchpad, 49, 70, 51, 70, 0, -40, 10, 0);

	libinput_dispatch(li);
	litest_wait_for_event(li);

	/* Make sure we get scroll events but _not_ the scroll release */
	while ((e = libinput_get_event(li))) {
		ck_assert_int_eq(libinput_event_get_type(e),
				 LIBINPUT_EVENT_POINTER_AXIS);
		pev = libinput_event_get_pointer_event(e);
		val = libinput_event_pointer_get_axis_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		ck_assert(val != 0.0);
		libinput_event_destroy(e);
	}

	litest_button_click(touchpad, BTN_1, true);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_PRESSED);

	litest_touch_move_to(touchpad, 0, 40, 30, 40, 70, 10, 0);
	litest_touch_move_to(touchpad, 1, 60, 30, 60, 70, 10, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	while ((e = libinput_get_event(li))) {
		ck_assert_int_eq(libinput_event_get_type(e),
				 LIBINPUT_EVENT_POINTER_AXIS);
		pev = libinput_event_get_pointer_event(e);
		val = libinput_event_pointer_get_axis_value(pev,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		ck_assert(val != 0.0);
		libinput_event_destroy(e);
	}

	litest_button_click(touchpad, BTN_1, false);
	assert_btnevent_from_device(trackpoint,
				    BTN_RIGHT,
				    LIBINPUT_BUTTON_STATE_RELEASED);

	/* the movement lags behind the touch movement, so the first couple
	   events can be downwards even though we started scrolling up. do a
	   short scroll up, drain those events, then we can use
	   litest_assert_scroll() which tests for the trailing 0/0 scroll
	   for us.
	   */
	litest_touch_move_to(touchpad, 0, 40, 70, 40, 60, 10, 0);
	litest_touch_move_to(touchpad, 1, 60, 70, 60, 60, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
	litest_touch_move_to(touchpad, 0, 40, 60, 40, 30, 10, 0);
	litest_touch_move_to(touchpad, 1, 60, 60, 60, 30, 10, 0);

	litest_touch_up(touchpad, 0);
	litest_touch_up(touchpad, 1);

	libinput_dispatch(li);

	litest_assert_scroll(li,
			     LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
			     -1);

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(touchpad_trackpoint_no_trackpoint)
{
	struct litest_device *touchpad = litest_current_device();
	struct libinput *li = touchpad->libinput;

	litest_drain_events(li);
	litest_button_click(touchpad, BTN_0, true); /* left */
	litest_button_click(touchpad, BTN_0, false);
	litest_assert_empty_queue(li);

	litest_button_click(touchpad, BTN_1, true); /* right */
	litest_button_click(touchpad, BTN_1, false);
	litest_assert_empty_queue(li);

	litest_button_click(touchpad, BTN_2, true); /* middle */
	litest_button_click(touchpad, BTN_2, false);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_initial_state)
{
	struct litest_device *dev;
	struct libinput *libinput1, *libinput2;
	struct libinput_event *ev1, *ev2;
	struct libinput_event_pointer *p1, *p2;
	int axis = _i; /* looped test */
	int x = 40, y = 60;

	dev = litest_current_device();
	libinput1 = dev->libinput;

	litest_disable_tap(dev->libinput_device);

	litest_touch_down(dev, 0, x, y);
	litest_touch_up(dev, 0);

	/* device is now on some x/y value */
	litest_drain_events(libinput1);

	libinput2 = litest_create_context();
	libinput_path_add_device(libinput2,
				 libevdev_uinput_get_devnode(dev->uinput));
	litest_drain_events(libinput2);

	if (axis == ABS_X)
		x = 30;
	else
		y = 30;
	litest_touch_down(dev, 0, x, y);
	litest_touch_move_to(dev, 0, x, y, 80, 80, 10, 1);
	litest_touch_up(dev, 0);

	litest_wait_for_event(libinput1);
	litest_wait_for_event(libinput2);

	while (libinput_next_event_type(libinput1)) {
		ev1 = libinput_get_event(libinput1);
		ev2 = libinput_get_event(libinput2);

		p1 = litest_is_motion_event(ev1);
		p2 = litest_is_motion_event(ev2);

		ck_assert_int_eq(libinput_event_get_type(ev1),
				 libinput_event_get_type(ev2));

		ck_assert_int_eq(libinput_event_pointer_get_dx(p1),
				 libinput_event_pointer_get_dx(p2));
		ck_assert_int_eq(libinput_event_pointer_get_dy(p1),
				 libinput_event_pointer_get_dy(p2));
		libinput_event_destroy(ev1);
		libinput_event_destroy(ev2);
	}

	libinput_unref(libinput2);
}
END_TEST

static inline bool
has_disable_while_typing(struct litest_device *device)
{
	if (libevdev_get_id_vendor(device->evdev) == VENDOR_ID_WACOM)
		return false;
	if (libevdev_get_id_bustype(device->evdev) == BUS_BLUETOOTH)
		return false;

	return true;
}

START_TEST(touchpad_dwt)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* within timeout - no events */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_short();
	libinput_dispatch(li);

	/* after timeout  - motion events*/
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_enable_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* finger down after last key event, but
	   we're still within timeout - no events */
	msleep(10);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_short();
	libinput_dispatch(li);

	/* same touch after timeout  - motion events*/
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 50, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_touch_hold)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	msleep(1); /* make sure touch starts after key press */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* touch still down - no events */
	litest_keyboard_key(keyboard, KEY_A, false);
	libinput_dispatch(li);
	litest_touch_move_to(touchpad, 0, 70, 50, 30, 50, 5, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* touch still down - no events */
	litest_timeout_dwt_short();
	libinput_dispatch(li);
	litest_touch_move_to(touchpad, 0, 30, 50, 50, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_key_hold)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_type)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	int i;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	for (i = 0; i < 5; i++) {
		litest_keyboard_key(keyboard, KEY_A, true);
		litest_keyboard_key(keyboard, KEY_A, false);
		libinput_dispatch(li);
	}

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_long();
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_type_short_timeout)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	int i;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	for (i = 0; i < 5; i++) {
		litest_keyboard_key(keyboard, KEY_A, true);
		litest_keyboard_key(keyboard, KEY_A, false);
		libinput_dispatch(li);
	}

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_timeout_dwt_short();
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_tap)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_enable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_up(touchpad, 0);

	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_timeout_dwt_short();
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_tap_drag)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_enable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	msleep(1); /* make sure touch starts after key press */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_up(touchpad, 0);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);

	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_timeout_dwt_short();
	libinput_dispatch(li);
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_click)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_button_click(touchpad, BTN_LEFT, true);
	litest_button_click(touchpad, BTN_LEFT, false);
	libinput_dispatch(li);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	litest_keyboard_key(keyboard, KEY_A, false);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_edge_scroll)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	enable_edge_scroll(touchpad);

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 99, 20);
	libinput_dispatch(li);
	litest_timeout_edgescroll();
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	/* edge scroll timeout is 300ms atm, make sure we don't accidentally
	   exit the DWT timeout */
	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_move_to(touchpad, 0, 99, 20, 99, 80, 60, 10);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_touch_move_to(touchpad, 0, 99, 80, 99, 20, 60, 10);
	litest_touch_up(touchpad, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_edge_scroll_interrupt)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;
	struct libinput_event_pointer *stop_event;

	if (!has_disable_while_typing(touchpad))
		return;

	enable_edge_scroll(touchpad);

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 99, 20);
	libinput_dispatch(li);
	litest_timeout_edgescroll();
	litest_touch_move_to(touchpad, 0, 99, 20, 99, 30, 10, 10);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);

	/* scroll stop event */
	litest_wait_for_event(li);
	stop_event = litest_is_axis_event(libinput_get_event(li),
					  LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
					  LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
	libinput_event_destroy(libinput_event_pointer_get_base_event(stop_event));
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_timeout_dwt_long();

	/* Known bad behavior: a touch starting to edge-scroll before dwt
	 * kicks in will stop to scroll but be recognized as normal
	 * pointer-moving touch once the timeout expires. We'll fix that
	 * when we need to.
	 */
	litest_touch_move_to(touchpad, 0, 99, 30, 99, 80, 10, 5);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

static int
has_thumb_detect(struct litest_device *dev)
{
	double w, h;

	if (!libevdev_has_event_code(dev->evdev, EV_ABS, ABS_MT_PRESSURE))
		return 0;

	if (libinput_device_get_size(dev->libinput_device, &w, &h) != 0)
		return 0;

	return h >= 50.0;
}

START_TEST(touchpad_thumb_begin_no_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 100 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down_extended(dev, 0, 50, 50, axes);
	litest_touch_move_to(dev, 0, 50, 50, 80, 50, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_thumb_update_no_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 100 },
		{ -1, 0 }
	};

	litest_disable_tap(dev->libinput_device);

	if (!has_thumb_detect(dev))
		return;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 60, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_move_extended(dev, 0, 65, 50, axes);
	litest_touch_move_to(dev, 0, 65, 50, 80, 50, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_thumb_clickfinger)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 100 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	libinput_device_config_click_set_method(dev->libinput_device,
						LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 60, 50);
	litest_touch_move_extended(dev, 0, 55, 50, axes);
	litest_button_click(dev, BTN_LEFT, true);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(libinput_event_pointer_get_base_event(ptrev));

	litest_assert_empty_queue(li);

	litest_button_click(dev, BTN_LEFT, false);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 60, 50);
	litest_touch_move_extended(dev, 1, 65, 50, axes);
	litest_button_click(dev, BTN_LEFT, true);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(libinput_event_pointer_get_base_event(ptrev));

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_thumb_btnarea)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 100 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_disable_tap(dev->libinput_device);

	libinput_device_config_click_set_method(dev->libinput_device,
						LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 90, 95);
	litest_touch_move_extended(dev, 0, 95, 95, axes);
	litest_button_click(dev, BTN_LEFT, true);

	/* button areas work as usual with a thumb */

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_RIGHT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(libinput_event_pointer_get_base_event(ptrev));

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_thumb_edgescroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 100 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	enable_edge_scroll(dev);
	litest_disable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 30);
	litest_touch_move_to(dev, 0, 99, 30, 99, 50, 10, 0);
	litest_drain_events(li);

	litest_touch_move_extended(dev, 0, 99, 55, axes);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_touch_move_to(dev, 0, 99, 55, 99, 70, 10, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);
}
END_TEST

START_TEST(touchpad_thumb_tap_begin)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 100 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	/* touch down is a thumb */
	litest_touch_down_extended(dev, 0, 50, 50, axes);
	litest_touch_up(dev, 0);
	litest_timeout_tap();

	litest_assert_empty_queue(li);

	/* make sure normal tap still works */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);
}
END_TEST

START_TEST(touchpad_thumb_tap_touch)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 100 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	/* event after touch down is thumb */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_extended(dev, 0, 51, 50, axes);
	litest_touch_up(dev, 0);
	litest_timeout_tap();
	litest_assert_empty_queue(li);

	/* make sure normal tap still works */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);
}
END_TEST

START_TEST(touchpad_thumb_tap_hold)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 100 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	/* event in state HOLD is thumb */
	litest_touch_down(dev, 0, 50, 50);
	litest_timeout_tap();
	libinput_dispatch(li);
	litest_touch_move_extended(dev, 0, 51, 50, axes);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	/* make sure normal tap still works */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);
}
END_TEST

START_TEST(touchpad_thumb_tap_hold_2ndfg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 100 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	/* event in state HOLD is thumb */
	litest_touch_down(dev, 0, 50, 50);
	litest_timeout_tap();
	libinput_dispatch(li);
	litest_touch_move_extended(dev, 0, 51, 50, axes);

	litest_assert_empty_queue(li);

	/* one finger is a thumb, now get second finger down */
	litest_touch_down(dev, 1, 60, 50);
	litest_assert_empty_queue(li);

	/* release thumb */
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	/* timeout -> into HOLD, no event on release */
	litest_timeout_tap();
	libinput_dispatch(li);
	litest_touch_up(dev, 1);
	litest_assert_empty_queue(li);

	/* make sure normal tap still works */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);
}
END_TEST

START_TEST(touchpad_thumb_tap_hold_2ndfg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	struct axis_replacement axes[] = {
		{ ABS_MT_PRESSURE, 100 },
		{ -1, 0 }
	};

	if (!has_thumb_detect(dev))
		return;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(li);

	/* event in state HOLD is thumb */
	litest_touch_down(dev, 0, 50, 50);
	litest_timeout_tap();
	libinput_dispatch(li);
	litest_touch_move_extended(dev, 0, 51, 50, axes);

	litest_assert_empty_queue(li);

	/* one finger is a thumb, now get second finger down */
	litest_touch_down(dev, 1, 60, 50);
	litest_assert_empty_queue(li);

	/* release thumb */
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	/* release second finger, within timeout, ergo event */
	litest_touch_up(dev, 1);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(libinput_event_pointer_get_base_event(ptrev));

	litest_timeout_tap();
	libinput_dispatch(li);
	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_RELEASED);
	libinput_event_destroy(libinput_event_pointer_get_base_event(ptrev));

	/* make sure normal tap still works */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);
}
END_TEST

void
litest_setup_tests(void)
{
	struct range axis_range = {ABS_X, ABS_Y + 1};

	litest_add("touchpad:motion", touchpad_1fg_motion, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:motion", touchpad_2fg_no_motion, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);

	litest_add("touchpad:clickfinger", touchpad_1fg_clickfinger, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger", touchpad_1fg_clickfinger_no_touch, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger", touchpad_2fg_clickfinger, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger", touchpad_3fg_clickfinger, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger", touchpad_3fg_clickfinger_btntool, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger", touchpad_4fg_clickfinger, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger", touchpad_4fg_clickfinger_btntool_2slots, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger", touchpad_4fg_clickfinger_btntool_3slots, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger", touchpad_2fg_clickfinger_distance, LITEST_CLICKPAD, LITEST_ANY);
	litest_add_for_device("touchpad:clickfinger", touchpad_2fg_clickfinger_bottom, LITEST_SYNAPTICS_TOPBUTTONPAD);
	litest_add("touchpad:clickfinger", touchpad_clickfinger_to_area_method, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger",
		   touchpad_clickfinger_to_area_method_while_down, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger", touchpad_area_to_clickfinger_method, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:clickfinger",
		   touchpad_area_to_clickfinger_method_while_down, LITEST_CLICKPAD, LITEST_ANY);

	litest_add("touchpad:click", touchpad_click_defaults_clickfinger, LITEST_APPLE_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:click", touchpad_click_defaults_btnarea, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:click", touchpad_click_defaults_none, LITEST_TOUCHPAD, LITEST_CLICKPAD);

	litest_add("touchpad:click", touchpad_btn_left, LITEST_TOUCHPAD|LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("touchpad:click", clickpad_btn_left, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:click", clickpad_click_n_drag, LITEST_CLICKPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:click", clickpad_finger_pin, LITEST_CLICKPAD, LITEST_ANY);

	litest_add("touchpad:softbutton", clickpad_softbutton_left, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:softbutton", clickpad_softbutton_right, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:softbutton", clickpad_softbutton_left_tap_n_drag, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:softbutton", clickpad_softbutton_right_tap_n_drag, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:softbutton", clickpad_softbutton_left_1st_fg_move, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:softbutton", clickpad_softbutton_left_2nd_fg_move, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:softbutton", clickpad_softbutton_left_to_right, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:softbutton", clickpad_softbutton_right_to_left, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);

	litest_add("touchpad:topsoftbuttons", clickpad_topsoftbuttons_left, LITEST_TOPBUTTONPAD, LITEST_ANY);
	litest_add("touchpad:topsoftbuttons", clickpad_topsoftbuttons_right, LITEST_TOPBUTTONPAD, LITEST_ANY);
	litest_add("touchpad:topsoftbuttons", clickpad_topsoftbuttons_middle, LITEST_TOPBUTTONPAD, LITEST_ANY);
	litest_add("touchpad:topsoftbuttons", clickpad_topsoftbuttons_move_out_ignore, LITEST_TOPBUTTONPAD, LITEST_ANY);
	litest_add("touchpad:topsoftbuttons", clickpad_topsoftbuttons_clickfinger, LITEST_TOPBUTTONPAD, LITEST_ANY);
	litest_add("touchpad:topsoftbuttons", clickpad_topsoftbuttons_clickfinger_dev_disabled, LITEST_TOPBUTTONPAD, LITEST_ANY);

	litest_add("touchpad:scroll", touchpad_2fg_scroll, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_slow_distance, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_return_to_motion, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_source, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_semi_mt, LITEST_SEMI_MT, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_scroll_natural_defaults, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_scroll_natural_enable_config, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_scroll_natural, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
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

	litest_add("touchpad:palm", touchpad_palm_detect_at_edge, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_at_bottom_corners, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_at_top_corners, LITEST_TOUCHPAD, LITEST_TOPBUTTONPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_palm_becomes_pointer, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_palm_stays_palm, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_no_palm_moving_into_edges, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_no_palm_detect_at_edge_for_edge_scrolling, LITEST_TOUCHPAD, LITEST_CLICKPAD);

	litest_add("touchpad:left-handed", touchpad_left_handed, LITEST_TOUCHPAD|LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("touchpad:left-handed", touchpad_left_handed_clickpad, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:left-handed", touchpad_left_handed_clickfinger, LITEST_APPLE_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:left-handed", touchpad_left_handed_tapping, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:left-handed", touchpad_left_handed_tapping_2fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:left-handed", touchpad_left_handed_delayed, LITEST_TOUCHPAD|LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("touchpad:left-handed", touchpad_left_handed_clickpad_delayed, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);

	/* Semi-MT hover tests aren't generic, they only work on this device and
	 * ignore the semi-mt capability (it doesn't matter for the tests */
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_noevent, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_down, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_down_up, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_down_hover_down, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_2fg_noevent, LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_add_for_device("touchpad:semi-mt-hover", touchpad_semi_mt_hover_2fg_1fg_down, LITEST_SYNAPTICS_HOVER_SEMI_MT);

	litest_add("touchpad:hover", touchpad_hover_noevent, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_down, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_down_up, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_down_hover_down, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_2fg_noevent, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);
	litest_add("touchpad:hover", touchpad_hover_2fg_1fg_down, LITEST_TOUCHPAD|LITEST_HOVER, LITEST_ANY);

	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_buttons, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);
	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_mb_scroll, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);
	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_mb_click, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);
	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_buttons_softbuttons, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);
	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_buttons_2fg_scroll, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);
	litest_add_for_device("touchpad:trackpoint", touchpad_trackpoint_no_trackpoint, LITEST_SYNAPTICS_TRACKPOINT_BUTTONS);

	litest_add_ranged("touchpad:state", touchpad_initial_state, LITEST_TOUCHPAD, LITEST_ANY, &axis_range);

	litest_add("touchpad:dwt", touchpad_dwt, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_enable_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_touch_hold, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_key_hold, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_type, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_type_short_timeout, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_tap_drag, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_click, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_edge_scroll, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:dwt", touchpad_dwt_edge_scroll_interrupt, LITEST_TOUCHPAD, LITEST_CLICKPAD);

	litest_add("touchpad:thumb", touchpad_thumb_begin_no_motion, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_update_no_motion, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_clickfinger, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_btnarea, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_edgescroll, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_tap_begin, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_tap_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_tap_hold, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:thumb", touchpad_thumb_tap_hold_2ndfg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:thumb", touchpad_thumb_tap_hold_2ndfg_tap, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
}
