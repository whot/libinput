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

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include "linux/input.h"

#include "evdev-mt-touchpad.h"

#define DEFAULT_BUTTON_MOTION_THRESHOLD 0.02 /* 2% of size */
#define DEFAULT_BUTTON_ENTER_TIMEOUT 100 /* ms */
#define DEFAULT_BUTTON_LEAVE_TIMEOUT 300 /* ms */

/*****************************************
 * BEFORE YOU EDIT THIS FILE, look at the state diagram in
 * doc/touchpad-softbutton-state-machine.svg, or online at
 * https://drive.google.com/file/d/0B1NwWmji69nocUs1cVJTbkdwMFk/edit?usp=sharing
 * (it's a http://draw.io diagram)
 *
 * Any changes in this file must be represented in the diagram.
 *
 * The state machine only affects the soft button area code.
 */

#define CASE_RETURN_STRING(a) case a: return #a;

static inline const char*
button_state_to_str(enum button_state state) {
	switch(state) {
	CASE_RETURN_STRING(BUTTON_STATE_NONE);
	CASE_RETURN_STRING(BUTTON_STATE_AREA);
	CASE_RETURN_STRING(BUTTON_STATE_BOTTOM);
	CASE_RETURN_STRING(BUTTON_STATE_TOP);
	CASE_RETURN_STRING(BUTTON_STATE_TOP_NEW);
	CASE_RETURN_STRING(BUTTON_STATE_TOP_TO_IGNORE);
	CASE_RETURN_STRING(BUTTON_STATE_IGNORE);
	}
	return NULL;
}

static inline const char*
button_event_to_str(enum button_event event) {
	switch(event) {
	CASE_RETURN_STRING(BUTTON_EVENT_IN_BOTTOM_R);
	CASE_RETURN_STRING(BUTTON_EVENT_IN_BOTTOM_L);
	CASE_RETURN_STRING(BUTTON_EVENT_IN_TOP_R);
	CASE_RETURN_STRING(BUTTON_EVENT_IN_TOP_M);
	CASE_RETURN_STRING(BUTTON_EVENT_IN_TOP_L);
	CASE_RETURN_STRING(BUTTON_EVENT_IN_AREA);
	CASE_RETURN_STRING(BUTTON_EVENT_UP);
	CASE_RETURN_STRING(BUTTON_EVENT_PRESS);
	CASE_RETURN_STRING(BUTTON_EVENT_RELEASE);
	CASE_RETURN_STRING(BUTTON_EVENT_TIMEOUT);
	}
	return NULL;
}

static inline bool
is_inside_bottom_button_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return t->y >= tp->buttons.bottom_area.top_edge;
}

static inline bool
is_inside_bottom_right_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_bottom_button_area(tp, t) &&
	       t->x > tp->buttons.bottom_area.rightbutton_left_edge;
}

static inline bool
is_inside_bottom_left_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_bottom_button_area(tp, t) &&
	       !is_inside_bottom_right_area(tp, t);
}

static inline bool
is_inside_top_button_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return t->y <= tp->buttons.top_area.bottom_edge;
}

static inline bool
is_inside_top_right_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_top_button_area(tp, t) &&
	       t->x > tp->buttons.top_area.rightbutton_left_edge;
}

static inline bool
is_inside_top_left_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_top_button_area(tp, t) &&
	       t->x < tp->buttons.top_area.leftbutton_right_edge;
}

static inline bool
is_inside_top_middle_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_top_button_area(tp, t) &&
	       t->x >= tp->buttons.top_area.leftbutton_right_edge &&
	       t->x <= tp->buttons.top_area.rightbutton_left_edge;
}

static void
tp_button_set_enter_timer(struct tp_dispatch *tp, struct tp_touch *t)
{
	libinput_timer_set(&t->button.timer,
			   t->millis + DEFAULT_BUTTON_ENTER_TIMEOUT);
}

static void
tp_button_set_leave_timer(struct tp_dispatch *tp, struct tp_touch *t)
{
	libinput_timer_set(&t->button.timer,
			   t->millis + DEFAULT_BUTTON_LEAVE_TIMEOUT);
}

