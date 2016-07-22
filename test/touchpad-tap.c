/*
 * Copyright © 2014-2015 Red Hat, Inc.
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

START_TEST(touchpad_1fg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_tap();
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event == NULL);
}
END_TEST

START_TEST(touchpad_1fg_doubletap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	uint32_t oldtime, curtime;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tap();

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	oldtime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_RELEASED);
	curtime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);
	ck_assert_int_le(oldtime, curtime);

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	curtime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);
	ck_assert_int_lt(oldtime, curtime);
	oldtime = curtime;

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_RELEASED);
	curtime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);
	ck_assert_int_le(oldtime, curtime);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_multitap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	uint32_t oldtime = 0,
		 curtime;
	int range = _i, /* looped test */
	    ntaps;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		litest_touch_down(dev, 0, 50, 50);
		litest_touch_up(dev, 0);
		libinput_dispatch(li);
		msleep(10);
	}

	litest_timeout_tap();
	libinput_dispatch(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_PRESSED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_gt(curtime, oldtime);

		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_RELEASED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_ge(curtime, oldtime);
		oldtime = curtime;
	}
	litest_timeout_tap();
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_multitap_n_drag_move)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	uint32_t oldtime = 0,
		 curtime;
	int range = _i, /* looped test */
	    ntaps;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		litest_touch_down(dev, 0, 50, 50);
		litest_touch_up(dev, 0);
		libinput_dispatch(li);
		msleep(10);
	}

	libinput_dispatch(li);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 70, 50, 10, 4);
	libinput_dispatch(li);

	for (ntaps = 0; ntaps < range; ntaps++) {
		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_PRESSED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_gt(curtime, oldtime);

		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_RELEASED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_ge(curtime, oldtime);
		oldtime = curtime;
	}

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	curtime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);
	ck_assert_int_gt(curtime, oldtime);

	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_multitap_n_drag_2fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	uint32_t oldtime = 0,
		 curtime;
	int range = _i,
	    ntaps;

	if (libevdev_has_property(dev->evdev, INPUT_PROP_SEMI_MT))
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		litest_touch_down(dev, 0, 50, 50);
		litest_touch_up(dev, 0);
		libinput_dispatch(li);
		msleep(10);
	}

	libinput_dispatch(li);
	litest_touch_down(dev, 0, 50, 50);
	msleep(10);
	litest_touch_down(dev, 1, 70, 50);
	libinput_dispatch(li);

	for (ntaps = 0; ntaps < range; ntaps++) {
		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_PRESSED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_gt(curtime, oldtime);

		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_RELEASED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_ge(curtime, oldtime);
		oldtime = curtime;
	}

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	curtime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);
	ck_assert_int_gt(curtime, oldtime);

	litest_touch_move_to(dev, 1, 70, 50, 90, 50, 10, 4);

	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);
	litest_timeout_tap();
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_multitap_n_drag_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	uint32_t oldtime = 0,
		 curtime;
	int range = _i, /* looped test */
	    ntaps;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		litest_touch_down(dev, 0, 50, 50);
		litest_touch_up(dev, 0);
		libinput_dispatch(li);
		msleep(10);
	}

	litest_touch_down(dev, 0, 50, 50);
	libinput_dispatch(li);
	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_LEFT, false);
	libinput_dispatch(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_PRESSED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_gt(curtime, oldtime);

		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_RELEASED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_ge(curtime, oldtime);
		oldtime = curtime;
	}

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_touch_up(dev, 0);
	litest_timeout_tap();

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_multitap_n_drag_timeout)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	uint32_t oldtime = 0,
		 curtime;
	int range = _i, /* looped test */
	    ntaps;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		litest_touch_down(dev, 0, 50, 50);
		litest_touch_up(dev, 0);
		libinput_dispatch(li);
		msleep(10);
	}

	libinput_dispatch(li);
	litest_touch_down(dev, 0, 50, 50);
	libinput_dispatch(li);

	litest_timeout_tap();
	libinput_dispatch(li);

	for (ntaps = 0; ntaps < range; ntaps++) {
		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_PRESSED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_gt(curtime, oldtime);

		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_RELEASED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_ge(curtime, oldtime);
		oldtime = curtime;
	}

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	curtime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);
	ck_assert_int_gt(curtime, oldtime);

	litest_touch_move_to(dev, 0, 50, 50, 70, 50, 10, 4);

	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_multitap_n_drag_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	uint32_t oldtime = 0,
		 curtime;
	int range = _i, /* looped test */
	    ntaps;

	litest_enable_tap(dev->libinput_device);
	litest_enable_drag_lock(dev->libinput_device);

	litest_drain_events(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		litest_touch_down(dev, 0, 50, 50);
		litest_touch_up(dev, 0);
		libinput_dispatch(li);
		msleep(10);
	}

	libinput_dispatch(li);
	litest_touch_down(dev, 0, 50, 50);
	libinput_dispatch(li);

	litest_timeout_tap();
	libinput_dispatch(li);

	for (ntaps = 0; ntaps < range; ntaps++) {
		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_PRESSED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_gt(curtime, oldtime);

		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_RELEASED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_ge(curtime, oldtime);
		oldtime = curtime;
	}

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	curtime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);
	ck_assert_int_gt(curtime, oldtime);

	litest_touch_move_to(dev, 0, 50, 50, 70, 50, 10, 4);

	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 70, 50);
	litest_touch_up(dev, 0);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_multitap_n_drag_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	uint32_t oldtime = 0,
		 curtime;
	int range = _i, /* looped test */
	    ntaps;

	litest_enable_tap(dev->libinput_device);
	litest_enable_drag_lock(dev->libinput_device);

	litest_drain_events(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		litest_touch_down(dev, 0, 50, 50);
		litest_touch_up(dev, 0);
		libinput_dispatch(li);
		msleep(10);
	}

	libinput_dispatch(li);
	litest_touch_down(dev, 0, 50, 50);
	libinput_dispatch(li);

	litest_timeout_tap();
	libinput_dispatch(li);

	for (ntaps = 0; ntaps < range; ntaps++) {
		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_PRESSED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_gt(curtime, oldtime);

		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_RELEASED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_ge(curtime, oldtime);
		oldtime = curtime;
	}

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	curtime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);
	ck_assert_int_gt(curtime, oldtime);

	litest_touch_move_to(dev, 0, 50, 50, 70, 50, 10, 4);

	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 70, 50);
	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_LEFT, false);
	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	/* the physical click */
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_tap_n_drag)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev __attribute__((unused));

	litest_enable_tap(dev->libinput_device);
	litest_disable_drag_lock(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 80, 5, 40);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);

	/* don't use helper functions here, we expect the event be available
	 * immediately, not after a timeout that the helper functions may
	 * trigger.
	 */
	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert_notnull(event);
	ptrev = litest_is_button_event(event,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_RELEASED);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_tap_n_drag_draglock)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);
	litest_enable_drag_lock(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 80, 5, 40);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* lift finger, set down again, should continue dragging */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 80, 5, 40);
	litest_touch_up(dev, 0);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_timeout_tap();

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_tap_n_drag_draglock_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);
	litest_enable_drag_lock(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 80, 5, 40);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* lift finger, set down again, should continue dragging */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 80, 5, 40);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_tap_n_drag_draglock_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);
	litest_enable_drag_lock(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 80, 5, 40);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_dispatch(li);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_LEFT, false);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	/* the physical click */
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_tap_n_drag_draglock_timeout)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);
	litest_enable_drag_lock(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	libinput_dispatch(li);
	litest_timeout_tap();

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	litest_assert_empty_queue(li);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_timeout_tapndrag();
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_n_drag)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);
	litest_disable_drag_lock(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 30, 70);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 30, 70);
	litest_touch_down(dev, 1, 80, 70);
	litest_touch_move_to(dev, 0, 30, 70, 30, 30, 5, 40);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_n_drag_3fg_btntool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (libevdev_get_abs_maximum(dev->evdev,
				     ABS_MT_SLOT) > 2)
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 30, 70);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 30, 70);
	litest_touch_down(dev, 1, 80, 90);
	litest_touch_move_to(dev, 0, 30, 70, 30, 30, 5, 40);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* Putting down a third finger should end the drag */
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	/* Releasing the fingers should not cause any events */
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_n_drag_3fg)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (libevdev_get_abs_maximum(dev->evdev,
				     ABS_MT_SLOT) <= 2)
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 30, 70);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 30, 70);
	litest_touch_down(dev, 1, 80, 90);
	litest_touch_move_to(dev, 0, 30, 70, 30, 30, 5, 40);
	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* Putting down a third finger should end the drag */
	litest_touch_down(dev, 2, 50, 50);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	/* Releasing the fingers should not cause any events */
	litest_touch_up(dev, 2);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 70);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_tap();
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_inverted)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 70);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_tap();
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_n_hold_first)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 70);
	litest_touch_up(dev, 1);

	libinput_dispatch(li);

	litest_assert_empty_queue(li);
	litest_timeout_tap();

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_n_hold_second)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 70);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_empty_queue(li);
	litest_timeout_tap();

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_quickrelease)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 70);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_tap();
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	/* Finger down, finger up -> tap button press
	 * Physical button click -> no button press/release
	 * Tap timeout -> tap button release */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_timeout_tap();

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	/* two fingers down, left button click, fingers up
	   -> one left button, one right button event pair */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_2fg_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	/* two fingers down, button click, fingers up
	   -> only one button left event pair */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_click_apple)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	/* two fingers down, button click, fingers up
	   -> only one button right event pair
	   (apple have clickfinger enabled by default) */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_no_2fg_tap_after_move)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(dev->libinput);

	/* one finger down, move past threshold,
	   second finger down, first finger up
	   -> no event
	 */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 90, 90, 10, 0);
	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 1, 70, 50);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_no_2fg_tap_after_timeout)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);
	litest_drain_events(dev->libinput);

	/* one finger down, wait past tap timeout,
	   second finger down, first finger up
	   -> no event
	 */
	litest_touch_down(dev, 0, 50, 50);
	libinput_dispatch(dev->libinput);
	litest_timeout_tap();
	libinput_dispatch(dev->libinput);
	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 1, 70, 50);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_no_first_fg_tap_after_move)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	/* one finger down, second finger down,
	   second finger moves beyond threshold,
	   first finger up
	   -> no event
	 */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	libinput_dispatch(dev->libinput);
	litest_touch_move_to(dev, 1, 70, 50, 90, 90, 10, 0);
	libinput_dispatch(dev->libinput);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);
	libinput_dispatch(dev->libinput);

	while ((event = libinput_get_event(li))) {
		ck_assert_int_ne(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_BUTTON);
		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(touchpad_1fg_double_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	/* one finger down, up, down, button click, finger up
	   -> two button left event pairs */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
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
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_tap_n_drag_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	/* one finger down, up, down, move, button click, finger up
	   -> two button left event pairs, motion allowed */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 50, 10, 0);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_3fg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int i;

	if (libevdev_get_abs_maximum(dev->evdev,
				     ABS_MT_SLOT) <= 2)
		return;

	litest_enable_tap(dev->libinput_device);

	for (i = 0; i < 3; i++) {
		litest_drain_events(li);

		litest_touch_down(dev, 0, 50, 50);
		litest_touch_down(dev, 1, 70, 50);
		litest_touch_down(dev, 2, 80, 50);

		litest_touch_up(dev, (i + 2) % 3);
		litest_touch_up(dev, (i + 1) % 3);
		litest_touch_up(dev, (i + 0) % 3);

		libinput_dispatch(li);

		litest_assert_button_event(li, BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_PRESSED);
		litest_timeout_tap();
		litest_assert_button_event(li, BTN_MIDDLE,
					   LIBINPUT_BUTTON_STATE_RELEASED);

		libinput_dispatch(li);
		event = libinput_get_event(li);
		ck_assert(event == NULL);
	}
}
END_TEST

