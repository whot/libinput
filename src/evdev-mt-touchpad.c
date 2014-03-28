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

#include "config.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>

#include "evdev-mt-touchpad.h"

#define DEFAULT_ACCEL_NUMERATOR 1200.0
#define DEFAULT_HYSTERESIS_MARGIN_DENOMINATOR 700.0

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

static inline struct tp_motion *
tp_motion_history_offset(struct tp_touch *t, int offset)
{
	int offset_index =
		(t->history.index - offset + TOUCHPAD_HISTORY_LENGTH) %
		TOUCHPAD_HISTORY_LENGTH;

	return &t->history.samples[offset_index];
}

static void
tp_filter_motion(struct tp_dispatch *tp,
	         double *dx, double *dy, uint64_t time)
{
	struct motion_params motion;

	motion.dx = *dx * tp->accel.x_scale_coeff;
	motion.dy = *dy * tp->accel.y_scale_coeff;

	if (motion.dx != 0.0 || motion.dy != 0.0)
		filter_dispatch(tp->filter, &motion, tp, time);

	*dx = motion.dx;
	*dy = motion.dy;
}

static inline void
tp_motion_history_push(struct tp_touch *t)
{
	int motion_index = (t->history.index + 1) % TOUCHPAD_HISTORY_LENGTH;

	if (t->history.count < TOUCHPAD_HISTORY_LENGTH)
		t->history.count++;

	t->history.samples[motion_index].x = t->x;
	t->history.samples[motion_index].y = t->y;
	t->history.index = motion_index;
}

static inline void
tp_motion_hysteresis(struct tp_dispatch *tp,
		     struct tp_touch *t)
{
	int x = t->x,
	    y = t->y;