/*
 * tp_button_set_state, change state and implement on-entry behavior
 * as described in the state machine diagram.
 */
static void
tp_button_set_state(struct tp_dispatch *tp, struct tp_touch *t,
		    enum button_state new_state, enum button_event event)
{
	libinput_timer_cancel(&t->button.timer);

	t->button.state = new_state;
	switch (t->button.state) {
	case BUTTON_STATE_NONE:
		t->button.curr = 0;
		break;
	case BUTTON_STATE_AREA:
		t->button.curr = BUTTON_EVENT_IN_AREA;
		tp_set_pointer(tp, t);
		break;
	case BUTTON_STATE_BOTTOM:
		t->button.curr = event;
		break;
	case BUTTON_STATE_TOP:
		break;
	case BUTTON_STATE_TOP_NEW:
		t->button.curr = event;
		tp_button_set_enter_timer(tp, t);
		break;
	case BUTTON_STATE_TOP_TO_IGNORE:
		tp_button_set_leave_timer(tp, t);
		break;
	case BUTTON_STATE_IGNORE:
		t->button.curr = 0;
		break;
	}
}

static void
tp_button_none_handle_event(struct tp_dispatch *tp,
			    struct tp_touch *t,
			    enum button_event event)
{
	switch (event) {
	case BUTTON_EVENT_IN_BOTTOM_R:
	case BUTTON_EVENT_IN_BOTTOM_L:
		tp_button_set_state(tp, t, BUTTON_STATE_BOTTOM, event);
		break;
	case BUTTON_EVENT_IN_TOP_R:
	case BUTTON_EVENT_IN_TOP_M:
	case BUTTON_EVENT_IN_TOP_L:
		tp_button_set_state(tp, t, BUTTON_STATE_TOP_NEW, event);
		break;
	case BUTTON_EVENT_IN_AREA:
		tp_button_set_state(tp, t, BUTTON_STATE_AREA, event);
		break;
	case BUTTON_EVENT_UP:
		tp_button_set_state(tp, t, BUTTON_STATE_NONE, event);
		break;
	case BUTTON_EVENT_PRESS:
	case BUTTON_EVENT_RELEASE:
	case BUTTON_EVENT_TIMEOUT:
		break;
	}
}

static void
tp_button_area_handle_event(struct tp_dispatch *tp,
			    struct tp_touch *t,
			    enum button_event event)
{
	switch (event) {
	case BUTTON_EVENT_IN_BOTTOM_R:
	case BUTTON_EVENT_IN_BOTTOM_L:
	case BUTTON_EVENT_IN_TOP_R:
	case BUTTON_EVENT_IN_TOP_M:
	case BUTTON_EVENT_IN_TOP_L:
	case BUTTON_EVENT_IN_AREA:
		break;
	case BUTTON_EVENT_UP:
		tp_button_set_state(tp, t, BUTTON_STATE_NONE, event);
		break;
	case BUTTON_EVENT_PRESS:
	case BUTTON_EVENT_RELEASE:
	case BUTTON_EVENT_TIMEOUT:
		break;
	}
}

static void
tp_button_bottom_handle_event(struct tp_dispatch *tp,
			      struct tp_touch *t,
			      enum button_event event)
{
	switch (event) {
	case BUTTON_EVENT_IN_BOTTOM_R:
	case BUTTON_EVENT_IN_BOTTOM_L:
		if (event != t->button.curr)
			tp_button_set_state(tp, t, BUTTON_STATE_BOTTOM,
					    event);
		break;
	case BUTTON_EVENT_IN_TOP_R:
	case BUTTON_EVENT_IN_TOP_M:
	case BUTTON_EVENT_IN_TOP_L:
	case BUTTON_EVENT_IN_AREA:
		tp_button_set_state(tp, t, BUTTON_STATE_AREA, event);
		break;
	case BUTTON_EVENT_UP:
		tp_button_set_state(tp, t, BUTTON_STATE_NONE, event);
		break;
	case BUTTON_EVENT_PRESS:
	case BUTTON_EVENT_RELEASE:
	case BUTTON_EVENT_TIMEOUT:
		break;
	}
}

