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

#include "evdev-mt-touchpad.h"

#define DEFAULT_CONSTANT_ACCEL_NUMERATOR 50
#define DEFAULT_MIN_ACCEL_FACTOR 0.16
#define DEFAULT_MAX_ACCEL_FACTOR 1.0
#define DEFAULT_HYSTERESIS_MARGIN_DENOMINATOR 700.0

static inline int
tp_hysteresis(int in, int center, int margin)
{
	int diff = in - center;
	if (abs(diff) <= margin)
		return center;

	if (diff > margin)
		return center + diff - margin;
	else if (diff < -margin)
		return center + diff + margin;
	return center + diff;
}

static double
tp_accel_profile(struct motion_filter *filter,
		 void *data,
		 double velocity,
		 uint32_t time)
{
	struct tp_dispatch *tp =
		(struct tp_dispatch *) data;

	double accel_factor;

	accel_factor = velocity * tp->accel.constant_factor;

	if (accel_factor > tp->accel.max_factor)
		accel_factor = tp->accel.max_factor;
	else if (accel_factor < tp->accel.min_factor)
		accel_factor = tp->accel.min_factor;

	return accel_factor;
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
	         double *dx, double *dy, uint32_t time)
{
	struct motion_params motion;

	motion.dx = *dx;
	motion.dy = *dy;

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
	return &tp->touches[min(tp->slot, tp->ntouches)];
}

static inline struct tp_touch *
tp_get_touch(struct tp_dispatch *tp, unsigned int slot)
{
	assert(slot < tp->ntouches);
	return &tp->touches[slot];
}

static inline void
tp_begin_touch(struct tp_dispatch *tp, struct tp_touch *t)
{
	if (t->state != TOUCH_UPDATE) {
		tp_motion_history_reset(t);
		t->dirty = true;
		t->state = TOUCH_BEGIN;
		tp->nfingers_down++;
		assert(tp->nfingers_down >= 1);
		tp->queued |= TOUCHPAD_EVENT_MOTION;

		if (tp->nfingers_down == 1)
			t->is_pointer = true;
	}
}

static inline void
tp_end_touch(struct tp_dispatch *tp, struct tp_touch *t)
{
	if (t->state == TOUCH_NONE)
		return;

	t->dirty = true;
	t->is_pointer = false;
	t->state = TOUCH_END;
	assert(tp->nfingers_down >= 1);
	tp->nfingers_down--;
	tp->queued |= TOUCHPAD_EVENT_MOTION;
}

static double
tp_estimate_delta(int x0, int x1, int x2, int x3)
{
	return (x0 + x1 - x2 - x3) / 4;
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
		    uint32_t time)
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
		t->millis = time;
		if (e->value != -1)
			tp_begin_touch(tp, t);
		else
			tp_end_touch(tp, t);
	}
}

static void
tp_process_absolute_st(struct tp_dispatch *tp,
		       const struct input_event *e,
		       uint32_t time)
{
	struct tp_touch *t = tp_current_touch(tp);

	switch(e->code) {
	case ABS_X:
		t->x = e->value;
		t->millis = time;
		t->dirty = true;
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
		      uint32_t time)
{
	struct tp_touch *t;
	unsigned int fake_touches;
	unsigned int nfake_touches;
	unsigned int i;
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

	for (i = 0; i < tp->ntouches; i++) {
		t = tp_get_touch(tp, i);
		if (i >= nfake_touches) {
			if (t->state != TOUCH_NONE) {
				tp_end_touch(tp, t);
				t->millis = time;
			}
		} else if (t->state != TOUCH_UPDATE &&
			   t->state != TOUCH_BEGIN) {
			t->state = TOUCH_NONE;
			tp_begin_touch(tp, t);
			t->millis = time;
			t->fake =true;
		}
	}

	assert(tp->nfingers_down == nfake_touches);
}

static void
tp_process_key(struct tp_dispatch *tp,
	       const struct input_event *e,
	       uint32_t time)
{
	uint32_t mask;

	switch (e->code) {
		case BTN_LEFT:
		case BTN_MIDDLE:
		case BTN_RIGHT:
			mask = 1 << (e->code - BTN_LEFT);
			if (e->value) {
				tp->buttons.state |= mask;
				tp->queued |= TOUCHPAD_EVENT_BUTTON_PRESS;
			} else {
				tp->buttons.state &= ~mask;
				tp->queued |= TOUCHPAD_EVENT_BUTTON_RELEASE;
			}
			break;
		case BTN_TOUCH:
		case BTN_TOOL_DOUBLETAP:
		case BTN_TOOL_TRIPLETAP:
		case BTN_TOOL_QUADTAP:
			if (!tp->has_mt)
				tp_process_fake_touch(tp, e, time);
			break;
	}
}

static void
tp_process_state(struct tp_dispatch *tp, uint32_t time)
{
	struct tp_touch *t;
	struct tp_touch *first = tp_get_touch(tp, 0);

	tp_for_each_touch(tp, t) {
		if (!tp->has_mt && t != first && first->fake) {
			t->x = first->x;
			t->y = first->y;
			if (!t->dirty)
				t->dirty = first->dirty;
		} else if (!t->dirty)
			continue;

		tp_motion_hysteresis(tp, t);
		tp_motion_history_push(t);
	}
}

static void
tp_post_process_state(struct tp_dispatch *tp, uint32_t time)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		if (!t->dirty)
			continue;

		if (t->state == TOUCH_END) {
			t->state = TOUCH_NONE;
			t->fake = false;
		} else if (t->state == TOUCH_BEGIN)
			t->state = TOUCH_UPDATE;

		t->dirty = false;
	}

	tp->buttons.old_state = tp->buttons.state;

	tp->queued = TOUCHPAD_EVENT_NONE;
}

