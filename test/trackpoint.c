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

START_TEST(trackpoint_middlebutton)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;
	uint64_t ptime, rtime;

	litest_drain_events(li);

	/* A quick middle button click should get reported normally */
	litest_button_click(dev, BTN_MIDDLE, 1);
	msleep(2);
	litest_button_click(dev, BTN_MIDDLE, 0);

	litest_wait_for_event(li);

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_MIDDLE,
				       LIBINPUT_BUTTON_STATE_PRESSED);
	ptime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	ptrev = litest_is_button_event(event,
				       BTN_MIDDLE,
				       LIBINPUT_BUTTON_STATE_RELEASED);
	rtime = libinput_event_pointer_get_time(ptrev);
	libinput_event_destroy(event);

	ck_assert_int_lt(ptime, rtime);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(trackpoint_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_button_scroll(dev, BTN_MIDDLE, 1, 6);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 6);
	litest_button_scroll(dev, BTN_MIDDLE, 1, -7);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -7);
	litest_button_scroll(dev, BTN_MIDDLE, 8, 1);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 8);
	litest_button_scroll(dev, BTN_MIDDLE, -9, 1);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -9);

	/* scroll smaller than the threshold should not generate events */
	litest_button_scroll(dev, BTN_MIDDLE, 1, 1);
	/* long middle press without movement should not generate events */
	litest_button_scroll(dev, BTN_MIDDLE, 0, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(trackpoint_middlebutton_noscroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	/* Disable middle button scrolling */
	libinput_device_config_scroll_set_method(dev->libinput_device,
					LIBINPUT_CONFIG_SCROLL_NO_SCROLL);

	litest_drain_events(li);

	/* A long middle button click + motion should get reported normally now */
	litest_button_scroll(dev, BTN_MIDDLE, 0, 10);

	litest_assert_button_event(li, BTN_MIDDLE, 1);

	event = libinput_get_event(li);
	ck_assert(event != NULL);
	ck_assert_int_eq(libinput_event_get_type(event), LIBINPUT_EVENT_POINTER_MOTION);
	libinput_event_destroy(event);

	litest_assert_button_event(li, BTN_MIDDLE, 0);

	litest_assert_empty_queue(li);

	/* Restore default scroll behavior */
	libinput_device_config_scroll_set_method(dev->libinput_device,
		libinput_device_config_scroll_get_default_method(
			dev->libinput_device));
}
END_TEST

START_TEST(trackpoint_scroll_source)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	litest_drain_events(li);

	litest_button_scroll(dev, BTN_MIDDLE, 0, 6);
	litest_wait_for_event_of_type(li, LIBINPUT_EVENT_POINTER_AXIS, -1);

	while ((event = libinput_get_event(li))) {
		ptrev = libinput_event_get_pointer_event(event);

		ck_assert_int_eq(libinput_event_pointer_get_axis_source(ptrev),
				 LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS);

		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(trackpoint_topsoftbuttons_left_handed_trackpoint)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;
	enum libinput_config_status status;
	struct libinput_event *event;
	struct libinput_device *device;

	trackpoint = litest_add_device(li, LITEST_TRACKPOINT);
	litest_drain_events(li);
	/* touchpad right-handed, trackpoint left-handed */
	status = libinput_device_config_left_handed_set(
					trackpoint->libinput_device, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_touch_down(touchpad, 0, 5, 5);
	libinput_dispatch(li);
	litest_button_click(touchpad, BTN_LEFT, true);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_RIGHT,
			       LIBINPUT_BUTTON_STATE_PRESSED);
	device = libinput_event_get_device(event);
	ck_assert(device == trackpoint->libinput_device);
	libinput_event_destroy(event);

	litest_button_click(touchpad, BTN_LEFT, false);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_RIGHT,
			       LIBINPUT_BUTTON_STATE_RELEASED);
	device = libinput_event_get_device(event);
	ck_assert(device == trackpoint->libinput_device);
	libinput_event_destroy(event);

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(trackpoint_topsoftbuttons_left_handed_touchpad)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;
	enum libinput_config_status status;
	struct libinput_event *event;
	struct libinput_device *device;

	trackpoint = litest_add_device(li, LITEST_TRACKPOINT);
	litest_drain_events(li);
	/* touchpad left-handed, trackpoint right-handed */
	status = libinput_device_config_left_handed_set(
					touchpad->libinput_device, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_touch_down(touchpad, 0, 5, 5);
	libinput_dispatch(li);
	litest_button_click(touchpad, BTN_LEFT, true);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_is_button_event(event, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	device = libinput_event_get_device(event);
	ck_assert(device == trackpoint->libinput_device);
	libinput_event_destroy(event);

	litest_button_click(touchpad, BTN_LEFT, false);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_LEFT,
			       LIBINPUT_BUTTON_STATE_RELEASED);
	device = libinput_event_get_device(event);
	ck_assert(device == trackpoint->libinput_device);
	libinput_event_destroy(event);

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(trackpoint_topsoftbuttons_left_handed_both)
{
	struct litest_device *touchpad = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = touchpad->libinput;
	enum libinput_config_status status;
	struct libinput_event *event;
	struct libinput_device *device;

	trackpoint = litest_add_device(li, LITEST_TRACKPOINT);
	litest_drain_events(li);
	/* touchpad left-handed, trackpoint left-handed */
	status = libinput_device_config_left_handed_set(
					touchpad->libinput_device, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_left_handed_set(
					trackpoint->libinput_device, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_touch_down(touchpad, 0, 5, 5);
	libinput_dispatch(li);
	litest_button_click(touchpad, BTN_LEFT, true);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_RIGHT,
			       LIBINPUT_BUTTON_STATE_PRESSED);
	device = libinput_event_get_device(event);
	ck_assert(device == trackpoint->libinput_device);
	libinput_event_destroy(event);

	litest_button_click(touchpad, BTN_LEFT, false);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	litest_is_button_event(event,
			       BTN_RIGHT,
			       LIBINPUT_BUTTON_STATE_RELEASED);
	device = libinput_event_get_device(event);
	ck_assert(device == trackpoint->libinput_device);
	libinput_event_destroy(event);

	litest_delete_device(trackpoint);
}
END_TEST

void
litest_setup_tests_trackpoint(void)
{
	litest_add("trackpoint:middlebutton", trackpoint_middlebutton, LITEST_POINTINGSTICK, LITEST_ANY);
	litest_add("trackpoint:middlebutton", trackpoint_middlebutton_noscroll, LITEST_POINTINGSTICK, LITEST_ANY);
	litest_add("trackpoint:scroll", trackpoint_scroll, LITEST_POINTINGSTICK, LITEST_ANY);
	litest_add("trackpoint:scroll", trackpoint_scroll_source, LITEST_POINTINGSTICK, LITEST_ANY);
	litest_add("trackpoint:left-handed", trackpoint_topsoftbuttons_left_handed_trackpoint, LITEST_TOPBUTTONPAD, LITEST_ANY);
	litest_add("trackpoint:left-handed", trackpoint_topsoftbuttons_left_handed_touchpad, LITEST_TOPBUTTONPAD, LITEST_ANY);
	litest_add("trackpoint:left-handed", trackpoint_topsoftbuttons_left_handed_both, LITEST_TOPBUTTONPAD, LITEST_ANY);
}