static void
tp_button_top_handle_event(struct tp_dispatch *tp,
			    struct tp_touch *t,
			    enum button_event event)
{
	switch (event) {
	case BUTTON_EVENT_IN_BOTTOM_R:
	case BUTTON_EVENT_IN_BOTTOM_L:
		tp_button_set_state(tp, t, BUTTON_STATE_TOP_TO_IGNORE, event);
		break;
	case BUTTON_EVENT_IN_TOP_R:
	case BUTTON_EVENT_IN_TOP_M:
	case BUTTON_EVENT_IN_TOP_L:
		if (event != t->button.curr)
			tp_button_set_state(tp, t, BUTTON_STATE_TOP_NEW,
					    event);
		break;
	case BUTTON_EVENT_IN_AREA:
		tp_button_set_state(tp, t, BUTTON_STATE_TOP_TO_IGNORE, event);
		break;
	case BUTTON_EVENT_UP:
		tp_button_set_state(tp, t, BUTTON_STATE_NONE, event);
		break;
	case BUTTON_EVENT_PRESS:
	case BUTTON_EVENT_RELEASE:
	case BUTTON_EVENT_TIMEOUT:
		break;
	}
}

static void
tp_button_top_new_handle_event(struct tp_dispatch *tp,
				struct tp_touch *t,
				enum button_event event)
{
	switch(event) {
	case BUTTON_EVENT_IN_BOTTOM_R:
	case BUTTON_EVENT_IN_BOTTOM_L:
		tp_button_set_state(tp, t, BUTTON_STATE_AREA, event);
		break;
	case BUTTON_EVENT_IN_TOP_R:
	case BUTTON_EVENT_IN_TOP_M:
	case BUTTON_EVENT_IN_TOP_L:
		if (event != t->button.curr)
			tp_button_set_state(tp, t, BUTTON_STATE_TOP_NEW,
					    event);
		break;
	case BUTTON_EVENT_IN_AREA:
		tp_button_set_state(tp, t, BUTTON_STATE_AREA, event);
		break;
	case BUTTON_EVENT_UP:
		tp_button_set_state(tp, t, BUTTON_STATE_NONE, event);
		break;
	case BUTTON_EVENT_PRESS:
		tp_button_set_state(tp, t, BUTTON_STATE_TOP, event);
		break;
	case BUTTON_EVENT_RELEASE:
		break;
	case BUTTON_EVENT_TIMEOUT:
		tp_button_set_state(tp, t, BUTTON_STATE_TOP, event);
		break;
	}
}

static void
tp_button_top_to_ignore_handle_event(struct tp_dispatch *tp,
				      struct tp_touch *t,
				      enum button_event event)
{
	switch(event) {
	case BUTTON_EVENT_IN_TOP_R:
	case BUTTON_EVENT_IN_TOP_M:
	case BUTTON_EVENT_IN_TOP_L:
		if (event == t->button.curr)
			tp_button_set_state(tp, t, BUTTON_STATE_TOP,
					    event);
		else
			tp_button_set_state(tp, t, BUTTON_STATE_TOP_NEW,
					    event);
		break;
	case BUTTON_EVENT_IN_BOTTOM_R:
	case BUTTON_EVENT_IN_BOTTOM_L:
	case BUTTON_EVENT_IN_AREA:
		break;
	case BUTTON_EVENT_UP:
		tp_button_set_state(tp, t, BUTTON_STATE_NONE, event);
		break;
	case BUTTON_EVENT_PRESS:
	case BUTTON_EVENT_RELEASE:
		break;
	case BUTTON_EVENT_TIMEOUT:
		tp_button_set_state(tp, t, BUTTON_STATE_IGNORE, event);
		break;
	}
}