	if (t->history.count == 0) {
		t->hysteresis.center_x = t->x;
		t->hysteresis.center_y = t->y;
	} else {
		x = tp_hysteresis(x,
				  t->hysteresis.center_x,
				  tp->hysteresis.margin_x);
		y = tp_hysteresis(y,
				  t->hysteresis.center_y,
				  tp->hysteresis.margin_y);
		t->hysteresis.center_x = x;
		t->hysteresis.center_y = y;
		t->x = x;
		t->y = y;
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

static inline void
tp_begin_touch(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	if (t->state == TOUCH_BEGIN || t->state == TOUCH_UPDATE)
		return;

	tp_motion_history_reset(t);
	t->dirty = true;
	t->state = TOUCH_BEGIN;
	t->pinned.is_pinned = false;
	t->millis = time;
	tp->nfingers_down++;
	assert(tp->nfingers_down >= 1);
	tp->queued |= TOUCHPAD_EVENT_MOTION;
}

static inline void
tp_end_touch(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	if (t->state == TOUCH_END || t->state == TOUCH_NONE)
		return;

	t->dirty = true;
	t->is_pointer = false;
	t->palm.is_palm = false;
	t->state = TOUCH_END;
	t->pinned.is_pinned = false;
	t->millis = time;
	assert(tp->nfingers_down >= 1);
	tp->nfingers_down--;
	tp->queued |= TOUCHPAD_EVENT_MOTION;
}

static inline void
tp_disrupt_touch(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	if (t->state == TOUCH_NONE)
		return;

	tp_end_touch(tp, t, time);
	t->has_jumped = true;
}

static inline void
tp_undisrupt_touch(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	if (!t->has_jumped)
		return;

	t->has_jumped = false;
	tp_begin_touch(tp, t, time);
}

static inline int
tp_detect_jumps(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	struct tp_motion *oldpos;
	double dx, dy;

	/* Detect cursor jumps. The sampling rate of touchpads limits what
	 * we can detect, if one finger leaves touch and another sets down
	 * quickly enough, the firmware may not notice the touch up/down
	 * event but assume the finger moved.
	 * See https://bugs.freedesktop.org/show_bug.cgi?id=76722
	 *
	 * We detect the jump here, mark the touch as jumped so we can end
	 * it and then continue with the current touch as new touch.
	 *
	 * This is only done for MT touchpads, on ST touchpads two fingers
	 * are problematic anyway.
	 */
	if (!tp->has_mt ||
	    t->history.count == 0)
		return 0;

	/* This is called before tp_motion_history_push(),
	   so the latest historical datum is at offset 0. */
	oldpos = tp_motion_history_offset(t, 0);
	dx = abs(t->x - oldpos->x);
	dy = abs(t->y - oldpos->y);

	if (tp->device->abs.absinfo_x->resolution > 1 &&
	    tp->device->abs.absinfo_y->resolution > 1) {
		dx /= tp->device->abs.absinfo_x->resolution;
		dy /= tp->device->abs.absinfo_y->resolution;
	}

	if (dx * dx + dy * dy > tp->jump_threshold * tp->jump_threshold) {
		tp_disrupt_touch(tp, t, time);
		return 1;
	}
	return 0;
}

static double
tp_estimate_delta(int x0, int x1, int x2, int x3)
{
	return (x0 + x1 - x2 - x3) / 4.0;
}

void
tp_get_delta(struct tp_touch *t, double *dx, double *dy)
{
	if (t->history.count < 4) {
		*dx = 0;
		*dy = 0;
		return;
	}

	*dx = tp_estimate_delta(tp_motion_history_offset(t, 0)->x,
				tp_motion_history_offset(t, 1)->x,
				tp_motion_history_offset(t, 2)->x,
				tp_motion_history_offset(t, 3)->x);
	*dy = tp_estimate_delta(tp_motion_history_offset(t, 0)->y,
				tp_motion_history_offset(t, 1)->y,
				tp_motion_history_offset(t, 2)->y,
				tp_motion_history_offset(t, 3)->y);
}

static void
tp_process_absolute(struct tp_dispatch *tp,
		    const struct input_event *e,
		    uint64_t time)
{
	struct tp_touch *t = tp_current_touch(tp);

	switch(e->code) {
	case ABS_MT_POSITION_X:
		t->x = e->value;
		t->millis = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	case ABS_MT_POSITION_Y:
		t->y = e->value;
		t->millis = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	case ABS_MT_SLOT:
		tp->slot = e->value;
		break;
	case ABS_MT_TRACKING_ID:
		if (e->value != -1)
			tp_begin_touch(tp, t, time);
		else
			tp_end_touch(tp, t, time);
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
		t->x = e->value;
		t->millis = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	case ABS_Y:
		t->y = e->value;
		t->millis = time;
		t->dirty = true;
		tp->queued |= TOUCHPAD_EVENT_MOTION;
		break;
	}
}

static void
tp_process_fake_touch(struct tp_dispatch *tp,
		      const struct input_event *e,
		      uint64_t time)
{
	struct tp_touch *t;
	unsigned int fake_touches;
	unsigned int nfake_touches;
	unsigned int i, start;
	unsigned int shift;

	if (e->code != BTN_TOUCH &&
	    (e->code < BTN_TOOL_DOUBLETAP || e->code > BTN_TOOL_QUADTAP))
		return;

	shift = e->code == BTN_TOUCH ? 0 : (e->code - BTN_TOOL_DOUBLETAP + 1);

	if (e->value)
		tp->fake_touches |= 1 << shift;
	else
		tp->fake_touches &= ~(0x1 << shift);

	fake_touches = tp->fake_touches;
	nfake_touches = 0;
	while (fake_touches) {
		nfake_touches++;
		fake_touches >>= 1;
	}

	/* For single touch tps we use BTN_TOUCH for begin / end of touch 0 */
	start = tp->has_mt ? tp->real_touches : 0;
	for (i = start; i < tp->ntouches; i++) {
		t = tp_get_touch(tp, i);
		if (i < nfake_touches)
			tp_begin_touch(tp, t, time);
		else
			tp_end_touch(tp, t, time);
	}

	/* On mt the actual touch info may arrive after BTN_TOOL_FOO */
	assert(tp->has_mt || tp->nfingers_down == nfake_touches);
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
		case BTN_TOOL_DOUBLETAP:
		case BTN_TOOL_TRIPLETAP:
		case BTN_TOOL_QUADTAP:
			tp_process_fake_touch(tp, e, time);
			break;
	}
}

static void
tp_unpin_finger(struct tp_dispatch *tp, struct tp_touch *t)
{
	unsigned int xdist, ydist;

	if (!t->pinned.is_pinned)
		return;

	xdist = abs(t->x - t->pinned.center_x);
	ydist = abs(t->y - t->pinned.center_y);

	if (xdist * xdist + ydist * ydist >=
			tp->buttons.motion_dist * tp->buttons.motion_dist) {
		t->pinned.is_pinned = false;
		tp_set_pointer(tp, t);
	}
}

static void
tp_pin_fingers(struct tp_dispatch *tp)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		t->is_pointer = false;
		t->pinned.is_pinned = true;
		t->pinned.center_x = t->x;
		t->pinned.center_y = t->y;
	}
}

