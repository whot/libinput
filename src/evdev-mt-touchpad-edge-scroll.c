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

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include "linux/input.h"

#include "evdev-mt-touchpad.h"

/* Use a reasonably large threshold until locked into scrolling mode, to
   avoid accidentally locking in scrolling mode when trying to use the entire
   touchpad to move the pointer. The user can wait for the timeout to trigger
   to do a small scroll. */
#define DEFAULT_SCROLL_THRESHOLD TP_MM_TO_DPI_NORMALIZED(3)

enum scroll_event {
	SCROLL_EVENT_TOUCH,
	SCROLL_EVENT_MOTION,
	SCROLL_EVENT_RELEASE,
	SCROLL_EVENT_TIMEOUT,
	SCROLL_EVENT_POSTED,
};

static inline const char*
edge_state_to_str(enum tp_edge_scroll_touch_state state)
{

	switch (state) {
	CASE_RETURN_STRING(EDGE_SCROLL_TOUCH_STATE_NONE);
	CASE_RETURN_STRING(EDGE_SCROLL_TOUCH_STATE_EDGE_NEW);
	CASE_RETURN_STRING(EDGE_SCROLL_TOUCH_STATE_EDGE);
	CASE_RETURN_STRING(EDGE_SCROLL_TOUCH_STATE_AREA);
	}
	return NULL;
}

static inline const char*
edge_event_to_str(enum scroll_event event)
{
	switch (event) {
	CASE_RETURN_STRING(SCROLL_EVENT_TOUCH);
	CASE_RETURN_STRING(SCROLL_EVENT_MOTION);
	CASE_RETURN_STRING(SCROLL_EVENT_RELEASE);
	CASE_RETURN_STRING(SCROLL_EVENT_TIMEOUT);
	CASE_RETURN_STRING(SCROLL_EVENT_POSTED);
	}
	return NULL;
}

uint32_t
tp_touch_get_edge(struct tp_dispatch *tp, struct tp_touch *t)
{
	uint32_t edge = EDGE_NONE;

	if (tp->scroll.method != LIBINPUT_CONFIG_SCROLL_EDGE)
		return EDGE_NONE;

	if (t->point.x > tp->scroll.right_edge)
		edge |= EDGE_RIGHT;

	if (t->point.y > tp->scroll.bottom_edge)
		edge |= EDGE_BOTTOM;

	return edge;
}

static inline void
tp_edge_scroll_set_timer(struct tp_dispatch *tp,
			 struct tp_touch *t)
{
	const int DEFAULT_SCROLL_LOCK_TIMEOUT = 300; /* ms */
	/* if we use software buttons, we disable timeout-based
	 * edge scrolling. A finger resting on the button areas is
	 * likely there to trigger a button event.
	 */
	if (tp->buttons.click_method ==
	    LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS)
		return;

	libinput_timer_set(&t->scroll.timer,
			   t->millis + DEFAULT_SCROLL_LOCK_TIMEOUT);
}

static void
tp_edge_scroll_set_state(struct tp_dispatch *tp,
			 struct tp_touch *t,
			 enum tp_edge_scroll_touch_state state)
{
	libinput_timer_cancel(&t->scroll.timer);

	t->scroll.edge_state = state;

	switch (state) {
	case EDGE_SCROLL_TOUCH_STATE_NONE:
		t->scroll.edge = EDGE_NONE;
		break;
	case EDGE_SCROLL_TOUCH_STATE_EDGE_NEW:
		t->scroll.edge = tp_touch_get_edge(tp, t);
		t->scroll.initial = t->point;
		tp_edge_scroll_set_timer(tp, t);
		break;
	case EDGE_SCROLL_TOUCH_STATE_EDGE:
		break;
	case EDGE_SCROLL_TOUCH_STATE_AREA:
		t->scroll.edge = EDGE_NONE;
		break;
	}
}

static void
tp_edge_scroll_handle_none(struct tp_dispatch *tp,
			   struct tp_touch *t,
			   enum scroll_event event)
{
	struct libinput *libinput = tp_libinput_context(tp);

	switch (event) {
	case SCROLL_EVENT_TOUCH:
		if (tp_touch_get_edge(tp, t)) {
			tp_edge_scroll_set_state(tp, t,
					EDGE_SCROLL_TOUCH_STATE_EDGE_NEW);
		} else {
			tp_edge_scroll_set_state(tp, t,
					EDGE_SCROLL_TOUCH_STATE_AREA);
		}
		break;
	case SCROLL_EVENT_MOTION:
	case SCROLL_EVENT_RELEASE:
	case SCROLL_EVENT_TIMEOUT:
	case SCROLL_EVENT_POSTED:
		log_bug_libinput(libinput,
				 "unexpected scroll event %d in none state\n",
				 event);
		break;
	}
}

static void
tp_edge_scroll_handle_edge_new(struct tp_dispatch *tp,
			       struct tp_touch *t,
			       enum scroll_event event)
{
	struct libinput *libinput = tp_libinput_context(tp);

