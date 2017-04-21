/*
 * Copyright © 2017 James Ye <jye836@gmail.com>
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

START_TEST(lid_switch)
{
	struct litest_device *sw = litest_current_device();
	struct libinput *li = sw->libinput;
	struct libinput_event *event;

	litest_drain_events(li);

	/* lid closed */
	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_is_switch_event(event,
			       LIBINPUT_SWITCH_LID,
			       LIBINPUT_SWITCH_STATE_ON);
	libinput_event_destroy(event);

	/* lid opened */
	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_OFF);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_is_switch_event(event,
			       LIBINPUT_SWITCH_LID,
			       LIBINPUT_SWITCH_STATE_OFF);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(lid_switch_double)
{
	struct litest_device *sw = litest_current_device();
	struct libinput *li = sw->libinput;
	struct libinput_event *event;

	litest_drain_events(li);

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_is_switch_event(event,
			       LIBINPUT_SWITCH_LID,
			       LIBINPUT_SWITCH_STATE_ON);
	libinput_event_destroy(event);

	/* This will be filtered by the kernel, so this test is a bit
	 * useless */
	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	libinput_dispatch(li);

	litest_assert_empty_queue(li);
}
END_TEST

static bool
lid_switch_is_reliable(struct litest_device *dev)
{
	struct udev_device *udev_device;
	const char *prop;
	bool is_reliable = false;

	udev_device = libinput_device_get_udev_device(dev->libinput_device);
	prop = udev_device_get_property_value(udev_device,
					      "LIBINPUT_ATTR_LID_SWITCH_RELIABILITY");

	is_reliable = prop && streq(prop, "reliable");
	udev_device_unref(udev_device);

	return is_reliable;
}

START_TEST(lid_switch_down_on_init)
{
	struct litest_device *sw = litest_current_device();
	struct libinput *li;
	struct libinput_event *event;

	if (!lid_switch_is_reliable(sw))
		return;

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);

	/* need separate context to test */
	li = litest_create_context();
	libinput_path_add_device(li,
				 libevdev_uinput_get_devnode(sw->uinput));
	libinput_dispatch(li);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_SWITCH_TOGGLE, -1);
	event = libinput_get_event(li);
	litest_is_switch_event(event,
			       LIBINPUT_SWITCH_LID,
			       LIBINPUT_SWITCH_STATE_ON);
	libinput_event_destroy(event);

	while ((event = libinput_get_event(li))) {
		ck_assert_int_ne(libinput_event_get_type(event),
				 LIBINPUT_EVENT_SWITCH_TOGGLE);
		libinput_event_destroy(event);
	}

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_OFF);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	litest_is_switch_event(event,
			       LIBINPUT_SWITCH_LID,
			       LIBINPUT_SWITCH_STATE_OFF);
	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

	libinput_unref(li);

}
END_TEST

START_TEST(lid_switch_not_down_on_init)
{
	struct litest_device *sw = litest_current_device();
	struct libinput *li;
	struct libinput_event *event;

	if (lid_switch_is_reliable(sw))
		return;

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);

	/* need separate context to test */
	li = litest_create_context();
	libinput_path_add_device(li,
				 libevdev_uinput_get_devnode(sw->uinput));
	libinput_dispatch(li);

	while ((event = libinput_get_event(li)) != NULL) {
		ck_assert_int_ne(libinput_event_get_type(event),
				 LIBINPUT_EVENT_SWITCH_TOGGLE);
		libinput_event_destroy(event);
	}

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_OFF);
	litest_assert_empty_queue(li);
	libinput_unref(li);
}
END_TEST

static inline struct litest_device *
lid_init_paired_touchpad(struct libinput *li)
{
	enum litest_device_type which = LITEST_SYNAPTICS_I2C;

	return litest_add_device(li, which);
}

START_TEST(lid_disable_touchpad)
{
	struct litest_device *sw = litest_current_device();
	struct litest_device *touchpad;
	struct libinput *li = sw->libinput;

	touchpad = lid_init_paired_touchpad(li);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	/* lid is down - no events */
	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_SWITCH_TOGGLE);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	/* lid is up - motion events */
	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_OFF);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_SWITCH_TOGGLE);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(touchpad);
}
END_TEST

