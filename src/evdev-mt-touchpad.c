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

#include "config.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>

#include "evdev-mt-touchpad.h"

#define DEFAULT_TRACKPOINT_ACTIVITY_TIMEOUT 300 /* ms */
#define DEFAULT_KEYBOARD_ACTIVITY_TIMEOUT_1 200 /* ms */
#define DEFAULT_KEYBOARD_ACTIVITY_TIMEOUT_2 500 /* ms */
#define FAKE_FINGER_OVERFLOW (1 << 7)

static inline int
tp_hysteresis(int in, int center, int margin)
{
	int diff = in - center;
	if (abs(diff) <= margin)
		return center;

	if (diff > margin)
		return center + diff - margin;
	else
		return center + diff + margin;
}

static inline struct device_coords *
tp_motion_history_offset(struct tp_touch *t, int offset)
{
	int offset_index =
		(t->history.index - offset + TOUCHPAD_HISTORY_LENGTH) %
		TOUCHPAD_HISTORY_LENGTH;

	return &t->history.samples[offset_index];
}

struct normalized_coords
tp_filter_motion(struct tp_dispatch *tp,
		 const struct normalized_coords *unaccelerated,
		 uint64_t time)
{
	if (normalized_is_zero(*unaccelerated))
		return *unaccelerated;

	return filter_dispatch(tp->device->pointer.filter,
			       unaccelerated, tp, time);
}

static inline void
tp_motion_history_push(struct tp_touch *t)
{
	int motion_index = (t->history.index + 1) % TOUCHPAD_HISTORY_LENGTH;

	if (t->history.count < TOUCHPAD_HISTORY_LENGTH)
		t->history.count++;

	t->history.samples[motion_index] = t->point;
	t->history.index = motion_index;
}

static inline void
tp_motion_hysteresis(struct tp_dispatch *tp,
		     struct tp_touch *t)
{
	int x = t->point.x,
	    y = t->point.y;

	if (t->history.count == 0) {
		t->hysteresis_center = t->point;
	} else {
		x = tp_hysteresis(x,
				  t->hysteresis_center.x,
				  tp->hysteresis_margin.x);
		y = tp_hysteresis(y,
				  t->hysteresis_center.y,
				  tp->hysteresis_margin.y);
		t->hysteresis_center.x = x;
		t->hysteresis_center.y = y;
		t->point.x = x;
		t->point.y = y;
	}
}

static inline void
tp_motion_history_reset(struct tp_touch *t)
{
	t->history.count = 0;
}

static inline struct tp_touch *
tp_current_touch(struct tp_dispatch *tp)
{
	return &tp->touches[min(tp->slot, tp->ntouches - 1)];
}

static inline struct tp_touch *
tp_get_touch(struct tp_dispatch *tp, unsigned int slot)
{
	assert(slot < tp->ntouches);
	return &tp->touches[slot];
}

static inline unsigned int
tp_fake_finger_count(struct tp_dispatch *tp)
{
	if (tp->fake_touches & FAKE_FINGER_OVERFLOW)
		return FAKE_FINGER_OVERFLOW;
	else /* don't count BTN_TOUCH */
		return ffs(tp->fake_touches >> 1);
}

static inline bool
tp_fake_finger_is_touching(struct tp_dispatch *tp)
{
	return tp->fake_touches & 0x1;
}

static inline void
tp_fake_finger_set(struct tp_dispatch *tp,
		   unsigned int code,
		   bool is_press)
{
	unsigned int shift;

	switch (code) {
	case BTN_TOUCH:
		if (!is_press)
			tp->fake_touches &= ~FAKE_FINGER_OVERFLOW;
		shift = 0;
		break;
	case BTN_TOOL_FINGER:
		shift = 1;
		break;
	case BTN_TOOL_DOUBLETAP:
	case BTN_TOOL_TRIPLETAP:
	case BTN_TOOL_QUADTAP:
		shift = code - BTN_TOOL_DOUBLETAP + 2;
		break;
	/* when QUINTTAP is released we're either switching to 6 fingers
	   (flag stays in place until BTN_TOUCH is released) or
	   one of DOUBLE/TRIPLE/QUADTAP (will clear the flag on press) */
	case BTN_TOOL_QUINTTAP:
		if (is_press)
			tp->fake_touches |= FAKE_FINGER_OVERFLOW;
		return;
	default:
		return;
	}

	if (is_press) {
		tp->fake_touches &= ~FAKE_FINGER_OVERFLOW;
		tp->fake_touches |= 1 << shift;

	} else {
		tp->fake_touches &= ~(0x1 << shift);
	}
}

static inline void
tp_new_touch(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	if (t->state == TOUCH_BEGIN ||
	    t->state == TOUCH_UPDATE ||
	    t->state == TOUCH_HOVERING)
		return;

	/* we begin the touch as hovering because until BTN_TOUCH happens we
	 * don't know if it's a touch down or not. And BTN_TOUCH may happen
	 * after ABS_MT_TRACKING_ID */
	tp_motion_history_reset(t);
	t->dirty = true;
	t->has_ended = false;
	t->state = TOUCH_HOVERING;
	t->pinned.is_pinned = false;
	t->millis = time;
	tp->queued |= TOUCHPAD_EVENT_MOTION;
}

static inline void
tp_begin_touch(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	t->dirty = true;
	t->state = TOUCH_BEGIN;
	t->millis = time;
	tp->nfingers_down++;
	t->palm.time = time;
	t->is_thumb = false;
	t->tap.is_thumb = false;
	assert(tp->nfingers_down >= 1);
}

/**
 * End a touch, even if the touch sequence is still active.
 */
static inline void
tp_end_touch(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	switch (t->state) {
	case TOUCH_HOVERING:
		t->state = TOUCH_NONE;
		/* fallthough */
	case TOUCH_NONE:
	case TOUCH_END:
		return;
	case TOUCH_BEGIN:
	case TOUCH_UPDATE:
		break;

	}

	t->dirty = true;
	t->palm.state = PALM_NONE;
	t->state = TOUCH_END;
	t->pinned.is_pinned = false;
	t->millis = time;
	t->palm.time = 0;
	assert(tp->nfingers_down >= 1);
	tp->nfingers_down--;
	tp->queued |= TOUCHPAD_EVENT_MOTION;
}

/**
 * End the touch sequence on ABS_MT_TRACKING_ID -1 or when the BTN_TOOL_* 0 is received.
 */
static inline void
tp_end_sequence(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	t->has_ended = true;
	tp_end_touch(tp, t, time);
}

static double
tp_estimate_delta(int x0, int x1, int x2, int x3)
{
	return (x0 + x1 - x2 - x3) / 4.0;
}