static void
tp_button_ignore_handle_event(struct tp_dispatch *tp,
			    struct tp_touch *t,
			    enum button_event event)
{
	switch (event) {
	case BUTTON_EVENT_IN_BOTTOM_R:
	case BUTTON_EVENT_IN_BOTTOM_L:
	case BUTTON_EVENT_IN_TOP_R:
	case BUTTON_EVENT_IN_TOP_M:
	case BUTTON_EVENT_IN_TOP_L:
	case BUTTON_EVENT_IN_AREA:
		break;
	case BUTTON_EVENT_UP:
		tp_button_set_state(tp, t, BUTTON_STATE_NONE, event);
		break;
	case BUTTON_EVENT_PRESS:
	case BUTTON_EVENT_RELEASE:
	case BUTTON_EVENT_TIMEOUT:
		break;
	}
}

static void
tp_button_handle_event(struct tp_dispatch *tp,
		       struct tp_touch *t,
		       enum button_event event,
		       uint64_t time)
{
	struct libinput *libinput = tp->device->base.seat->libinput;
	enum button_state current = t->button.state;

	switch(t->button.state) {
	case BUTTON_STATE_NONE:
		tp_button_none_handle_event(tp, t, event);
		break;
	case BUTTON_STATE_AREA:
		tp_button_area_handle_event(tp, t, event);
		break;
	case BUTTON_STATE_BOTTOM:
		tp_button_bottom_handle_event(tp, t, event);
		break;
	case BUTTON_STATE_TOP:
		tp_button_top_handle_event(tp, t, event);
		break;
	case BUTTON_STATE_TOP_NEW:
		tp_button_top_new_handle_event(tp, t, event);
		break;
	case BUTTON_STATE_TOP_TO_IGNORE:
		tp_button_top_to_ignore_handle_event(tp, t, event);
		break;
	case BUTTON_STATE_IGNORE:
		tp_button_ignore_handle_event(tp, t, event);
		break;
	}

	if (current != t->button.state)
		log_debug(libinput,
			  "button state: from %s, event %s to %s\n",
			  button_state_to_str(current),
			  button_event_to_str(event),
			  button_state_to_str(t->button.state));
}

int
tp_button_handle_state(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		if (t->state == TOUCH_NONE)
			continue;

		if (t->state == TOUCH_END) {
			tp_button_handle_event(tp, t, BUTTON_EVENT_UP, time);
		} else if (t->dirty) {
			if (is_inside_bottom_right_area(tp, t))
				tp_button_handle_event(tp, t, BUTTON_EVENT_IN_BOTTOM_R, time);
			else if (is_inside_bottom_left_area(tp, t))
				tp_button_handle_event(tp, t, BUTTON_EVENT_IN_BOTTOM_L, time);
			else if (is_inside_top_right_area(tp, t))
				tp_button_handle_event(tp, t, BUTTON_EVENT_IN_TOP_R, time);
			else if (is_inside_top_middle_area(tp, t))
				tp_button_handle_event(tp, t, BUTTON_EVENT_IN_TOP_M, time);
			else if (is_inside_top_left_area(tp, t))
				tp_button_handle_event(tp, t, BUTTON_EVENT_IN_TOP_L, time);
			else
				tp_button_handle_event(tp, t, BUTTON_EVENT_IN_AREA, time);
		}
		if (tp->queued & TOUCHPAD_EVENT_BUTTON_RELEASE)
			tp_button_handle_event(tp, t, BUTTON_EVENT_RELEASE, time);
		if (tp->queued & TOUCHPAD_EVENT_BUTTON_PRESS)
			tp_button_handle_event(tp, t, BUTTON_EVENT_PRESS, time);
	}

	return 0;
}

static void
tp_button_handle_timeout(uint64_t now, void *data)
{
	struct tp_touch *t = data;

	tp_button_handle_event(t->tp, t, BUTTON_EVENT_TIMEOUT, now);
}