START_TEST(lid_disable_touchpad_during_touch)
{
	struct litest_device *sw = litest_current_device();
	struct litest_device *touchpad;
	struct libinput *li = sw->libinput;

	touchpad = lid_init_paired_touchpad(li);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_SWITCH_TOGGLE);

	litest_touch_move_to(touchpad, 0, 70, 50, 50, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_delete_device(touchpad);
}
END_TEST

START_TEST(lid_disable_touchpad_edge_scroll)
{
	struct litest_device *sw = litest_current_device();
	struct litest_device *touchpad;
	struct libinput *li = sw->libinput;

	touchpad = lid_init_paired_touchpad(li);
	litest_enable_edge_scroll(touchpad);

	litest_drain_events(li);

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_SWITCH_TOGGLE);

	litest_touch_down(touchpad, 0, 99, 20);
	libinput_dispatch(li);
	litest_timeout_edgescroll();
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_touch_move_to(touchpad, 0, 99, 20, 99, 80, 60, 10);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_touch_move_to(touchpad, 0, 99, 80, 99, 20, 60, 10);
	litest_touch_up(touchpad, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_delete_device(touchpad);
}
END_TEST

START_TEST(lid_disable_touchpad_edge_scroll_interrupt)
{
	struct litest_device *sw = litest_current_device();
	struct litest_device *touchpad;
	struct libinput *li = sw->libinput;
	struct libinput_event *event;

	touchpad = lid_init_paired_touchpad(li);
	litest_enable_edge_scroll(touchpad);

	litest_drain_events(li);

	litest_touch_down(touchpad, 0, 99, 20);
	libinput_dispatch(li);
	litest_timeout_edgescroll();
	litest_touch_move_to(touchpad, 0, 99, 20, 99, 30, 10, 10);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_is_axis_event(event,
			     LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
			     LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	litest_is_switch_event(event,
			       LIBINPUT_SWITCH_LID,
			       LIBINPUT_SWITCH_STATE_ON);
	libinput_event_destroy(event);

	litest_delete_device(touchpad);
}
END_TEST

START_TEST(lid_disable_touchpad_already_open)
{
	struct litest_device *sw = litest_current_device();
	struct litest_device *touchpad;
	struct libinput *li = sw->libinput;

	touchpad = lid_init_paired_touchpad(li);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	/* default: lid is up - motion events */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* open lid - motion events */
	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_OFF);
	litest_assert_empty_queue(li);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(touchpad);
}
END_TEST

START_TEST(lid_open_on_key)
{
	struct litest_device *sw = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = sw->libinput;
	struct libinput_event *event;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	litest_drain_events(li);

	litest_event(keyboard, EV_KEY, KEY_A, 1);
	litest_event(keyboard, EV_SYN, SYN_REPORT, 0);
	litest_event(keyboard, EV_KEY, KEY_A, 0);
	litest_event(keyboard, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_SWITCH_TOGGLE, -1);
	event = libinput_get_event(li);
	litest_is_switch_event(event,
			       LIBINPUT_SWITCH_LID,
			       LIBINPUT_SWITCH_STATE_OFF);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_OFF);
	litest_assert_empty_queue(li);

	libinput_event_destroy(event);
	litest_delete_device(keyboard);
}
END_TEST

START_TEST(lid_open_on_key_touchpad_enabled)
{
	struct litest_device *sw = litest_current_device();
	struct litest_device *keyboard, *touchpad;
	struct libinput *li = sw->libinput;

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	touchpad = litest_add_device(li, LITEST_SYNAPTICS_I2C);

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	litest_drain_events(li);

	litest_event(keyboard, EV_KEY, KEY_A, 1);
	litest_event(keyboard, EV_SYN, SYN_REPORT, 0);
	litest_event(keyboard, EV_KEY, KEY_A, 0);
	litest_event(keyboard, EV_SYN, SYN_REPORT, 0);
	litest_drain_events(li);
	litest_timeout_dwt_long();

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 70, 10, 1);
	litest_touch_up(touchpad, 0);
	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
	litest_delete_device(touchpad);
}
END_TEST

