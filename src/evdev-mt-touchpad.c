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
#include <stdbool.h>

#include "evdev.h"

#define TOUCHPAD_HISTORY_LENGTH 4

#define tp_for_each_touch(_tp, _t) \
	for (unsigned int _i = 0; _i < (_tp)->ntouches && (_t = &(_tp)->touches[_i]); _i++)

enum touch_state {
	TOUCH_NONE = 0,
	TOUCH_BEGIN,
	TOUCH_UPDATE,
	TOUCH_END
};

struct tp_motion {
	int32_t x;
	int32_t y;
};

struct tp_touch {
	enum touch_state state;
	bool dirty;
	int32_t x;
	int32_t y;
	uint32_t millis;

	struct {
		struct tp_motion samples[TOUCHPAD_HISTORY_LENGTH];
		unsigned int index;
		unsigned int count;
	} history;
};

struct tp_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;
	unsigned int nfingers_down;		/* number of fingers down */
	unsigned int slot;			/* current slot */

	unsigned int ntouches;			/* number of slots */
	struct tp_touch *touches;		/* len == ntouches */
};

static inline struct tp_motion *
tp_motion_history_offset(struct tp_touch *t, int offset)
{
	int offset_index =
		(t->history.index - offset + TOUCHPAD_HISTORY_LENGTH) %
		TOUCHPAD_HISTORY_LENGTH;

	return &t->history.samples[offset_index];
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
tp_motion_history_reset(struct tp_touch *t)
{
	t->history.count = 0;
}

static inline struct tp_touch *
tp_current_touch(struct tp_dispatch *tp)
{
	return &tp->touches[min(tp->slot, tp->ntouches)];
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
	}
}

static inline void
tp_end_touch(struct tp_dispatch *tp, struct tp_touch *t)
{
	if (t->state == TOUCH_NONE)
		return;

	t->dirty = true;
	t->state = TOUCH_END;
	assert(tp->nfingers_down >= 1);
	tp->nfingers_down--;
}

static double
tp_estimate_delta(int x0, int x1, int x2, int x3)
{
	return (x0 + x1 - x2 - x3) / 4;
}

static void
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
		break;
	case ABS_MT_POSITION_Y:
		t->y = e->value;
		t->millis = time;
		t->dirty = true;
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
tp_process_key(struct tp_dispatch *tp,
	       const struct input_event *e,
	       uint32_t time)
{
	switch (e->code) {
		case BTN_LEFT:
		case BTN_MIDDLE:
		case BTN_RIGHT:
			pointer_notify_button(
				&tp->device->base,
				time,
				e->code,
				e->value ? LIBINPUT_POINTER_BUTTON_STATE_PRESSED :
					   LIBINPUT_POINTER_BUTTON_STATE_RELEASED);
			break;
	}
}

static void
tp_process_state(struct tp_dispatch *tp, uint32_t time)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		if (!t->dirty)
			continue;

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

		if (t->state == TOUCH_END)
			t->state = TOUCH_NONE;
		else if (t->state == TOUCH_BEGIN)
			t->state = TOUCH_UPDATE;

		t->dirty = false;
	}
}

static void
tp_post_events(struct tp_dispatch *tp, uint32_t time)
{
	struct tp_touch *t = tp_current_touch(tp);
	double dx, dy;

	if (tp->nfingers_down != 1)
		return;

	tp_get_delta(t, &dx, &dy);

	if (dx != 0 || dy != 0)
		pointer_notify_motion(
			&tp->device->base,
			time,
			li_fixed_from_double(dx),
			li_fixed_from_double(dy));
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
		tp_process_absolute(tp, e, time);
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

	ioctl(device->fd, EVIOCGABS(ABS_MT_SLOT), &absinfo);

	tp->ntouches = absinfo.maximum + 1;
	tp->touches = calloc(tp->ntouches,
			     sizeof(struct tp_touch));
	tp->slot = absinfo.value;

	return 0;
}


static int
tp_init(struct tp_dispatch *tp,
	struct evdev_device *device)
{
	tp->base.interface = &tp_interface;
	tp->device = device;

	if (tp_init_slots(tp, device) != 0)
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
