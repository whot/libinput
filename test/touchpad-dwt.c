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

static inline bool
has_disable_while_typing(struct litest_device *device)
{
	return libinput_device_config_dwt_is_available(device->libinput_device);
}

static inline struct litest_device *
dwt_init_paired_keyboard(struct libinput *li,
			 struct litest_device *touchpad)
{
	enum litest_device_type which = LITEST_KEYBOARD;

	if (libevdev_get_id_vendor(touchpad->evdev) == VENDOR_ID_APPLE)
		which = LITEST_APPLE_KEYBOARD;

	return litest_add_device(li, which);
}

START_TEST(touchpad_dwt)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

START_TEST(touchpad_dwt_update_keyboard)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard, *yubikey;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	litest_disable_tap(touchpad->libinput_device);

	/* Yubikey is initialized first */
	yubikey = litest_add_device(li, LITEST_YUBIKEY);
	litest_drain_events(li);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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
	litest_delete_device(yubikey);
}
END_TEST

START_TEST(touchpad_dwt_update_keyboard_with_state)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard, *yubikey;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	litest_disable_tap(touchpad->libinput_device);

	/* Yubikey is initialized first */
	yubikey = litest_add_device(li, LITEST_YUBIKEY);
	litest_drain_events(li);

	litest_keyboard_key(yubikey, KEY_A, true);
	litest_keyboard_key(yubikey, KEY_A, false);
	litest_keyboard_key(yubikey, KEY_A, true);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	litest_keyboard_key(yubikey, KEY_A, false);
	litest_keyboard_key(yubikey, KEY_A, true);
	litest_drain_events(li);

	/* yubikey still has A down */
	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_drain_events(li);

	/* expected repairing, dwt should be disabled */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* release remaining key */
	litest_keyboard_key(yubikey, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
	litest_delete_device(yubikey);
}
END_TEST
START_TEST(touchpad_dwt_enable_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

START_TEST(touchpad_dwt_key_hold_timeout)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	litest_timeout_dwt_long();
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_empty_queue(li);

	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	/* key is up, but still within timeout */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

	/* expire timeout */
	litest_timeout_dwt_long();
	libinput_dispatch(li);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_key_hold_timeout_existing_touch_cornercase)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	/* Note: this tests for the current behavior of a cornercase, and
	 * the behaviour is essentially a bug. If this test fails it may be
	 * because the buggy behavior was fixed.
	 */

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	litest_timeout_dwt_long();
	libinput_dispatch(li);

	/* Touch starting after re-issuing the dwt timeout */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);

	litest_assert_empty_queue(li);

	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	/* key is up, but still within timeout */
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 50, 5, 1);
	litest_assert_empty_queue(li);

	/* Expire dwt timeout. Because the touch started after re-issuing
	 * the last timeout, it looks like the touch started after the last
	 * key press. Such touches are enabled for pointer motion by
	 * libinput when dwt expires.
	 * This is buggy behavior and not what a user would typically
	 * expect. But it's hard to trigger in real life too.
	 */
	litest_timeout_dwt_long();
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	/* If the below check for motion event fails because no events are
	 * in the pipe, the buggy behavior was fixed and this test case
	 * can be removed */
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_key_hold_timeout_existing_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	libinput_dispatch(li);
	litest_timeout_dwt_long();
	libinput_dispatch(li);

	litest_assert_empty_queue(li);

	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);
	/* key is up, but still within timeout */
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 50, 5, 1);
	litest_assert_empty_queue(li);

	/* expire timeout, but touch started before release */
	litest_timeout_dwt_long();
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 5, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_empty_queue(li);

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

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

	litest_enable_edge_scroll(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

	litest_enable_edge_scroll(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
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

START_TEST(touchpad_dwt_config_default_on)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	enum libinput_config_dwt_state state;

	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_WACOM ||
	    libevdev_get_id_bustype(dev->evdev) == BUS_BLUETOOTH) {
		ck_assert(!libinput_device_config_dwt_is_available(device));
		return;
	}

	ck_assert(libinput_device_config_dwt_is_available(device));
	state = libinput_device_config_dwt_get_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DWT_ENABLED);
	state = libinput_device_config_dwt_get_default_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DWT_ENABLED);

	status = libinput_device_config_dwt_set_enabled(device,
					LIBINPUT_CONFIG_DWT_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_dwt_set_enabled(device,
					LIBINPUT_CONFIG_DWT_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_dwt_set_enabled(device, 3);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

START_TEST(touchpad_dwt_config_default_off)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	enum libinput_config_dwt_state state;

	ck_assert(!libinput_device_config_dwt_is_available(device));
	state = libinput_device_config_dwt_get_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DWT_DISABLED);
	state = libinput_device_config_dwt_get_default_enabled(device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DWT_DISABLED);

	status = libinput_device_config_dwt_set_enabled(device,
					LIBINPUT_CONFIG_DWT_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = libinput_device_config_dwt_set_enabled(device,
					LIBINPUT_CONFIG_DWT_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_dwt_set_enabled(device, 3);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

static inline void
disable_dwt(struct litest_device *dev)
{
	enum libinput_config_status status,
				    expected = LIBINPUT_CONFIG_STATUS_SUCCESS;
	status = libinput_device_config_dwt_set_enabled(dev->libinput_device,
						LIBINPUT_CONFIG_DWT_DISABLED);
	litest_assert_int_eq(status, expected);
}

static inline void
enable_dwt(struct litest_device *dev)
{
	enum libinput_config_status status,
				    expected = LIBINPUT_CONFIG_STATUS_SUCCESS;
	status = libinput_device_config_dwt_set_enabled(dev->libinput_device,
						LIBINPUT_CONFIG_DWT_ENABLED);
	litest_assert_int_eq(status, expected);
}

START_TEST(touchpad_dwt_disabled)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	disable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_disable_during_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	enable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_empty_queue(li);

	disable_dwt(touchpad);

	/* touch already down -> keeps being ignored */
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 70, 10, 1);
	litest_touch_up(touchpad, 0);

	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_disable_before_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	enable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	disable_dwt(touchpad);
	libinput_dispatch(li);

	/* touch down during timeout -> still discarded */
	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_disable_during_key_release)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	enable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	disable_dwt(touchpad);
	libinput_dispatch(li);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	/* touch down during timeout, wait, should generate events */
	litest_touch_down(touchpad, 0, 50, 50);
	libinput_dispatch(li);
	litest_timeout_dwt_long();
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_disable_during_key_hold)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	enable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	disable_dwt(touchpad);
	libinput_dispatch(li);

	/* touch down during timeout, wait, should generate events */
	litest_touch_down(touchpad, 0, 50, 50);
	libinput_dispatch(li);
	litest_timeout_dwt_long();
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_enable_during_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	disable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	enable_dwt(touchpad);

	/* touch already down -> still sends events */
	litest_touch_move_to(touchpad, 0, 70, 50, 50, 70, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_enable_before_touch)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	disable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_disable_tap(touchpad->libinput_device);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	enable_dwt(touchpad);
	libinput_dispatch(li);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_enable_during_tap)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard;
	struct libinput *li = touchpad->libinput;

	if (!has_disable_while_typing(touchpad))
		return;

	litest_enable_tap(touchpad->libinput_device);
	disable_dwt(touchpad);

	keyboard = dwt_init_paired_keyboard(li, touchpad);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	libinput_dispatch(li);
	enable_dwt(touchpad);
	libinput_dispatch(li);
	litest_touch_up(touchpad, 0);
	libinput_dispatch(li);

	litest_timeout_tap();
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_BUTTON);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_delete_device(keyboard);
}
END_TEST

