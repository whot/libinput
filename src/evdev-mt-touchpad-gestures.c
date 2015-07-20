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

#include <math.h>
#include <stdbool.h>
#include <limits.h>

#include "evdev-mt-touchpad.h"

#define DEFAULT_GESTURE_SWITCH_TIMEOUT 100 /* ms */
#define DEFAULT_GESTURE_2FG_SCROLL_TIMEOUT 1000 /* ms */

static inline const char*
gesture_state_to_str(enum tp_gesture_2fg_state state)
{
	switch (state) {
	CASE_RETURN_STRING(GESTURE_2FG_STATE_NONE);
	CASE_RETURN_STRING(GESTURE_2FG_STATE_UNKNOWN);
	CASE_RETURN_STRING(GESTURE_2FG_STATE_SCROLL);
	CASE_RETURN_STRING(GESTURE_2FG_STATE_PINCH);
	}
	return NULL;
}

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
	struct libinput *libinput = tp->device->base.seat->libinput;
	const struct normalized_coords zero = { 0.0, 0.0 };

	if (tp->gesture.started)
		return;

	switch (tp->gesture.finger_count) {
	case 2:
		switch (tp->gesture.twofinger_state) {
		case GESTURE_2FG_STATE_NONE:
		case GESTURE_2FG_STATE_UNKNOWN:
			log_bug_libinput(libinput,
					 "%s in unknown gesture mode\n",
					 __func__);
			break;
		case GESTURE_2FG_STATE_SCROLL:
			/* NOP */
			break;
		case GESTURE_2FG_STATE_PINCH:
			gesture_notify_pinch(&tp->device->base, time,
					    LIBINPUT_EVENT_GESTURE_PINCH_BEGIN,
					    &zero, &zero, 1.0, 0.0);
			break;
		}
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
	struct device_float_coords raw;

	/* When a clickpad is clicked, combine motion of all active touches */
	if (tp->buttons.is_clickpad && tp->buttons.state)
		unaccel = tp_get_combined_touches_delta(tp);
	else
		unaccel = tp_get_average_touches_delta(tp);

	delta = tp_filter_motion(tp, &unaccel, time);

	if (!normalized_is_zero(delta) || !normalized_is_zero(unaccel)) {
		raw = tp_unnormalize_for_xaxis(tp, unaccel);
		pointer_notify_motion(&tp->device->base,
				      time,
				      &delta,
				      &raw);
	}
}

static unsigned int
tp_gesture_get_active_touches(struct tp_dispatch *tp,
			      struct tp_touch **touches,
			      unsigned int count)
{
	unsigned int i, n = 0;
	struct tp_touch *t;

	memset(touches, 0, count * sizeof(struct tp_touch *));

	for (i = 0; i < tp->num_slots; i++) {
		t = &tp->touches[i];
		if (tp_touch_active(tp, t)) {
			touches[n++] = t;
			if (n == count)
				return count;
		}
	}

	/*
	 * This can happen when the user does .e.g:
	 * 1) Put down 1st finger in center (so active)
	 * 2) Put down 2nd finger in a button area (so inactive)
	 * 3) Put down 3th finger somewhere, gets reported as a fake finger,
	 *    so gets same coordinates as 1st -> active
	 *
	 * We could avoid this by looking at all touches, be we really only
	 * want to look at real touches.
	 */
	return n;
}

static int
tp_gesture_get_direction(struct tp_dispatch *tp, struct tp_touch *touch)
{
	struct normalized_coords normalized;
	struct device_float_coords delta;
	double move_threshold;

	/*
	 * Semi-mt touchpads have somewhat inaccurate coordinates when
	 * 2 fingers are down, so use a slightly larger threshold.
	 */
	if (tp->semi_mt)
		move_threshold = TP_MM_TO_DPI_NORMALIZED(4);
	else
		move_threshold = TP_MM_TO_DPI_NORMALIZED(2);

	delta = device_delta(touch->point, touch->gesture.initial);

	normalized = tp_normalize_delta(tp, delta);

	if (normalized_length(normalized) < move_threshold)
		return UNDEFINED_DIRECTION;

	return normalized_get_direction(normalized);
}

static void
tp_gesture_get_pinch_info(struct tp_dispatch *tp,
			  double *distance,
			  double *angle,
			  struct device_float_coords *center)
{
	struct normalized_coords normalized;
	struct device_float_coords delta;
	struct tp_touch *first = tp->gesture.touches[0],
			*second = tp->gesture.touches[1];

	delta = device_delta(first->point, second->point);
	normalized = tp_normalize_delta(tp, delta);
	*distance = normalized_length(normalized);

	if (!tp->semi_mt)
		*angle = atan2(normalized.y, normalized.x) * 180.0 / M_PI;
	else
		*angle = 0.0;

	*center = device_average(first->point, second->point);
}

