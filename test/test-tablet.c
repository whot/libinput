/*
 * Copyright © 2014-2015 Red Hat, Inc.
 * Copyright © 2014 Lyude Paul
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
#include <stdarg.h>

#include "libinput-util.h"
#include "evdev-tablet.h"
#include "litest.h"

START_TEST(button_down_up)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	if (!libevdev_has_event_code(dev->evdev, EV_KEY, BTN_STYLUS))
		return;

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button(tev),
			 BTN_STYLUS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button_state(tev),
			 LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_STYLUS, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button(tev),
			 BTN_STYLUS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button_state(tev),
			 LIBINPUT_BUTTON_STATE_RELEASED);
	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

}
END_TEST

START_TEST(button_seat_count)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct litest_device *dev2;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	if (!libevdev_has_event_code(dev->evdev, EV_KEY, BTN_STYLUS))
		return;

	dev2 = litest_add_device(li, LITEST_WACOM_CINTIQ_13HDT_PEN);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_tablet_proximity_in(dev2, 10, 10, axes);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev2, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev2, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button(tev),
			 BTN_STYLUS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button_state(tev),
			 LIBINPUT_BUTTON_STATE_PRESSED);
	ck_assert_int_eq(libinput_event_tablet_tool_get_seat_button_count(tev), 1);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button(tev),
			 BTN_STYLUS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button_state(tev),
			 LIBINPUT_BUTTON_STATE_PRESSED);
	ck_assert_int_eq(libinput_event_tablet_tool_get_seat_button_count(tev), 2);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);

	litest_event(dev2, EV_KEY, BTN_STYLUS, 0);
	litest_event(dev2, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_STYLUS, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button_state(tev),
			 LIBINPUT_BUTTON_STATE_RELEASED);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button(tev),
			 BTN_STYLUS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_seat_button_count(tev), 1);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button_state(tev),
			 LIBINPUT_BUTTON_STATE_RELEASED);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button(tev),
			 BTN_STYLUS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_seat_button_count(tev), 0);
	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

	litest_delete_device(dev2);
}
END_TEST

START_TEST(button_up_on_delete)
{
	struct libinput *li = litest_create_context();
	struct litest_device *dev = litest_add_device(li, LITEST_WACOM_INTUOS);
	struct libevdev *evdev = libevdev_new();
	unsigned int code;

	litest_tablet_proximity_in(dev, 10, 10, NULL);
	litest_drain_events(li);

	for (code = BTN_LEFT; code <= BTN_TASK; code++) {
		if (!libevdev_has_event_code(dev->evdev, EV_KEY, code))
			continue;

		libevdev_enable_event_code(evdev, EV_KEY, code, NULL);
		litest_event(dev, EV_KEY, code, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}

	litest_drain_events(li);
	litest_delete_device(dev);
	libinput_dispatch(li);

	for (code = BTN_LEFT; code <= BTN_TASK; code++) {
		if (!libevdev_has_event_code(evdev, EV_KEY, code))
			continue;

		litest_assert_tablet_button_event(li,
					  code,
					  LIBINPUT_BUTTON_STATE_RELEASED);
	}

	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	libevdev_free(evdev);
	libinput_unref(li);
}
END_TEST

START_TEST(tip_down_up)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 30);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_DOWN);
	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 10);
	litest_axis_set_value(axes, ABS_PRESSURE, 0);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);

}
END_TEST

START_TEST(tip_down_prox_in)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 0 },
		{ ABS_PRESSURE, 30 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	ck_assert_int_eq(libinput_event_tablet_tool_get_proximity_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_DOWN);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);

}
END_TEST

static inline bool
tablet_has_proxout_quirk(struct litest_device *dev)
{
	bool is_set = false;
	if (!quirks_get_bool(dev->quirks,
			     QUIRK_MODEL_TABLET_NO_PROXIMITY_OUT,
			     &is_set))
		return false;

	return is_set;
}

START_TEST(tip_up_prox_out)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 0 },
		{ ABS_PRESSURE, 30 },
		{ -1, -1 }
	};

	if (tablet_has_proxout_quirk(dev))
		return;

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 30);
	litest_axis_set_value(axes, ABS_PRESSURE, 0);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_tablet_proximity_out(dev);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	litest_timeout_tablet_proxout();
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	ck_assert_int_eq(libinput_event_tablet_tool_get_proximity_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);

}
END_TEST

START_TEST(tip_up_btn_change)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 0 },
		{ ABS_PRESSURE, 30 },
		{ -1, -1 }
	};

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 30);
	litest_axis_set_value(axes, ABS_PRESSURE, 0);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 20, axes);
	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button(tablet_event),
			 BTN_STYLUS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button_state(tablet_event),
			 LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 30);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	/* same thing with a release at tip-up */
	litest_axis_set_value(axes, ABS_DISTANCE, 30);
	litest_axis_set_value(axes, ABS_PRESSURE, 0);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_event(dev, EV_KEY, BTN_STYLUS, 0);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button(tablet_event),
			 BTN_STYLUS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button_state(tablet_event),
			 LIBINPUT_BUTTON_STATE_RELEASED);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(tip_down_btn_change)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 30);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 20, axes);
	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_DOWN);
	libinput_event_destroy(event);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button(tablet_event),
			 BTN_STYLUS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button_state(tablet_event),
			 LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 30);
	litest_axis_set_value(axes, ABS_PRESSURE, 0);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 20, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	/* same thing with a release at tip-down */
	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 30);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 20, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_KEY, BTN_STYLUS, 0);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_DOWN);
	libinput_event_destroy(event);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button(tablet_event),
			 BTN_STYLUS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_button_state(tablet_event),
			 LIBINPUT_BUTTON_STATE_RELEASED);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(tip_down_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};
	double x, y, last_x, last_y;

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	last_x = libinput_event_tablet_tool_get_x(tablet_event);
	last_y = libinput_event_tablet_tool_get_y(tablet_event);
	libinput_event_destroy(event);

	/* move x/y on tip down, make sure x/y changed */
	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 20);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 70, 70, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_DOWN);
	ck_assert(libinput_event_tablet_tool_x_has_changed(tablet_event));
	ck_assert(libinput_event_tablet_tool_y_has_changed(tablet_event));
	x = libinput_event_tablet_tool_get_x(tablet_event);
	y = libinput_event_tablet_tool_get_y(tablet_event);
	ck_assert_double_lt(last_x, x);
	ck_assert_double_lt(last_y, y);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(tip_up_motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 0 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};
	double x, y, last_x, last_y;

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_PRESSURE, 20);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 70, 70, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	last_x = libinput_event_tablet_tool_get_x(tablet_event);
	last_y = libinput_event_tablet_tool_get_y(tablet_event);
	libinput_event_destroy(event);

	/* move x/y on tip up, make sure x/y changed */
	litest_axis_set_value(axes, ABS_PRESSURE, 0);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 40, 40, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_pop_event_frame(dev);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	ck_assert(libinput_event_tablet_tool_x_has_changed(tablet_event));
	ck_assert(libinput_event_tablet_tool_y_has_changed(tablet_event));
	x = libinput_event_tablet_tool_get_x(tablet_event);
	y = libinput_event_tablet_tool_get_y(tablet_event);
	ck_assert_double_ne(last_x, x);
	ck_assert_double_ne(last_y, y);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(tip_state_proximity)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	litest_axis_set_value(axes, ABS_PRESSURE, 30);
	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);

	litest_axis_set_value(axes, ABS_PRESSURE, 0);
	litest_axis_set_value(axes, ABS_DISTANCE, 10);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_pop_event_frame(dev);

	litest_drain_events(li);

	litest_tablet_proximity_out(dev);
	libinput_dispatch(li);

	litest_timeout_tablet_proxout();
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(tip_state_axis)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	litest_tablet_motion(dev, 70, 70, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	litest_axis_set_value(axes, ABS_PRESSURE, 30);
	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 40, 40, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	litest_tablet_motion(dev, 30, 30, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_DOWN);
	libinput_event_destroy(event);

	litest_axis_set_value(axes, ABS_PRESSURE, 0);
	litest_axis_set_value(axes, ABS_DISTANCE, 10);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 40, 40, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	litest_tablet_motion(dev, 40, 80, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(tip_state_button)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	litest_axis_set_value(axes, ABS_PRESSURE, 30);
	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 40, 40, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_STYLUS, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_DOWN);
	libinput_event_destroy(event);

	litest_axis_set_value(axes, ABS_PRESSURE, 0);
	litest_axis_set_value(axes, ABS_DISTANCE, 10);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 40, 40, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 0);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	litest_event(dev, EV_KEY, BTN_STYLUS, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(tip_up_on_delete)
{
	struct libinput *li = litest_create_context();
	struct litest_device *dev = litest_add_device(li, LITEST_WACOM_INTUOS);
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 30);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);

	litest_drain_events(li);
	litest_delete_device(dev);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_TIP);
	ck_assert_int_eq(libinput_event_tablet_tool_get_tip_state(tablet_event),
			 LIBINPUT_TABLET_TOOL_TIP_UP);
	libinput_event_destroy(event);

	libinput_unref(li);
}
END_TEST

