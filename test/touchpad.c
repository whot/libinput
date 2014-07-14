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
	litest_touch_move_to(dev, 0, 50, 50, 80, 50, 5);
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
	litest_touch_move_to(dev, 0, 20, 20, 80, 80, 5);
	litest_touch_move_to(dev, 1, 70, 20, 80, 50, 5);
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

static void
assert_button_event(struct libinput *li, unsigned int button,
		    enum libinput_button_state state)
{
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	libinput_dispatch(li);
	event = libinput_get_event(li);

	ck_assert(event != NULL);
	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_POINTER_BUTTON);
	ptrev = libinput_event_get_pointer_event(event);
	ck_assert_int_eq(libinput_event_pointer_get_button(ptrev),
			 button);
	ck_assert_int_eq(libinput_event_pointer_get_button_state(ptrev),
			 state);
	libinput_event_destroy(event);
}

START_TEST(touchpad_1fg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	msleep(300); /* tap-n-drag timeout */
	assert_button_event(li, BTN_LEFT,
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
	struct libinput_event *event;

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 80, 5);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_dispatch(li);
	while (libinput_next_event_type(li) == LIBINPUT_EVENT_POINTER_MOTION) {
		event = libinput_get_event(li);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}

	ck_assert_int_eq(libinput_next_event_type(li), LIBINPUT_EVENT_NONE);

	/* lift finger, set down again, should continue dragging */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 80, 5);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
	while (libinput_next_event_type(li) == LIBINPUT_EVENT_POINTER_MOTION) {
		event = libinput_get_event(li);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}

	ck_assert_int_eq(libinput_next_event_type(li), LIBINPUT_EVENT_NONE);

	msleep(300); /* tap-n-drag timeout */

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 70);
	litest_touch_up(dev, 0);
	litest_touch_up(dev, 1);

	libinput_dispatch(li);

	assert_button_event(li, BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	msleep(300); /* tap-n-drag timeout */
	assert_button_event(li, BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_inverted)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

	litest_drain_events(dev->libinput);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_down(dev, 1, 70, 70);
	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	assert_button_event(li, BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	msleep(300); /* tap-n-drag timeout */
	assert_button_event(li, BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

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

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

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

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_2fg_tap_click_apple)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

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

	assert_button_event(li, BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_button_event(li, BTN_RIGHT,
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
	litest_touch_move_to(dev, 0, 50, 50, 90, 90, 10);
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
	msleep(300);
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
	litest_touch_move_to(dev, 1, 70, 50, 90, 90, 10);
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

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

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

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);
	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_1fg_tap_n_drag_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

	litest_drain_events(dev->libinput);

	/* one finger down, up, down, move, button click, finger up
	   -> two button left event pairs, motion allowed */
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 80, 50, 10);

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_dispatch(li);

	ck_assert_int_eq(libinput_next_event_type(li),
			 LIBINPUT_EVENT_POINTER_MOTION);
	while (libinput_next_event_type(li) == LIBINPUT_EVENT_POINTER_MOTION) {
		event = libinput_get_event(li);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}

	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);
	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
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

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_button_event(li, BTN_LEFT,
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

	assert_button_event(li, BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_button_event(li, BTN_RIGHT,
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

	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);
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
	struct libinput_event *event;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	assert_button_event(li, BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);

	libinput_dispatch(li);
	ck_assert_int_eq(libinput_next_event_type(li), LIBINPUT_EVENT_NONE);

	/* now put a second finger down */
	litest_touch_down(dev, 1, 70, 70);
	litest_touch_move_to(dev, 1, 70, 70, 80, 50, 5);
	litest_touch_up(dev, 1);

	libinput_dispatch(li);
	ck_assert_int_eq(libinput_next_event_type(li),
			 LIBINPUT_EVENT_POINTER_MOTION);
	do {
		event = libinput_get_event(li);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	} while (libinput_next_event_type(li) == LIBINPUT_EVENT_POINTER_MOTION);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li, BTN_LEFT,
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

	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li,
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

	assert_button_event(li,
			    BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_PRESSED);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li,
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

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

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

	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);
	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(clickpad_softbutton_right_tap_n_drag)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	libinput_device_config_tap_set_enabled(dev->libinput_device, 1);

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

	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_RELEASED);
	assert_button_event(li,
			    BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li,
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

	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	/* move out of the area, then wait for softbutton timer */
	litest_touch_move_to(dev, 0, 20, 90, 90, 20, 10);
	libinput_dispatch(li);
	msleep(400);
	libinput_dispatch(li);
	litest_drain_events(li);

	/* move down left, expect motion */
	litest_touch_move_to(dev, 0, 90, 20, 20, 90, 10);

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

	assert_button_event(li,
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

	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 1, 20, 20);
	litest_touch_move_to(dev, 1, 20, 20, 80, 20, 10);

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

		ck_assert(x > 0);
		ck_assert(y == 0);

		libinput_event_destroy(event);
		libinput_dispatch(li);
		event = libinput_get_event(li);
	}
	litest_touch_up(dev, 1);

	/* second finger down */
	litest_touch_down(dev, 1, 20, 20);
	litest_touch_move_to(dev, 1, 20, 20, 20, 80, 10);

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

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li,
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
	litest_touch_move_to(dev, 0, 20, 90, 90, 90, 10);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	assert_button_event(li,
			    BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li,
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
	litest_touch_move_to(dev, 0, 90, 90, 20, 90, 10);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li,
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

	assert_button_event(li,
			    BTN_LEFT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li,
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

	assert_button_event(li,
			    BTN_RIGHT,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li,
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

	assert_button_event(li,
			    BTN_MIDDLE,
			    LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_LEFT, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_touch_up(dev, 0);

	assert_button_event(li,
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
	msleep(200);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

	litest_touch_move_to(dev, 0, 50, 5, 80, 90, 20);
	libinput_dispatch(li);
	msleep(400);
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
test_2fg_scroll(struct litest_device *dev, int dx, int dy, int sleep)
{
	struct libinput *li = dev->libinput;

	litest_touch_down(dev, 0, 47, 50);
	litest_touch_down(dev, 1, 53, 50);

	litest_touch_move_to(dev, 0, 47, 50, 47 + dx, 50 + dy, 5);
	litest_touch_move_to(dev, 1, 53, 50, 53 + dx, 50 + dy, 5);

	/* Avoid a small scroll being seen as a tap */
	if (sleep) {
		libinput_dispatch(li);
		msleep(sleep);
		libinput_dispatch(li);
	}

	litest_touch_up(dev, 1);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);
}

static void
check_2fg_scroll(struct litest_device *dev, unsigned int axis, int dir)
{
	struct libinput *li = dev->libinput;
	struct libinput_event *event, *next_event;
	struct libinput_event_pointer *ptrev;

	event = libinput_get_event(li);
	next_event = libinput_get_event(li);
	ck_assert(next_event != NULL); /* At least 1 scroll + stop scroll */

	while (event) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_AXIS);
		ptrev = libinput_event_get_pointer_event(event);
		ck_assert(ptrev != NULL);
		ck_assert_int_eq(libinput_event_pointer_get_axis(ptrev), axis);

		if (next_event) {
			/* Normal scroll event, check dir */
			if (dir > 0) {
				ck_assert_int_ge(
					libinput_event_pointer_get_axis_value(ptrev),
					dir);
			} else {
				ck_assert_int_le(
					libinput_event_pointer_get_axis_value(ptrev),
					dir);
			}
		} else {
			/* Last scroll event, must be 0 */
			ck_assert_int_eq(
				libinput_event_pointer_get_axis_value(ptrev),
				0);
		}
		libinput_event_destroy(event);
		event = next_event;
		next_event = libinput_get_event(li);
	}
}

START_TEST(touchpad_2fg_scroll)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	/* Note this mixes in a tiny amount of movement in the wrong direction,
	   which should be ignored */
	test_2fg_scroll(dev, 1, 40, 0);
	check_2fg_scroll(dev, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, 10);
	test_2fg_scroll(dev, 1, -40, 0);
	check_2fg_scroll(dev, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, -10);
	test_2fg_scroll(dev, 40, 1, 0);
	check_2fg_scroll(dev, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, 10);
	test_2fg_scroll(dev, -40, 1, 0);
	check_2fg_scroll(dev, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, -10);

	/* 2fg scroll smaller than the threshold should not generate events */
	test_2fg_scroll(dev, 1, 1, 200);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_tap_is_available)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_ge(libinput_device_config_tap_get_finger_count(dev->libinput_device), 1);
	ck_assert_int_eq(libinput_device_config_tap_get_enabled(dev->libinput_device), 0);
}
END_TEST

START_TEST(touchpad_tap_is_not_available)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_eq(libinput_device_config_tap_get_finger_count(dev->libinput_device), 0);
	ck_assert_int_eq(libinput_device_config_tap_get_enabled(dev->libinput_device), 0);
	ck_assert_int_eq(libinput_device_config_tap_set_enabled(dev->libinput_device, 1),
			 LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
}
END_TEST

START_TEST(touchpad_tap_default)
{
	struct litest_device *dev = litest_current_device();

	ck_assert_int_eq(libinput_device_config_tap_get_default_enabled(dev->libinput_device), 0);
}
END_TEST

START_TEST(touchpad_palm_detect_at_edge)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to(dev, 0, 99, 50, 99, 70, 5);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 50);
	litest_touch_move_to(dev, 0, 5, 50, 5, 70, 5);
	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_palm_detect_at_bottom_corners)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	/* Run for non-clickpads only: make sure the bottom corners trigger
	   palm detection too */
	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 95);
	litest_touch_move_to(dev, 0, 99, 95, 99, 99, 10);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 95);
	litest_touch_move_to(dev, 0, 5, 95, 5, 99, 5);
	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_palm_detect_at_top_corners)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	/* Run for non-clickpads only: make sure the bottom corners trigger
	   palm detection too */
	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 5);
	litest_touch_move_to(dev, 0, 99, 5, 99, 9, 10);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 5, 5);
	litest_touch_move_to(dev, 0, 5, 5, 5, 9, 5);
	litest_touch_up(dev, 0);
}
END_TEST