static void
tp_gesture_set_scroll_buildup(struct tp_dispatch *tp)
{
	struct device_float_coords d0, d1;
	struct device_float_coords average;
	struct tp_touch *first = tp->gesture.touches[0],
			*second = tp->gesture.touches[1];

	d0 = device_delta(first->point, first->gesture.initial);
	d1 = device_delta(second->point, second->gesture.initial);

	average = device_float_average(d0, d1);
	tp->device->scroll.buildup = tp_normalize_delta(tp, average);
}

static enum tp_gesture_2fg_state
tp_gesture_twofinger_handle_state_none(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *first, *second;

	if (tp_gesture_get_active_touches(tp, tp->gesture.touches, 2) != 2)
		return GESTURE_2FG_STATE_NONE;

	first = tp->gesture.touches[0];
	second = tp->gesture.touches[1];

	tp->gesture.initial_time = time;
	first->gesture.initial = first->point;
	second->gesture.initial = second->point;

	return GESTURE_2FG_STATE_UNKNOWN;
}

static enum tp_gesture_2fg_state
tp_gesture_twofinger_handle_state_unknown(struct tp_dispatch *tp, uint64_t time)
{
	struct normalized_coords normalized;
	struct device_float_coords delta;
	struct tp_touch *first = tp->gesture.touches[0],
			*second = tp->gesture.touches[1];
	int dir1, dir2;

	delta = device_delta(first->point, second->point);
	normalized = tp_normalize_delta(tp, delta);

	/* If fingers are further than 3 cm apart assume pinch */
	if (normalized_length(normalized) > TP_MM_TO_DPI_NORMALIZED(30)) {
		tp_gesture_get_pinch_info(tp,
					  &tp->gesture.initial_distance,
					  &tp->gesture.angle,
					  &tp->gesture.center);
		tp->gesture.prev_scale = 1.0;
		return GESTURE_2FG_STATE_PINCH;
	}

	/* Elif fingers have been close together for a while, scroll */
	if (time > (tp->gesture.initial_time + DEFAULT_GESTURE_2FG_SCROLL_TIMEOUT)) {
		tp_gesture_set_scroll_buildup(tp);
		return GESTURE_2FG_STATE_SCROLL;
	}

	/* Else wait for both fingers to have moved */
	dir1 = tp_gesture_get_direction(tp, first);
	dir2 = tp_gesture_get_direction(tp, second);
	if (dir1 == UNDEFINED_DIRECTION || dir2 == UNDEFINED_DIRECTION)
		return GESTURE_2FG_STATE_UNKNOWN;

	/*
	 * If both touches are moving in the same direction assume scroll.
	 *
	 * In some cases (semi-mt touchpads) We may seen one finger move
	 * e.g. N/NE and the other W/NW so we not only check for overlapping
	 * directions, but also for neighboring bits being set.
	 * The ((dira & 0x80) && (dirb & 0x01)) checks are to check for bit 0
	 * and 7 being set as they also represent neighboring directions.
	 */
	if (((dir1 | (dir1 >> 1)) & dir2) ||
	    ((dir2 | (dir2 >> 1)) & dir1) ||
	    ((dir1 & 0x80) && (dir2 & 0x01)) ||
	    ((dir2 & 0x80) && (dir1 & 0x01))) {
		tp_gesture_set_scroll_buildup(tp);
		return GESTURE_2FG_STATE_SCROLL;
	} else {
		tp_gesture_get_pinch_info(tp,
					  &tp->gesture.initial_distance,
					  &tp->gesture.angle,
					  &tp->gesture.center);
		tp->gesture.prev_scale = 1.0;
		return GESTURE_2FG_STATE_PINCH;
	}
}

static enum tp_gesture_2fg_state
tp_gesture_twofinger_handle_state_scroll(struct tp_dispatch *tp, uint64_t time)
{
	struct normalized_coords delta;

	if (tp->scroll.method != LIBINPUT_CONFIG_SCROLL_2FG)
		return GESTURE_2FG_STATE_SCROLL;

	/* On some semi-mt models slot 0 is more accurate, so for semi-mt
	 * we only use slot 0. */
	if (tp->semi_mt) {
		if (!tp->touches[0].dirty)
			return GESTURE_2FG_STATE_SCROLL;

		delta = tp_get_delta(&tp->touches[0]);
	} else {
		delta = tp_get_average_touches_delta(tp);
	}

	delta = tp_filter_motion(tp, &delta, time);

	if (normalized_is_zero(delta))
		return GESTURE_2FG_STATE_SCROLL;

	tp_gesture_start(tp, time);
	evdev_post_scroll(tp->device,
			  time,
			  LIBINPUT_POINTER_AXIS_SOURCE_FINGER,
			  &delta);

	return GESTURE_2FG_STATE_SCROLL;
}