START_TEST(proximity_in_out)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_event *event;
	bool have_tool_update = false,
	     have_proximity_out = false;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) ==
		    LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY) {
			struct libinput_tablet_tool * tool;

			ck_assert(!have_tool_update);
			have_tool_update = true;
			tablet_event = libinput_event_get_tablet_tool_event(event);
			tool = libinput_event_tablet_tool_get_tool(tablet_event);
			ck_assert_int_eq(libinput_tablet_tool_get_type(tool),
					 LIBINPUT_TABLET_TOOL_TYPE_PEN);
		}
		libinput_event_destroy(event);
	}
	ck_assert(have_tool_update);

	litest_tablet_proximity_out(dev);
	libinput_dispatch(li);

	litest_timeout_tablet_proxout();
	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		if (libinput_event_get_type(event) ==
		    LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY) {
			struct libinput_event_tablet_tool *t =
				libinput_event_get_tablet_tool_event(event);

			if (libinput_event_tablet_tool_get_proximity_state(t) ==
			    LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT)
				have_proximity_out = true;
		}

		libinput_event_destroy(event);
	}
	ck_assert(have_proximity_out);

	/* Proximity out must not emit axis events */
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(proximity_in_button_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);

	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);
	litest_assert_tablet_button_event(li,
					  BTN_STYLUS,
					  LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(proximity_out_button_up)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_tablet_proximity_in(dev, 10, 10, axes);

	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_tablet_proximity_out(dev);
	litest_event(dev, EV_KEY, BTN_STYLUS, 0);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);

	litest_assert_tablet_button_event(li,
					  BTN_STYLUS,
					  LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(proximity_out_clear_buttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_event *event;
	uint32_t button;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	/* Test that proximity out events send button releases for any currently
	 * pressed stylus buttons
	 */
	for (button = BTN_TOUCH + 1; button <= BTN_STYLUS2; button++) {
		bool button_released = false;
		uint32_t event_button = 0;
		enum libinput_button_state state;

		if (!libevdev_has_event_code(dev->evdev, EV_KEY, button))
			continue;

		litest_tablet_proximity_in(dev, 10, 10, axes);
		litest_event(dev, EV_KEY, button, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		litest_tablet_proximity_out(dev);
		libinput_dispatch(li);

		litest_timeout_tablet_proxout();
		libinput_dispatch(li);

		while ((event = libinput_get_event(li))) {
			tablet_event = libinput_event_get_tablet_tool_event(event);

			if (libinput_event_get_type(event) ==
			    LIBINPUT_EVENT_TABLET_TOOL_BUTTON) {

				event_button = libinput_event_tablet_tool_get_button(tablet_event);
				state = libinput_event_tablet_tool_get_button_state(tablet_event);

				if (event_button == button &&
				    state == LIBINPUT_BUTTON_STATE_RELEASED)
					button_released = true;
			}

			libinput_event_destroy(event);
		}

		ck_assert_msg(button_released,
			      "Button %s (%d) was not released.",
			      libevdev_event_code_get_name(EV_KEY, button),
			      event_button);
	}

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(proximity_has_axes)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_event *event;
	struct libinput_tablet_tool *tool;
	double x, y,
	       distance;
	double last_x, last_y,
	       last_distance = 0.0,
	       last_tx = 0.0, last_ty = 0.0;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ ABS_TILT_X, 10 },
		{ ABS_TILT_Y, 10 },
		{ -1, -1}
	};

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tablet_event);

	ck_assert(libinput_event_tablet_tool_x_has_changed(tablet_event));
	ck_assert(libinput_event_tablet_tool_y_has_changed(tablet_event));

	x = libinput_event_tablet_tool_get_x(tablet_event);
	y = libinput_event_tablet_tool_get_y(tablet_event);

	litest_assert_double_ne(x, 0);
	litest_assert_double_ne(y, 0);

	if (libinput_tablet_tool_has_distance(tool)) {
		ck_assert(libinput_event_tablet_tool_distance_has_changed(
				tablet_event));

		distance = libinput_event_tablet_tool_get_distance(tablet_event);
		litest_assert_double_ne(distance, 0);
	}

	if (libinput_tablet_tool_has_tilt(tool)) {
		ck_assert(libinput_event_tablet_tool_tilt_x_has_changed(
				tablet_event));
		ck_assert(libinput_event_tablet_tool_tilt_y_has_changed(
				tablet_event));

		x = libinput_event_tablet_tool_get_tilt_x(tablet_event);
		y = libinput_event_tablet_tool_get_tilt_y(tablet_event);

		litest_assert_double_ne(x, 0);
		litest_assert_double_ne(y, 0);
	}

	litest_assert_empty_queue(li);
	libinput_event_destroy(event);

	litest_axis_set_value(axes, ABS_DISTANCE, 20);
	litest_axis_set_value(axes, ABS_TILT_X, 15);
	litest_axis_set_value(axes, ABS_TILT_Y, 25);

	/* work around axis smoothing */
	litest_tablet_motion(dev, 20, 30, axes);
	litest_tablet_motion(dev, 20, 29, axes);
	litest_tablet_motion(dev, 20, 31, axes);
	litest_drain_events(li);

	litest_tablet_motion(dev, 20, 30, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	last_x = libinput_event_tablet_tool_get_x(tablet_event);
	last_y = libinput_event_tablet_tool_get_y(tablet_event);
	if (libinput_tablet_tool_has_distance(tool))
		last_distance = libinput_event_tablet_tool_get_distance(
					     tablet_event);
	if (libinput_tablet_tool_has_tilt(tool)) {
		last_tx = libinput_event_tablet_tool_get_tilt_x(tablet_event);
		last_ty = libinput_event_tablet_tool_get_tilt_y(tablet_event);
	}

	libinput_event_destroy(event);

	/* Make sure that the axes are still present on proximity out */
	litest_tablet_proximity_out(dev);
	libinput_dispatch(li);

	litest_timeout_tablet_proxout();
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tablet_event);

	ck_assert(!libinput_event_tablet_tool_x_has_changed(tablet_event));
	ck_assert(!libinput_event_tablet_tool_y_has_changed(tablet_event));

	x = libinput_event_tablet_tool_get_x(tablet_event);
	y = libinput_event_tablet_tool_get_y(tablet_event);
	litest_assert_double_ge(x, last_x - 1);
	litest_assert_double_le(x, last_x + 1);
	litest_assert_double_ge(y, last_y - 1);
	litest_assert_double_le(y, last_y + 1);

	if (libinput_tablet_tool_has_distance(tool)) {
		ck_assert(!libinput_event_tablet_tool_distance_has_changed(
				tablet_event));

		distance = libinput_event_tablet_tool_get_distance(
						tablet_event);
		litest_assert_double_eq(distance, last_distance);
	}

	if (libinput_tablet_tool_has_tilt(tool)) {
		ck_assert(!libinput_event_tablet_tool_tilt_x_has_changed(
				tablet_event));
		ck_assert(!libinput_event_tablet_tool_tilt_y_has_changed(
				tablet_event));

		x = libinput_event_tablet_tool_get_tilt_x(tablet_event);
		y = libinput_event_tablet_tool_get_tilt_y(tablet_event);

		litest_assert_double_eq(x, last_tx);
		litest_assert_double_eq(y, last_ty);
	}

	litest_assert_empty_queue(li);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(proximity_range_enter)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 90 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_pop_event_frame(dev);
	litest_assert_empty_queue(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 20);
	litest_tablet_motion(dev, 10, 10, axes);
	libinput_dispatch(li);

	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);

	litest_axis_set_value(axes, ABS_DISTANCE, 90);
	litest_tablet_motion(dev, 10, 10, axes);
	libinput_dispatch(li);
	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);

	litest_tablet_proximity_out(dev);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(proximity_range_in_out)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 20 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);
	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);

	litest_axis_set_value(axes, ABS_DISTANCE, 90);
	litest_tablet_motion(dev, 10, 10, axes);
	libinput_dispatch(li);
	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);

	litest_tablet_motion(dev, 30, 30, axes);
	litest_assert_empty_queue(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 20);
	litest_tablet_motion(dev, 10, 10, axes);
	libinput_dispatch(li);
	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);

	litest_tablet_proximity_out(dev);
	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(proximity_range_button_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 90 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_event(dev, EV_KEY, BTN_STYLUS, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_tablet_proximity_out(dev);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(proximity_range_button_press)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 20 },
		{ -1, -1 }
	};

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_assert_tablet_button_event(li,
					  BTN_STYLUS,
					  LIBINPUT_BUTTON_STATE_PRESSED);

	litest_axis_set_value(axes, ABS_DISTANCE, 90);
	litest_tablet_motion(dev, 15, 15, axes);
	libinput_dispatch(li);

	/* expect fake button release */
	litest_assert_tablet_button_event(li,
					  BTN_STYLUS,
					  LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);

	litest_event(dev, EV_KEY, BTN_STYLUS, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	litest_tablet_proximity_out(dev);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(proximity_range_button_release)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 90 },
		{ -1, -1 }
	};

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_empty_queue(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 20);
	litest_tablet_motion(dev, 15, 15, axes);
	libinput_dispatch(li);

	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);
	/* expect fake button press */
	litest_assert_tablet_button_event(li,
					  BTN_STYLUS,
					  LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);

	litest_event(dev, EV_KEY, BTN_STYLUS, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_assert_tablet_button_event(li,
					  BTN_STYLUS,
					  LIBINPUT_BUTTON_STATE_RELEASED);

	litest_tablet_proximity_out(dev);
	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
}
END_TEST

START_TEST(proximity_out_on_delete)
{
	struct libinput *li = litest_create_context();
	struct litest_device *dev = litest_add_device(li, LITEST_WACOM_INTUOS);

	litest_tablet_proximity_in(dev, 10, 10, NULL);
	litest_drain_events(li);

	litest_delete_device(dev);
	libinput_dispatch(li);

	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	libinput_unref(li);
}
END_TEST

START_TEST(motion)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_event *event;
	int test_x, test_y;
	double last_reported_x = 0, last_reported_y = 0;
	enum libinput_event_type type;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 5, 100, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);

	do {
		bool x_changed, y_changed;
		double reported_x, reported_y;

		tablet_event = libinput_event_get_tablet_tool_event(event);
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);

		x_changed = libinput_event_tablet_tool_x_has_changed(
							tablet_event);
		y_changed = libinput_event_tablet_tool_y_has_changed(
							tablet_event);

		ck_assert(x_changed);
		ck_assert(y_changed);

		reported_x = libinput_event_tablet_tool_get_x(tablet_event);
		reported_y = libinput_event_tablet_tool_get_y(tablet_event);

		litest_assert_double_lt(reported_x, reported_y);

		last_reported_x = reported_x;
		last_reported_y = reported_y;

		libinput_event_destroy(event);
		event = libinput_get_event(li);
	} while (event != NULL);

	for (test_x = 10, test_y = 90;
	     test_x <= 100;
	     test_x += 10, test_y -= 10) {
		bool x_changed, y_changed;
		double reported_x, reported_y;

		litest_tablet_motion(dev, test_x, test_y, axes);
		libinput_dispatch(li);

		while ((event = libinput_get_event(li))) {
			tablet_event = libinput_event_get_tablet_tool_event(event);
			type = libinput_event_get_type(event);

			if (type == LIBINPUT_EVENT_TABLET_TOOL_AXIS) {
				x_changed = libinput_event_tablet_tool_x_has_changed(
							    tablet_event);
				y_changed = libinput_event_tablet_tool_y_has_changed(
							    tablet_event);

				ck_assert(x_changed);
				ck_assert(y_changed);

				reported_x = libinput_event_tablet_tool_get_x(
								tablet_event);
				reported_y = libinput_event_tablet_tool_get_y(
								tablet_event);

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

START_TEST(left_handed)
{
#if HAVE_LIBWACOM
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	double libinput_max_x, libinput_max_y;
	double last_x = -1.0, last_y = -1.0;
	double x, y;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_drain_events(li);

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

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
				LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);

	last_x = libinput_event_tablet_tool_get_x(tablet_event);
	last_y = libinput_event_tablet_tool_get_y(tablet_event);

	litest_assert_double_eq(last_x, 0);
	litest_assert_double_eq(last_y, libinput_max_y);

	libinput_event_destroy(event);

	/* work around smoothing */
	litest_axis_set_value(axes, ABS_DISTANCE, 9);
	litest_tablet_motion(dev, 100, 0, axes);
	litest_axis_set_value(axes, ABS_DISTANCE, 7);
	litest_tablet_motion(dev, 100, 0, axes);
	litest_axis_set_value(axes, ABS_DISTANCE, 10);
	litest_tablet_motion(dev, 100, 0, axes);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 5);
	litest_tablet_motion(dev, 100, 0, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	x = libinput_event_tablet_tool_get_x(tablet_event);
	y = libinput_event_tablet_tool_get_y(tablet_event);

	litest_assert_double_eq(x, libinput_max_x);
	litest_assert_double_eq(y, 0);

	litest_assert_double_gt(x, last_x);
	litest_assert_double_lt(y, last_y);

	libinput_event_destroy(event);

	litest_tablet_proximity_out(dev);
	litest_drain_events(li);

	/* Since we've drained the events and libinput's aware the tool is out
	 * of proximity, it should have finally transitioned into left-handed
	 * mode, so the axes should be inverted once we bring it back into
	 * proximity */
	litest_tablet_proximity_in(dev, 0, 100, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
				LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);

	last_x = libinput_event_tablet_tool_get_x(tablet_event);
	last_y = libinput_event_tablet_tool_get_y(tablet_event);

	litest_assert_double_eq(last_x, libinput_max_x);
	litest_assert_double_eq(last_y, 0);

	libinput_event_destroy(event);

	/* work around smoothing */
	litest_axis_set_value(axes, ABS_DISTANCE, 9);
	litest_tablet_motion(dev, 100, 0, axes);
	litest_axis_set_value(axes, ABS_DISTANCE, 7);
	litest_tablet_motion(dev, 100, 0, axes);
	litest_axis_set_value(axes, ABS_DISTANCE, 10);
	litest_tablet_motion(dev, 100, 0, axes);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 5);
	litest_tablet_motion(dev, 100, 0, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
				LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	x = libinput_event_tablet_tool_get_x(tablet_event);
	y = libinput_event_tablet_tool_get_y(tablet_event);

	litest_assert_double_eq(x, 0);
	litest_assert_double_eq(y, libinput_max_y);

	litest_assert_double_lt(x, last_x);
	litest_assert_double_gt(y, last_y);

	libinput_event_destroy(event);
#endif
}
END_TEST

START_TEST(no_left_handed)
{
	struct litest_device *dev = litest_current_device();

	ck_assert(!libinput_device_config_left_handed_is_available(dev->libinput_device));
}
END_TEST

START_TEST(left_handed_tilt)
{
#if HAVE_LIBWACOM
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	enum libinput_config_status status;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ ABS_TILT_X, 90 },
		{ ABS_TILT_Y, 10 },
		{ -1, -1 }
	};
	double tx, ty;

	status = libinput_device_config_left_handed_set(dev->libinput_device, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tx = libinput_event_tablet_tool_get_tilt_x(tev);
	ty = libinput_event_tablet_tool_get_tilt_y(tev);

	ck_assert_double_lt(tx, 0);
	ck_assert_double_gt(ty, 0);

	libinput_event_destroy(event);
#endif
}
END_TEST

static inline double
rotate_event(struct litest_device *dev, int angle_degrees)
{
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	const struct input_absinfo *abs;
	double a = (angle_degrees - 90 - 175)/180.0 * M_PI;
	double val;
	int x, y;
	int tilt_center_x, tilt_center_y;

	abs = libevdev_get_abs_info(dev->evdev, ABS_TILT_X);
	ck_assert_notnull(abs);
	tilt_center_x = (abs->maximum - abs->minimum + 1) / 2;

	abs = libevdev_get_abs_info(dev->evdev, ABS_TILT_Y);
	ck_assert_notnull(abs);
	tilt_center_y = (abs->maximum - abs->minimum + 1) / 2;

	x = cos(a) * 20 + tilt_center_x;
	y = sin(a) * 20 + tilt_center_y;

	litest_event(dev, EV_ABS, ABS_TILT_X, x);
	litest_event(dev, EV_ABS, ABS_TILT_Y, y);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	ck_assert(libinput_event_tablet_tool_rotation_has_changed(tev));
	val = libinput_event_tablet_tool_get_rotation(tev);

	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

	return val;
}

START_TEST(left_handed_mouse_rotation)
{
#if HAVE_LIBWACOM
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;
	int angle;
	double val, old_val = 0;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ ABS_TILT_X, 0 },
		{ ABS_TILT_Y, 0 },
		{ -1, -1 }
	};

	status = libinput_device_config_left_handed_set(dev->libinput_device, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_pop_event_frame(dev);

	litest_drain_events(li);

	/* cos/sin are 90 degrees offset from the north-is-zero that
	   libinput uses. 175 is the CCW offset in the mouse HW */
	for (angle = 185; angle < 540; angle += 5) {
		int expected_angle = angle - 180;

		val = rotate_event(dev, angle % 360);

		/* rounding error galore, we can't test for anything more
		   precise than these */
		litest_assert_double_lt(val, 360.0);
		litest_assert_double_gt(val, old_val);
		litest_assert_double_lt(val, expected_angle + 5);

		old_val = val;
	}
#endif
}
END_TEST