static int
tp_touch_active(struct tp_dispatch *tp, struct tp_touch *t)
{
	return (t->state == TOUCH_BEGIN || t->state == TOUCH_UPDATE) &&
		!t->palm.is_palm &&
		!t->pinned.is_pinned && tp_button_touch_active(tp, t);
}

void
tp_set_pointer(struct tp_dispatch *tp, struct tp_touch *t)
{
	struct tp_touch *tmp = NULL;

	/* Only set the touch as pointer if we don't have one yet */
	tp_for_each_touch(tp, tmp) {
		if (tmp->is_pointer)
			return;
	}

	if (tp_touch_active(tp, t))
		t->is_pointer = true;
}

static void
tp_palm_detect(struct tp_dispatch *tp, struct tp_touch *t, uint64_t time)
{
	const int PALM_TIMEOUT = 200; /* ms */
	const int DIRECTIONS = NE|E|SE|SW|W|NW;

	/* If labelled a touch as palm, we unlabel as palm when
	   we move out of the palm edge zone within the timeout, provided
	   the direction is within 45 degrees of the horizontal.
	 */
	if (t->palm.is_palm) {
		if (time < t->palm.time + PALM_TIMEOUT &&
		    (t->x > tp->palm.left_edge && t->x < tp->palm.right_edge)) {
			int dirs = vector_get_direction(t->x - t->palm.x, t->y - t->palm.y);
			if ((dirs & DIRECTIONS) && !(dirs & ~DIRECTIONS)) {
				t->palm.is_palm = false;
				tp_set_pointer(tp, t);
			}
		}
		return;
	}

	/* palm must start in exclusion zone, it's ok to move into
	   the zone without being a palm */
	if (t->state != TOUCH_BEGIN ||
	    (t->x > tp->palm.left_edge && t->x < tp->palm.right_edge))
		return;

	/* don't detect palm in software button areas, it's
	   likely that legitimate touches start in the area
	   covered by the exclusion zone */
	if (tp->buttons.is_clickpad &&
	    tp_button_is_inside_softbutton_area(tp, t))
		return;

	t->palm.is_palm = true;
	t->palm.time = time;
	t->palm.x = t->x;
	t->palm.y = t->y;
}

static int
tp_process_state(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;
	struct tp_touch *first = tp_get_touch(tp, 0);
	unsigned int i;
	bool touch_jumps = false;

	for (i = 0; i < tp->ntouches; i++) {
		t = tp_get_touch(tp, i);

		/* semi-mt finger postions may "jump" when nfingers changes */
		if (tp->semi_mt && tp->nfingers_down != tp->old_nfingers_down)
			tp_motion_history_reset(t);

		if (i >= tp->real_touches && t->state != TOUCH_NONE) {
			t->x = first->x;
			t->y = first->y;
			if (!t->dirty)
				t->dirty = first->dirty;
		}

		tp_undisrupt_touch(tp, t, time);
		if (!t->dirty)
			continue;

		tp_palm_detect(tp, t, time);

		if (tp_detect_jumps(tp, t, time)) {
			touch_jumps = true;
		} else {
			tp_motion_hysteresis(tp, t);
			tp_motion_history_push(t);
		}

		tp_unpin_finger(tp, t);
	}

	/* If we have a touch jump we ignore button presses.
	 * We get called again, this time without a cursor jump
	 * and continue normal then. */
	tp_button_handle_state(tp, time, touch_jumps);

	if (touch_jumps)
		return 1;

	/*
	 * We have a physical button down event on a clickpad. To avoid
	 * spurious pointer moves by the clicking finger we pin all fingers.
	 * We unpin fingers when they move more then a certain threshold to
	 * to allow drag and drop.
	 */
	if ((tp->queued & TOUCHPAD_EVENT_BUTTON_PRESS) &&
	    tp->buttons.is_clickpad)
		tp_pin_fingers(tp);

	return 0;
}