START_TEST(touchpad_palm_detect_palm_stays_palm)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 20);
	litest_touch_move_to(dev, 0, 99, 20, 75, 99, 5);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_palm_becomes_pointer)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *ev;
	enum libinput_event_type type;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 99, 50);
	litest_touch_move_to(dev, 0, 99, 70, 0, 70, 5);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	ev = libinput_get_event(li);
	ck_assert_notnull(ev);
	do {
		type = libinput_event_get_type(ev);
		ck_assert_int_eq(type, LIBINPUT_EVENT_POINTER_MOTION);

		libinput_event_destroy(ev);
		ev = libinput_get_event(li);
	} while (ev);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(touchpad_palm_detect_no_palm_moving_into_edges)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *ev;
	enum libinput_event_type type;

	/* moving non-palm into the edge does not label it as palm */
	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 99, 50, 5);

	litest_drain_events(li);

	litest_touch_move_to(dev, 0, 99, 50, 99, 90, 5);
	libinput_dispatch(li);

	type = libinput_next_event_type(li);
	do {

		ck_assert_int_eq(type, LIBINPUT_EVENT_POINTER_MOTION);
		ev = libinput_get_event(li);
		libinput_event_destroy(ev);

		type = libinput_next_event_type(li);
		libinput_dispatch(li);
	} while (type != LIBINPUT_EVENT_NONE);

	litest_touch_up(dev, 0);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);
}
END_TEST