START_TEST(left_handed_artpen_rotation)
{
#if HAVE_LIBWACOM
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	const struct input_absinfo *abs;
	enum libinput_config_status status;
	double val;
	double scale;
	int angle;

	if (!libevdev_has_event_code(dev->evdev,
				    EV_ABS,
				    ABS_Z))
		return;

	status = libinput_device_config_left_handed_set(dev->libinput_device, 1);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

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

	for (angle = 188; angle < 540; angle += 8) {
		int a = angle * scale + abs->minimum;
		int expected_angle = angle - 180;

		litest_event(dev, EV_ABS, ABS_Z, a);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event,
					     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
		ck_assert(libinput_event_tablet_tool_rotation_has_changed(tev));
		val = libinput_event_tablet_tool_get_rotation(tev);

		/* artpen has a 90 deg offset cw */
		ck_assert_int_eq(round(val), (expected_angle + 90) % 360);

		libinput_event_destroy(event);
		litest_assert_empty_queue(li);

	}
#endif
}
END_TEST

START_TEST(motion_event_state)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	int test_x, test_y;
	double last_x, last_y;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_drain_events(li);
	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_drain_events(li);

	/* couple of events that go left/bottom to right/top */
	for (test_x = 0, test_y = 100; test_x < 100; test_x += 10, test_y -= 10)
		litest_tablet_motion(dev, test_x, test_y, axes);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	last_x = libinput_event_tablet_tool_get_x(tablet_event);
	last_y = libinput_event_tablet_tool_get_y(tablet_event);

	/* mark with a button event, then go back to bottom/left */
	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	for (test_x = 100, test_y = 0; test_x > 0; test_x -= 10, test_y += 10)
		litest_tablet_motion(dev, test_x, test_y, axes);

	libinput_event_destroy(event);
	libinput_dispatch(li);
	ck_assert_int_eq(libinput_next_event_type(li),
			 LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	/* we expect all events up to the button event to go from
	   bottom/left to top/right */
	while ((event = libinput_get_event(li))) {
		double x, y;

		if (libinput_event_get_type(event) != LIBINPUT_EVENT_TABLET_TOOL_AXIS)
			break;

		tablet_event = libinput_event_get_tablet_tool_event(event);
		ck_assert_notnull(tablet_event);

		x = libinput_event_tablet_tool_get_x(tablet_event);
		y = libinput_event_tablet_tool_get_y(tablet_event);

		ck_assert(x > last_x);
		ck_assert(y < last_y);

		last_x = x;
		last_y = y;
		libinput_event_destroy(event);
	}

	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(motion_outside_bounds)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	double val;
	int i;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	litest_tablet_proximity_in(dev, 50, 50, axes);
	litest_drain_events(li);

	/* Work around smoothing */
	for (i = 5; i > 0; i--) {
		litest_event(dev, EV_ABS, ABS_X, 0 + 5 * i);
		litest_event(dev, EV_ABS, ABS_Y, 1000);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}
	litest_drain_events(li);

	/* On the 24HD x/y of 0 is outside the limit */
	litest_event(dev, EV_ABS, ABS_X, 0);
	litest_event(dev, EV_ABS, ABS_Y, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	val = libinput_event_tablet_tool_get_x(tablet_event);
	ck_assert_double_lt(val, 0.0);
	val = libinput_event_tablet_tool_get_y(tablet_event);
	ck_assert_double_gt(val, 0.0);

	val = libinput_event_tablet_tool_get_x_transformed(tablet_event, 100);
	ck_assert_double_lt(val, 0.0);

	libinput_event_destroy(event);

	/* Work around smoothing */
	for (i = 5; i > 0; i--) {
		litest_event(dev, EV_ABS, ABS_X, 1000);
		litest_event(dev, EV_ABS, ABS_Y, 0 + 5 * i);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}
	litest_drain_events(li);

	/* On the 24HD x/y of 0 is outside the limit */
	litest_event(dev, EV_ABS, ABS_X, 1000);
	litest_event(dev, EV_ABS, ABS_Y, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	val = libinput_event_tablet_tool_get_x(tablet_event);
	ck_assert_double_gt(val, 0.0);
	val = libinput_event_tablet_tool_get_y(tablet_event);
	ck_assert_double_lt(val, 0.0);

	val = libinput_event_tablet_tool_get_y_transformed(tablet_event, 100);
	ck_assert_double_lt(val, 0.0);

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
	litest_drain_events(li);

	absinfo = libevdev_get_abs_info(dev->evdev, ABS_DISTANCE);
	ck_assert(absinfo != NULL);

	litest_event(dev, EV_ABS, ABS_DISTANCE, absinfo->maximum);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_ABS, ABS_DISTANCE, absinfo->minimum);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(tool_unique)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_event *event;
	struct libinput_tablet_tool *tool;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
				LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tablet_event);
	ck_assert(libinput_tablet_tool_is_unique(tool));
	libinput_event_destroy(event);
}
END_TEST

START_TEST(tool_serial)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_event *event;
	struct libinput_tablet_tool *tool;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
				LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tablet_event);
	ck_assert_uint_eq(libinput_tablet_tool_get_serial(tool), 1000);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(tool_id)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_event *event;
	struct libinput_tablet_tool *tool;
	uint64_t tool_id;

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, NULL);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
				LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tablet_event);

	ck_assert_int_eq(libinput_device_get_id_vendor(dev->libinput_device),
			 VENDOR_ID_WACOM);

	switch (libinput_device_get_id_product(dev->libinput_device)) {
	case 0x27: /* Intuos 5 */
		tool_id = 1050626;
		break;
	case 0xc6: /* Cintiq 12WX */
	case 0xf4: /* Cintiq 24HD */
	case 0x333: /* Cintiq 13HD */
		tool_id = 2083;
		break;
	default:
		ck_abort();
	}

	ck_assert(tool_id == libinput_tablet_tool_get_tool_id(tool));
	libinput_event_destroy(event);
}
END_TEST

START_TEST(serial_changes_tool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_event *event;
	struct libinput_tablet_tool *tool;

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
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
				LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tablet_event);

	ck_assert_uint_eq(libinput_tablet_tool_get_serial(tool), 2000);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(invalid_serials)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_tablet_tool *tool;

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
		    LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY) {
			tablet_event = libinput_event_get_tablet_tool_event(event);
			tool = libinput_event_tablet_tool_get_tool(tablet_event);

			ck_assert_uint_eq(libinput_tablet_tool_get_serial(tool), 1000);
		}

		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(tool_ref)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_event *event;
	struct libinput_tablet_tool *tool;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
				LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tablet_event);

	ck_assert_notnull(tool);
	ck_assert(tool == libinput_tablet_tool_ref(tool));
	ck_assert(tool == libinput_tablet_tool_unref(tool));
	libinput_event_destroy(event);

	ck_assert(libinput_tablet_tool_unref(tool) == NULL);
}
END_TEST