START_TEST(touchpad_3fg_tap_quickrelease)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (libevdev_get_abs_maximum(dev->evdev,
				     ABS_MT_SLOT) <= 2)
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	litest_touch_down(dev, 2, 80, 50);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 2);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_tap();
	litest_assert_button_event(li, BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	libinput_dispatch(li);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_3fg_tap_btntool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	if (libevdev_get_abs_maximum(dev->evdev,
				     ABS_MT_SLOT) > 2)
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_tap();
	litest_assert_button_event(li, BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event == NULL);
}
END_TEST

START_TEST(touchpad_3fg_tap_btntool_inverted)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	if (libevdev_get_abs_maximum(dev->evdev,
				     ABS_MT_SLOT) > 2)
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 1);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_tap();
	litest_assert_button_event(li, BTN_MIDDLE,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert(event == NULL);
}
END_TEST

START_TEST(touchpad_4fg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int i;

	if (libevdev_get_abs_maximum(dev->evdev,
				     ABS_MT_SLOT) <= 3)
		return;

	litest_enable_tap(dev->libinput_device);

	for (i = 0; i < 4; i++) {
		litest_drain_events(li);

		litest_touch_down(dev, 0, 50, 50);
		litest_touch_down(dev, 1, 70, 50);
		litest_touch_down(dev, 2, 80, 50);
		litest_touch_down(dev, 3, 90, 50);

		litest_touch_up(dev, (i + 3) % 4);
		litest_touch_up(dev, (i + 2) % 4);
		litest_touch_up(dev, (i + 1) % 4);
		litest_touch_up(dev, (i + 0) % 4);

		libinput_dispatch(li);
		litest_assert_empty_queue(li);
		litest_timeout_tap();
		litest_assert_empty_queue(li);
		event = libinput_get_event(li);
		ck_assert(event == NULL);
	}
}
END_TEST