int main(int argc, char **argv) {

	litest_add("touchpad:motion", touchpad_1fg_motion, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:motion", touchpad_2fg_no_motion, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);

	litest_add("touchpad:tap", touchpad_1fg_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_1fg_tap_n_drag, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_2fg_tap, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_2fg_tap_inverted, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_1fg_tap_click, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_2fg_tap_click, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH|LITEST_APPLE_CLICKPAD);
	litest_add("touchpad:tap", touchpad_2fg_tap_click_apple, LITEST_APPLE_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_no_2fg_tap_after_move, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_no_2fg_tap_after_timeout, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_no_first_fg_tap_after_move, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);
	litest_add("touchpad:tap", touchpad_no_first_fg_tap_after_move, LITEST_TOUCHPAD, LITEST_SINGLE_TOUCH);

	/* Real buttons don't interfere with tapping, so don't run those for
	   pads with buttons */
	litest_add("touchpad:tap", touchpad_1fg_double_tap_click, LITEST_CLICKPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_1fg_tap_n_drag_click, LITEST_CLICKPAD, LITEST_ANY);

	litest_add("touchpad:tap", touchpad_tap_default, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_tap_is_available, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:tap", touchpad_tap_is_not_available, LITEST_ANY, LITEST_TOUCHPAD);

	litest_add_no_device("touchpad:clickfinger", touchpad_1fg_clickfinger);
	litest_add_no_device("touchpad:clickfinger", touchpad_2fg_clickfinger);

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

	litest_add("touchpad:palm", touchpad_palm_detect_at_edge, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_at_bottom_corners, LITEST_TOUCHPAD, LITEST_CLICKPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_at_top_corners, LITEST_TOUCHPAD, LITEST_TOPBUTTONPAD);
	litest_add("touchpad:palm", touchpad_palm_detect_palm_becomes_pointer, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_palm_stays_palm, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("touchpad:palm", touchpad_palm_detect_no_palm_moving_into_edges, LITEST_TOUCHPAD, LITEST_ANY);

	return litest_run(argc, argv);
}