START_TEST(tool_user_data)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_tablet_tool *tablet_event;
	struct libinput_event *event;
	struct libinput_tablet_tool *tool;
	void *userdata = &dev; /* not dereferenced */

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
				LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tablet_event);
	ck_assert_notnull(tool);

	ck_assert(libinput_tablet_tool_get_user_data(tool) == NULL);
	libinput_tablet_tool_set_user_data(tool, userdata);
	ck_assert(libinput_tablet_tool_get_user_data(tool) == userdata);
	libinput_tablet_tool_set_user_data(tool, NULL);
	ck_assert(libinput_tablet_tool_get_user_data(tool) == NULL);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(pad_buttons_ignored)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
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

	litest_assert_empty_queue(li);

	/* same thing while in prox */
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	for (button = BTN_0; button < BTN_MOUSE; button++) {
		litest_event(dev, EV_KEY, button, 1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		litest_event(dev, EV_KEY, button, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(tools_with_serials)
{
	struct libinput *li = litest_create_context();
	struct litest_device *dev[2];
	struct libinput_tablet_tool *tool[2] = {0};
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	int i;

	for (i = 0; i < 2; i++) {
		dev[i] = litest_add_device(li, LITEST_WACOM_INTUOS);
		litest_drain_events(li);

		/* WARNING: this test fails if UI_GET_SYSNAME isn't
		 * available or isn't used by libevdev (1.3, commit 2ff45c73).
		 * Put a sleep(1) here and that usually fixes it.
		 */

		litest_push_event_frame(dev[i]);
		litest_tablet_proximity_in(dev[i], 10, 10, NULL);
		litest_event(dev[i], EV_MSC, MSC_SERIAL, 100);
		litest_pop_event_frame(dev[i]);

		libinput_dispatch(li);
		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
		tool[i] = libinput_event_tablet_tool_get_tool(tev);
		libinput_event_destroy(event);
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
	struct libinput_tablet_tool *tool[2] = {0};
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	int i;

	for (i = 0; i < 2; i++) {
		dev[i] = litest_add_device_with_overrides(li,
							  LITEST_WACOM_ISDV4,
							  NULL,
							  NULL,
							  NULL,
							  NULL);

		litest_drain_events(li);

		/* WARNING: this test fails if UI_GET_SYSNAME isn't
		 * available or isn't used by libevdev (1.3, commit 2ff45c73).
		 * Put a sleep(1) here and that usually fixes it.
		 */

		litest_tablet_proximity_in(dev[i], 10, 10, NULL);

		libinput_dispatch(li);
		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
		tool[i] = libinput_event_tablet_tool_get_tool(tev);
		libinput_event_destroy(event);
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

START_TEST(tool_delayed_serial)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct libinput_tablet_tool *tool;
	unsigned int serial;

	litest_drain_events(li);

	litest_event(dev, EV_ABS, ABS_X, 4500);
	litest_event(dev, EV_ABS, ABS_Y, 2000);
	litest_event(dev, EV_MSC, MSC_SERIAL, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tev);
	serial = libinput_tablet_tool_get_serial(tool);
	ck_assert_int_eq(serial, 0);
	libinput_event_destroy(event);

	for (int x = 4500; x < 8000; x += 1000) {
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, 2000);
		litest_event(dev, EV_MSC, MSC_SERIAL, 0);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}
	litest_drain_events(li);

	/* Now send the serial */
	litest_event(dev, EV_ABS, ABS_X, 4500);
	litest_event(dev, EV_ABS, ABS_Y, 2000);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1234566);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	tool = libinput_event_tablet_tool_get_tool(tev);
	serial = libinput_tablet_tool_get_serial(tool);
	ck_assert_int_eq(serial, 0);
	libinput_event_destroy(event);

	for (int x = 4500; x < 8000; x += 500) {
		litest_event(dev, EV_ABS, ABS_X, x);
		litest_event(dev, EV_ABS, ABS_Y, 2000);
		litest_event(dev, EV_MSC, MSC_SERIAL, 1234566);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}

	event = libinput_get_event(li);
	do {
		tev = litest_is_tablet_event(event,
					     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
		tool = libinput_event_tablet_tool_get_tool(tev);
		serial = libinput_tablet_tool_get_serial(tool);
		ck_assert_int_eq(serial, 0);
		libinput_event_destroy(event);
		event = libinput_get_event(li);
	} while (event != NULL);

	/* Quirk: tool out event is a serial of 0 */
	litest_event(dev, EV_ABS, ABS_X, 4500);
	litest_event(dev, EV_ABS, ABS_Y, 2000);
	litest_event(dev, EV_MSC, MSC_SERIAL, 0);
	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tev);
	serial = libinput_tablet_tool_get_serial(tool);
	ck_assert_int_eq(serial, 0);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(tool_capability)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;

	ck_assert(libinput_device_has_capability(device,
						 LIBINPUT_DEVICE_CAP_TABLET_TOOL));
}
END_TEST

START_TEST(tool_capabilities)
{
	struct libinput *li = litest_create_context();
	struct litest_device *intuos;
	struct litest_device *bamboo;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *t;
	struct libinput_tablet_tool *tool;

	/* The axis capabilities of a tool can differ depending on the type of
	 * tablet the tool is being used with */
	bamboo = litest_add_device(li, LITEST_WACOM_BAMBOO);
	intuos = litest_add_device(li, LITEST_WACOM_INTUOS);
	litest_drain_events(li);

	litest_event(bamboo, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(bamboo, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	t = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(t);

	ck_assert(libinput_tablet_tool_has_pressure(tool));
	ck_assert(libinput_tablet_tool_has_distance(tool));
	ck_assert(!libinput_tablet_tool_has_tilt(tool));

	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

	litest_event(intuos, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(intuos, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	t = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(t);

	ck_assert(libinput_tablet_tool_has_pressure(tool));
	ck_assert(libinput_tablet_tool_has_distance(tool));
	ck_assert(libinput_tablet_tool_has_tilt(tool));

	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

	litest_delete_device(bamboo);
	litest_delete_device(intuos);
	libinput_unref(li);
}
END_TEST

static inline bool
tablet_has_mouse(struct litest_device *dev)
{
	return libevdev_has_event_code(dev->evdev, EV_KEY, BTN_TOOL_MOUSE) &&
	       libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_WACOM;
}

START_TEST(tool_type)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *t;
	struct libinput_tablet_tool *tool;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ ABS_TILT_X, 0 },
		{ ABS_TILT_Y, 0 },
		{ -1, -1 }
	};
	struct tool_type_match {
		int code;
		enum libinput_tablet_tool_type type;
	} types[] = {
		{ BTN_TOOL_PEN, LIBINPUT_TABLET_TOOL_TYPE_PEN },
		{ BTN_TOOL_RUBBER, LIBINPUT_TABLET_TOOL_TYPE_ERASER },
		{ BTN_TOOL_BRUSH, LIBINPUT_TABLET_TOOL_TYPE_BRUSH },
		{ BTN_TOOL_BRUSH, LIBINPUT_TABLET_TOOL_TYPE_BRUSH },
		{ BTN_TOOL_PENCIL, LIBINPUT_TABLET_TOOL_TYPE_PENCIL },
		{ BTN_TOOL_AIRBRUSH, LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH },
		{ BTN_TOOL_MOUSE, LIBINPUT_TABLET_TOOL_TYPE_MOUSE },
		{ BTN_TOOL_LENS, LIBINPUT_TABLET_TOOL_TYPE_LENS },
		{ -1, -1 }
	};
	struct tool_type_match *tt;

	litest_drain_events(li);

	for (tt = types; tt->code != -1; tt++) {
		if (!libevdev_has_event_code(dev->evdev,
					     EV_KEY,
					     tt->code))
			continue;

		if ((tt->code == BTN_TOOL_MOUSE || tt->code == BTN_TOOL_LENS) &&
		    !tablet_has_mouse(dev))
			continue;

		litest_push_event_frame(dev);
		litest_filter_event(dev, EV_KEY, BTN_TOOL_PEN);
		litest_tablet_proximity_in(dev, 50, 50, axes);
		litest_unfilter_event(dev, EV_KEY, BTN_TOOL_PEN);
		litest_event(dev, EV_KEY, tt->code, 1);
		litest_pop_event_frame(dev);
		libinput_dispatch(li);

		event = libinput_get_event(li);
		t = litest_is_tablet_event(event,
				   LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
		tool = libinput_event_tablet_tool_get_tool(t);

		ck_assert_int_eq(libinput_tablet_tool_get_type(tool),
				 tt->type);

		libinput_event_destroy(event);
		litest_assert_empty_queue(li);

		litest_push_event_frame(dev);
		litest_filter_event(dev, EV_KEY, BTN_TOOL_PEN);
		litest_tablet_proximity_out(dev);
		litest_unfilter_event(dev, EV_KEY, BTN_TOOL_PEN);
		litest_event(dev, EV_KEY, tt->code, 0);
		litest_pop_event_frame(dev);
		litest_drain_events(li);
	}
}
END_TEST

START_TEST(tool_in_prox_before_start)
{
	struct libinput *li;
	struct litest_device *dev = litest_current_device();
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct libinput_tablet_tool *tool;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ ABS_TILT_X, 0 },
		{ ABS_TILT_Y, 0 },
		{ -1, -1 }
	};
	const char *devnode;
	unsigned int serial;

	litest_tablet_proximity_in(dev, 10, 10, axes);

	/* for simplicity, we create a new litest context */
	devnode = libevdev_uinput_get_devnode(dev->uinput);
	li = litest_create_context();
	libinput_path_add_device(li, devnode);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_DEVICE_ADDED,
				      -1);
	event = libinput_get_event(li);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);

	litest_tablet_motion(dev, 10, 20, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tev);
	serial = libinput_tablet_tool_get_serial(tool);
	libinput_event_destroy(event);

	litest_tablet_motion(dev, 30, 40, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	tool = libinput_event_tablet_tool_get_tool(tev);
	ck_assert_int_eq(serial,
			 libinput_tablet_tool_get_serial(tool));
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);
	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, BTN_STYLUS, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_TABLET_TOOL_BUTTON);
	litest_tablet_proximity_out(dev);
	libinput_dispatch(li);

	litest_timeout_tablet_proxout();
	libinput_dispatch(li);

	litest_wait_for_event_of_type(li,
				      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY,
				      -1);
	libinput_unref(li);
}
END_TEST

START_TEST(mouse_tool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct libinput_tablet_tool *tool;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tev);
	ck_assert_notnull(tool);
	ck_assert_int_eq(libinput_tablet_tool_get_type(tool),
			 LIBINPUT_TABLET_TOOL_TYPE_MOUSE);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(mouse_buttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct libinput_tablet_tool *tool;
	int code;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_event(dev, EV_ABS, ABS_MISC, 0x806); /* 5-button mouse tool_id */
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tev);
	ck_assert_notnull(tool);
	libinput_tablet_tool_ref(tool);

	libinput_event_destroy(event);

	for (code = BTN_LEFT; code <= BTN_TASK; code++) {
		bool has_button = libevdev_has_event_code(dev->evdev,
							  EV_KEY,
							  code);
		ck_assert_int_eq(!!has_button,
				 !!libinput_tablet_tool_has_button(tool, code));

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

	libinput_tablet_tool_unref(tool);
}
END_TEST

START_TEST(mouse_rotation)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int angle;
	double val, old_val = 0;

	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ ABS_TILT_X, 0 },
		{ ABS_TILT_Y, 0 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_pop_event_frame(dev);

	litest_drain_events(li);

	/* cos/sin are 90 degrees offset from the north-is-zero that
	   libinput uses. 175 is the CCW offset in the mouse HW */
	for (angle = 5; angle < 360; angle += 5) {
		val = rotate_event(dev, angle);

		/* rounding error galore, we can't test for anything more
		   precise than these */
		litest_assert_double_lt(val, 360.0);
		litest_assert_double_gt(val, old_val);
		litest_assert_double_lt(val, angle + 5);

		old_val = val;
	}
}
END_TEST

START_TEST(mouse_wheel)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct libinput_tablet_tool *tool;
	const struct input_absinfo *abs;
	double val;
	int i;

	if (!libevdev_has_event_code(dev->evdev,
				     EV_REL,
				     REL_WHEEL))
		return;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_MOUSE, 1);
	litest_event(dev, EV_ABS, ABS_MISC, 0x806); /* 5-button mouse tool_id */
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tev);
	ck_assert_notnull(tool);
	libinput_tablet_tool_ref(tool);

	libinput_event_destroy(event);

	ck_assert(libinput_tablet_tool_has_wheel(tool));

	for (i = 0; i < 3; i++) {
		litest_event(dev, EV_REL, REL_WHEEL, -1);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);

		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event,
					     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
		ck_assert(libinput_event_tablet_tool_wheel_has_changed(tev));

		val = libinput_event_tablet_tool_get_wheel_delta(tev);
		ck_assert_int_eq(val, 15);

		val = libinput_event_tablet_tool_get_wheel_delta_discrete(tev);
		ck_assert_int_eq(val, 1);

		libinput_event_destroy(event);

		litest_assert_empty_queue(li);
	}

	for (i = 2; i < 5; i++) {
		/* send  x/y events to make sure we reset the wheel */
		abs = libevdev_get_abs_info(dev->evdev, ABS_X);
		litest_event(dev, EV_ABS, ABS_X, (abs->maximum - abs->minimum)/i);
		abs = libevdev_get_abs_info(dev->evdev, ABS_Y);
		litest_event(dev, EV_ABS, ABS_Y, (abs->maximum - abs->minimum)/i);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);

		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event,
					     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
		ck_assert(!libinput_event_tablet_tool_wheel_has_changed(tev));

		val = libinput_event_tablet_tool_get_wheel_delta(tev);
		ck_assert_int_eq(val, 0);

		val = libinput_event_tablet_tool_get_wheel_delta_discrete(tev);
		ck_assert_int_eq(val, 0);

		libinput_event_destroy(event);

		litest_assert_empty_queue(li);
	}

	libinput_tablet_tool_unref(tool);
}
END_TEST

START_TEST(airbrush_tool)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct libinput_tablet_tool *tool;

	if (!libevdev_has_event_code(dev->evdev,
				    EV_KEY,
				    BTN_TOOL_AIRBRUSH))
		return;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_AIRBRUSH, 1);
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tev);

	ck_assert_notnull(tool);
	ck_assert_int_eq(libinput_tablet_tool_get_type(tool),
			 LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH);

	ck_assert(libinput_tablet_tool_has_slider(tool));

	libinput_event_destroy(event);
}
END_TEST