struct normalized_coords
tp_get_delta(struct tp_touch *t)
{
	struct device_float_coords delta;
	const struct normalized_coords zero = { 0.0, 0.0 };

	if (t->history.count < TOUCHPAD_MIN_SAMPLES)
		return zero;

	delta.x = tp_estimate_delta(tp_motion_history_offset(t, 0)->x,
				    tp_motion_history_offset(t, 1)->x,
				    tp_motion_history_offset(t, 2)->x,
				    tp_motion_history_offset(t, 3)->x);
	delta.y = tp_estimate_delta(tp_motion_history_offset(t, 0)->y,
				    tp_motion_history_offset(t, 1)->y,
				    tp_motion_history_offset(t, 2)->y,
				    tp_motion_history_offset(t, 3)->y);

	return tp_normalize_delta(t->tp, delta);
}

static void
tp_process_absolute(struct tp_dispatch *tp,
		    const struct input_event *e,
		    uint64_t time)
{
	struct tp_touch *t = tp_current_touch(tp);

	switch(e->code) {
	case ABS_MT_POSITION_X:
		t->point.x = e->value;
		t->millis = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	case ABS_MT_POSITION_Y:
		t->point.y = e->value;
		t->millis = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	case ABS_MT_SLOT:
		tp->slot = e->value;
		break;
	case ABS_MT_DISTANCE:
		t->distance = e->value;
		break;
	case ABS_MT_TRACKING_ID:
		if (e->value != -1)
			tp_new_touch(tp, t, time);
		else
			tp_end_sequence(tp, t, time);
		break;
	case ABS_MT_PRESSURE:
		t->pressure = e->value;
		break;
	}
}

static void
tp_process_absolute_st(struct tp_dispatch *tp,
		       const struct input_event *e,
		       uint64_t time)
{
	struct tp_touch *t = tp_current_touch(tp);

	switch(e->code) {
	case ABS_X:
		t->point.x = e->value;
		t->millis = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	case ABS_Y:
		t->point.y = e->value;
		t->millis = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	}
}

static void
tp_process_fake_touches(struct tp_dispatch *tp,
			uint64_t time)
{
	struct tp_touch *t;
	unsigned int nfake_touches;
	unsigned int i, start;

	nfake_touches = tp_fake_finger_count(tp);
	if (nfake_touches == FAKE_FINGER_OVERFLOW)
		return;

	start = tp->has_mt ? tp->num_slots : 0;
	for (i = start; i < tp->ntouches; i++) {
		t = tp_get_touch(tp, i);
		if (i < nfake_touches)
			tp_new_touch(tp, t, time);
		else
			tp_end_sequence(tp, t, time);
	}
}

static void
tp_process_trackpoint_button(struct tp_dispatch *tp,
			     const struct input_event *e,
			     uint64_t time)
{
	struct evdev_dispatch *dispatch;
	struct input_event event;

	if (!tp->buttons.trackpoint ||
	    (tp->device->tags & EVDEV_TAG_TOUCHPAD_TRACKPOINT) == 0)
		return;

	dispatch = tp->buttons.trackpoint->dispatch;

	event = *e;

	switch (event.code) {
	case BTN_0:
		event.code = BTN_LEFT;
		break;
	case BTN_1:
		event.code = BTN_RIGHT;
		break;
	case BTN_2:
		event.code = BTN_MIDDLE;
		break;
	default:
		return;
	}

	dispatch->interface->process(dispatch,
				     tp->buttons.trackpoint,
				     &event, time);
}

static void
tp_process_key(struct tp_dispatch *tp,
	       const struct input_event *e,
	       uint64_t time)
{
	switch (e->code) {
		case BTN_LEFT:
		case BTN_MIDDLE:
		case BTN_RIGHT:
			tp_process_button(tp, e, time);
			break;
		case BTN_TOUCH:
		case BTN_TOOL_FINGER:
		case BTN_TOOL_DOUBLETAP:
		case BTN_TOOL_TRIPLETAP:
		case BTN_TOOL_QUADTAP:
		case BTN_TOOL_QUINTTAP:
			tp_fake_finger_set(tp, e->code, !!e->value);
			break;
		case BTN_0:
		case BTN_1:
		case BTN_2:
			tp_process_trackpoint_button(tp, e, time);
			break;
	}
}

static void
tp_unpin_finger(struct tp_dispatch *tp, struct tp_touch *t)
{
	double xdist, ydist;

	if (!t->pinned.is_pinned)
		return;

	xdist = abs(t->point.x - t->pinned.center.x);
	xdist *= tp->buttons.motion_dist.x_scale_coeff;
	ydist = abs(t->point.y - t->pinned.center.y);
	ydist *= tp->buttons.motion_dist.y_scale_coeff;

	/* 3mm movement -> unpin */
	if (hypot(xdist, ydist) >= 3.0) {
		t->pinned.is_pinned = false;
		return;
	}
}

static void
tp_pin_fingers(struct tp_dispatch *tp)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		t->pinned.is_pinned = true;
		t->pinned.center = t->point;
	}
}

int
tp_touch_active(struct tp_dispatch *tp, struct tp_touch *t)
{
	return (t->state == TOUCH_BEGIN || t->state == TOUCH_UPDATE) &&
		t->palm.state == PALM_NONE &&
		!t->pinned.is_pinned &&
		!t->is_thumb &&
		tp_button_touch_active(tp, t) &&
		tp_edge_scroll_touch_active(tp, t);
}

bool
tp_palm_tap_is_palm(struct tp_dispatch *tp, struct tp_touch *t)
{
	if (t->state != TOUCH_BEGIN)
		return false;

	if (t->point.x > tp->palm.left_edge &&
	    t->point.x < tp->palm.right_edge)
		return false;

	/* We're inside the left/right palm edge and in the northern half of
	 * the touchpad - this tap is a palm */
	if (t->point.y < tp->palm.vert_center) {
		log_debug(tp_libinput_context(tp),
			  "palm: palm-tap detected\n");
		return true;
	}

	return false;
}

static int
tp_palm_detect_dwt(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	if (tp->dwt.keyboard_active &&
	    t->state == TOUCH_BEGIN) {
		t->palm.state = PALM_TYPING;
		t->palm.first = t->point;
		return 1;
	} else if (!tp->dwt.keyboard_active &&
		   t->state == TOUCH_UPDATE &&
		   t->palm.state == PALM_TYPING) {
		/* If a touch has started before the first or after the last
		   key press, release it on timeout. Benefit: a palm rested
		   while typing on the touchpad will be ignored, but a touch
		   started once we stop typing will be able to control the
		   pointer (alas not tap, etc.).
		   */
		if (t->palm.time == 0 ||
		    t->palm.time > tp->dwt.keyboard_last_press_time) {
			t->palm.state = PALM_NONE;
			log_debug(tp_libinput_context(tp),
				  "palm: touch released, timeout after typing\n");
		}
	}

	return 0;
}