START_TEST(touchpad_4fg_tap_quickrelease)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (libevdev_get_abs_maximum(dev->evdev,
				     ABS_MT_SLOT) <= 3)
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	litest_touch_down(dev, 2, 80, 50);
	litest_touch_down(dev, 3, 90, 50);

	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 2);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 3);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_KEY, BTN_TOOL_QUADTAP, 0);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	litest_assert_empty_queue(li);
	litest_timeout_tap();
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_5fg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int i;

	if (libevdev_get_abs_maximum(dev->evdev,
				     ABS_MT_SLOT) <= 4)
		return;

	litest_enable_tap(dev->libinput_device);

	for (i = 0; i < 5; i++) {
		litest_drain_events(li);

		litest_touch_down(dev, 0, 20, 50);
		litest_touch_down(dev, 1, 30, 50);
		litest_touch_down(dev, 2, 40, 50);
		litest_touch_down(dev, 3, 50, 50);
		litest_touch_down(dev, 4, 60, 50);

		litest_touch_up(dev, (i + 4) % 5);
		litest_touch_up(dev, (i + 3) % 5);
		litest_touch_up(dev, (i + 2) % 5);
		litest_touch_up(dev, (i + 1) % 5);
		litest_touch_up(dev, (i + 0) % 5);

		libinput_dispatch(li);
		litest_assert_empty_queue(li);
		litest_timeout_tap();
		litest_assert_empty_queue(li);
		event = libinput_get_event(li);
		ck_assert(event == NULL);
	}
}
END_TEST