START_TEST(airbrush_slider)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	const struct input_absinfo *abs;
	double val;
	double scale;
	double expected;
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
		libinput_dispatch(li);
		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event,
					     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
		ck_assert(libinput_event_tablet_tool_slider_has_changed(tev));
		val = libinput_event_tablet_tool_get_slider_position(tev);

		expected = ((v - abs->minimum)/scale) * 2 - 1;
		ck_assert_double_eq(val, expected);
		ck_assert_double_ge(val, -1.0);
		ck_assert_double_le(val, 1.0);
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
	struct libinput_event_tablet_tool *tev;
	struct libinput_tablet_tool *tool;

	if (!libevdev_has_event_code(dev->evdev,
				    EV_ABS,
				    ABS_Z))
		return;

	litest_drain_events(li);

	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_ABS, ABS_MISC, 0x804); /* Art Pen */
	litest_event(dev, EV_MSC, MSC_SERIAL, 1000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	tool = libinput_event_tablet_tool_get_tool(tev);
	ck_assert_notnull(tool);
	ck_assert_int_eq(libinput_tablet_tool_get_type(tool),
			 LIBINPUT_TABLET_TOOL_TYPE_PEN);
	ck_assert(libinput_tablet_tool_has_rotation(tool));

	libinput_event_destroy(event);
}
END_TEST

START_TEST(artpen_rotation)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
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
		libinput_dispatch(li);
		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event,
					     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
		ck_assert(libinput_event_tablet_tool_rotation_has_changed(tev));
		val = libinput_event_tablet_tool_get_rotation(tev);

		/* artpen has a 90 deg offset cw */
		ck_assert_int_eq(round(val), (angle + 90) % 360);

		libinput_event_destroy(event);
		litest_assert_empty_queue(li);

	}
}
END_TEST

START_TEST(tablet_time_usec)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};
	uint64_t time_usec;

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 5, 100, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	time_usec = libinput_event_tablet_tool_get_time_usec(tev);
	ck_assert_int_eq(libinput_event_tablet_tool_get_time(tev),
			 (uint32_t) (time_usec / 1000));
	libinput_event_destroy(event);
}
END_TEST

START_TEST(tablet_pressure_distance_exclusive)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 },
	};
	double pressure, distance;

	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_PRESSURE, 2);
	litest_tablet_motion(dev, 70, 70, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	pressure = libinput_event_tablet_tool_get_pressure(tev);
	distance = libinput_event_tablet_tool_get_distance(tev);

	ck_assert_double_ne(pressure, 0.0);
	ck_assert_double_eq(distance, 0.0);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(tablet_calibration_has_matrix)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *d = dev->libinput_device;
	enum libinput_config_status status;
	int rc;
	float calibration[6] = {1, 0, 0, 0, 1, 0};
	int has_calibration;

	has_calibration = libevdev_has_property(dev->evdev, INPUT_PROP_DIRECT);

	rc = libinput_device_config_calibration_has_matrix(d);
	ck_assert_int_eq(rc, has_calibration);
	rc = libinput_device_config_calibration_get_matrix(d, calibration);
	ck_assert_int_eq(rc, 0);
	rc = libinput_device_config_calibration_get_default_matrix(d,
								   calibration);
	ck_assert_int_eq(rc, 0);

	status = libinput_device_config_calibration_set_matrix(d,
							       calibration);
	if (has_calibration)
		ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	else
		ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
}
END_TEST

START_TEST(tablet_calibration_set_matrix_delta)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *d = dev->libinput_device;
	enum libinput_config_status status;
	float calibration[6] = {0.5, 0, 0, 0, 0.5, 0};
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 0 },
		{ ABS_PRESSURE, 10 },
		{ -1, -1 }
	};
	int has_calibration;
	double x, y, dx, dy, mdx, mdy;

	has_calibration = libevdev_has_property(dev->evdev, INPUT_PROP_DIRECT);
	if (!has_calibration)
		return;

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 100, 100, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	x = libinput_event_tablet_tool_get_x(tablet_event);
	y = libinput_event_tablet_tool_get_y(tablet_event);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_TIP);
	libinput_event_destroy(event);

	litest_tablet_motion(dev, 80, 80, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	dx = libinput_event_tablet_tool_get_x(tablet_event) - x;
	dy = libinput_event_tablet_tool_get_y(tablet_event) - y;
	libinput_event_destroy(event);
	litest_tablet_proximity_out(dev);
	litest_drain_events(li);

	status = libinput_device_config_calibration_set_matrix(d,
							       calibration);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_tablet_proximity_in(dev, 100, 100, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	x = libinput_event_tablet_tool_get_x(tablet_event);
	y = libinput_event_tablet_tool_get_y(tablet_event);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_TIP);
	libinput_event_destroy(event);

	litest_tablet_motion(dev, 80, 80, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	mdx = libinput_event_tablet_tool_get_x(tablet_event) - x;
	mdy = libinput_event_tablet_tool_get_y(tablet_event) - y;
	libinput_event_destroy(event);
	litest_drain_events(li);

	ck_assert_double_gt(dx, mdx * 2 - 1);
	ck_assert_double_lt(dx, mdx * 2 + 1);
	ck_assert_double_gt(dy, mdy * 2 - 1);
	ck_assert_double_lt(dy, mdy * 2 + 1);
}
END_TEST

START_TEST(tablet_calibration_set_matrix)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *d = dev->libinput_device;
	enum libinput_config_status status;
	float calibration[6] = {0.5, 0, 0, 0, 1, 0};
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tablet_event;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};
	int has_calibration;
	double x, y;

	has_calibration = libevdev_has_property(dev->evdev, INPUT_PROP_DIRECT);
	if (!has_calibration)
		return;

	litest_drain_events(li);

	status = libinput_device_config_calibration_set_matrix(d,
							       calibration);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_tablet_proximity_in(dev, 100, 100, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	x = libinput_event_tablet_tool_get_x_transformed(tablet_event, 100);
	y = libinput_event_tablet_tool_get_y_transformed(tablet_event, 100);
	libinput_event_destroy(event);

	ck_assert_double_gt(x, 49.0);
	ck_assert_double_lt(x, 51.0);
	ck_assert_double_gt(y, 99.0);
	ck_assert_double_lt(y, 100.0);

	litest_tablet_proximity_out(dev);
	libinput_dispatch(li);
	litest_tablet_proximity_in(dev, 50, 50, axes);
	litest_tablet_proximity_out(dev);
	litest_drain_events(li);

	calibration[0] = 1;
	calibration[4] = 0.5;
	status = libinput_device_config_calibration_set_matrix(d,
							       calibration);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_tablet_proximity_in(dev, 100, 100, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tablet_event = litest_is_tablet_event(event,
					      LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	x = libinput_event_tablet_tool_get_x_transformed(tablet_event, 100);
	y = libinput_event_tablet_tool_get_y_transformed(tablet_event, 100);
	libinput_event_destroy(event);

	ck_assert(x > 99.0);
	ck_assert(x < 100.0);
	ck_assert(y > 49.0);
	ck_assert(y < 51.0);

	litest_tablet_proximity_out(dev);
}
END_TEST

START_TEST(tablet_pressure_offset)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 70 },
		{ ABS_PRESSURE, 20 },
		{ -1, -1 },
	};
	double pressure;

	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_drain_events(li);

	/* Put the pen down, with a pressure high enough to meet the
	 * threshold */
	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 25);

	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 70, 70, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);
	litest_drain_events(li);

	/* Reduce pressure to just a tick over the offset, otherwise we get
	 * the tip up event again */
	litest_axis_set_value(axes, ABS_PRESSURE, 20.1);
	litest_tablet_motion(dev, 70, 70, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	pressure = libinput_event_tablet_tool_get_pressure(tev);

	/* we can't actually get a real 0.0 because that would trigger a tip
	 * up. but it's close enough to zero that ck_assert_double_eq won't
	 * notice */
	ck_assert_double_eq(pressure, 0.0);

	libinput_event_destroy(event);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_PRESSURE, 21);
	litest_tablet_motion(dev, 70, 70, axes);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	pressure = libinput_event_tablet_tool_get_pressure(tev);

	/* can't use the double_eq here, the pressure value is too tiny */
	ck_assert(pressure > 0.0);
	ck_assert(pressure < 1.0);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(tablet_pressure_offset_decrease)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 70 },
		{ ABS_PRESSURE, 20 },
		{ -1, -1 },
	};
	double pressure;

	/* offset 20 on prox in */
	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_tablet_proximity_out(dev);
	litest_drain_events(li);

	/* a reduced pressure value must reduce the offset */
	litest_axis_set_value(axes, ABS_PRESSURE, 10);
	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_tablet_proximity_out(dev);
	litest_drain_events(li);

	/* a reduced pressure value must reduce the offset */
	litest_tablet_proximity_in(dev, 5, 100, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	pressure = libinput_event_tablet_tool_get_pressure(tev);
	ck_assert_double_eq(pressure, 0.0);

	libinput_event_destroy(event);
	litest_drain_events(li);

	/* trigger the pressure threshold */
	litest_axis_set_value(axes, ABS_PRESSURE, 15);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 70, 70, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_TIP);

	pressure = libinput_event_tablet_tool_get_pressure(tev);

	/* can't use the double_eq here, the pressure value is too tiny */
	ck_assert(pressure > 0.0);
	ck_assert(pressure < 1.0);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(tablet_pressure_offset_increase)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 70 },
		{ ABS_PRESSURE, 20 },
		{ -1, -1 },
	};
	double pressure;

	/* offset 20 on first prox in */
	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_tablet_proximity_out(dev);
	litest_drain_events(li);

	/* offset 30 on second prox in - must not change the offset */
	litest_axis_set_value(axes, ABS_PRESSURE, 30);
	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 31);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 70, 70, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_PRESSURE, 30);
	litest_tablet_motion(dev, 70, 70, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	pressure = libinput_event_tablet_tool_get_pressure(tev);
	/* can't use the double_eq here, the pressure value is too tiny */
	ck_assert(pressure > 0.0);
	ck_assert(pressure < 1.0);
	libinput_event_destroy(event);

	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_PRESSURE, 20);
	litest_tablet_motion(dev, 70, 70, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_TIP);

	pressure = libinput_event_tablet_tool_get_pressure(tev);

	ck_assert_double_eq(pressure, 0.0);
	libinput_event_destroy(event);
}
END_TEST

static void pressure_threshold_warning(struct libinput *libinput,
				       enum libinput_log_priority priority,
				       const char *format,
				       va_list args)
{
	int *warning_triggered = (int*)libinput_get_user_data(libinput);

	if (priority == LIBINPUT_LOG_PRIORITY_ERROR &&
	    strstr(format, "pressure offset greater"))
		(*warning_triggered)++;
}