static int
tp_palm_detect_trackpoint(struct tp_dispatch *tp,
			  struct tp_touch *t,
			  uint64_t time)
{
	if (!tp->palm.monitor_trackpoint)
		return 0;

	if (t->palm.state == PALM_NONE &&
	    t->state == TOUCH_BEGIN &&
	    tp->palm.trackpoint_active) {
		t->palm.state = PALM_TRACKPOINT;
		return 1;
	} else if (t->palm.state == PALM_TRACKPOINT &&
		   t->state == TOUCH_UPDATE &&
		   !tp->palm.trackpoint_active) {

		if (t->palm.time == 0 ||
		    t->palm.time > tp->palm.trackpoint_last_event_time) {
			t->palm.state = PALM_NONE;
			log_debug(tp_libinput_context(tp),
				  "palm: touch released, timeout after trackpoint\n");
		}
	}

	return 0;
}

static void
tp_palm_detect(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	const int PALM_TIMEOUT = 200; /* ms */
	const int DIRECTIONS = NE|E|SE|SW|W|NW;
	struct device_float_coords delta;
	int dirs;

	if (tp_palm_detect_dwt(tp, t, time))
		goto out;

	if (tp_palm_detect_trackpoint(tp, t, time))
		goto out;

	/* If labelled a touch as palm, we unlabel as palm when
	   we move out of the palm edge zone within the timeout, provided
	   the direction is within 45 degrees of the horizontal.
	 */
	if (t->palm.state == PALM_EDGE) {
		if (time < t->palm.time + PALM_TIMEOUT &&
		    (t->point.x > tp->palm.left_edge && t->point.x < tp->palm.right_edge)) {
			delta = device_delta(t->point, t->palm.first);
			dirs = normalized_get_direction(
						tp_normalize_delta(tp, delta));
			if ((dirs & DIRECTIONS) && !(dirs & ~DIRECTIONS)) {
				t->palm.state = PALM_NONE;
				log_debug(tp_libinput_context(tp),
					  "palm: touch released, out of edge zone\n");
			}
		}
		return;
	}

	/* palm must start in exclusion zone, it's ok to move into
	   the zone without being a palm */
	if (t->state != TOUCH_BEGIN ||
	    (t->point.x > tp->palm.left_edge && t->point.x < tp->palm.right_edge))
		return;

	/* don't detect palm in software button areas, it's
	   likely that legitimate touches start in the area
	   covered by the exclusion zone */
	if (tp->buttons.is_clickpad &&
	    tp_button_is_inside_softbutton_area(tp, t))
		return;

	if (tp_touch_get_edge(tp, t) & EDGE_RIGHT)
		return;

	t->palm.state = PALM_EDGE;
	t->palm.time = time;
	t->palm.first = t->point;

out:
	log_debug(tp_libinput_context(tp),
		  "palm: palm detected (%s)\n",
		  t->palm.state == PALM_EDGE ? "edge" :
		  t->palm.state == PALM_TYPING ? "typing" : "trackpoint");
}

static void
tp_thumb_detect(struct tp_dispatch *tp, struct tp_touch *t)
{
	/* once a thumb, always a thumb */
	if (!tp->thumb.detect_thumbs || t->is_thumb)
		return;

	/* Note: a thumb at the edge of the touchpad won't trigger the
	 * threshold, the surface area is usually too small.
	 */
	if (t->pressure < tp->thumb.threshold)
		return;

	t->is_thumb = true;

	/* now what? we marked it as thumb, so:
	 *
	 * - pointer motion must ignore this touch
	 * - clickfinger must ignore this touch for finger count
	 * - software buttons are unaffected
	 * - edge scrolling unaffected
	 * - gestures: unaffected
	 * - tapping: honour thumb on begin, ignore it otherwise for now,
	 *   this gets a tad complicated otherwise
	 */
}

static void
tp_unhover_abs_distance(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;
	unsigned int i;

	for (i = 0; i < tp->ntouches; i++) {
		t = tp_get_touch(tp, i);

		if (t->state == TOUCH_HOVERING) {
			if (t->distance == 0) {
				/* avoid jumps when landing a finger */
				tp_motion_history_reset(t);
				tp_begin_touch(tp, t, time);
			}
		} else {
			if (t->distance > 0)
				tp_end_touch(tp, t, time);
		}
	}
}

static void
tp_unhover_fake_touches(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;
	unsigned int nfake_touches;
	int i;

	if (!tp->fake_touches && !tp->nfingers_down)
		return;

	nfake_touches = tp_fake_finger_count(tp);
	if (nfake_touches == FAKE_FINGER_OVERFLOW)
		return;

	if (tp->nfingers_down == nfake_touches &&
	    ((tp->nfingers_down == 0 && !tp_fake_finger_is_touching(tp)) ||
	     (tp->nfingers_down > 0 && tp_fake_finger_is_touching(tp))))
		return;

	/* if BTN_TOUCH is set and we have less fingers down than fake
	 * touches, switch each hovering touch to BEGIN
	 * until nfingers_down matches nfake_touches
	 */
	if (tp_fake_finger_is_touching(tp) &&
	    tp->nfingers_down < nfake_touches) {
		for (i = 0; i < (int)tp->ntouches; i++) {
			t = tp_get_touch(tp, i);

			if (t->state == TOUCH_HOVERING) {
				tp_begin_touch(tp, t, time);

				if (tp->nfingers_down >= nfake_touches)
					break;
			}
		}
	}

	/* if BTN_TOUCH is unset end all touches, we're hovering now. If we
	 * have too many touches also end some of them. This is done in
	 * reverse order.
	 */
	if (tp->nfingers_down > nfake_touches ||
	    !tp_fake_finger_is_touching(tp)) {
		for (i = tp->ntouches - 1; i >= 0; i--) {
			t = tp_get_touch(tp, i);

			if (t->state == TOUCH_HOVERING ||
			    t->state == TOUCH_NONE)
				continue;

			tp_end_touch(tp, t, time);

			if (tp_fake_finger_is_touching(tp) &&
			    tp->nfingers_down == nfake_touches)
				break;
		}
	}
}

static void
tp_unhover_touches(struct tp_dispatch *tp, uint64_t time)
{
	if (tp->reports_distance)
		tp_unhover_abs_distance(tp, time);
	else
		tp_unhover_fake_touches(tp, time);

}