START_TEST(touchpad_5fg_tap_quickrelease)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (libevdev_get_abs_maximum(dev->evdev,
				     ABS_MT_SLOT) <= 4)
		return;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 50);
	litest_touch_down(dev, 1, 30, 50);
	litest_touch_down(dev, 2, 40, 50);
	litest_touch_down(dev, 3, 70, 50);
	litest_touch_down(dev, 4, 90, 50);

	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 1);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 2);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 3);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 4);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
	litest_event(dev, EV_KEY, BTN_TOOL_QUINTTAP, 0);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	litest_assert_empty_queue(li);
	litest_timeout_tap();
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_1fg_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);

	litest_drain_events(dev->libinput);

	/* finger down, button click, finger up
	   -> only one button left event pair */
	litest_touch_down(dev, 0, 50, 50);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_timeout_tap();

	libinput_dispatch(li);

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_tap_is_available)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_ge(libinput_device_config_tap_get_finger_count(dev->libinput_device), 1);
}
END_TEST

START_TEST(touchpad_tap_is_not_available)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_eq(libinput_device_config_tap_get_finger_count(dev->libinput_device), 0);
	ck_assert_int_eq(libinput_device_config_tap_get_enabled(dev->libinput_device),
			 LIBINPUT_CONFIG_TAP_DISABLED);
	ck_assert_int_eq(libinput_device_config_tap_set_enabled(dev->libinput_device,
								LIBINPUT_CONFIG_TAP_ENABLED),
			 LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	ck_assert_int_eq(libinput_device_config_tap_set_enabled(dev->libinput_device,
								LIBINPUT_CONFIG_TAP_DISABLED),
			 LIBINPUT_CONFIG_STATUS_SUCCESS);
}
END_TEST