START_TEST(tablet_pressure_min_max)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 0 },
		{ ABS_PRESSURE, 2 },
		{ -1, -1 },
	};
	double p;

	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_drain_events(li);
	libinput_dispatch(li);

	litest_axis_set_value(axes, ABS_PRESSURE, 0);
	litest_tablet_motion(dev, 5, 100, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	ck_assert(libinput_event_tablet_tool_pressure_has_changed(tev));
	p = libinput_event_tablet_tool_get_pressure(tev);
	ck_assert_double_ge(p, 0.0);
	libinput_event_destroy(event);

	/* skip over pressure-based tip down */
	litest_axis_set_value(axes, ABS_PRESSURE, 90);
	litest_tablet_motion(dev, 5, 100, axes);
	litest_drain_events(li);

	/* need to fill the motion history */
	litest_axis_set_value(axes, ABS_PRESSURE, 100);
	litest_tablet_motion(dev, 5, 100, axes);
	litest_tablet_motion(dev, 6, 100, axes);
	litest_tablet_motion(dev, 7, 100, axes);
	litest_tablet_motion(dev, 8, 100, axes);
	litest_drain_events(li);

	litest_tablet_motion(dev, 5, 100, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	p = libinput_event_tablet_tool_get_pressure(tev);
	ck_assert_double_ge(p, 1.0);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(tablet_pressure_range)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 0 },
		{ ABS_PRESSURE, 10 },
		{ -1, -1 },
	};
	int pressure;
	double p;

	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_drain_events(li);
	libinput_dispatch(li);

	for (pressure = 10; pressure <= 100; pressure += 10) {
		litest_axis_set_value(axes, ABS_PRESSURE, pressure);
		litest_tablet_motion(dev, 70, 70, axes);
		libinput_dispatch(li);

		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_AXIS);
		p = libinput_event_tablet_tool_get_pressure(tev);
		ck_assert_double_ge(p, 0.0);
		ck_assert_double_le(p, 1.0);
		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(tablet_pressure_offset_exceed_threshold)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 70 },
		{ ABS_PRESSURE, 30 },
		{ -1, -1 },
	};
	double pressure;
	int warning_triggered = 0;

	litest_drain_events(li);

	libinput_set_user_data(li, &warning_triggered);

	libinput_log_set_handler(li, pressure_threshold_warning);
	litest_tablet_proximity_in(dev, 5, 100, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	pressure = libinput_event_tablet_tool_get_pressure(tev);
	ck_assert_double_gt(pressure, 0.0);
	libinput_event_destroy(event);

	ck_assert_int_eq(warning_triggered, 1);
	litest_restore_log_handler(li);
}
END_TEST

START_TEST(tablet_pressure_offset_none_for_zero_distance)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 0 },
		{ ABS_PRESSURE, 20 },
		{ -1, -1 },
	};
	double pressure;

	litest_drain_events(li);

	/* we're going straight to touch on proximity, make sure we don't
	 * offset the pressure here */
	litest_push_event_frame(dev);
	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	pressure = libinput_event_tablet_tool_get_pressure(tev);
	ck_assert_double_gt(pressure, 0.0);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(tablet_pressure_offset_none_for_small_distance)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 20 },
		{ ABS_PRESSURE, 20 },
		{ -1, -1 },
	};
	double pressure;

	/* stylus too close to the tablet on the proximity in, ignore any
	 * pressure offset */
	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_drain_events(li);
	libinput_dispatch(li);

	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 21);
	litest_push_event_frame(dev);
	litest_tablet_motion(dev, 70, 70, axes);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_pop_event_frame(dev);
	litest_drain_events(li);

	litest_axis_set_value(axes, ABS_PRESSURE, 20);
	litest_tablet_motion(dev, 70, 70, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	pressure = libinput_event_tablet_tool_get_pressure(tev);
	ck_assert_double_gt(pressure, 0.0);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(tablet_distance_range)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 20 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 },
	};
	int distance;
	double dist;

	litest_tablet_proximity_in(dev, 5, 100, axes);
	litest_drain_events(li);
	libinput_dispatch(li);

	for (distance = 0; distance <= 100; distance += 10) {
		litest_axis_set_value(axes, ABS_DISTANCE, distance);
		litest_tablet_motion(dev, 70, 70, axes);
		libinput_dispatch(li);

		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event, LIBINPUT_EVENT_TABLET_TOOL_AXIS);
		dist = libinput_event_tablet_tool_get_distance(tev);
		ck_assert_double_ge(dist, 0.0);
		ck_assert_double_le(dist, 1.0);
		libinput_event_destroy(event);
	}
}
END_TEST

START_TEST(tilt_available)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct libinput_tablet_tool *tool;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ ABS_TILT_X, 80 },
		{ ABS_TILT_Y, 20 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);

	tool = libinput_event_tablet_tool_get_tool(tev);
	ck_assert(libinput_tablet_tool_has_tilt(tool));

	libinput_event_destroy(event);
}
END_TEST

START_TEST(tilt_not_available)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct libinput_tablet_tool *tool;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ ABS_TILT_X, 80 },
		{ ABS_TILT_Y, 20 },
		{ -1, -1 }
	};

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);

	tool = libinput_event_tablet_tool_get_tool(tev);
	ck_assert(!libinput_tablet_tool_has_tilt(tool));

	libinput_event_destroy(event);
}
END_TEST

START_TEST(tilt_x)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ ABS_TILT_X, 10 },
		{ ABS_TILT_Y, 0 },
		{ -1, -1 }
	};
	double tx, ty;
	int tilt;
	double expected_tx;

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);

	/* 90% of the actual axis but mapped into a [-64, 64] tilt range, so
	 * we expect 51 degrees ± rounding errors */
	tx = libinput_event_tablet_tool_get_tilt_x(tev);
	ck_assert_double_le(tx, -50);
	ck_assert_double_ge(tx, -52);

	ty = libinput_event_tablet_tool_get_tilt_y(tev);
	ck_assert_double_ge(ty, -65);
	ck_assert_double_lt(ty, -63);

	libinput_event_destroy(event);

	expected_tx = -64.0;

	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 1);

	for (tilt = 0; tilt <= 100; tilt += 5) {
		litest_axis_set_value(axes, ABS_TILT_X, tilt);
		/* work around smoothing */
		litest_tablet_motion(dev, 10, 10, axes);
		litest_tablet_motion(dev, 10, 11, axes);
		litest_tablet_motion(dev, 10, 10, axes);
		litest_drain_events(li);
		litest_tablet_motion(dev, 10, 11, axes);
		libinput_dispatch(li);
		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event,
					     LIBINPUT_EVENT_TABLET_TOOL_AXIS);

		tx = libinput_event_tablet_tool_get_tilt_x(tev);
		ck_assert_double_ge(tx, expected_tx - 2);
		ck_assert_double_le(tx, expected_tx + 2);

		ty = libinput_event_tablet_tool_get_tilt_y(tev);
		ck_assert_double_ge(ty, -65);
		ck_assert_double_lt(ty, -63);

		libinput_event_destroy(event);

		expected_tx = tx + 6.04;
	}

	/* the last event must reach the max */
	ck_assert_double_ge(tx, 63.0);
	ck_assert_double_le(tx, 64.0);
}
END_TEST

START_TEST(tilt_y)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ ABS_TILT_X, 0 },
		{ ABS_TILT_Y, 10 },
		{ -1, -1 }
	};
	double tx, ty;
	int tilt;
	double expected_ty;

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);

	/* 90% of the actual axis but mapped into a [-64, 64] tilt range, so
	 * we expect 50 degrees ± rounding errors */
	ty = libinput_event_tablet_tool_get_tilt_y(tev);
	ck_assert_double_le(ty, -50);
	ck_assert_double_ge(ty, -52);

	tx = libinput_event_tablet_tool_get_tilt_x(tev);
	ck_assert_double_ge(tx, -65);
	ck_assert_double_lt(tx, -63);

	libinput_event_destroy(event);

	expected_ty = -64;

	litest_axis_set_value(axes, ABS_DISTANCE, 0);
	litest_axis_set_value(axes, ABS_PRESSURE, 1);

	for (tilt = 0; tilt <= 100; tilt += 5) {
		litest_axis_set_value(axes, ABS_TILT_Y, tilt);
		/* work around smoothing */
		litest_tablet_motion(dev, 10, 11, axes);
		litest_tablet_motion(dev, 10, 10, axes);
		litest_tablet_motion(dev, 10, 11, axes);
		litest_drain_events(li);
		litest_tablet_motion(dev, 10, 10, axes);
		libinput_dispatch(li);
		event = libinput_get_event(li);
		tev = litest_is_tablet_event(event,
					     LIBINPUT_EVENT_TABLET_TOOL_AXIS);

		ty = libinput_event_tablet_tool_get_tilt_y(tev);
		ck_assert_double_ge(ty, expected_ty - 2);
		ck_assert_double_le(ty, expected_ty + 2);

		tx = libinput_event_tablet_tool_get_tilt_x(tev);
		ck_assert_double_ge(tx, -65);
		ck_assert_double_lt(tx, -63);

		libinput_event_destroy(event);

		expected_ty = ty + 6;
	}

	/* the last event must reach the max */
	ck_assert_double_ge(ty, 63.0);
	ck_assert_double_le(tx, 64.0);
}
END_TEST

START_TEST(relative_no_profile)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_accel_profile profile;
	enum libinput_config_status status;
	uint32_t profiles;

	ck_assert(libinput_device_config_accel_is_available(device));

	profile = libinput_device_config_accel_get_default_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);

	profile = libinput_device_config_accel_get_profile(device);
	ck_assert_int_eq(profile, LIBINPUT_CONFIG_ACCEL_PROFILE_NONE);

	profiles = libinput_device_config_accel_get_profiles(device);
	ck_assert_int_eq(profiles & LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE, 0);
	ck_assert_int_eq(profiles & LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT, 0);

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