static inline void
tp_position_fake_touches(struct tp_dispatch *tp)
{
	struct tp_touch *t;
	struct tp_touch *topmost = NULL;
	unsigned int start, i;

	if (tp_fake_finger_count(tp) <= tp->num_slots)
		return;

	/* We have at least one fake touch down. Find the top-most real
	 * touch and copy its coordinates over to to all fake touches.
	 * This is more reliable than just taking the first touch.
	 */
	for (i = 0; i < tp->num_slots; i++) {
		t = tp_get_touch(tp, i);
		if (t->state == TOUCH_END ||
		    t->state == TOUCH_NONE)
			continue;

		if (topmost == NULL || t->point.y < topmost->point.y)
			topmost = t;
	}

	if (!topmost) {
		log_bug_libinput(tp_libinput_context(tp),
				 "Unable to find topmost touch\n");
		return;
	}

	start = tp->has_mt ? tp->num_slots : 1;
	for (i = start; i < tp->ntouches; i++) {
		t = tp_get_touch(tp, i);
		if (t->state == TOUCH_NONE)
			continue;

		t->point = topmost->point;
		if (!t->dirty)
			t->dirty = topmost->dirty;
	}
}

static void
tp_process_state(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;
	unsigned int i;
	bool restart_filter = false;

	tp_process_fake_touches(tp, time);
	tp_unhover_touches(tp, time);
	tp_position_fake_touches(tp);

	for (i = 0; i < tp->ntouches; i++) {
		t = tp_get_touch(tp, i);

		/* semi-mt finger postions may "jump" when nfingers changes */
		if (tp->semi_mt && tp->nfingers_down != tp->old_nfingers_down)
			tp_motion_history_reset(t);

		if (!t->dirty)
			continue;

		tp_thumb_detect(tp, t);
		tp_palm_detect(tp, t, time);

		tp_motion_hysteresis(tp, t);
		tp_motion_history_push(t);

		tp_unpin_finger(tp, t);

		if (t->state == TOUCH_BEGIN)
			restart_filter = true;
	}

	if (restart_filter)
		filter_restart(tp->device->pointer.filter, tp, time);

	tp_button_handle_state(tp, time);
	tp_edge_scroll_handle_state(tp, time);

	/*
	 * We have a physical button down event on a clickpad. To avoid
	 * spurious pointer moves by the clicking finger we pin all fingers.
	 * We unpin fingers when they move more then a certain threshold to
	 * to allow drag and drop.
	 */
	if ((tp->queued & TOUCHPAD_EVENT_BUTTON_PRESS) &&
	    tp->buttons.is_clickpad)
		tp_pin_fingers(tp);

	tp_gesture_handle_state(tp, time);
}

static void
tp_post_process_state(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {

		if (!t->dirty)
			continue;

		if (t->state == TOUCH_END) {
			if (t->has_ended)
				t->state = TOUCH_NONE;
			else
				t->state = TOUCH_HOVERING;
		} else if (t->state == TOUCH_BEGIN) {
			t->state = TOUCH_UPDATE;
		}

		t->dirty = false;
	}

	tp->old_nfingers_down = tp->nfingers_down;
	tp->buttons.old_state = tp->buttons.state;

	tp->queued = TOUCHPAD_EVENT_NONE;
}

static void
tp_post_events(struct tp_dispatch *tp, uint64_t time)
{
	int filter_motion = 0;

	/* Only post (top) button events while suspended */
	if (tp->device->suspended) {
		tp_post_button_events(tp, time);
		return;
	}

	filter_motion |= tp_tap_handle_state(tp, time);
	filter_motion |= tp_post_button_events(tp, time);

	if (filter_motion ||
	    tp->palm.trackpoint_active ||
	    tp->dwt.keyboard_active) {
		tp_edge_scroll_stop_events(tp, time);
		tp_gesture_cancel(tp, time);
		return;
	}

	if (tp_edge_scroll_post_events(tp, time) != 0)
		return;

	tp_gesture_post_events(tp, time);
}

static void
tp_handle_state(struct tp_dispatch *tp,
		uint64_t time)
{
	tp_process_state(tp, time);
	tp_post_events(tp, time);
	tp_post_process_state(tp, time);
}

static void
tp_interface_process(struct evdev_dispatch *dispatch,
		     struct evdev_device *device,
		     struct input_event *e,
		     uint64_t time)
{
	struct tp_dispatch *tp =
		(struct tp_dispatch *)dispatch;

	switch (e->type) {
	case EV_ABS:
		if (tp->has_mt)
			tp_process_absolute(tp, e, time);
		else
			tp_process_absolute_st(tp, e, time);
		break;
	case EV_KEY:
		tp_process_key(tp, e, time);
		break;
	case EV_SYN:
		tp_handle_state(tp, time);
		break;
	}
}

static void
tp_remove_sendevents(struct tp_dispatch *tp)
{
	libinput_timer_cancel(&tp->palm.trackpoint_timer);
	libinput_timer_cancel(&tp->dwt.keyboard_timer);

	if (tp->buttons.trackpoint &&
	    tp->palm.monitor_trackpoint)
		libinput_device_remove_event_listener(
					&tp->palm.trackpoint_listener);

	if (tp->dwt.keyboard)
		libinput_device_remove_event_listener(
					&tp->dwt.keyboard_listener);
}

static void
tp_interface_remove(struct evdev_dispatch *dispatch)
{
	struct tp_dispatch *tp =
		(struct tp_dispatch*)dispatch;

	tp_remove_tap(tp);
	tp_remove_buttons(tp);
	tp_remove_sendevents(tp);
	tp_remove_edge_scroll(tp);
	tp_remove_gesture(tp);
}

static void
tp_interface_destroy(struct evdev_dispatch *dispatch)
{
	struct tp_dispatch *tp =
		(struct tp_dispatch*)dispatch;

	free(tp->touches);
	free(tp);
}

static void
tp_release_fake_touches(struct tp_dispatch *tp)
{
	tp->fake_touches = 0;
}

static void
tp_clear_state(struct tp_dispatch *tp)
{
	uint64_t now = libinput_now(tp_libinput_context(tp));
	struct tp_touch *t;

	/* Unroll the touchpad state.
	 * Release buttons first. If tp is a clickpad, the button event
	 * must come before the touch up. If it isn't, the order doesn't
	 * matter anyway
	 *
	 * Then cancel all timeouts on the taps, triggering the last set
	 * of events.
	 *
	 * Then lift all touches so the touchpad is in a neutral state.
	 *
	 */
	tp_release_all_buttons(tp, now);
	tp_release_all_taps(tp, now);

	tp_for_each_touch(tp, t) {
		tp_end_sequence(tp, t, now);
	}
	tp_release_fake_touches(tp);

	tp_handle_state(tp, now);
}

static void
tp_suspend(struct tp_dispatch *tp, struct evdev_device *device)
{
	tp_clear_state(tp);

	/* On devices with top softwarebuttons we don't actually suspend the
	 * device, to keep the "trackpoint" buttons working. tp_post_events()
	 * will only send events for the trackpoint while suspended.
	 */
	if (tp->buttons.has_topbuttons) {
		evdev_notify_suspended_device(device);
		/* Enlarge topbutton area while suspended */
		tp_init_top_softbuttons(tp, device, 1.5);
	} else {
		evdev_device_suspend(device);
	}
}