static void
tp_post_process_state(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		if (!t->dirty)
			continue;

		if (t->state == TOUCH_END)
			t->state = TOUCH_NONE;
		else if (t->state == TOUCH_BEGIN)
			t->state = TOUCH_UPDATE;

		t->dirty = false;
	}

	tp->old_nfingers_down = tp->nfingers_down;

	tp->queued = TOUCHPAD_EVENT_NONE;
}

static void
tp_post_twofinger_scroll(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;
	int nchanged = 0;
	double dx = 0, dy =0;
	double tmpx, tmpy;

	tp_for_each_touch(tp, t) {
		if (tp_touch_active(tp, t) && t->dirty) {
			nchanged++;
			tp_get_delta(t, &tmpx, &tmpy);

			dx += tmpx;
			dy += tmpy;
		}
		/* Stop spurious MOTION events at the end of scrolling */
		t->is_pointer = false;
	}

	if (nchanged == 0)
		return;

	dx /= nchanged;
	dy /= nchanged;

	tp_filter_motion(tp, &dx, &dy, time);
	evdev_post_scroll(tp->device, time, dx, dy);
}

static int
tp_post_scroll_events(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;
	int nfingers_down = 0;

	/* Only count active touches for 2 finger scrolling */
	tp_for_each_touch(tp, t) {
		if (tp_touch_active(tp, t))
			nfingers_down++;
	}

	if (nfingers_down != 2) {
		evdev_stop_scroll(tp->device, time);
		return 0;
	}

	tp_post_twofinger_scroll(tp, time);
	return 1;
}

static void
tp_post_events(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t = tp_current_touch(tp);
	double dx, dy;
	int consumed = 0;

	/* Only post (top) button events while suspended */
	if (tp->device->suspended) {
		tp_post_button_events(tp, time);
		return;
	}

	consumed |= tp_tap_handle_state(tp, time);
	consumed |= tp_post_button_events(tp, time);

	if (consumed) {
		evdev_stop_scroll(tp->device, time);
		return;
	}

	if (tp_post_scroll_events(tp, time) != 0)
		return;

	if (!t->is_pointer) {
		tp_for_each_touch(tp, t) {
			if (t->is_pointer)
				break;
		}
	}

	if (!t->is_pointer ||
	    !t->dirty ||
	    t->history.count < TOUCHPAD_MIN_SAMPLES)
		return;

	tp_get_delta(t, &dx, &dy);
	tp_filter_motion(tp, &dx, &dy, time);

	if (dx != 0.0 || dy != 0.0)
		pointer_notify_motion(&tp->device->base, time, dx, dy);
}

static void
tp_handle_state(struct tp_dispatch *tp,
		uint64_t time)
{
	if (tp_process_state(tp, time)) {
		/* some touches jumped position and were marked as
		   TOUCH_END. Post events in the current state to
		   release anything down, then re-process the
		   current state which will move the touches to
		   TOUCH_BEGIN */
		enum touchpad_event queued = tp->queued;

		/* ditch press events for now */
		tp->queued &= ~TOUCHPAD_EVENT_BUTTON_PRESS;
		tp_post_events(tp, time);
		tp_post_process_state(tp, time);

		/* ditch release events, re-instate press */
		tp->queued &= ~TOUCHPAD_EVENT_BUTTON_RELEASE;
		if (queued & TOUCHPAD_EVENT_BUTTON_PRESS)
			tp->queued |= TOUCHPAD_EVENT_BUTTON_PRESS;

		/* process again */
		tp_process_state(tp, time);
	}
	tp_post_events(tp, time);
	tp_post_process_state(tp, time);
}

static void
tp_process(struct evdev_dispatch *dispatch,
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
tp_destroy(struct evdev_dispatch *dispatch)
{
	struct tp_dispatch *tp =
		(struct tp_dispatch*)dispatch;

	tp_destroy_tap(tp);
	tp_destroy_buttons(tp);

	filter_destroy(tp->filter);
	free(tp->touches);
	free(tp);
}

static void
tp_clear_state(struct tp_dispatch *tp, struct evdev_device *device)
{
	uint64_t now = libinput_now(tp->device->base.seat->libinput);
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
		tp_end_touch(tp, t, now);
	}

	tp_handle_state(tp, now);
}