int
tp_process_button(struct tp_dispatch *tp,
		  const struct input_event *e,
		  uint64_t time)
{
	struct libinput *libinput = tp->device->base.seat->libinput;
	uint32_t mask = 1 << (e->code - BTN_LEFT);

	/* Ignore other buttons on clickpads */
	if (tp->buttons.is_clickpad && e->code != BTN_LEFT) {
		log_bug_kernel(libinput,
			       "received %s button event on a clickpad\n",
			       libevdev_event_code_get_name(EV_KEY, e->code));
		return 0;
	}

	if (e->value) {
		tp->buttons.state |= mask;
		tp->queued |= TOUCHPAD_EVENT_BUTTON_PRESS;
	} else {
		tp->buttons.state &= ~mask;
		tp->queued |= TOUCHPAD_EVENT_BUTTON_RELEASE;
	}

	return 0;
}

int
tp_init_buttons(struct tp_dispatch *tp,
		struct evdev_device *device)
{
	struct libinput *libinput = tp->device->base.seat->libinput;
	struct tp_touch *t;
	int width, height;
	double diagonal;
	const struct input_absinfo *absinfo_x, *absinfo_y;

	tp->buttons.is_clickpad = libevdev_has_property(device->evdev,
							INPUT_PROP_BUTTONPAD);
	tp->buttons.has_topbuttons = libevdev_has_property(device->evdev,
						        INPUT_PROP_TOPBUTTONPAD);

	if (libevdev_has_event_code(device->evdev, EV_KEY, BTN_MIDDLE) ||
	    libevdev_has_event_code(device->evdev, EV_KEY, BTN_RIGHT)) {
		if (tp->buttons.is_clickpad)
			log_bug_kernel(libinput,
				       "%s: clickpad advertising right button\n",
				       device->sysname);
	} else {
		if (!tp->buttons.is_clickpad)
			log_bug_kernel(libinput,
				       "%s: non clickpad without right button?\n",
				       device->sysname);
	}

	absinfo_x = device->abs.absinfo_x;
	absinfo_y = device->abs.absinfo_y;

	width = abs(absinfo_x->maximum - absinfo_x->minimum);
	height = abs(absinfo_y->maximum - absinfo_y->minimum);
	diagonal = sqrt(width*width + height*height);

	tp->buttons.motion_dist = diagonal * DEFAULT_BUTTON_MOTION_THRESHOLD;

	if (libevdev_get_id_vendor(device->evdev) == 0x5ac) /* Apple */
		tp->buttons.use_clickfinger = true;

	if (tp->buttons.is_clickpad && !tp->buttons.use_clickfinger) {
		int xoffset = absinfo_x->minimum,
		    yoffset = absinfo_y->minimum;
		int yres = absinfo_y->resolution;

		/* button height: 10mm or 15% of the touchpad height,
		   whichever is smaller */
		if (yres > 1 && (height * 0.15/yres) > 10) {
			tp->buttons.bottom_area.top_edge =
				absinfo_y->maximum - 10 * yres;
		} else {
			tp->buttons.bottom_area.top_edge = height * .85 + yoffset;
		}

		tp->buttons.bottom_area.rightbutton_left_edge = width/2 + xoffset;

		if (tp->buttons.has_topbuttons) {
			/* T440s has the top button line 5mm from the top,
			   make the buttons 6mm high */
			if (yres > 1) {
				tp->buttons.top_area.bottom_edge =
					yoffset + 6 * yres;
			} else {
				tp->buttons.top_area.bottom_edge = height * .08 + yoffset;
			}
			tp->buttons.top_area.rightbutton_left_edge = width * .58 + xoffset;
			tp->buttons.top_area.leftbutton_right_edge = width * .42 + xoffset;
		} else {
			tp->buttons.top_area.bottom_edge = INT_MIN;
		}
	} else {
		tp->buttons.bottom_area.top_edge = INT_MAX;
		tp->buttons.top_area.bottom_edge = INT_MIN;
	}

	tp_for_each_touch(tp, t) {
		t->button.state = BUTTON_STATE_NONE;
		libinput_timer_init(&t->button.timer,
				    tp->device->base.seat->libinput,
				    tp_button_handle_timeout, t);
	}

	return 0;
}

void
tp_destroy_buttons(struct tp_dispatch *tp)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t)
		libinput_timer_cancel(&t->button.timer);
}