static void
tp_interface_suspend(struct evdev_dispatch *dispatch,
		     struct evdev_device *device)
{
	struct tp_dispatch *tp = (struct tp_dispatch *)dispatch;

	tp_clear_state(tp);
}

static void
tp_resume(struct tp_dispatch *tp, struct evdev_device *device)
{
	if (tp->buttons.has_topbuttons) {
		/* tap state-machine is offline while suspended, reset state */
		tp_clear_state(tp);
		/* restore original topbutton area size */
		tp_init_top_softbuttons(tp, device, 1.0);
		evdev_notify_resumed_device(device);
	} else {
		evdev_device_resume(device);
	}
}

static void
tp_trackpoint_timeout(uint64_t now, void *data)
{
	struct tp_dispatch *tp = data;

	tp_tap_resume(tp, now);
	tp->palm.trackpoint_active = false;
}

static void
tp_trackpoint_event(uint64_t time, struct libinput_event *event, void *data)
{
	struct tp_dispatch *tp = data;

	/* Buttons do not count as trackpad activity, as people may use
	   the trackpoint buttons in combination with the touchpad. */
	if (event->type == LIBINPUT_EVENT_POINTER_BUTTON)
		return;

	if (!tp->palm.trackpoint_active) {
		tp_edge_scroll_stop_events(tp, time);
		tp_gesture_cancel(tp, time);
		tp_tap_suspend(tp, time);
		tp->palm.trackpoint_active = true;
	}

	tp->palm.trackpoint_last_event_time = time;
	libinput_timer_set(&tp->palm.trackpoint_timer,
			   time + DEFAULT_TRACKPOINT_ACTIVITY_TIMEOUT);
}

static void
tp_keyboard_timeout(uint64_t now, void *data)
{
	struct tp_dispatch *tp = data;

	tp_tap_resume(tp, now);

	tp->dwt.keyboard_active = false;

	log_debug(tp_libinput_context(tp), "palm: keyboard timeout\n");
}

static inline bool
tp_key_ignore_for_dwt(unsigned int keycode)
{
	switch (keycode) {
	/* Ignore modifiers to be responsive to ctrl-click, alt-tab, etc. */
	case KEY_LEFTCTRL:
	case KEY_RIGHTCTRL:
	case KEY_LEFTALT:
	case KEY_RIGHTALT:
	case KEY_LEFTSHIFT:
	case KEY_RIGHTSHIFT:
	case KEY_FN:
	case KEY_CAPSLOCK:
	case KEY_TAB:
	case KEY_COMPOSE:
	case KEY_RIGHTMETA:
	case KEY_LEFTMETA:
		return true;
	default:
		break;
	}

	/* Ignore keys not part of the "typewriter set", i.e. F-keys,
	 * multimedia keys, numpad, etc.
	 */
	if (keycode >= KEY_F1)
		return true;

	return false;
}

static void
tp_keyboard_event(uint64_t time, struct libinput_event *event, void *data)
{
	struct tp_dispatch *tp = data;
	struct libinput_event_keyboard *kbdev;
	unsigned int timeout;

	if (event->type != LIBINPUT_EVENT_KEYBOARD_KEY)
		return;

	kbdev = libinput_event_get_keyboard_event(event);

	/* Only trigger the timer on key down. */
	if (libinput_event_keyboard_get_key_state(kbdev) !=
	    LIBINPUT_KEY_STATE_PRESSED)
		return;

	/* modifier keys don't trigger disable-while-typing so things like
	 * ctrl+zoom or ctrl+click are possible */
	if (tp_key_ignore_for_dwt(libinput_event_keyboard_get_key(kbdev)))
		return;

	if (!tp->dwt.keyboard_active) {
		tp_edge_scroll_stop_events(tp, time);
		tp_gesture_cancel(tp, time);
		tp_tap_suspend(tp, time);
		tp->dwt.keyboard_active = true;
		timeout = DEFAULT_KEYBOARD_ACTIVITY_TIMEOUT_1;
	} else {
		timeout = DEFAULT_KEYBOARD_ACTIVITY_TIMEOUT_2;
	}

	tp->dwt.keyboard_last_press_time = time;
	libinput_timer_set(&tp->dwt.keyboard_timer,
			   time + timeout);
}

static bool
tp_want_dwt(struct evdev_device *touchpad,
	    struct evdev_device *keyboard)
{
	unsigned int bus_tp = libevdev_get_id_bustype(touchpad->evdev),
		     bus_kbd = libevdev_get_id_bustype(keyboard->evdev);

	if (bus_tp == BUS_BLUETOOTH || bus_kbd == BUS_BLUETOOTH)
		return false;

	/* evemu will set the right bus type */
	if (bus_tp == BUS_VIRTUAL || bus_kbd == BUS_VIRTUAL)
		return false;

	/* If the touchpad is on serio, the keyboard is too, so ignore any
	   other devices */
	if (bus_tp == BUS_I8042 && bus_kbd != bus_tp)
		return false;

	/* Wacom makes touchpads, but not internal ones */
	if (libevdev_get_id_vendor(touchpad->evdev) == VENDOR_ID_WACOM)
		return false;

	/* everything else we don't really know, so we have to assume
	   they go together */

	return true;
}

static void
tp_interface_device_added(struct evdev_device *device,
			  struct evdev_device *added_device)
{
	struct tp_dispatch *tp = (struct tp_dispatch*)device->dispatch;
	unsigned int bus_tp = libevdev_get_id_bustype(device->evdev),
		     bus_trp = libevdev_get_id_bustype(added_device->evdev);
	bool tp_is_internal, trp_is_internal;

	tp_is_internal = bus_tp != BUS_USB && bus_tp != BUS_BLUETOOTH;
	trp_is_internal = bus_trp != BUS_USB && bus_trp != BUS_BLUETOOTH;

	if (tp->buttons.trackpoint == NULL &&
	    (added_device->tags & EVDEV_TAG_TRACKPOINT) &&
	    tp_is_internal && trp_is_internal) {
		/* Don't send any pending releases to the new trackpoint */
		tp->buttons.active_is_topbutton = false;
		tp->buttons.trackpoint = added_device;
		if (tp->palm.monitor_trackpoint)
			libinput_device_add_event_listener(&added_device->base,
						&tp->palm.trackpoint_listener,
						tp_trackpoint_event, tp);
	}

	if (added_device->tags & EVDEV_TAG_KEYBOARD &&
	    tp->dwt.keyboard == NULL &&
	    tp_want_dwt(device, added_device)) {
		log_debug(tp_libinput_context(tp),
			  "palm: dwt activated with %s<->%s\n",
			  device->devname,
			  added_device->devname);

		libinput_device_add_event_listener(&added_device->base,
					&tp->dwt.keyboard_listener,
					tp_keyboard_event, tp);
		tp->dwt.keyboard = added_device;
		tp->dwt.keyboard_active = false;
	}