static void
tp_suspend(struct tp_dispatch *tp, struct evdev_device *device)
{
	tp_clear_state(tp, device);

	/* On devices with top softwarebuttons we don't actually suspend the
	 * device, to keep the "trackpoint" buttons working. tp_post_events()
	 * will only send events for the trackpoint while suspended.
	 */
	if (tp->buttons.has_topbuttons) {
		evdev_notify_suspended_device(device);
	} else {
		evdev_device_suspend(device);
	}
}

static void
tp_resume(struct tp_dispatch *tp, struct evdev_device *device)
{
	if (tp->buttons.has_topbuttons) {
		/* tap state-machine is offline while suspended, reset state */
		tp_clear_state(tp, device);
		evdev_notify_resumed_device(device);
	} else {
		evdev_device_resume(device);
	}
}

static void
tp_device_added(struct evdev_device *device,
		struct evdev_device *added_device)
{
	struct tp_dispatch *tp = (struct tp_dispatch*)device->dispatch;

	if (tp->buttons.trackpoint == NULL &&
	    (added_device->tags & EVDEV_TAG_TRACKPOINT)) {
		/* Don't send any pending releases to the new trackpoint */
		tp->buttons.active_is_topbutton = false;
		tp->buttons.trackpoint = added_device;
	}

	if (tp->sendevents.current_mode !=
	    LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE)
		return;

	if (added_device->tags & EVDEV_TAG_EXTERNAL_MOUSE)
		tp_suspend(tp, device);
}

static void
tp_device_removed(struct evdev_device *device,
		  struct evdev_device *removed_device)
{
	struct tp_dispatch *tp = (struct tp_dispatch*)device->dispatch;
	struct libinput_device *dev;

	if (removed_device == tp->buttons.trackpoint) {
		/* Clear any pending releases for the trackpoint */
		if (tp->buttons.active && tp->buttons.active_is_topbutton) {
			tp->buttons.active = 0;
			tp->buttons.active_is_topbutton = true;
		}
		tp->buttons.trackpoint = NULL;
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

static void
tp_tag_device(struct evdev_device *device,
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
		 if (libevdev_get_id_vendor(device->evdev) == VENDOR_ID_APPLE)
			 device->tags |= EVDEV_TAG_INTERNAL_TOUCHPAD;
	} else if (bustype != BUS_BLUETOOTH)
		device->tags |= EVDEV_TAG_INTERNAL_TOUCHPAD;
}

static struct evdev_dispatch_interface tp_interface = {
	tp_process,
	tp_destroy,
	tp_device_added,
	tp_device_removed,
	tp_device_removed, /* device_suspended, treat as remove */
	tp_device_added,   /* device_resumed, treat as add */
	tp_tag_device,
};

static void
tp_init_touch(struct tp_dispatch *tp,
	      struct tp_touch *t)
{
	t->tp = tp;
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
		tp->real_touches = absinfo->maximum + 1;
		tp->slot = absinfo->value;
		tp->has_mt = true;
	} else {
		tp->real_touches = 1;
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

	tp->ntouches = max(tp->real_touches, n_btn_tool_touches);
	tp->touches = calloc(tp->ntouches, sizeof(struct tp_touch));
	if (!tp->touches)
		return -1;

	for (i = 0; i < tp->ntouches; i++)
		tp_init_touch(tp, &tp->touches[i]);

	return 0;
}

static int
tp_init_accel(struct tp_dispatch *tp, double diagonal)
{
	struct motion_filter *accel;
	int res_x, res_y;

	if (tp->has_mt) {
		res_x = libevdev_get_abs_resolution(tp->device->evdev,
						    ABS_MT_POSITION_X);
		res_y = libevdev_get_abs_resolution(tp->device->evdev,
						    ABS_MT_POSITION_Y);
	} else {
		res_x = libevdev_get_abs_resolution(tp->device->evdev,
						    ABS_X);
		res_y = libevdev_get_abs_resolution(tp->device->evdev,
						    ABS_Y);
	}

	/*
	 * Not all touchpads report the same amount of units/mm (resolution).
	 * Normalize motion events to a resolution of 15.74 units/mm
	 * (== 400 dpi) as base (unaccelerated) speed. This also evens out any
	 * differences in x and y resolution, so that a circle on the
	 * touchpad does not turn into an elipse on the screen.
	 *
	 * We pick 400dpi as thats one of the many default resolutions
	 * for USB mice, so we end up with a similar base speed on the device.
	 */
	if (res_x > 1 && res_y > 1) {
		tp->accel.x_scale_coeff = (400/25.4) / res_x;
		tp->accel.y_scale_coeff = (400/25.4) / res_y;
	} else {
	/*
	 * For touchpads where the driver does not provide resolution, fall
	 * back to scaling motion events based on the diagonal size in units.
	 */
		tp->accel.x_scale_coeff = DEFAULT_ACCEL_NUMERATOR / diagonal;
		tp->accel.y_scale_coeff = DEFAULT_ACCEL_NUMERATOR / diagonal;
	}

	accel = create_pointer_accelator_filter(
			pointer_accel_profile_smooth_simple);
	if (accel == NULL)
		return -1;

	tp->filter = accel;

	return 0;
}

static int
tp_init_palmdetect(struct tp_dispatch *tp,
		   struct evdev_device *device)
{
	int width;

