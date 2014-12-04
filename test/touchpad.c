/*
 * Copyright © 2014 Red Hat, Inc.
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

#include "libinput-util.h"
#include "litest.h"

START_TEST(touchpad_1fg_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

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

START_TEST(touchpad_1fg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

START_TEST(touchpad_1fg_tap_n_drag)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

START_TEST(touchpad_1fg_tap_n_drag_timeout)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_n_drag)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

	/* This will wait for the DRAGGING_WAIT timeout */
	litest_assert_button_event(li, BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_n_drag_3fg_btntool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

START_TEST(touchpad_2fg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

START_TEST(touchpad_1fg_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

START_TEST(touchpad_3fg_tap_btntool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

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

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

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
	struct litest_device *dev = litest_create_device(LITEST_BCM5974);
	struct libinput *li = dev->libinput;

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

	litest_delete_device(dev);
}
END_TEST

START_TEST(touchpad_2fg_clickfinger)
{
	struct litest_device *dev = litest_create_device(LITEST_BCM5974);
	struct libinput *li = dev->libinput;

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

	litest_delete_device(dev);
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

START_TEST(clickpad_1fg_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

START_TEST(clickpad_btn_left)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

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

static void
test_2fg_scroll(struct litest_device *dev, double dx, double dy, int want_sleep)
{
	struct libinput *li = dev->libinput;

	litest_touch_down(dev, 0, 47, 50);
	litest_touch_down(dev, 1, 53, 50);

	litest_touch_move_to(dev, 0, 47, 50, 47 + dx, 50 + dy, 5, 0);
	litest_touch_move_to(dev, 1, 53, 50, 53 + dx, 50 + dy, 5, 0);

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

	litest_drain_events(li);

	litest_touch_down(dev, 0, 20, 30);
	litest_touch_down(dev, 1, 40, 30);
	litest_touch_move_to(dev, 0, 20, 30, 20, 50, 60, 10);
	litest_touch_move_to(dev, 1, 40, 30, 40, 50, 60, 10);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert_notnull(event);

	/* last event is value 0, tested elsewhere */
	while (libinput_next_event_type(li) != LIBINPUT_EVENT_NONE) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_AXIS);
		ptrev = libinput_event_get_pointer_event(event);

		ck_assert_int_eq(libinput_event_pointer_get_axis(ptrev),
				 LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		ck_assert(libinput_event_pointer_get_axis_value(ptrev) > 0.0);

		/* this is to verify we test the right thing, if the value
		   is greater than scroll.threshold we triggered the wrong
		   condition */
		ck_assert(libinput_event_pointer_get_axis_value(ptrev) < 5.0);

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

	test_2fg_scroll(dev, 0, 20, 0);
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

START_TEST(touchpad_2fg_scroll_return_to_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	/* start with motion */
	litest_touch_down(dev, 0, 70, 70);
	litest_touch_move_to(dev, 0, 70, 70, 47, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* 2fg scroll */
	litest_touch_down(dev, 1, 53, 50);
	litest_touch_move_to(dev, 0, 47, 50, 47, 70, 5, 0);
	litest_touch_move_to(dev, 1, 53, 50, 53, 70, 5, 0);
	litest_touch_up(dev, 1);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	litest_touch_move_to(dev, 0, 47, 70, 47, 50, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* back to 2fg scroll, lifting the other finger */
	litest_touch_down(dev, 1, 50, 50);
	litest_touch_move_to(dev, 0, 47, 50, 47, 70, 5, 0);
	litest_touch_move_to(dev, 1, 53, 50, 53, 70, 5, 0);
	litest_touch_up(dev, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_AXIS);

	/* move with second finger */
	litest_touch_move_to(dev, 1, 53, 70, 53, 50, 10, 0);
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

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 10);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 99, 80);
	litest_touch_move_to(dev, 0, 99, 80, 99, 20, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -10);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 20, 99);
	litest_touch_move_to(dev, 0, 20, 99, 70, 99, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 10);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 70, 99);
	litest_touch_move_to(dev, 0, 70, 99, 20, 99, 10, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -10);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_edge_scroll_slow_distance)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 80, 60, 10);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert_notnull(event);

	while (libinput_next_event_type(li) != LIBINPUT_EVENT_NONE) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_AXIS);
		ptrev = libinput_event_get_pointer_event(event);

		ck_assert_int_eq(libinput_event_pointer_get_axis(ptrev),
				 LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		ck_assert(libinput_event_pointer_get_axis_value(ptrev) > 0.0);

		/* this is to verify we test the right thing, if the value
		   is greater than scroll.threshold we triggered the wrong
		   condition */
		ck_assert(libinput_event_pointer_get_axis_value(ptrev) < 5.0);

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

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 99, 60, 10, 0);
	/* moving outside -> no motion event */
	litest_touch_move_to(dev, 0, 99, 60, 20, 80, 10, 0);
	/* moving down outside edge once scrolling had started -> scroll */
	litest_touch_move_to(dev, 0, 20, 80, 40, 99, 10, 0);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	litest_assert_scroll(li, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 5);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_edge_scroll_no_edge_after_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

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

START_TEST(touchpad_tap_is_available)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_ge(libinput_device_config_tap_get_finger_count(dev->libinput_device), 1);
	ck_assert_int_eq(libinput_device_config_tap_get_enabled(dev->libinput_device),
			 LIBINPUT_CONFIG_TAP_DISABLED);
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
}
END_TEST

START_TEST(touchpad_tap_default)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_eq(libinput_device_config_tap_get_default_enabled(dev->libinput_device),
			 LIBINPUT_CONFIG_TAP_DISABLED);
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

static int
touchpad_has_palm_detect_size(struct litest_device *dev)
{
	double width, height;
	int rc;

	if (libinput_device_get_id_vendor(dev->libinput_device) == 0x5ac) /* Apple */
		return 1;

	rc = libinput_device_get_size(dev->libinput_device, &width, &height);

	return rc == 0 && width >= 80;
}

START_TEST(touchpad_palm_detect_at_edge)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

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

START_TEST(touchpad_palm_detect_at_bottom_corners)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	if (!touchpad_has_palm_detect_size(dev))
		return;

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

START_TEST(touchpad_left_handed)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	status = libinput_device_config_buttons_set_left_handed(d, 1);
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

	status = libinput_device_config_buttons_set_left_handed(d, 1);
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

	status = libinput_device_config_buttons_set_left_handed(d, 1);
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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);
	status = libinput_device_config_buttons_set_left_handed(d, 1);
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

	libinput_device_config_tap_set_enabled(dev->libinput_device,
					       LIBINPUT_CONFIG_TAP_ENABLED);
	status = libinput_device_config_buttons_set_left_handed(d, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 50);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

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

	status = libinput_device_config_buttons_set_left_handed(d, 1);
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
	litest_button_click(dev, BTN_LEFT, 1);
	libinput_dispatch(li);

	status = libinput_device_config_buttons_set_left_handed(d, 0);
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

	status = libinput_device_config_buttons_set_left_handed(d, 1);
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

	status = libinput_device_config_buttons_set_left_handed(d, 0);
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