	if (tp->sendevents.current_mode !=
	    LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE)
		return;

	if (added_device->tags & EVDEV_TAG_EXTERNAL_MOUSE)
		tp_suspend(tp, device);
}

static void
tp_interface_device_removed(struct evdev_device *device,
			    struct evdev_device *removed_device)
{
	struct tp_dispatch *tp = (struct tp_dispatch*)device->dispatch;
	struct libinput_device *dev;

	if (removed_device == tp->buttons.trackpoint) {
		/* Clear any pending releases for the trackpoint */
		if (tp->buttons.active && tp->buttons.active_is_topbutton) {
			tp->buttons.active = 0;
			tp->buttons.active_is_topbutton = false;
		}
		if (tp->palm.monitor_trackpoint)
			libinput_device_remove_event_listener(
						&tp->palm.trackpoint_listener);
		tp->buttons.trackpoint = NULL;
	}

	if (removed_device == tp->dwt.keyboard) {
		libinput_device_remove_event_listener(
					&tp->dwt.keyboard_listener);
		tp->dwt.keyboard = NULL;
	}

	if (tp->sendevents.current_mode !=
	    LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE)
		return;

	list_for_each(dev, &device->base.seat->devices_list, link) {
		struct evdev_device *d = (struct evdev_device*)dev;
		if (d != removed_device &&
		    (d->tags & EVDEV_TAG_EXTERNAL_MOUSE)) {
			return;
		}
	}

	tp_resume(tp, device);
}

void
evdev_tag_touchpad(struct evdev_device *device,
		   struct udev_device *udev_device)
{
	int bustype;

	/* simple approach: touchpads on USB or Bluetooth are considered
	 * external, anything else is internal. Exception is Apple -
	 * internal touchpads are connected over USB and it doesn't have
	 * external USB touchpads anyway.
	 */
	bustype = libevdev_get_id_bustype(device->evdev);
	if (bustype == BUS_USB) {
		if (device->model == EVDEV_MODEL_APPLE_TOUCHPAD)
			 device->tags |= EVDEV_TAG_INTERNAL_TOUCHPAD;
	} else if (bustype != BUS_BLUETOOTH)
		device->tags |= EVDEV_TAG_INTERNAL_TOUCHPAD;

	if (udev_device_get_property_value(udev_device,
					   "TOUCHPAD_HAS_TRACKPOINT_BUTTONS"))
		device->tags |= EVDEV_TAG_TOUCHPAD_TRACKPOINT;
}

static struct evdev_dispatch_interface tp_interface = {
	tp_interface_process,
	tp_interface_suspend,
	tp_interface_remove,
	tp_interface_destroy,
	tp_interface_device_added,
	tp_interface_device_removed,
	tp_interface_device_removed, /* device_suspended, treat as remove */
	tp_interface_device_added,   /* device_resumed, treat as add */
};

static void
tp_init_touch(struct tp_dispatch *tp,
	      struct tp_touch *t)
{
	t->tp = tp;
	t->has_ended = true;
}

static void
tp_sync_touch(struct tp_dispatch *tp,
	      struct evdev_device *device,
	      struct tp_touch *t,
	      int slot)
{
	struct libevdev *evdev = device->evdev;

	if (!libevdev_fetch_slot_value(evdev,
				       slot,
				       ABS_MT_POSITION_X,
				       &t->point.x))
		t->point.x = libevdev_get_event_value(evdev, EV_ABS, ABS_X);
	if (!libevdev_fetch_slot_value(evdev,
				       slot,
				       ABS_MT_POSITION_Y,
				       &t->point.y))
		t->point.y = libevdev_get_event_value(evdev, EV_ABS, ABS_Y);

	libevdev_fetch_slot_value(evdev, slot, ABS_MT_DISTANCE, &t->distance);
}

static int
tp_init_slots(struct tp_dispatch *tp,
	      struct evdev_device *device)
{
	const struct input_absinfo *absinfo;
	struct map {
		unsigned int code;
		int ntouches;
	} max_touches[] = {
		{ BTN_TOOL_QUINTTAP, 5 },
		{ BTN_TOOL_QUADTAP, 4 },
		{ BTN_TOOL_TRIPLETAP, 3 },
		{ BTN_TOOL_DOUBLETAP, 2 },
	};
	struct map *m;
	unsigned int i, n_btn_tool_touches = 1;

	absinfo = libevdev_get_abs_info(device->evdev, ABS_MT_SLOT);
	if (absinfo) {
		tp->num_slots = absinfo->maximum + 1;
		tp->slot = absinfo->value;
		tp->has_mt = true;
	} else {
		tp->num_slots = 1;
		tp->slot = 0;
		tp->has_mt = false;
	}

	tp->semi_mt = libevdev_has_property(device->evdev, INPUT_PROP_SEMI_MT);

	ARRAY_FOR_EACH(max_touches, m) {
		if (libevdev_has_event_code(device->evdev,
					    EV_KEY,
					    m->code)) {
			n_btn_tool_touches = m->ntouches;
			break;
		}
	}

	tp->ntouches = max(tp->num_slots, n_btn_tool_touches);
	tp->touches = calloc(tp->ntouches, sizeof(struct tp_touch));
	if (!tp->touches)
		return -1;

	for (i = 0; i < tp->ntouches; i++)
		tp_init_touch(tp, &tp->touches[i]);

	/* Always sync the first touch so we get ABS_X/Y synced on
	 * single-touch touchpads */
	tp_sync_touch(tp, device, &tp->touches[0], 0);
	for (i = 1; i < tp->num_slots; i++)
		tp_sync_touch(tp, device, &tp->touches[i], i);

	return 0;
}

static int
tp_init_accel(struct tp_dispatch *tp, double diagonal)
{
	int res_x, res_y;
	accel_profile_func_t profile;

	res_x = tp->device->abs.absinfo_x->resolution;
	res_y = tp->device->abs.absinfo_y->resolution;

	/*
	 * Not all touchpads report the same amount of units/mm (resolution).
	 * Normalize motion events to the default mouse DPI as base
	 * (unaccelerated) speed. This also evens out any differences in x
	 * and y resolution, so that a circle on the
	 * touchpad does not turn into an elipse on the screen.
	 */
	tp->accel.x_scale_coeff = (DEFAULT_MOUSE_DPI/25.4) / res_x;
	tp->accel.y_scale_coeff = (DEFAULT_MOUSE_DPI/25.4) / res_y;

	switch (tp->device->model) {
	case EVDEV_MODEL_LENOVO_X230:
		profile = touchpad_lenovo_x230_accel_profile;
		break;
	default:
		profile = touchpad_accel_profile_linear;
		break;
	}

	if (evdev_device_init_pointer_acceleration(tp->device, profile) == -1)
		return -1;

	return 0;
}