	switch (event) {
	case SCROLL_EVENT_TOUCH:
		log_bug_libinput(libinput,
				 "unexpected scroll event %d in edge new state\n",
				 event);
		break;
	case SCROLL_EVENT_MOTION:
		t->scroll.edge &= tp_touch_get_edge(tp, t);
		if (!t->scroll.edge)
			tp_edge_scroll_set_state(tp, t,
					EDGE_SCROLL_TOUCH_STATE_AREA);
		break;
	case SCROLL_EVENT_RELEASE:
		tp_edge_scroll_set_state(tp, t, EDGE_SCROLL_TOUCH_STATE_NONE);
		break;
	case SCROLL_EVENT_TIMEOUT:
	case SCROLL_EVENT_POSTED:
		tp_edge_scroll_set_state(tp, t, EDGE_SCROLL_TOUCH_STATE_EDGE);
		break;
	}
}

static void
tp_edge_scroll_handle_edge(struct tp_dispatch *tp,
			   struct tp_touch *t,
			   enum scroll_event event)
{
	struct libinput *libinput = tp_libinput_context(tp);

	switch (event) {
	case SCROLL_EVENT_TOUCH:
	case SCROLL_EVENT_TIMEOUT:
		log_bug_libinput(libinput,
				 "unexpected scroll event %d in edge state\n",
				 event);
		break;
	case SCROLL_EVENT_MOTION:
		/* If started at the bottom right, decide in which dir to scroll */
		if (t->scroll.edge == (EDGE_RIGHT | EDGE_BOTTOM)) {
			t->scroll.edge &= tp_touch_get_edge(tp, t);
			if (!t->scroll.edge)
				tp_edge_scroll_set_state(tp, t,
						EDGE_SCROLL_TOUCH_STATE_AREA);
		}
		break;
	case SCROLL_EVENT_RELEASE:
		tp_edge_scroll_set_state(tp, t, EDGE_SCROLL_TOUCH_STATE_NONE);
		break;
	case SCROLL_EVENT_POSTED:
		break;
	}
}

static void
tp_edge_scroll_handle_area(struct tp_dispatch *tp,
			   struct tp_touch *t,
			   enum scroll_event event)
{
	struct libinput *libinput = tp_libinput_context(tp);

	switch (event) {
	case SCROLL_EVENT_TOUCH:
	case SCROLL_EVENT_TIMEOUT:
	case SCROLL_EVENT_POSTED:
		log_bug_libinput(libinput,
				 "unexpected scroll event %d in area state\n",
				 event);
		break;
	case SCROLL_EVENT_MOTION:
		break;
	case SCROLL_EVENT_RELEASE:
		tp_edge_scroll_set_state(tp, t, EDGE_SCROLL_TOUCH_STATE_NONE);
		break;
	}
}

static void
tp_edge_scroll_handle_event(struct tp_dispatch *tp,
			    struct tp_touch *t,
			    enum scroll_event event)
{
	struct libinput *libinput = tp_libinput_context(tp);
	enum tp_edge_scroll_touch_state current = t->scroll.edge_state;

	switch (current) {
	case EDGE_SCROLL_TOUCH_STATE_NONE:
		tp_edge_scroll_handle_none(tp, t, event);
		break;
	case EDGE_SCROLL_TOUCH_STATE_EDGE_NEW:
		tp_edge_scroll_handle_edge_new(tp, t, event);
		break;
	case EDGE_SCROLL_TOUCH_STATE_EDGE:
		tp_edge_scroll_handle_edge(tp, t, event);
		break;
	case EDGE_SCROLL_TOUCH_STATE_AREA:
		tp_edge_scroll_handle_area(tp, t, event);
		break;
	}

	log_debug(libinput,
		  "edge state: %s → %s → %s\n",
		  edge_state_to_str(current),
		  edge_event_to_str(event),
		  edge_state_to_str(t->scroll.edge_state));
}

static void
tp_edge_scroll_handle_timeout(uint64_t now, void *data)
{
	struct tp_touch *t = data;

	tp_edge_scroll_handle_event(t->tp, t, SCROLL_EVENT_TIMEOUT);
}

int
tp_edge_scroll_init(struct tp_dispatch *tp, struct evdev_device *device)
{
	struct tp_touch *t;
	int width, height;
	int edge_width, edge_height;

	width = device->abs.dimensions.x;
	height = device->abs.dimensions.y;

	switch (tp->model) {
	case MODEL_ALPS:
		edge_width = width * .15;
		edge_height = height * .15;
		break;
	case MODEL_APPLETOUCH: /* unibody are all clickpads, so N/A */
		edge_width = width * .085;
		edge_height = height * .085;
		break;
	default:
		/* For elantech and synaptics, note for lenovo #40 series,
		 * e.g. the T440s min/max are the absolute edges, not the
		 * recommended ones as usual with synaptics.
		 */
		edge_width = width * .04;
		edge_height = height * .054;
		break;
	}

	tp->scroll.right_edge = device->abs.absinfo_x->maximum - edge_width;
	tp->scroll.bottom_edge = device->abs.absinfo_y->maximum - edge_height;

	tp_for_each_touch(tp, t) {
		t->scroll.direction = -1;
		libinput_timer_init(&t->scroll.timer,
				    tp_libinput_context(tp),
				    tp_edge_scroll_handle_timeout, t);
	}

	return 0;
}