START_TEST(lid_suspend_with_keyboard)
{
	struct libinput *li;
	struct litest_device *keyboard;
	struct litest_device *sw;

	li = litest_create_context();

	sw = litest_add_device(li, LITEST_LID_SWITCH);
	libinput_dispatch(li);

	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	libinput_dispatch(li);

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	litest_drain_events(li);
	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_OFF);
	litest_drain_events(li);

	litest_delete_device(keyboard);
	litest_drain_events(li);

	litest_delete_device(sw);
	libinput_dispatch(li);

	libinput_unref(li);
}
END_TEST

START_TEST(lid_suspend_with_touchpad)
{
	struct libinput *li;
	struct litest_device *touchpad, *sw;

	li = litest_create_context();

	sw = litest_add_device(li, LITEST_LID_SWITCH);
	litest_drain_events(li);

	touchpad = litest_add_device(li, LITEST_SYNAPTICS_I2C);
	litest_delete_device(touchpad);
	touchpad = litest_add_device(li, LITEST_SYNAPTICS_I2C);
	litest_drain_events(li);

	litest_delete_device(sw);
	litest_drain_events(li);
	litest_delete_device(touchpad);
	litest_drain_events(li);

	libinput_unref(li);
}
END_TEST

START_TEST(lid_update_hw_on_key)
{
	struct litest_device *sw = litest_current_device();
	struct libinput *li = sw->libinput;
	struct libinput *li2;
	struct litest_device *keyboard;
	struct libinput_event *event;

	sleep(5);
	keyboard = litest_add_device(li, LITEST_KEYBOARD);

	/* separate context to listen to the fake hw event */
	li2 = litest_create_context();
	libinput_path_add_device(li2,
				 libevdev_uinput_get_devnode(sw->uinput));
	litest_drain_events(li2);

	litest_lid_action(sw, LIBINPUT_SWITCH_STATE_ON);
	litest_drain_events(li);

	libinput_dispatch(li2);
	event = libinput_get_event(li2);
	litest_is_switch_event(event,
			       LIBINPUT_SWITCH_LID,
			       LIBINPUT_SWITCH_STATE_ON);
	libinput_event_destroy(event);

	litest_event(keyboard, EV_KEY, KEY_A, 1);
	litest_event(keyboard, EV_SYN, SYN_REPORT, 0);
	litest_event(keyboard, EV_KEY, KEY_A, 0);
	litest_event(keyboard, EV_SYN, SYN_REPORT, 0);
	litest_drain_events(li);

	libinput_dispatch(li2);
	event = libinput_get_event(li2);
	litest_is_switch_event(event,
			       LIBINPUT_SWITCH_LID,
			       LIBINPUT_SWITCH_STATE_OFF);
	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

	libinput_unref(li2);
	litest_delete_device(keyboard);
}
END_TEST

void
litest_setup_tests_lid(void)
{
	litest_add("lid:switch", lid_switch, LITEST_SWITCH, LITEST_ANY);
	litest_add("lid:switch", lid_switch_double, LITEST_SWITCH, LITEST_ANY);
	litest_add("lid:switch", lid_switch_down_on_init, LITEST_SWITCH, LITEST_ANY);
	litest_add("lid:switch", lid_switch_not_down_on_init, LITEST_SWITCH, LITEST_ANY);
	litest_add("lid:disable_touchpad", lid_disable_touchpad, LITEST_SWITCH, LITEST_ANY);
	litest_add("lid:disable_touchpad", lid_disable_touchpad_during_touch, LITEST_SWITCH, LITEST_ANY);
	litest_add("lid:disable_touchpad", lid_disable_touchpad_edge_scroll, LITEST_SWITCH, LITEST_ANY);
	litest_add("lid:disable_touchpad", lid_disable_touchpad_edge_scroll_interrupt, LITEST_SWITCH, LITEST_ANY);
	litest_add("lid:disable_touchpad", lid_disable_touchpad_already_open, LITEST_SWITCH, LITEST_ANY);

	litest_add("lid:keyboard", lid_open_on_key, LITEST_SWITCH, LITEST_ANY);
	litest_add("lid:keyboard", lid_open_on_key_touchpad_enabled, LITEST_SWITCH, LITEST_ANY);

	litest_add_no_device("lid:keyboard", lid_suspend_with_keyboard);
	litest_add_no_device("lid:disable_touchpad", lid_suspend_with_touchpad);

	litest_add_for_device("lid:buggy", lid_update_hw_on_key, LITEST_LID_SWITCH_SURFACE3);
}