static uint32_t
tp_scroll_config_scroll_method_get_methods(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;
	uint32_t methods = LIBINPUT_CONFIG_SCROLL_EDGE;

	if (tp->ntouches >= 2)
		methods |= LIBINPUT_CONFIG_SCROLL_2FG;

	return methods;
}

static enum libinput_config_status
tp_scroll_config_scroll_method_set_method(struct libinput_device *device,
		        enum libinput_config_scroll_method method)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;
	uint64_t time = libinput_now(tp_libinput_context(tp));

	if (method == tp->scroll.method)
		return LIBINPUT_CONFIG_STATUS_SUCCESS;

	tp_edge_scroll_stop_events(tp, time);
	tp_gesture_stop_twofinger_scroll(tp, time);

	tp->scroll.method = method;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_scroll_method
tp_scroll_config_scroll_method_get_method(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	return tp->scroll.method;
}

static enum libinput_config_scroll_method
tp_scroll_get_default_method(struct tp_dispatch *tp)
{
	if (tp->ntouches >= 2)
		return LIBINPUT_CONFIG_SCROLL_2FG;
	else
		return LIBINPUT_CONFIG_SCROLL_EDGE;
}

static enum libinput_config_scroll_method
tp_scroll_config_scroll_method_get_default_method(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	return tp_scroll_get_default_method(tp);
}

static int
tp_init_scroll(struct tp_dispatch *tp, struct evdev_device *device)
{
	if (tp_edge_scroll_init(tp, device) != 0)
		return -1;

	evdev_init_natural_scroll(device);

	tp->scroll.config_method.get_methods = tp_scroll_config_scroll_method_get_methods;
	tp->scroll.config_method.set_method = tp_scroll_config_scroll_method_set_method;
	tp->scroll.config_method.get_method = tp_scroll_config_scroll_method_get_method;
	tp->scroll.config_method.get_default_method = tp_scroll_config_scroll_method_get_default_method;
	tp->scroll.method = tp_scroll_get_default_method(tp);
	tp->device->base.config.scroll_method = &tp->scroll.config_method;

	/* In mm for touchpads with valid resolution, see tp_init_accel() */
	tp->device->scroll.threshold = 5.0;

	return 0;
}

static int
tp_init_palmdetect(struct tp_dispatch *tp,
		   struct evdev_device *device)
{
	int width, height;

	tp->palm.right_edge = INT_MAX;
	tp->palm.left_edge = INT_MIN;
	tp->palm.vert_center = INT_MIN;

	width = device->abs.dimensions.x;
	height = device->abs.dimensions.y;

	/* Wacom doesn't have internal touchpads,
	 * Apple touchpads are always big enough to warrant palm detection */
	if (device->model == EVDEV_MODEL_WACOM_TOUCHPAD)
		return 0;

	/* Enable palm detection on touchpads >= 70 mm. Anything smaller
	   probably won't need it, until we find out it does */
	if (width/device->abs.absinfo_x->resolution < 70)
		return 0;

	/* palm edges are 5% of the width on each side */
	tp->palm.right_edge = device->abs.absinfo_x->maximum - width * 0.05;
	tp->palm.left_edge = device->abs.absinfo_x->minimum + width * 0.05;
	tp->palm.vert_center = device->abs.absinfo_y->minimum + height/2;

	tp->palm.monitor_trackpoint = true;

	return 0;
}

static int
tp_init_sendevents(struct tp_dispatch *tp,
		   struct evdev_device *device)
{
	libinput_timer_init(&tp->palm.trackpoint_timer,
			    tp_libinput_context(tp),
			    tp_trackpoint_timeout, tp);

	libinput_timer_init(&tp->dwt.keyboard_timer,
			    tp_libinput_context(tp),
			    tp_keyboard_timeout, tp);
	return 0;
}

static int
tp_init_thumb(struct tp_dispatch *tp)
{
	struct evdev_device *device = tp->device;
	const struct input_absinfo *abs;
	double w = 0.0, h = 0.0;
	int xres, yres;
	double threshold;

	if (!tp->buttons.is_clickpad)
		return 0;

	abs = libevdev_get_abs_info(device->evdev, ABS_MT_PRESSURE);
	if (!abs)
		return 0;

	if (abs->maximum - abs->minimum < 255)
		return 0;

	/* if the touchpad is less than 50mm high, skip thumb detection.
	 * it's too small to meaningfully interact with a thumb on the
	 * touchpad */
	evdev_device_get_size(device, &w, &h);
	if (h < 50)
		return 0;

	/* Our reference touchpad is the T440s with 42x42 resolution.
	 * Higher-res touchpads exhibit higher pressure for the same
	 * interaction. On the T440s, the threshold value is 100, you don't
	 * reach that with a normal finger interaction.
	 * Note: "thumb" means massive touch that should not interact, not
	 * "using the tip of my thumb for a pinch gestures".
	 */
	xres = tp->device->abs.absinfo_x->resolution;
	yres = tp->device->abs.absinfo_y->resolution;
	threshold = 100.0 * hypot(xres, yres)/hypot(42, 42);
	tp->thumb.threshold = max(100, threshold);
	tp->thumb.detect_thumbs = true;

	return 0;
}

static int
tp_sanity_check(struct tp_dispatch *tp,
		struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;
	struct libinput *libinput = tp_libinput_context(tp);

	if (!libevdev_has_event_code(evdev, EV_ABS, ABS_X))
		goto error;

	if (!libevdev_has_event_code(evdev, EV_KEY, BTN_TOUCH))
		goto error;

	if (!libevdev_has_event_code(evdev, EV_KEY, BTN_TOOL_FINGER))
		goto error;

	return 0;

error:
	log_bug_kernel(libinput,
		       "device %s failed touchpad sanity checks\n",
		       device->devname);
	return -1;
}

static int
tp_init_default_resolution(struct tp_dispatch *tp,
			   struct evdev_device *device)
{
	const int touchpad_width_mm = 69, /* 1 under palm detection */
		  touchpad_height_mm = 50;
	int xres, yres;

	if (!device->abs.fake_resolution)
		return 0 ;

	/* we only get here if
	 * - the touchpad provides no resolution
	 * - the udev hwdb didn't override the resoluion
	 * - no ATTR_SIZE_HINT is set
	 *
	 * The majority of touchpads that triggers all these conditions
	 * are old ones, so let's assume a small touchpad size and assume
	 * that.
	 */
	log_info(tp_libinput_context(tp),
		 "%s: no resolution or size hints, assuming a size of %dx%dmm\n",
		 device->devname,
		 touchpad_width_mm,
		 touchpad_height_mm);