static int
tp_post_clickfinger_buttons(struct tp_dispatch *tp, uint64_t time)
{
	uint32_t current, old, button;
	enum libinput_button_state state;

	current = tp->buttons.state;
	old = tp->buttons.old_state;

	if (current == old)
		return 0;

	if (current) {
		switch (tp->nfingers_down) {
		case 1: button = BTN_LEFT; break;
		case 2: button = BTN_RIGHT; break;
		case 3: button = BTN_MIDDLE; break;
		default:
			return 0;
		}
		tp->buttons.active = button;
		state = LIBINPUT_BUTTON_STATE_PRESSED;
	} else {
		button = tp->buttons.active;
		tp->buttons.active = 0;
		state = LIBINPUT_BUTTON_STATE_RELEASED;
	}

	if (button)
		pointer_notify_button(&tp->device->base,
				      time,
				      button,
				      state);
	return 1;
}

static int
tp_post_physical_buttons(struct tp_dispatch *tp, uint64_t time)
{
	uint32_t current, old, button;

	current = tp->buttons.state;
	old = tp->buttons.old_state;
	button = BTN_LEFT;

	while (current || old) {
		enum libinput_button_state state;

		if ((current & 0x1) ^ (old & 0x1)) {
			if (!!(current & 0x1))
				state = LIBINPUT_BUTTON_STATE_PRESSED;
			else
				state = LIBINPUT_BUTTON_STATE_RELEASED;

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
tp_post_softbutton_buttons(struct tp_dispatch *tp, uint64_t time)
{
	uint32_t current, old, button;
	enum libinput_button_state state;
	enum { AREA = 0x01, LEFT = 0x02, MIDDLE = 0x04, RIGHT = 0x08 };

	current = tp->buttons.state;
	old = tp->buttons.old_state;
	button = 0;

	if (!tp->buttons.click_pending && current == old)
		return 0;

	if (current) {
		struct tp_touch *t;

		tp_for_each_touch(tp, t) {
			switch (t->button.curr) {
			case BUTTON_EVENT_IN_AREA:
				button |= AREA;
				break;
			case BUTTON_EVENT_IN_BOTTOM_L:
			case BUTTON_EVENT_IN_TOP_L:
				button |= LEFT;
				break;
			case BUTTON_EVENT_IN_TOP_M:
				button |= MIDDLE;
				break;
			case BUTTON_EVENT_IN_BOTTOM_R:
			case BUTTON_EVENT_IN_TOP_R:
				button |= RIGHT;
				break;
			default:
				break;
			}
		}

		if (button == 0) {
			/* No touches, wait for a touch before processing */
			tp->buttons.click_pending = true;
			return 0;
		}

		if ((button & MIDDLE) || ((button & LEFT) && (button & RIGHT)))
			button = BTN_MIDDLE;
		else if (button & RIGHT)
			button = BTN_RIGHT;
		else
			button = BTN_LEFT;

		tp->buttons.active = button;
		state = LIBINPUT_BUTTON_STATE_PRESSED;
	} else {
		button = tp->buttons.active;
		tp->buttons.active = 0;
		state = LIBINPUT_BUTTON_STATE_RELEASED;
	}

	tp->buttons.click_pending = false;

	if (button)
		pointer_notify_button(&tp->device->base,
				      time,
				      button,
				      state);
	return 1;
}

int
tp_post_button_events(struct tp_dispatch *tp, uint64_t time)
{
	if (tp->buttons.is_clickpad) {
		if (tp->buttons.use_clickfinger)
			return tp_post_clickfinger_buttons(tp, time);
		else
			return tp_post_softbutton_buttons(tp, time);
	}

	return tp_post_physical_buttons(tp, time);
}

int
tp_button_touch_active(struct tp_dispatch *tp, struct tp_touch *t)
{
	return t->button.state == BUTTON_STATE_AREA;
}

bool
tp_button_is_inside_softbutton_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_top_button_area(tp, t) || is_inside_bottom_button_area(tp, t);
}