	tp->palm.right_edge = INT_MAX;
	tp->palm.left_edge = INT_MIN;

	width = abs(device->abs.absinfo_x->maximum -
		    device->abs.absinfo_x->minimum);

	/* Apple touchpads are always big enough to warrant palm detection */
	if (evdev_device_get_id_vendor(device) != VENDOR_ID_APPLE) {
		/* We don't know how big the touchpad is */
		if (device->abs.absinfo_x->resolution == 1)
			return 0;

		/* Enable palm detection on touchpads >= 80 mm. Anything smaller
		   probably won't need it, until we find out it does */
		if (width/device->abs.absinfo_x->resolution < 80)
			return 0;
	}

	/* palm edges are 5% of the width on each side */
	tp->palm.right_edge = device->abs.absinfo_x->maximum - width * 0.05;
	tp->palm.left_edge = device->abs.absinfo_x->minimum + width * 0.05;

	return 0;
}

static void
tp_init_jump_threshold(struct tp_dispatch *tp,
		       struct evdev_device *device,
		       double diagonal)
{
	int res_x, res_y;

	res_x = device->abs.absinfo_x->resolution;
	res_y = device->abs.absinfo_y->resolution;

	if (res_x > 1 && res_y > 1)
		tp->jump_threshold = 20; /* mm */
	else
		tp->jump_threshold = diagonal / 4;
}

static int
tp_init(struct tp_dispatch *tp,
	struct evdev_device *device)
{
	int width, height;
	double diagonal;

	tp->base.interface = &tp_interface;
	tp->device = device;

	if (tp_init_slots(tp, device) != 0)
		return -1;

	width = abs(device->abs.absinfo_x->maximum -
		    device->abs.absinfo_x->minimum);
	height = abs(device->abs.absinfo_y->maximum -
		     device->abs.absinfo_y->minimum);
	diagonal = sqrt(width*width + height*height);

	tp->hysteresis.margin_x =
		diagonal / DEFAULT_HYSTERESIS_MARGIN_DENOMINATOR;
	tp->hysteresis.margin_y =
		diagonal / DEFAULT_HYSTERESIS_MARGIN_DENOMINATOR;

	tp_init_jump_threshold(tp, device, diagonal);

	if (tp_init_accel(tp, diagonal) != 0)
		return -1;

	if (tp_init_tap(tp) != 0)
		return -1;

	if (tp_init_buttons(tp, device) != 0)
		return -1;

	if (tp_init_palmdetect(tp, device) != 0)
		return -1;

	device->seat_caps |= EVDEV_DEVICE_POINTER;

	return 0;
}

static uint32_t
tp_sendevents_get_modes(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	uint32_t modes = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED |
			 LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;

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

struct evdev_dispatch *
evdev_mt_touchpad_create(struct evdev_device *device)
{
	struct tp_dispatch *tp;

	tp = zalloc(sizeof *tp);
	if (!tp)
		return NULL;

	if (tp_init(tp, device) != 0) {
		tp_destroy(&tp->base);
		return NULL;
	}

	device->base.config.sendevents = &tp->sendevents.config;

	tp->sendevents.current_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	tp->sendevents.config.get_modes = tp_sendevents_get_modes;
	tp->sendevents.config.set_mode = tp_sendevents_set_mode;
	tp->sendevents.config.get_mode = tp_sendevents_get_mode;
	tp->sendevents.config.get_default_mode = tp_sendevents_get_default_mode;

	return  &tp->base;
}