	xres = device->abs.dimensions.x/touchpad_width_mm;
	yres = device->abs.dimensions.y/touchpad_height_mm;
	libevdev_set_abs_resolution(device->evdev, ABS_X, xres);
	libevdev_set_abs_resolution(device->evdev, ABS_Y, yres);
	libevdev_set_abs_resolution(device->evdev, ABS_MT_POSITION_X, xres);
	libevdev_set_abs_resolution(device->evdev, ABS_MT_POSITION_Y, yres);
	device->abs.fake_resolution = 0;

	return 0;
}

static int
tp_init(struct tp_dispatch *tp,
	struct evdev_device *device)
{
	int width, height;
	double diagonal;
	int res_x, res_y;

	tp->base.interface = &tp_interface;
	tp->device = device;

	if (tp_sanity_check(tp, device) != 0)
		return -1;

	if (tp_init_default_resolution(tp, device) != 0)
		return -1;

	if (tp_init_slots(tp, device) != 0)
		return -1;

	res_x = tp->device->abs.absinfo_x->resolution;
	res_y = tp->device->abs.absinfo_y->resolution;
	width = device->abs.dimensions.x;
	height = device->abs.dimensions.y;
	diagonal = sqrt(width*width + height*height);

	tp->reports_distance = libevdev_has_event_code(device->evdev,
						       EV_ABS,
						       ABS_MT_DISTANCE);

	tp->hysteresis_margin.x = res_x/2;
	tp->hysteresis_margin.y = res_y/2;

	if (tp_init_accel(tp, diagonal) != 0)
		return -1;

	if (tp_init_tap(tp) != 0)
		return -1;

	if (tp_init_buttons(tp, device) != 0)
		return -1;

	if (tp_init_palmdetect(tp, device) != 0)
		return -1;

	if (tp_init_sendevents(tp, device) != 0)
		return -1;

	if (tp_init_scroll(tp, device) != 0)
		return -1;

	if (tp_init_gesture(tp) != 0)
		return -1;

	if (tp_init_thumb(tp) != 0)
		return -1;

	device->seat_caps |= EVDEV_DEVICE_POINTER;

	return 0;
}

static uint32_t
tp_sendevents_get_modes(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	uint32_t modes = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;

	if (evdev->tags & EVDEV_TAG_INTERNAL_TOUCHPAD)
		modes |= LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;

	return modes;
}

static void
tp_suspend_conditional(struct tp_dispatch *tp,
		       struct evdev_device *device)
{
	struct libinput_device *dev;

	list_for_each(dev, &device->base.seat->devices_list, link) {
		struct evdev_device *d = (struct evdev_device*)dev;
		if (d->tags & EVDEV_TAG_EXTERNAL_MOUSE) {
			tp_suspend(tp, device);
			return;
		}
	}
}

static enum libinput_config_status
tp_sendevents_set_mode(struct libinput_device *device,
		       enum libinput_config_send_events_mode mode)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	/* DISABLED overrides any DISABLED_ON_ */
	if ((mode & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED) &&
	    (mode & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE))
	    mode &= ~LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;

	if (mode == tp->sendevents.current_mode)
		return LIBINPUT_CONFIG_STATUS_SUCCESS;

	switch(mode) {
	case LIBINPUT_CONFIG_SEND_EVENTS_ENABLED:
		tp_resume(tp, evdev);
		break;
	case LIBINPUT_CONFIG_SEND_EVENTS_DISABLED:
		tp_suspend(tp, evdev);
		break;
	case LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE:
		tp_suspend_conditional(tp, evdev);
		break;
	default:
		return LIBINPUT_CONFIG_STATUS_UNSUPPORTED;
	}

	tp->sendevents.current_mode = mode;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_send_events_mode
tp_sendevents_get_mode(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tp_dispatch *dispatch = (struct tp_dispatch*)evdev->dispatch;

	return dispatch->sendevents.current_mode;
}

static enum libinput_config_send_events_mode
tp_sendevents_get_default_mode(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
}

static void
tp_change_to_left_handed(struct evdev_device *device)
{
	struct tp_dispatch *tp = (struct tp_dispatch *)device->dispatch;

	if (device->left_handed.want_enabled == device->left_handed.enabled)
		return;

	if (tp->buttons.state & 0x3) /* BTN_LEFT|BTN_RIGHT */
		return;

	/* tapping and clickfinger aren't affected by left-handed config,
	 * so checking physical buttons is enough */

	device->left_handed.enabled = device->left_handed.want_enabled;
}

struct model_lookup_t {
	uint16_t vendor;
	uint16_t product_start;
	uint16_t product_end;
	enum touchpad_model model;
};

static struct model_lookup_t model_lookup_table[] = {
	{ 0x0002, 0x0007, 0x0007, MODEL_SYNAPTICS },
	{ 0x0002, 0x0008, 0x0008, MODEL_ALPS },
	{ 0x0002, 0x000e, 0x000e, MODEL_ELANTECH },
	{ 0x05ac,      0, 0x0222, MODEL_APPLETOUCH },
	{ 0x05ac, 0x0223, 0x0228, MODEL_UNIBODY_MACBOOK },
	{ 0x05ac, 0x0229, 0x022b, MODEL_APPLETOUCH },
	{ 0x05ac, 0x022c, 0xffff, MODEL_UNIBODY_MACBOOK },
	{ 0, 0, 0, 0 }
};

static enum touchpad_model
tp_get_model(struct evdev_device *device)
{
	struct model_lookup_t *lookup;
	uint16_t vendor  = libevdev_get_id_vendor(device->evdev);
	uint16_t product = libevdev_get_id_product(device->evdev);

	for (lookup = model_lookup_table; lookup->vendor; lookup++) {
		if (lookup->vendor == vendor &&
		    lookup->product_start <= product &&
		    product <= lookup->product_end)
			return lookup->model;
	}
	return MODEL_UNKNOWN;
}

struct evdev_dispatch *
evdev_mt_touchpad_create(struct evdev_device *device)
{
	struct tp_dispatch *tp;

	tp = zalloc(sizeof *tp);
	if (!tp)
		return NULL;

	tp->model = tp_get_model(device);

	if (tp_init(tp, device) != 0) {
		tp_interface_destroy(&tp->base);
		return NULL;
	}

	device->base.config.sendevents = &tp->sendevents.config;

	tp->sendevents.current_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	tp->sendevents.config.get_modes = tp_sendevents_get_modes;
	tp->sendevents.config.set_mode = tp_sendevents_set_mode;
	tp->sendevents.config.get_mode = tp_sendevents_get_mode;
	tp->sendevents.config.get_default_mode = tp_sendevents_get_default_mode;

	evdev_init_left_handed(device, tp_change_to_left_handed);

	return  &tp->base;
}