START_TEST(touchpad_dwt_apple)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *keyboard, *apple_keyboard;
	struct libinput *li = touchpad->libinput;

	ck_assert(has_disable_while_typing(touchpad));

	/* Only the apple keyboard can trigger DWT */
	keyboard = litest_add_device(li, LITEST_KEYBOARD);
	litest_drain_events(li);

	litest_keyboard_key(keyboard, KEY_A, true);
	litest_keyboard_key(keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	apple_keyboard = litest_add_device(li, LITEST_APPLE_KEYBOARD);
	litest_drain_events(li);

	litest_keyboard_key(apple_keyboard, KEY_A, true);
	litest_keyboard_key(apple_keyboard, KEY_A, false);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_KEYBOARD_KEY);

	litest_touch_down(touchpad, 0, 50, 50);
	litest_touch_move_to(touchpad, 0, 50, 50, 70, 50, 10, 1);
	litest_touch_up(touchpad, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_delete_device(keyboard);
	litest_delete_device(apple_keyboard);
}
END_TEST

void
litest_setup_tests(void)
{
	litest_add("touchpad:dwt", touchpad_dwt, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add_for_device("touchpad:dwt", touchpad_dwt_update_keyboard, LITEST_SYNAPTICS_I2C);
	litest_add_for_device("touchpad:dwt", touchpad_dwt_update_keyboard_with_state, LITEST_SYNAPTICS_I2C);
	litest_add("touchpad:dwt", touchpad_dwt_enable_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_touch_hold, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_key_hold, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_key_hold_timeout, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_key_hold_timeout_existing_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_key_hold_timeout_existing_touch_cornercase, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_type, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_type_short_timeout, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_tap_drag, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_click, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_edge_scroll, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:dwt", touchpad_dwt_edge_scroll_interrupt, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:dwt", touchpad_dwt_config_default_on, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_config_default_off, LITEST_ANY, LITEST_TOUCHPAD);
	litest_add("touchpad:dwt", touchpad_dwt_disabled, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_disable_during_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_disable_before_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_disable_during_key_release, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_disable_during_key_hold, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_enable_during_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_enable_before_touch, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:dwt", touchpad_dwt_enable_during_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add_for_device("touchpad:dwt", touchpad_dwt_apple, LITEST_BCM5974);
}
