/*
 * Copyright © 2015 Red Hat, Inc.
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

#include "config.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>

#include "evdev-mt-touchpad.h"

#define DEFAULT_GESTURE_SWITCH_TIMEOUT 100 /* ms */

static struct normalized_coords
tp_get_touches_delta(struct tp_dispatch *tp, bool average)
{
	struct tp_touch *t;
	unsigned int i, nchanged = 0;
	struct normalized_coords normalized;
	struct normalized_coords delta = {0.0, 0.0};

	for (i = 0; i < tp->num_slots; i++) {
		t = &tp->touches[i];

		if (tp_touch_active(tp, t) && t->dirty) {
			nchanged++;
			normalized = tp_get_delta(t);

			delta.x += normalized.x;
			delta.y += normalized.y;
		}
	}

	if (!average || nchanged == 0)
		return delta;

	delta.x /= nchanged;
	delta.y /= nchanged;

	return delta;
}

static inline struct normalized_coords
tp_get_combined_touches_delta(struct tp_dispatch *tp)
{
	return tp_get_touches_delta(tp, false);
}

static inline struct normalized_coords
tp_get_average_touches_delta(struct tp_dispatch *tp)
{
	return tp_get_touches_delta(tp, true);
}

static void
tp_gesture_start(struct tp_dispatch *tp, uint64_t time)
{
	const struct normalized_coords zero = { 0.0, 0.0 };

	if (tp->gesture.started)
		return;

	switch (tp->gesture.finger_count) {
	case 2:
		/* NOP */
		break;
	case 3:
	case 4:
		gesture_notify_swipe(&tp->device->base, time,
				     LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN,
				     tp->gesture.finger_count,
				     &zero, &zero);
		break;
	}
	tp->gesture.started = true;
}

static void
tp_gesture_post_pointer_motion(struct tp_dispatch *tp, uint64_t time)
{
	struct normalized_coords delta, unaccel;

	/* When a clickpad is clicked, combine motion of all active touches */
	if (tp->buttons.is_clickpad && tp->buttons.state)
		unaccel = tp_get_combined_touches_delta(tp);
	else
		unaccel = tp_get_average_touches_delta(tp);

	delta = tp_filter_motion(tp, &unaccel, time);

	if (!normalized_is_zero(delta) || !normalized_is_zero(unaccel)) {
		pointer_notify_motion(&tp->device->base, time,
				      &delta, &unaccel);
	}
}

static void
tp_gesture_post_twofinger_scroll(struct tp_dispatch *tp, uint64_t time)
{
	struct normalized_coords delta;

	if (tp->scroll.method != LIBINPUT_CONFIG_SCROLL_2FG)
		return;

	/* On some semi-mt models slot 0 is more accurate, so for semi-mt
	 * we only use slot 0. */
	if (tp->semi_mt) {
		if (!tp->touches[0].dirty)
			return;

		delta = tp_get_delta(&tp->touches[0]);
	} else {
		delta = tp_get_average_touches_delta(tp);
	}

	delta = tp_filter_motion(tp, &delta, time);

	if (normalized_is_zero(delta))
		return;

	tp_gesture_start(tp, time);
	evdev_post_scroll(tp->device,
			  time,
			  LIBINPUT_POINTER_AXIS_SOURCE_FINGER,
			  &delta);
}

static void
tp_gesture_post_swipe(struct tp_dispatch *tp, uint64_t time)
{
	struct normalized_coords delta, unaccel;

	unaccel = tp_get_average_touches_delta(tp);
	delta = tp_filter_motion(tp, &unaccel, time);

	if (!normalized_is_zero(delta) || !normalized_is_zero(unaccel)) {
		tp_gesture_start(tp, time);
		gesture_notify_swipe(&tp->device->base, time,
				     LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE,
				     tp->gesture.finger_count,
				     &delta, &unaccel);
	}
}

void
tp_gesture_post_events(struct tp_dispatch *tp, uint64_t time)
{
	if (tp->gesture.finger_count == 0)
		return;

	/* When tap-and-dragging, or a clickpad is clicked force 1fg mode */
	if (tp_tap_dragging(tp) || (tp->buttons.is_clickpad && tp->buttons.state)) {
		tp_gesture_stop(tp, time);
		tp->gesture.finger_count = 1;
		tp->gesture.finger_count_pending = 0;
	}

	/* Don't send events when we're unsure in which mode we are */
	if (tp->gesture.finger_count_pending)
		return;

	switch (tp->gesture.finger_count) {
	case 1:
		tp_gesture_post_pointer_motion(tp, time);
		break;
	case 2:
		tp_gesture_post_twofinger_scroll(tp, time);
		break;
	case 3:
	case 4:
		tp_gesture_post_swipe(tp, time);
		break;
	}
}

void
tp_gesture_stop_twofinger_scroll(struct tp_dispatch *tp, uint64_t time)
{
	if (tp->scroll.method != LIBINPUT_CONFIG_SCROLL_2FG)
		return;

	evdev_stop_scroll(tp->device,
			  time,
			  LIBINPUT_POINTER_AXIS_SOURCE_FINGER);
}

void
tp_gesture_stop(struct tp_dispatch *tp, uint64_t time)
{
	const struct normalized_coords zero = { 0.0, 0.0 };

	if (!tp->gesture.started)
		return;

	switch (tp->gesture.finger_count) {
	case 2:
		tp_gesture_stop_twofinger_scroll(tp, time);
		break;
	case 3:
	case 4:
		gesture_notify_swipe(&tp->device->base, time,
				     LIBINPUT_EVENT_GESTURE_SWIPE_END,
				     tp->gesture.finger_count,
				     &zero, &zero);
		break;
	}
	tp->gesture.started = false;
}

static void
tp_gesture_finger_count_switch_timeout(uint64_t now, void *data)
{
	struct tp_dispatch *tp = data;

	if (!tp->gesture.finger_count_pending)
		return;

	tp_gesture_stop(tp, now); /* End current gesture */
	tp->gesture.finger_count = tp->gesture.finger_count_pending;
	tp->gesture.finger_count_pending = 0;
}

void
tp_gesture_handle_state(struct tp_dispatch *tp, uint64_t time)
{
	unsigned int active_touches = 0;
	struct tp_touch *t;

	tp_for_each_touch(tp, t)
		if (tp_touch_active(tp, t))
			active_touches++;

	if (active_touches != tp->gesture.finger_count) {
		/* If all fingers are lifted immediately end the gesture */
		if (active_touches == 0) {
			tp_gesture_stop(tp, time);
			tp->gesture.finger_count = 0;
			tp->gesture.finger_count_pending = 0;
		/* Immediately switch to new mode to avoid initial latency */
		} else if (!tp->gesture.started) {
			tp->gesture.finger_count = active_touches;
			tp->gesture.finger_count_pending = 0;
		/* Else debounce finger changes */
		} else if (active_touches != tp->gesture.finger_count_pending) {
			tp->gesture.finger_count_pending = active_touches;
			libinput_timer_set(&tp->gesture.finger_count_switch_timer,
				time + DEFAULT_GESTURE_SWITCH_TIMEOUT);
		}
	} else {
		 tp->gesture.finger_count_pending = 0;
	}
}

int
tp_init_gesture(struct tp_dispatch *tp)
{
	libinput_timer_init(&tp->gesture.finger_count_switch_timer,
			    tp->device->base.seat->libinput,
			    tp_gesture_finger_count_switch_timeout, tp);
	return 0;
}

void
tp_remove_gesture(struct tp_dispatch *tp)
{
	libinput_timer_cancel(&tp->gesture.finger_count_switch_timer);
}