START_TEST(touchpad_tap_default_disabled)
{
	struct litest_device *dev = litest_current_device();

	/* this test is only run on specific devices */

	ck_assert_int_eq(libinput_device_config_tap_get_default_enabled(dev->libinput_device),
			 LIBINPUT_CONFIG_TAP_DISABLED);
}
END_TEST

START_TEST(touchpad_tap_default_enabled)
{
	struct litest_device *dev = litest_current_device();

	/* this test is only run on specific devices */

	ck_assert_int_eq(libinput_device_config_tap_get_default_enabled(dev->libinput_device),
			 LIBINPUT_CONFIG_TAP_ENABLED);
}
END_TEST

START_TEST(touchpad_tap_invalid)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_eq(libinput_device_config_tap_set_enabled(dev->libinput_device, 2),
			 LIBINPUT_CONFIG_STATUS_INVALID);
	ck_assert_int_eq(libinput_device_config_tap_set_enabled(dev->libinput_device, -1),
			 LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

START_TEST(touchpad_drag_default_disabled)
{
	struct litest_device *dev = litest_current_device();

	/* this test is only run on specific devices */

	ck_assert_int_eq(libinput_device_config_tap_get_default_drag_enabled(dev->libinput_device),
			 LIBINPUT_CONFIG_DRAG_DISABLED);
}
END_TEST

START_TEST(touchpad_drag_default_enabled)
{
	struct litest_device *dev = litest_current_device();

	/* this test is only run on specific devices */

	ck_assert_int_eq(libinput_device_config_tap_get_default_drag_enabled(dev->libinput_device),
			 LIBINPUT_CONFIG_DRAG_ENABLED);
}
END_TEST

START_TEST(touchpad_drag_config_invalid)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_eq(libinput_device_config_tap_set_drag_enabled(dev->libinput_device, 2),
			 LIBINPUT_CONFIG_STATUS_INVALID);
	ck_assert_int_eq(libinput_device_config_tap_set_drag_enabled(dev->libinput_device, -1),
			 LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

START_TEST(touchpad_drag_config_enabledisable)
{
	struct litest_device *dev = litest_current_device();
	enum libinput_config_drag_state state;

	litest_enable_tap(dev->libinput_device);

	litest_disable_tap_drag(dev->libinput_device);
	state = libinput_device_config_tap_get_drag_enabled(dev->libinput_device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DRAG_DISABLED);

	litest_enable_tap_drag(dev->libinput_device);
	state = libinput_device_config_tap_get_drag_enabled(dev->libinput_device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DRAG_ENABLED);

	/* same thing with tapping disabled */
	litest_enable_tap(dev->libinput_device);

	litest_disable_tap_drag(dev->libinput_device);
	state = libinput_device_config_tap_get_drag_enabled(dev->libinput_device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DRAG_DISABLED);

	litest_enable_tap_drag(dev->libinput_device);
	state = libinput_device_config_tap_get_drag_enabled(dev->libinput_device);
	ck_assert_int_eq(state, LIBINPUT_CONFIG_DRAG_ENABLED);
}
END_TEST

START_TEST(touchpad_drag_disabled)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_enable_tap(dev->libinput_device);
	litest_disable_tap_drag(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 90, 90, 10, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);

}
END_TEST

START_TEST(touchpad_drag_disabled_immediate)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *ev;
	struct libinput_event_pointer *ptrev;
	uint64_t press_time, release_time;

	litest_enable_tap(dev->libinput_device);
	litest_disable_tap_drag(dev->libinput_device);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	msleep(10); /* to force a time difference */
	libinput_dispatch(li);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	ev = libinput_get_event(li);
	ptrev = litest_is_button_event(ev,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	press_time = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(ev);

	ev = libinput_get_event(li);
	ptrev = litest_is_button_event(ev,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_RELEASED);
	release_time = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(ev);

	ck_assert_int_gt(release_time, press_time);
}
END_TEST

START_TEST(touchpad_drag_disabled_multitap_no_drag)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	uint32_t oldtime = 0,
		 curtime;
	int range = _i, /* looped test */
	    ntaps;

	litest_enable_tap(dev->libinput_device);
	litest_disable_tap_drag(dev->libinput_device);

	litest_drain_events(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		litest_touch_down(dev, 0, 50, 50);
		litest_touch_up(dev, 0);
		libinput_dispatch(li);
		msleep(10);
	}

	libinput_dispatch(li);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 70, 50, 10, 4);
	libinput_dispatch(li);

	for (ntaps = 0; ntaps <= range; ntaps++) {
		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_PRESSED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_gt(curtime, oldtime);

		event = libinput_get_event(li);
		ptrev = litest_is_button_event(event,
					       BTN_LEFT,
					       LIBINPUT_BUTTON_STATE_RELEASED);
		curtime = libinput_event_pointer_get_time(ptrev);
		libinput_event_destroy(event);
		ck_assert_int_ge(curtime, oldtime);
		oldtime = curtime;
	}

	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_POINTER_MOTION);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_drag_lock_default_disabled)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;

	ck_assert_int_eq(libinput_device_config_tap_get_drag_lock_enabled(device),
			 LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
	ck_assert_int_eq(libinput_device_config_tap_get_default_drag_lock_enabled(device),
			 LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);

	status = libinput_device_config_tap_set_drag_lock_enabled(device,
								  LIBINPUT_CONFIG_DRAG_LOCK_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_tap_set_drag_lock_enabled(device,
								  LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_tap_set_drag_lock_enabled(device,
								  LIBINPUT_CONFIG_DRAG_LOCK_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_tap_set_drag_lock_enabled(device,
								  3);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

START_TEST(touchpad_drag_lock_default_unavailable)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;

	ck_assert_int_eq(libinput_device_config_tap_get_drag_lock_enabled(device),
			 LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
	ck_assert_int_eq(libinput_device_config_tap_get_default_drag_lock_enabled(device),
			 LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);

	status = libinput_device_config_tap_set_drag_lock_enabled(device,
								  LIBINPUT_CONFIG_DRAG_LOCK_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);

	status = libinput_device_config_tap_set_drag_lock_enabled(device,
								  LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_tap_set_drag_lock_enabled(device,
								  3);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

void
litest_setup_tests_touchpad_tap(void)
{
	struct range multitap_range = {3, 8};

	litest_add("tap:1fg", touchpad_1fg_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:1fg", touchpad_1fg_doubletap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add_ranged("tap:1fg", touchpad_1fg_multitap, LITEST_TOUCHPAD, LITEST_ANY, &multitap_range);
	litest_add_ranged("tap:1fg", touchpad_1fg_multitap_n_drag_timeout, LITEST_TOUCHPAD, LITEST_ANY, &multitap_range);
	litest_add_ranged("tap:1fg", touchpad_1fg_multitap_n_drag_tap, LITEST_TOUCHPAD, LITEST_ANY, &multitap_range);
	litest_add_ranged("tap:1fg", touchpad_1fg_multitap_n_drag_move, LITEST_TOUCHPAD, LITEST_ANY, &multitap_range);
	litest_add_ranged("tap:1fg", touchpad_1fg_multitap_n_drag_2fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH, &multitap_range);
	litest_add_ranged("tap:1fg", touchpad_1fg_multitap_n_drag_click, LITEST_CLICKPAD, LITEST_ANY, &multitap_range);
	litest_add("tap:1fg", touchpad_1fg_tap_n_drag, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:1fg", touchpad_1fg_tap_n_drag_draglock, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:1fg", touchpad_1fg_tap_n_drag_draglock_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:1fg", touchpad_1fg_tap_n_drag_draglock_timeout, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:2fg", touchpad_2fg_tap_n_drag, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:2fg", touchpad_2fg_tap_n_drag_3fg_btntool, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_APPLE_CLICKPAD);
	litest_add("tap:2fg", touchpad_2fg_tap_n_drag_3fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:2fg", touchpad_2fg_tap, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("tap:2fg", touchpad_2fg_tap_inverted, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:2fg", touchpad_2fg_tap_n_hold_first, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:2fg", touchpad_2fg_tap_n_hold_second, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:2fg", touchpad_2fg_tap_quickrelease, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("tap:2fg", touchpad_1fg_tap_click, LITEST_TOUCHPAD|LITEST_BUTTON, LITEST_CLICKPAD);
	litest_add("tap:2fg", touchpad_2fg_tap_click, LITEST_TOUCHPAD|LITEST_BUTTON, LITEST_SINGLE_TOUCH|LITEST_CLICKPAD);

	litest_add("tap:2fg", touchpad_2fg_tap_click_apple, LITEST_APPLE_CLICKPAD, LITEST_ANY);
	litest_add("tap:2fg", touchpad_no_2fg_tap_after_move, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("tap:2fg", touchpad_no_2fg_tap_after_timeout, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("tap:2fg", touchpad_no_first_fg_tap_after_move, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:2fg", touchpad_no_first_fg_tap_after_move, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:3fg", touchpad_3fg_tap_btntool, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:3fg", touchpad_3fg_tap_btntool_inverted, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:3fg", touchpad_3fg_tap, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:3fg", touchpad_3fg_tap_quickrelease, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("tap:4fg", touchpad_4fg_tap, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("tap:4fg", touchpad_4fg_tap_quickrelease, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("tap:5fg", touchpad_5fg_tap, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);
	litest_add("tap:5fg", touchpad_5fg_tap_quickrelease, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_SEMI_MT);

	/* Real buttons don't interfere with tapping, so don't run those for
	   pads with buttons */
	litest_add("tap:1fg", touchpad_1fg_double_tap_click, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("tap:1fg", touchpad_1fg_tap_n_drag_click, LITEST_CLICKPAD, LITEST_ANY);
	litest_add_ranged("tap:1fg", touchpad_1fg_multitap_n_drag_tap_click, LITEST_CLICKPAD, LITEST_ANY, &multitap_range);
	litest_add("tap:1fg", touchpad_1fg_tap_n_drag_draglock_tap_click, LITEST_CLICKPAD, LITEST_ANY);

	litest_add("tap:config", touchpad_tap_default_disabled, LITEST_TOUCHPAD|LITEST_BUTTON, LITEST_ANY);
	litest_add("tap:config", touchpad_tap_default_enabled, LITEST_TOUCHPAD, LITEST_BUTTON);
	litest_add("tap:config", touchpad_tap_invalid, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:config", touchpad_tap_is_available, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:config", touchpad_tap_is_not_available, LITEST_ANY, LITEST_TOUCHPAD);

	litest_add("tap:1fg", clickpad_1fg_tap_click, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("tap:2fg", clickpad_2fg_tap_click, LITEST_CLICKPAD, LITEST_SINGLE_TOUCH|LITEST_APPLE_CLICKPAD);

	litest_add("tap:draglock", touchpad_drag_lock_default_disabled, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:draglock", touchpad_drag_lock_default_unavailable, LITEST_ANY, LITEST_TOUCHPAD);

	litest_add("tap:drag", touchpad_drag_default_disabled, LITEST_ANY, LITEST_TOUCHPAD);
	litest_add("tap:drag", touchpad_drag_default_enabled, LITEST_TOUCHPAD, LITEST_BUTTON);
	litest_add("tap:drag", touchpad_drag_config_invalid, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:drag", touchpad_drag_config_enabledisable, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:drag", touchpad_drag_disabled, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("tap:drag", touchpad_drag_disabled_immediate, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add_ranged("tap:drag", touchpad_drag_disabled_multitap_no_drag, LITEST_TOUCHPAD, LITEST_ANY, &multitap_range);
}