START_TEST(relative_no_delta_prox_in)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};
	double dx, dy;

	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
	dx = libinput_event_tablet_tool_get_dx(tev);
	dy = libinput_event_tablet_tool_get_dy(tev);
	ck_assert(dx == 0.0);
	ck_assert(dy == 0.0);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(relative_delta)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};
	double dx, dy;

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	litest_tablet_motion(dev, 20, 10, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	dx = libinput_event_tablet_tool_get_dx(tev);
	dy = libinput_event_tablet_tool_get_dy(tev);
	ck_assert(dx > 0.0);
	ck_assert(dy == 0.0);
	libinput_event_destroy(event);

	litest_tablet_motion(dev, 5, 10, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	dx = libinput_event_tablet_tool_get_dx(tev);
	dy = libinput_event_tablet_tool_get_dy(tev);
	ck_assert(dx < 0.0);
	ck_assert(dy == 0.0);
	libinput_event_destroy(event);

	litest_tablet_motion(dev, 10, 20, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	dx = libinput_event_tablet_tool_get_dx(tev);
	dy = libinput_event_tablet_tool_get_dy(tev);
	ck_assert(dx == 0.0);
	ck_assert(dy > 0.0);
	libinput_event_destroy(event);

	litest_tablet_motion(dev, 10, 5, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	dx = libinput_event_tablet_tool_get_dx(tev);
	dy = libinput_event_tablet_tool_get_dy(tev);
	ck_assert(dx == 0.0);
	ck_assert(dy < 0.0);
	libinput_event_destroy(event);
}
END_TEST

START_TEST(relative_calibration)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};
	double dx, dy;
	float calibration[] = { -1, 0, 1, 0, -1, 1 };
	enum libinput_config_status status;

	if (!libinput_device_config_calibration_has_matrix(dev->libinput_device))
		return;

	status = libinput_device_config_calibration_set_matrix(
							dev->libinput_device,
							calibration);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	litest_tablet_motion(dev, 20, 10, axes);
	libinput_dispatch(li);

	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	dx = libinput_event_tablet_tool_get_dx(tev);
	dy = libinput_event_tablet_tool_get_dy(tev);
	ck_assert(dx < 0.0);
	ck_assert(dy == 0.0);
	libinput_event_destroy(event);

	litest_tablet_motion(dev, 5, 10, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	dx = libinput_event_tablet_tool_get_dx(tev);
	dy = libinput_event_tablet_tool_get_dy(tev);
	ck_assert(dx > 0.0);
	ck_assert(dy == 0.0);
	libinput_event_destroy(event);

	litest_tablet_motion(dev, 10, 20, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	dx = libinput_event_tablet_tool_get_dx(tev);
	dy = libinput_event_tablet_tool_get_dy(tev);
	ck_assert(dx == 0.0);
	ck_assert(dy < 0.0);
	libinput_event_destroy(event);

	litest_tablet_motion(dev, 10, 5, axes);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	tev = litest_is_tablet_event(event,
				     LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	dx = libinput_event_tablet_tool_get_dx(tev);
	dy = libinput_event_tablet_tool_get_dy(tev);
	ck_assert(dx == 0.0);
	ck_assert(dy > 0.0);
	libinput_event_destroy(event);
}
END_TEST

static void
touch_arbitration(struct litest_device *dev,
		  enum litest_device_type other,
		  bool is_touchpad)
{
	struct litest_device *finger;
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	finger = litest_add_device(li, other);
	litest_drain_events(li);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_tablet_motion(dev, 20, 40, axes);
	litest_drain_events(li);

	litest_touch_down(finger, 0, 30, 30);
	litest_touch_move_to(finger, 0, 30, 30, 80, 80, 10, 1);
	litest_assert_empty_queue(li);

	litest_tablet_motion(dev, 10, 10, axes);
	litest_tablet_motion(dev, 20, 40, axes);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	litest_tablet_proximity_out(dev);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);

	litest_timeout_touch_arbitration();
	libinput_dispatch(li);

	/* finger still down */
	litest_touch_move_to(finger, 0, 80, 80, 30, 30, 10, 1);
	litest_touch_up(finger, 0);
	litest_assert_empty_queue(li);

	/* lift finger, expect expect events */
	litest_touch_down(finger, 0, 30, 30);
	litest_touch_move_to(finger, 0, 30, 30, 80, 80, 10, 1);
	litest_touch_up(finger, 0);
	libinput_dispatch(li);

	if (is_touchpad)
		litest_assert_only_typed_events(li,
						LIBINPUT_EVENT_POINTER_MOTION);
	else
		litest_assert_touch_sequence(li);

	litest_delete_device(finger);
}

START_TEST(intuos_touch_arbitration)
{
	touch_arbitration(litest_current_device(), LITEST_WACOM_FINGER, true);
}
END_TEST

START_TEST(cintiq_touch_arbitration)
{
	touch_arbitration(litest_current_device(),
			  LITEST_WACOM_CINTIQ_13HDT_FINGER,
			  false);
}
END_TEST

static void
touch_arbitration_stop_touch(struct litest_device *dev,
			     enum litest_device_type other,
			     bool is_touchpad)
{
	struct litest_device *finger;
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	finger = litest_add_device(li, other);
	litest_touch_down(finger, 0, 30, 30);
	litest_touch_move_to(finger, 0, 30, 30, 80, 80, 10, 1);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_tablet_motion(dev, 10, 10, axes);
	litest_tablet_motion(dev, 20, 40, axes);
	litest_drain_events(li);

	litest_touch_move_to(finger, 0, 80, 80, 30, 30, 10, 1);
	/* start another finger to make sure that one doesn't send events
	   either */
	litest_touch_down(finger, 1, 30, 30);
	litest_touch_move_to(finger, 1, 30, 30, 80, 80, 10, 1);
	litest_assert_empty_queue(li);

	litest_tablet_motion(dev, 10, 10, axes);
	litest_tablet_motion(dev, 20, 40, axes);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	litest_tablet_proximity_out(dev);
	litest_drain_events(li);

	/* Finger needs to be lifted for events to happen*/
	litest_touch_move_to(finger, 0, 30, 30, 80, 80, 10, 1);
	litest_assert_empty_queue(li);
	litest_touch_move_to(finger, 1, 80, 80, 30, 30, 10, 1);
	litest_assert_empty_queue(li);
	litest_touch_up(finger, 0);
	litest_touch_move_to(finger, 1, 30, 30, 80, 80, 10, 1);
	litest_assert_empty_queue(li);
	litest_touch_up(finger, 1);
	libinput_dispatch(li);

	litest_timeout_touch_arbitration();
	libinput_dispatch(li);

	litest_touch_down(finger, 0, 30, 30);
	litest_touch_move_to(finger, 0, 30, 30, 80, 80, 10, 1);
	litest_touch_up(finger, 0);
	libinput_dispatch(li);

	if (is_touchpad)
		litest_assert_only_typed_events(li,
						LIBINPUT_EVENT_POINTER_MOTION);
	else
		litest_assert_touch_sequence(li);

	litest_delete_device(finger);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_REMOVED);
}

START_TEST(intuos_touch_arbitration_stop_touch)
{
	touch_arbitration_stop_touch(litest_current_device(),
				     LITEST_WACOM_FINGER,
				     true);
}
END_TEST

START_TEST(cintiq_touch_arbitration_stop_touch)
{
	touch_arbitration_stop_touch(litest_current_device(),
				     LITEST_WACOM_CINTIQ_13HDT_FINGER,
				     false);
}
END_TEST

static void
touch_arbitration_suspend_touch(struct litest_device *dev,
				enum litest_device_type other,
				bool is_touchpad)
{
	struct litest_device *tablet;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	tablet = litest_add_device(li, other);

	/* we can't force a device suspend, but we can at least make sure
	   the device doesn't send events */
	status = libinput_device_config_send_events_set_mode(
			     dev->libinput_device,
			     LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_tablet_proximity_in(tablet, 10, 10, axes);
	litest_tablet_motion(tablet, 10, 10, axes);
	litest_tablet_motion(tablet, 20, 40, axes);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 30, 30);
	litest_touch_move_to(dev, 0, 30, 30, 80, 80, 10, 1);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	/* Remove tablet device to unpair, still disabled though */
	litest_delete_device(tablet);
	litest_assert_tablet_proximity_event(li,
					     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_REMOVED);

	litest_timeout_touch_arbitration();
	libinput_dispatch(li);

	litest_touch_down(dev, 0, 30, 30);
	litest_touch_move_to(dev, 0, 30, 30, 80, 80, 10, 1);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	/* Touch device is still disabled */
	litest_touch_down(dev, 0, 30, 30);
	litest_touch_move_to(dev, 0, 30, 30, 80, 80, 10, 1);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	status = libinput_device_config_send_events_set_mode(
			     dev->libinput_device,
			     LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_touch_down(dev, 0, 30, 30);
	litest_touch_move_to(dev, 0, 30, 30, 80, 80, 10, 1);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	if (is_touchpad)
		litest_assert_only_typed_events(li,
						LIBINPUT_EVENT_POINTER_MOTION);
	else
		litest_assert_touch_sequence(li);
}

START_TEST(intuos_touch_arbitration_suspend_touch_device)
{
	touch_arbitration_suspend_touch(litest_current_device(),
					LITEST_WACOM_INTUOS,
					true);
}
END_TEST

START_TEST(cintiq_touch_arbitration_suspend_touch_device)
{
	touch_arbitration_suspend_touch(litest_current_device(),
					LITEST_WACOM_CINTIQ_13HDT_PEN,
					false);
}
END_TEST

static void
touch_arbitration_remove_touch(struct litest_device *dev,
			       enum litest_device_type other,
			       bool is_touchpad)
{
	struct litest_device *finger;
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	finger = litest_add_device(li, other);
	litest_touch_down(finger, 0, 30, 30);
	litest_touch_move_to(finger, 0, 30, 30, 80, 80, 10, 1);

	litest_tablet_proximity_in(dev, 10, 10, axes);
	litest_drain_events(li);

	litest_delete_device(finger);
	libinput_dispatch(li);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_REMOVED);
	litest_assert_empty_queue(li);

	litest_tablet_motion(dev, 10, 10, axes);
	litest_tablet_motion(dev, 20, 40, axes);
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_TABLET_TOOL_AXIS);
}

START_TEST(intuos_touch_arbitration_remove_touch)
{
	touch_arbitration_remove_touch(litest_current_device(),
				       LITEST_WACOM_FINGER,
				       true);
}
END_TEST

START_TEST(cintiq_touch_arbitration_remove_touch)
{
	touch_arbitration_remove_touch(litest_current_device(),
				       LITEST_WACOM_CINTIQ_13HDT_FINGER,
				       false);
}
END_TEST

static void
touch_arbitration_remove_tablet(struct litest_device *dev,
				enum litest_device_type other,
				bool is_touchpad)
{
	struct litest_device *tablet;
	struct libinput *li = dev->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	tablet = litest_add_device(li, other);
	libinput_dispatch(li);
	litest_tablet_proximity_in(tablet, 10, 10, axes);
	litest_tablet_motion(tablet, 10, 10, axes);
	litest_tablet_motion(tablet, 20, 40, axes);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 30, 30);
	litest_touch_move_to(dev, 0, 30, 30, 80, 80, 10, 1);
	litest_assert_empty_queue(li);

	litest_delete_device(tablet);
	litest_assert_tablet_proximity_event(li,
			     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_DEVICE_REMOVED);

	litest_timeout_touch_arbitration();
	libinput_dispatch(li);

	/* Touch is still down, don't enable */
	litest_touch_move_to(dev, 0, 80, 80, 30, 30, 10, 1);
	litest_touch_up(dev, 0);
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 30, 30);
	litest_touch_move_to(dev, 0, 30, 30, 80, 80, 10, 1);
	litest_touch_up(dev, 0);
	libinput_dispatch(li);

	if (is_touchpad)
		litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
	else
		litest_assert_touch_sequence(li);
}

START_TEST(intuos_touch_arbitration_remove_tablet)
{
	touch_arbitration_remove_tablet(litest_current_device(),
					LITEST_WACOM_INTUOS,
					true);
}
END_TEST

START_TEST(cintiq_touch_arbitration_remove_tablet)
{
	touch_arbitration_remove_tablet(litest_current_device(),
					LITEST_WACOM_CINTIQ_13HDT_PEN,
					false);
}
END_TEST

START_TEST(intuos_touch_arbitration_keep_ignoring)
{
	struct litest_device *tablet = litest_current_device();
	struct litest_device *finger;
	struct libinput *li = tablet->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	finger = litest_add_device(li, LITEST_WACOM_FINGER);
	litest_enable_tap(finger->libinput_device);
	litest_tablet_proximity_in(tablet, 10, 10, axes);
	litest_tablet_motion(tablet, 10, 10, axes);
	litest_tablet_motion(tablet, 20, 40, axes);

	litest_touch_down(finger, 0, 30, 30);
	litest_drain_events(li);

	litest_tablet_proximity_out(tablet);
	litest_drain_events(li);

	/* a touch during pen interaction stays a palm after the pen lifts.
	 */
	litest_touch_move_to(finger, 0, 30, 30, 80, 80, 10, 1);
	litest_touch_up(finger, 0);
	libinput_dispatch(li);

	litest_assert_empty_queue(li);

	litest_delete_device(finger);
}
END_TEST

START_TEST(intuos_touch_arbitration_late_touch_lift)
{
	struct litest_device *tablet = litest_current_device();
	struct litest_device *finger;
	struct libinput *li = tablet->libinput;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ ABS_PRESSURE, 0 },
		{ -1, -1 }
	};

	finger = litest_add_device(li, LITEST_WACOM_FINGER);
	litest_enable_tap(finger->libinput_device);
	litest_tablet_proximity_in(tablet, 10, 10, axes);
	litest_tablet_motion(tablet, 10, 10, axes);
	litest_tablet_motion(tablet, 20, 40, axes);
	litest_drain_events(li);

	litest_tablet_proximity_out(tablet);
	litest_drain_events(li);

	/* with kernel arbitration, a finger + stylus in prox only generates
	 * stylus events. When lifting the hand off, the stylus goes out of
	 * prox when the hand is still touching. A few ms later, the hand
	 * goes out of prox, this can generate a fake tap event.
	 */
	litest_touch_down(finger, 0, 30, 30);
	litest_touch_up(finger, 0);
	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);

	litest_assert_empty_queue(li);

	litest_delete_device(finger);
}
END_TEST