int main(int argc, char **argv) {


	litest_add("touchpad:motion", touchpad_1fg_motion, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:motion", touchpad_2fg_no_motion, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);

	litest_add("touchpad:tap", touchpad_1fg_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_1fg_tap_n_drag, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_1fg_tap_n_drag_timeout, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_2fg_tap_n_drag, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_2fg_tap_n_drag_3fg_btntool, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:tap", touchpad_2fg_tap, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_2fg_tap_inverted, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_1fg_tap_click, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:tap", touchpad_2fg_tap_click, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_CLICKPAD);

	litest_add("touchpad:tap", touchpad_2fg_tap_click_apple, LITEST_APPLE_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_no_2fg_tap_after_move, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_no_2fg_tap_after_timeout, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_no_first_fg_tap_after_move, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_no_first_fg_tap_after_move, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	/* apple is the only one with real 3-finger support */
	litest_add("touchpad:tap", touchpad_3fg_tap_btntool, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:tap", touchpad_3fg_tap_btntool_inverted, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:tap", touchpad_3fg_tap, LITEST_APPLE_CLICKPAD, LITEST_ANY);

	/* Real buttons don't interfere with tapping, so don't run those for
	   pads with buttons */
	litest_add("touchpad:tap", touchpad_1fg_double_tap_click, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_1fg_tap_n_drag_click, LITEST_CLICKPAD, LITEST_ANY);

	litest_add("touchpad:tap", touchpad_tap_default, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_tap_invalid, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_tap_is_available, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_tap_is_not_available, LITEST_ANY, LITEST_TOUCHPAD);

	litest_add("touchpad:tap", clickpad_1fg_tap_click, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:tap", clickpad_2fg_tap_click, LITEST_CLICKPAD, LITEST_SINGLE_TOUCH|LITEST_APPLE_CLICKPAD);

	litest_add_no_device("touchpad:clickfinger", touchpad_1fg_clickfinger);
	litest_add_no_device("touchpad:clickfinger", touchpad_2fg_clickfinger);

	litest_add("touchpad:click", touchpad_click_defaults_clickfinger, LITEST_APPLE_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:click", touchpad_click_defaults_btnarea, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:click", touchpad_click_defaults_none, LITEST_TOUCHPAD, LITEST_CLICKPAD);

	litest_add("touchpad:click", touchpad_btn_left, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:click", clickpad_btn_left, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:click", clickpad_click_n_drag, LITEST_CLICKPAD, LITEST_SINGLE_TOUCH);

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

	litest_add("touchpad:scroll", touchpad_2fg_scroll, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_slow_distance, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_return_to_motion, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_2fg_scroll_source, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_scroll_natural_defaults, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_scroll_natural_enable_config, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_scroll_natural, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:scroll", touchpad_edge_scroll, LITEST_TOUCHPAD|LITEST_SINGLE_TOUCH, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_no_motion, LITEST_TOUCHPAD|LITEST_SINGLE_TOUCH, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_no_edge_after_motion, LITEST_TOUCHPAD|LITEST_SINGLE_TOUCH, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_slow_distance, LITEST_TOUCHPAD|LITEST_SINGLE_TOUCH, LITEST_ANY);
	litest_add("touchpad:scroll", touchpad_edge_scroll_source, LITEST_TOUCHPAD|LITEST_SINGLE_TOUCH, LITEST_ANY);

	litest_add("touchpad:palm", touchpad_palm_detect_at_edge, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_at_bottom_corners, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_at_top_corners, LITEST_TOUCHPAD, LITEST_TOPBUTTONPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_palm_becomes_pointer, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_palm_stays_palm, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_no_palm_moving_into_edges, LITEST_TOUCHPAD, LITEST_ANY);

	litest_add("touchpad:left-handed", touchpad_left_handed, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:left-handed", touchpad_left_handed_clickpad, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:left-handed", touchpad_left_handed_clickfinger, LITEST_APPLE_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:left-handed", touchpad_left_handed_tapping, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:left-handed", touchpad_left_handed_tapping_2fg, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:left-handed", touchpad_left_handed_delayed, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:left-handed", touchpad_left_handed_clickpad_delayed, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);

	return litest_run(argc, argv);
}