void
tp_remove_edge_scroll(struct tp_dispatch *tp)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t)
		libinput_timer_cancel(&t->scroll.timer);
}

void
tp_edge_scroll_handle_state(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		if (!t->dirty)
			continue;

		switch (t->state) {
		case TOUCH_NONE:
		case TOUCH_HOVERING:
			break;
		case TOUCH_BEGIN:
			tp_edge_scroll_handle_event(tp, t, SCROLL_EVENT_TOUCH);
			break;
		case TOUCH_UPDATE:
			tp_edge_scroll_handle_event(tp, t, SCROLL_EVENT_MOTION);
			break;
		case TOUCH_END:
			tp_edge_scroll_handle_event(tp, t, SCROLL_EVENT_RELEASE);
			break;
		}
	}
}

int
tp_edge_scroll_post_events(struct tp_dispatch *tp, uint64_t time)
{
	struct libinput_device *device = &tp->device->base;
	struct tp_touch *t;
	enum libinput_pointer_axis axis;
	double *delta;
	struct normalized_coords normalized, tmp;
	const struct normalized_coords zero = { 0.0, 0.0 };
	const struct discrete_coords zero_discrete = { 0.0, 0.0 };

	if (tp->scroll.method != LIBINPUT_CONFIG_SCROLL_EDGE)
		return 0;

	tp_for_each_touch(tp, t) {
		if (!t->dirty)
			continue;

		if (t->palm.state != PALM_NONE)
			continue;

		switch (t->scroll.edge) {
			case EDGE_NONE:
				if (t->scroll.direction != -1) {
					/* Send stop scroll event */
					pointer_notify_axis(device, time,
						AS_MASK(t->scroll.direction),
						LIBINPUT_POINTER_AXIS_SOURCE_FINGER,
						&zero,
						&zero_discrete);
					t->scroll.direction = -1;
				}
				continue;
			case EDGE_RIGHT:
				axis = LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL;
				delta = &normalized.y;
				break;
			case EDGE_BOTTOM:
				axis = LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL;
				delta = &normalized.x;
				break;
			default: /* EDGE_RIGHT | EDGE_BOTTOM */
				continue; /* Don't know direction yet, skip */
		}

		normalized = tp_get_delta(t);
		normalized = tp_filter_motion(tp, &normalized, time);

		switch (t->scroll.edge_state) {
		case EDGE_SCROLL_TOUCH_STATE_NONE:
		case EDGE_SCROLL_TOUCH_STATE_AREA:
			log_bug_libinput(device->seat->libinput,
					 "unexpected scroll state %d\n",
					 t->scroll.edge_state);
			break;
		case EDGE_SCROLL_TOUCH_STATE_EDGE_NEW:
			tmp = normalized;
			normalized = tp_normalize_delta(tp,
					device_delta(t->point,
						     t->scroll.initial));
			if (fabs(*delta) < DEFAULT_SCROLL_THRESHOLD)
				normalized = zero;
			else
				normalized = tmp;
			break;
		case EDGE_SCROLL_TOUCH_STATE_EDGE:
			break;
		}

		if (*delta == 0.0)
			continue;

		pointer_notify_axis(device, time,
				    AS_MASK(axis),
				    LIBINPUT_POINTER_AXIS_SOURCE_FINGER,
				    &normalized,
				    &zero_discrete);
		t->scroll.direction = axis;

		tp_edge_scroll_handle_event(tp, t, SCROLL_EVENT_POSTED);
	}

	return 0; /* Edge touches are suppressed by edge_scroll_touch_active */
}

void
tp_edge_scroll_stop_events(struct tp_dispatch *tp, uint64_t time)
{
	struct libinput_device *device = &tp->device->base;
	struct tp_touch *t;
	const struct normalized_coords zero = { 0.0, 0.0 };
	const struct discrete_coords zero_discrete = { 0.0, 0.0 };

	tp_for_each_touch(tp, t) {
		if (t->scroll.direction != -1) {
			pointer_notify_axis(device, time,
					    AS_MASK(t->scroll.direction),
					    LIBINPUT_POINTER_AXIS_SOURCE_FINGER,
					    &zero,
					    &zero_discrete);
			t->scroll.direction = -1;
			/* reset touch to area state, avoids loading the
			 * state machine with special case handling */
			t->scroll.edge = EDGE_NONE;
			t->scroll.edge_state = EDGE_SCROLL_TOUCH_STATE_AREA;
		}
	}
}

int
tp_edge_scroll_touch_active(struct tp_dispatch *tp, struct tp_touch *t)
{
	return t->scroll.edge_state == EDGE_SCROLL_TOUCH_STATE_AREA;
}