START_TEST(huion_static_btn_tool_pen)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;

	litest_drain_events(li);

	litest_event(dev, EV_ABS, ABS_X, 20000);
	litest_event(dev, EV_ABS, ABS_Y, 20000);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 100);
	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_drain_events(li);

	for (i = 0; i < 10; i++) {
		litest_event(dev, EV_ABS, ABS_X, 20000 + 10 * i);
		litest_event(dev, EV_ABS, ABS_Y, 20000 - 10 * i);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	/* Wait past the timeout to expect a proximity out */
	litest_timeout_tablet_proxout();
	libinput_dispatch(li);
	litest_assert_tablet_proximity_event(li,
			     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	libinput_dispatch(li);

	/* New events should fake a proximity in again */
	litest_event(dev, EV_ABS, ABS_X, 20000);
	litest_event(dev, EV_ABS, ABS_Y, 20000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
	litest_assert_tablet_proximity_event(li,
			     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);
	libinput_dispatch(li);

	for (i = 0; i < 10; i++) {
		litest_event(dev, EV_ABS, ABS_X, 20000 + 10 * i);
		litest_event(dev, EV_ABS, ABS_Y, 20000 - 10 * i);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	litest_timeout_tablet_proxout();
	libinput_dispatch(li);
	litest_assert_tablet_proximity_event(li,
			     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	libinput_dispatch(li);

	/* New events, just to ensure cleanup paths are correct */
	litest_event(dev, EV_ABS, ABS_X, 20000);
	litest_event(dev, EV_ABS, ABS_Y, 20000);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);
}
END_TEST

START_TEST(huion_static_btn_tool_pen_no_timeout_during_usage)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	int i;

	litest_drain_events(li);

	litest_event(dev, EV_ABS, ABS_X, 20000);
	litest_event(dev, EV_ABS, ABS_Y, 20000);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 100);
	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_drain_events(li);

	/* take longer than the no-activity timeout */
	for (i = 0; i < 50; i++) {
		litest_event(dev, EV_ABS, ABS_X, 20000 + 10 * i);
		litest_event(dev, EV_ABS, ABS_Y, 20000 - 10 * i);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
		msleep(5);
	}
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_TABLET_TOOL_AXIS);
	litest_timeout_tablet_proxout();
	libinput_dispatch(li);
	litest_assert_tablet_proximity_event(li,
			     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	libinput_dispatch(li);
}
END_TEST

START_TEST(huion_static_btn_tool_pen_disable_quirk_on_prox_out)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	bool with_timeout = _i; /* ranged test */
	int i;

	/* test is run twice, once where the real BTN_TOOL_PEN is triggered
	 * during proximity out, one where the real BTN_TOOL_PEN is
	 * triggered after we already triggered the quirk timeout
	 */

	litest_drain_events(li);

	litest_event(dev, EV_ABS, ABS_X, 20000);
	litest_event(dev, EV_ABS, ABS_Y, 20000);
	litest_event(dev, EV_ABS, ABS_PRESSURE, 100);
	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_drain_events(li);

	for (i = 0; i < 10; i++) {
		litest_event(dev, EV_ABS, ABS_X, 20000 + 10 * i);
		litest_event(dev, EV_ABS, ABS_Y, 20000 - 10 * i);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
		libinput_dispatch(li);
	}
	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	/* Wait past the timeout to expect a proximity out */
	if (with_timeout) {
		litest_timeout_tablet_proxout();
		libinput_dispatch(li);
		litest_assert_tablet_proximity_event(li,
						     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	}

	/* Send a real prox out, expect quirk to be disabled */
	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	if (with_timeout) {
		/* we got the proximity event above already */
		litest_assert_empty_queue(li);
	} else {
		litest_assert_tablet_proximity_event(li,
						     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	}

	litest_push_event_frame(dev);
	litest_tablet_proximity_out(dev);
	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_pop_event_frame(dev);

	litest_tablet_proximity_in(dev, 50, 50, NULL);
	libinput_dispatch(li);
	litest_assert_tablet_proximity_event(li,
			     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);
	libinput_dispatch(li);

	for (i = 0; i < 10; i++) {
		litest_tablet_motion(dev, 50 + i, 50 + i, NULL);
		libinput_dispatch(li);
	}

	litest_assert_only_typed_events(li,
					LIBINPUT_EVENT_TABLET_TOOL_AXIS);

	litest_push_event_frame(dev);
	litest_tablet_proximity_out(dev);
	litest_event(dev, EV_KEY, BTN_TOOL_PEN, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_pop_event_frame(dev);
	libinput_dispatch(li);

	litest_assert_tablet_proximity_event(li,
			     LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT);
	litest_assert_empty_queue(li);
}
END_TEST

TEST_COLLECTION(tablet)
{
	struct range with_timeout = { 0, 2 };

	litest_add("tablet:tool", tool_ref, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add("tablet:tool", tool_user_data, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add("tablet:tool", tool_capability, LITEST_TABLET, LITEST_ANY);
	litest_add_no_device("tablet:tool", tool_capabilities);
	litest_add("tablet:tool", tool_type, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tool", tool_in_prox_before_start, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tool_serial", tool_unique, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add("tablet:tool_serial", tool_serial, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add("tablet:tool_serial", tool_id, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add("tablet:tool_serial", serial_changes_tool, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add("tablet:tool_serial", invalid_serials, LITEST_TABLET | LITEST_TOOL_SERIAL, LITEST_ANY);
	litest_add_no_device("tablet:tool_serial", tools_with_serials);
	litest_add_no_device("tablet:tool_serial", tools_without_serials);
	litest_add_for_device("tablet:tool_serial", tool_delayed_serial, LITEST_WACOM_HID4800_PEN);
	litest_add("tablet:proximity", proximity_out_clear_buttons, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:proximity", proximity_in_out, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:proximity", proximity_in_button_down, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:proximity", proximity_out_button_up, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:proximity", proximity_has_axes, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:proximity", bad_distance_events, LITEST_TABLET | LITEST_DISTANCE, LITEST_ANY);
	litest_add("tablet:proximity", proximity_range_enter, LITEST_TABLET | LITEST_DISTANCE | LITEST_TOOL_MOUSE, LITEST_ANY);
	litest_add("tablet:proximity", proximity_range_in_out, LITEST_TABLET | LITEST_DISTANCE | LITEST_TOOL_MOUSE, LITEST_ANY);
	litest_add("tablet:proximity", proximity_range_button_click, LITEST_TABLET | LITEST_DISTANCE | LITEST_TOOL_MOUSE, LITEST_ANY);
	litest_add("tablet:proximity", proximity_range_button_press, LITEST_TABLET | LITEST_DISTANCE | LITEST_TOOL_MOUSE, LITEST_ANY);
	litest_add("tablet:proximity", proximity_range_button_release, LITEST_TABLET | LITEST_DISTANCE | LITEST_TOOL_MOUSE, LITEST_ANY);
	litest_add_no_device("tablet:proximity", proximity_out_on_delete);
	litest_add("tablet:button", button_down_up, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:button", button_seat_count, LITEST_TABLET, LITEST_ANY);
	litest_add_no_device("tablet:button", button_up_on_delete);
	litest_add("tablet:tip", tip_down_up, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tip", tip_down_prox_in, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tip", tip_up_prox_out, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tip", tip_down_btn_change, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tip", tip_up_btn_change, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tip", tip_down_motion, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tip", tip_up_motion, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tip", tip_state_proximity, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tip", tip_state_axis, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:tip", tip_state_button, LITEST_TABLET, LITEST_ANY);
	litest_add_no_device("tablet:tip", tip_up_on_delete);
	litest_add("tablet:motion", motion, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:motion", motion_event_state, LITEST_TABLET, LITEST_ANY);
	litest_add_for_device("tablet:motion", motion_outside_bounds, LITEST_WACOM_CINTIQ_24HD);
	litest_add("tablet:tilt", tilt_available, LITEST_TABLET|LITEST_TILT, LITEST_ANY);
	litest_add("tablet:tilt", tilt_not_available, LITEST_TABLET, LITEST_TILT);
	litest_add("tablet:tilt", tilt_x, LITEST_TABLET|LITEST_TILT, LITEST_ANY);
	litest_add("tablet:tilt", tilt_y, LITEST_TABLET|LITEST_TILT, LITEST_ANY);
	litest_add_for_device("tablet:left_handed", left_handed, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:left_handed", left_handed_tilt, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:left_handed", left_handed_mouse_rotation, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:left_handed", left_handed_artpen_rotation, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:left_handed", no_left_handed, LITEST_WACOM_CINTIQ);
	litest_add("tablet:pad", pad_buttons_ignored, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:mouse", mouse_tool, LITEST_TABLET | LITEST_TOOL_MOUSE, LITEST_ANY);
	litest_add("tablet:mouse", mouse_buttons, LITEST_TABLET | LITEST_TOOL_MOUSE, LITEST_ANY);
	litest_add("tablet:mouse", mouse_rotation, LITEST_TABLET | LITEST_TOOL_MOUSE, LITEST_ANY);
	litest_add("tablet:mouse", mouse_wheel, LITEST_TABLET | LITEST_TOOL_MOUSE, LITEST_WHEEL);
	litest_add("tablet:airbrush", airbrush_tool, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:airbrush", airbrush_slider, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:artpen", artpen_tool, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:artpen", artpen_rotation, LITEST_TABLET, LITEST_ANY);

	litest_add("tablet:time", tablet_time_usec, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:pressure", tablet_pressure_distance_exclusive, LITEST_TABLET | LITEST_DISTANCE, LITEST_ANY);

	litest_add("tablet:calibration", tablet_calibration_has_matrix, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:calibration", tablet_calibration_set_matrix, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:calibration", tablet_calibration_set_matrix_delta, LITEST_TABLET, LITEST_ANY);

	litest_add("tablet:pressure", tablet_pressure_min_max, LITEST_TABLET, LITEST_ANY);
	litest_add_for_device("tablet:pressure", tablet_pressure_range, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:pressure", tablet_pressure_offset, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:pressure", tablet_pressure_offset_decrease, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:pressure", tablet_pressure_offset_increase, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:pressure", tablet_pressure_offset_exceed_threshold, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:pressure", tablet_pressure_offset_none_for_zero_distance, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:pressure", tablet_pressure_offset_none_for_small_distance, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:distance", tablet_distance_range, LITEST_WACOM_INTUOS);

	litest_add("tablet:relative", relative_no_profile, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:relative", relative_no_delta_prox_in, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:relative", relative_delta, LITEST_TABLET, LITEST_ANY);
	litest_add("tablet:relative", relative_calibration, LITEST_TABLET, LITEST_ANY);

	litest_add_for_device("tablet:touch-arbitration", intuos_touch_arbitration, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:touch-arbitration", intuos_touch_arbitration_stop_touch, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:touch-arbitration", intuos_touch_arbitration_suspend_touch_device, LITEST_WACOM_FINGER);
	litest_add_for_device("tablet:touch-arbitration", intuos_touch_arbitration_remove_touch, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:touch-arbitration", intuos_touch_arbitration_remove_tablet, LITEST_WACOM_FINGER);
	litest_add_for_device("tablet:touch-arbitration", intuos_touch_arbitration_keep_ignoring, LITEST_WACOM_INTUOS);
	litest_add_for_device("tablet:touch-arbitration", intuos_touch_arbitration_late_touch_lift, LITEST_WACOM_INTUOS);

	litest_add_for_device("tablet:touch-arbitration", cintiq_touch_arbitration, LITEST_WACOM_CINTIQ_13HDT_PEN);
	litest_add_for_device("tablet:touch-arbitration", cintiq_touch_arbitration_stop_touch, LITEST_WACOM_CINTIQ_13HDT_PEN);
	litest_add_for_device("tablet:touch-arbitration", cintiq_touch_arbitration_suspend_touch_device, LITEST_WACOM_CINTIQ_13HDT_FINGER);
	litest_add_for_device("tablet:touch-arbitration", cintiq_touch_arbitration_remove_touch, LITEST_WACOM_CINTIQ_13HDT_PEN);
	litest_add_for_device("tablet:touch-arbitration", cintiq_touch_arbitration_remove_tablet, LITEST_WACOM_CINTIQ_13HDT_FINGER);

	litest_add_for_device("tablet:quirks", huion_static_btn_tool_pen, LITEST_HUION_TABLET);
	litest_add_for_device("tablet:quirks", huion_static_btn_tool_pen_no_timeout_during_usage, LITEST_HUION_TABLET);
	litest_add_ranged_for_device("tablet:quirks", huion_static_btn_tool_pen_disable_quirk_on_prox_out, LITEST_HUION_TABLET, &with_timeout);
}