static void
tp_post_twofinger_scroll(struct tp_dispatch *tp, uint32_t time)
{
	struct tp_touch *t;
	int nchanged = 0;
	double dx = 0, dy =0;
	double tmpx, tmpy;

	tp_for_each_touch(tp, t) {
		if (t->dirty) {
			nchanged++;
			tp_get_delta(t, &tmpx, &tmpy);

			dx += tmpx;
			dy += tmpy;
		}
	}

	if (nchanged == 0)
		return;

	dx /= nchanged;
	dy /= nchanged;

	tp_filter_motion(tp, &dx, &dy, time);

	if (tp->scroll.state == SCROLL_STATE_NONE) {
		/* Require at least one px scrolling to start */
		if (dx <= -1.0 || dx >= 1.0) {
			tp->scroll.state = SCROLL_STATE_SCROLLING;
			tp->scroll.direction |= (1 << LIBINPUT_POINTER_AXIS_HORIZONTAL_SCROLL);
		}

		if (dy <= -1.0 || dy >= 1.0) {
			tp->scroll.state = SCROLL_STATE_SCROLLING;
			tp->scroll.direction |= (1 << LIBINPUT_POINTER_AXIS_VERTICAL_SCROLL);
		}

		if (tp->scroll.state == SCROLL_STATE_NONE)
			return;
	}

	if (dy != 0.0 &&
	    (tp->scroll.direction & (1 << LIBINPUT_POINTER_AXIS_VERTICAL_SCROLL))) {
		pointer_notify_axis(&tp->device->base,
				    time,
				    LIBINPUT_POINTER_AXIS_VERTICAL_SCROLL,
				    li_fixed_from_double(dy));
	}

	if (dx != 0.0 &&
	    (tp->scroll.direction & (1 << LIBINPUT_POINTER_AXIS_HORIZONTAL_SCROLL))) {
		pointer_notify_axis(&tp->device->base,
				    time,
				    LIBINPUT_POINTER_AXIS_HORIZONTAL_SCROLL,
				    li_fixed_from_double(dx));
	}
}

static int
tp_post_scroll_events(struct tp_dispatch *tp, uint32_t time)
{
	if (tp->nfingers_down != 2) {
		/* terminate scrolling with a zero scroll event to notify
		 * caller that it really ended now */
		if (tp->scroll.state != SCROLL_STATE_NONE) {
			tp->scroll.state = SCROLL_STATE_NONE;
			tp->scroll.direction = 0;
			if (tp->scroll.direction & LIBINPUT_POINTER_AXIS_VERTICAL_SCROLL)
				pointer_notify_axis(&tp->device->base,
						    time,
						    LIBINPUT_POINTER_AXIS_VERTICAL_SCROLL,
						    0);
			if (tp->scroll.direction & LIBINPUT_POINTER_AXIS_HORIZONTAL_SCROLL)
				pointer_notify_axis(&tp->device->base,
						    time,
						    LIBINPUT_POINTER_AXIS_HORIZONTAL_SCROLL,
						    0);
		}
	} else {
		tp_post_twofinger_scroll(tp, time);
		return 1;
	}
	return 0;
}

static int
tp_post_clickfinger_buttons(struct tp_dispatch *tp, uint32_t time)
{
	uint32_t current, old, button;
	enum libinput_pointer_button_state state;

	current = tp->buttons.state;
	old = tp->buttons.old_state;

	if (current == old)
		return 0;

	switch (tp->nfingers_down) {
		case 1: button = BTN_LEFT; break;
		case 2: button = BTN_RIGHT; break;
		case 3: button = BTN_MIDDLE; break;
		default:
			return 0;
	}

	if (current)
		state = LIBINPUT_POINTER_BUTTON_STATE_PRESSED;
	else
		state = LIBINPUT_POINTER_BUTTON_STATE_RELEASED;

	pointer_notify_button(&tp->device->base,
			      time,
			      button,
			      state);
	return 1;
}