static enum tp_gesture_2fg_state
tp_gesture_twofinger_handle_state_pinch(struct tp_dispatch *tp, uint64_t time)
{
	double angle, angle_delta, distance, scale;
	struct device_float_coords center, fdelta;
	struct normalized_coords delta, unaccel;

	tp_gesture_get_pinch_info(tp, &distance, &angle, &center);

	scale = distance / tp->gesture.initial_distance;

	angle_delta = angle - tp->gesture.angle;
	tp->gesture.angle = angle;
	if (angle_delta > 180.0)
		angle_delta -= 360.0;
	else if (angle_delta < -180.0)
		angle_delta += 360.0;

	fdelta = device_float_delta(center, tp->gesture.center);
	tp->gesture.center = center;
	unaccel = tp_normalize_delta(tp, fdelta);
	delta = tp_filter_motion(tp, &unaccel, time);

	if (normalized_is_zero(delta) && normalized_is_zero(unaccel) &&
	    scale == tp->gesture.prev_scale && angle_delta == 0.0)
		return GESTURE_2FG_STATE_PINCH;

	tp_gesture_start(tp, time);
	gesture_notify_pinch(&tp->device->base, time,
			     LIBINPUT_EVENT_GESTURE_PINCH_UPDATE,
			     &delta, &unaccel, scale, angle_delta);

	tp->gesture.prev_scale = scale;

	return GESTURE_2FG_STATE_PINCH;
}

static void
tp_gesture_post_twofinger(struct tp_dispatch *tp, uint64_t time)
{
	enum tp_gesture_2fg_state oldstate = tp->gesture.twofinger_state;

	if (tp->gesture.twofinger_state == GESTURE_2FG_STATE_NONE)
		tp->gesture.twofinger_state =
			tp_gesture_twofinger_handle_state_none(tp, time);

	if (tp->gesture.twofinger_state == GESTURE_2FG_STATE_UNKNOWN)
		tp->gesture.twofinger_state =
			tp_gesture_twofinger_handle_state_unknown(tp, time);

	if (tp->gesture.twofinger_state == GESTURE_2FG_STATE_SCROLL)
		tp->gesture.twofinger_state =
			tp_gesture_twofinger_handle_state_scroll(tp, time);

	if (tp->gesture.twofinger_state == GESTURE_2FG_STATE_PINCH)
		tp->gesture.twofinger_state =
			tp_gesture_twofinger_handle_state_pinch(tp, time);

	log_debug(tp_libinput_context(tp),
		  "gesture state: %s → %s\n",
		  gesture_state_to_str(oldstate),
		  gesture_state_to_str(tp->gesture.twofinger_state));
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
		tp_gesture_cancel(tp, time);
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
		tp_gesture_post_twofinger(tp, time);
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

static void
tp_gesture_end(struct tp_dispatch *tp, uint64_t time, bool cancelled)
{
	struct libinput *libinput = tp->device->base.seat->libinput;
	enum tp_gesture_2fg_state twofinger_state = tp->gesture.twofinger_state;

	tp->gesture.twofinger_state = GESTURE_2FG_STATE_NONE;

	if (!tp->gesture.started)
		return;

	switch (tp->gesture.finger_count) {
	case 2:
		switch (twofinger_state) {
		case GESTURE_2FG_STATE_NONE:
		case GESTURE_2FG_STATE_UNKNOWN:
			log_bug_libinput(libinput,
					 "%s in unknown gesture mode\n",
					 __func__);
			break;
		case GESTURE_2FG_STATE_SCROLL:
			tp_gesture_stop_twofinger_scroll(tp, time);
			break;
		case GESTURE_2FG_STATE_PINCH:
			gesture_notify_pinch_end(&tp->device->base, time,
						 tp->gesture.prev_scale,
						 cancelled);
			break;
		}
		break;
	case 3:
	case 4:
		gesture_notify_swipe_end(&tp->device->base, time,
					 tp->gesture.finger_count, cancelled);
		break;
	}
	tp->gesture.started = false;
}

void
tp_gesture_cancel(struct tp_dispatch *tp, uint64_t time)
{
	tp_gesture_end(tp, time, true);
}

void
tp_gesture_stop(struct tp_dispatch *tp, uint64_t time)
{
	tp_gesture_end(tp, time, false);
}

static void
tp_gesture_finger_count_switch_timeout(uint64_t now, void *data)
{
	struct tp_dispatch *tp = data;

	if (!tp->gesture.finger_count_pending)
		return;

	tp_gesture_cancel(tp, now); /* End current gesture */
	tp->gesture.finger_count = tp->gesture.finger_count_pending;
	tp->gesture.finger_count_pending = 0;
}

void
tp_gesture_handle_state(struct tp_dispatch *tp, uint64_t time)
{
	unsigned int active_touches = 0;
	struct tp_touch *t;
	int i = 0;

	tp_for_each_touch(tp, t) {
		if (tp_touch_active(tp, t))
			active_touches++;

		i++;
	}

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
	tp->gesture.twofinger_state = GESTURE_2FG_STATE_NONE;

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