static int
tp_post_physical_buttons(struct tp_dispatch *tp, uint32_t time)
{
	uint32_t current, old, button;

	current = tp->buttons.state;
	old = tp->buttons.old_state;
	button = BTN_LEFT;

	while (current || old) {
		enum libinput_pointer_button_state state;

		if ((current & 0x1) ^ (old & 0x1)) {
			if (!!(current & 0x1))
				state = LIBINPUT_POINTER_BUTTON_STATE_PRESSED;
			else
				state = LIBINPUT_POINTER_BUTTON_STATE_RELEASED;

			pointer_notify_button(&tp->device->base,
					      time,
					      button,
					      state);
		}

		button++;
		current >>= 1;
		old >>= 1;
	}

	return 0;
}

static int
tp_post_button_events(struct tp_dispatch *tp, uint32_t time)
{
	int rc;

	if ((tp->queued &
		(TOUCHPAD_EVENT_BUTTON_PRESS|TOUCHPAD_EVENT_BUTTON_RELEASE)) == 0)
				return 0;

	if (tp->buttons.has_buttons)
		rc = tp_post_physical_buttons(tp, time);
	else
		rc = tp_post_clickfinger_buttons(tp, time);

	return rc;
}

static void
tp_post_events(struct tp_dispatch *tp, uint32_t time)
{
	struct tp_touch *t = tp_current_touch(tp);
	double dx, dy;

	if (tp_post_button_events(tp, time) != 0)
		return;

	if (tp_tap_handle_state(tp, time) != 0)
		return;

	if (tp_post_scroll_events(tp, time) != 0)
		return;

	if (t->history.count >= TOUCHPAD_MIN_SAMPLES &&
	    tp->nfingers_down == 1) {
		tp_get_delta(t, &dx, &dy);
		tp_filter_motion(tp, &dx, &dy, time);

		if (dx != 0 || dy != 0)
			pointer_notify_motion(
				&tp->device->base,
				time,
				li_fixed_from_double(dx),
				li_fixed_from_double(dy));
	}
}

static void
tp_process(struct evdev_dispatch *dispatch,
	   struct evdev_device *device,
	   struct input_event *e,
	   uint32_t time)
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
		tp_process_state(tp, time);
		tp_post_events(tp, time);
		tp_post_process_state(tp, time);
		break;
	}
}

static void
tp_destroy(struct evdev_dispatch *dispatch)
{
	struct tp_dispatch *tp =
		(struct tp_dispatch*)dispatch;

	if (tp->filter)
		tp->filter->interface->destroy(tp->filter);
	free(tp->touches);
	free(tp);
}

static struct evdev_dispatch_interface tp_interface = {
	tp_process,
	tp_destroy
};

static int
tp_init_slots(struct tp_dispatch *tp,
	      struct evdev_device *device)
{
	struct input_absinfo absinfo = {0};

	if (ioctl(device->fd, EVIOCGABS(ABS_MT_SLOT), &absinfo) == -1) {
		tp->ntouches = 5; /* FIXME: based on DOUBLETAP, etc. */
		tp->slot = 0;
		tp->has_mt = false;
	} else {
		tp->ntouches = absinfo.maximum + 1;
		tp->slot = absinfo.value;
		tp->has_mt = true;
	}
	tp->touches = calloc(tp->ntouches,
			     sizeof(struct tp_touch));

	return 0;
}

static int
tp_init_accel(struct tp_dispatch *touchpad, double diagonal)
{
	struct motion_filter *accel;

	touchpad->accel.constant_factor =
		DEFAULT_CONSTANT_ACCEL_NUMERATOR / diagonal;
	touchpad->accel.min_factor = DEFAULT_MIN_ACCEL_FACTOR;
	touchpad->accel.max_factor = DEFAULT_MAX_ACCEL_FACTOR;

	accel = create_pointer_accelator_filter(tp_accel_profile);
	if (accel == NULL)
		return -1;

	touchpad->filter = accel;

	return 0;
}

static int
tp_init_scroll(struct tp_dispatch *tp)
{
	tp->scroll.direction = 0;
	tp->scroll.state = SCROLL_STATE_NONE;

	return 0;
}

static int
tp_init(struct tp_dispatch *tp,
	struct evdev_device *device)
{
	int width, height;
	double diagonal;
	unsigned long key_bits[NBITS(KEY_MAX)];

	tp->base.interface = &tp_interface;
	tp->device = device;

	if (tp_init_slots(tp, device) != 0)
		return -1;

	width = abs(device->abs.max_x - device->abs.min_x);
	height = abs(device->abs.max_y - device->abs.min_y);
	diagonal = sqrt(width*width + height*height);

	tp->hysteresis.margin_x =
		diagonal / DEFAULT_HYSTERESIS_MARGIN_DENOMINATOR;
	tp->hysteresis.margin_y =
		diagonal / DEFAULT_HYSTERESIS_MARGIN_DENOMINATOR;

	ioctl(device->fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
	if (TEST_BIT(key_bits, BTN_RIGHT) || TEST_BIT(key_bits, BTN_MIDDLE))
		tp->buttons.has_buttons = true;

	if (tp_init_scroll(tp) != 0)
		return -1;

	if (tp_init_accel(tp, diagonal) != 0)
		return -1;

	if (tp_init_tap(tp) != 0)
		return -1;

	return 0;
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

	return  &tp->base;
}
