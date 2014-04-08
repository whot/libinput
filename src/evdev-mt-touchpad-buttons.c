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
#include <time.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/timerfd.h>

#include "evdev-mt-touchpad.h"

#define DEFAULT_BUTTON_MOTION_THRESHOLD 0.02 /* in percent of size */
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
	CASE_RETURN_STRING(BUTTON_STATE_BOTTOM_NEW);
	CASE_RETURN_STRING(BUTTON_STATE_BOTTOM_TO_AREA);
	}
	return NULL;
}

static inline const char*
button_event_to_str(enum button_event event) {
	switch(event) {
	CASE_RETURN_STRING(BUTTON_EVENT_IN_BOTTOM_R);
	CASE_RETURN_STRING(BUTTON_EVENT_IN_BOTTOM_L);
	CASE_RETURN_STRING(BUTTON_EVENT_IN_AREA);
	CASE_RETURN_STRING(BUTTON_EVENT_UP);
	CASE_RETURN_STRING(BUTTON_EVENT_PRESS);
	CASE_RETURN_STRING(BUTTON_EVENT_RELEASE);
	CASE_RETURN_STRING(BUTTON_EVENT_TIMEOUT);
	}
	return NULL;
}

static inline bool
is_inside_button_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return t->y >= tp->buttons.area.top_edge;
}

static inline bool
is_inside_right_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_button_area(tp, t) &&
	       t->x > tp->buttons.area.rightbutton_left_edge;
}

static inline bool
is_inside_left_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_button_area(tp, t) &&
	       !is_inside_right_area(tp, t);
}

static void
tp_button_set_timer(struct tp_dispatch *tp, uint64_t timeout)
{
	struct itimerspec its;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = timeout / 1000;
	its.it_value.tv_nsec = (timeout % 1000) * 1000 * 1000;
	timerfd_settime(tp->buttons.timer_fd, TFD_TIMER_ABSTIME, &its, NULL);
}

static void
tp_button_set_enter_timer(struct tp_dispatch *tp, struct tp_touch *t)
{
	t->button.timeout = t->millis + DEFAULT_BUTTON_ENTER_TIMEOUT;
	tp_button_set_timer(tp, t->button.timeout);
}

static void
tp_button_set_leave_timer(struct tp_dispatch *tp, struct tp_touch *t)
{
	t->button.timeout = t->millis + DEFAULT_BUTTON_LEAVE_TIMEOUT;
	tp_button_set_timer(tp, t->button.timeout);
}

static void
tp_button_clear_timer(struct tp_dispatch *tp, struct tp_touch *t)
{
	t->button.timeout = 0;
}

/*
 * tp_button_set_state, change state and implement on-entry behavior
 * as described in the state machine diagram.
 */
static void
tp_button_set_state(struct tp_dispatch *tp, struct tp_touch *t,
		    enum button_state new_state, enum button_event event)
{
	tp_button_clear_timer(tp, t);

	switch (new_state) {
	case BUTTON_STATE_NONE:
		t->button.curr = 0;
		break;
	case BUTTON_STATE_AREA:
		t->button.curr = BUTTON_EVENT_IN_AREA;
		break;
	case BUTTON_STATE_BOTTOM:
		break;
	case BUTTON_STATE_BOTTOM_NEW:
		t->button.curr = event;
		tp_button_set_enter_timer(tp, t);
		break;
	case BUTTON_STATE_BOTTOM_TO_AREA:
		tp_button_set_leave_timer(tp, t);
		break;
	}
	t->button.state = new_state;
}

static void
tp_button_none_handle_event(struct tp_dispatch *tp,
			    struct tp_touch *t,
			    enum button_event event)
{
	switch (event) {
	case BUTTON_EVENT_IN_BOTTOM_R:
	case BUTTON_EVENT_IN_BOTTOM_L:
		tp_button_set_state(tp, t, BUTTON_STATE_BOTTOM_NEW, event);
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
			tp_button_set_state(tp, t, BUTTON_STATE_BOTTOM_NEW,
					    event);
		break;
	case BUTTON_EVENT_IN_AREA:
		tp_button_set_state(tp, t, BUTTON_STATE_BOTTOM_TO_AREA, event);
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
tp_button_bottom_new_handle_event(struct tp_dispatch *tp,
				struct tp_touch *t,
				enum button_event event)
{
	switch(event) {
	case BUTTON_EVENT_IN_BOTTOM_R:
	case BUTTON_EVENT_IN_BOTTOM_L:
		if (event != t->button.curr)
			tp_button_set_state(tp, t, BUTTON_STATE_BOTTOM_NEW,
					    event);
		break;
	case BUTTON_EVENT_IN_AREA:
		tp_button_set_state(tp, t, BUTTON_STATE_AREA, event);
		break;
	case BUTTON_EVENT_UP:
		tp_button_set_state(tp, t, BUTTON_STATE_NONE, event);
		break;
	case BUTTON_EVENT_PRESS:
		tp_button_set_state(tp, t, BUTTON_STATE_BOTTOM, event);
		break;
	case BUTTON_EVENT_RELEASE:
		break;
	case BUTTON_EVENT_TIMEOUT:
		tp_button_set_state(tp, t, BUTTON_STATE_BOTTOM, event);
		break;
	}
}

static void
tp_button_bottom_to_area_handle_event(struct tp_dispatch *tp,
				      struct tp_touch *t,
				      enum button_event event)
{
	switch(event) {
	case BUTTON_EVENT_IN_BOTTOM_R:
	case BUTTON_EVENT_IN_BOTTOM_L:
		if (event == t->button.curr)
			tp_button_set_state(tp, t, BUTTON_STATE_BOTTOM,
					    event);
		else
			tp_button_set_state(tp, t, BUTTON_STATE_BOTTOM_NEW,
					    event);
		break;
	case BUTTON_EVENT_IN_AREA:
		break;
	case BUTTON_EVENT_UP:
		tp_button_set_state(tp, t, BUTTON_STATE_NONE, event);
		break;
	case BUTTON_EVENT_PRESS:
	case BUTTON_EVENT_RELEASE:
		break;
	case BUTTON_EVENT_TIMEOUT:
		tp_button_set_state(tp, t, BUTTON_STATE_AREA, event);
		break;
	}
}

static void
tp_button_handle_event(struct tp_dispatch *tp,
		       struct tp_touch *t,
		       enum button_event event,
		       uint64_t time)
{
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
	case BUTTON_STATE_BOTTOM_NEW:
		tp_button_bottom_new_handle_event(tp, t, event);
		break;
	case BUTTON_STATE_BOTTOM_TO_AREA:
		tp_button_bottom_to_area_handle_event(tp, t, event);
		break;
	}

	if (current != t->button.state)
		log_debug("button state: from %s, event %s to %s\n",
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
#if 0
		if (t->fake) {
			continue;
		}
#endif

		if (t->state == TOUCH_END) {
			tp_button_handle_event(tp, t, BUTTON_EVENT_UP, time);
		} else if (t->dirty) {
			if (is_inside_right_area(tp, t))
				tp_button_handle_event(tp, t, BUTTON_EVENT_IN_BOTTOM_R, time);
			else if (is_inside_left_area(tp, t))
				tp_button_handle_event(tp, t, BUTTON_EVENT_IN_BOTTOM_L, time);
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
tp_button_handle_timeout(struct tp_dispatch *tp, uint64_t now)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		if (t->button.timeout != 0 && t->button.timeout <= now) {
			tp_button_clear_timer(tp, t);
			tp_button_handle_event(tp, t, BUTTON_EVENT_TIMEOUT, now);
		}
	}
}

int
tp_process_button(struct tp_dispatch *tp,
		  const struct input_event *e,
		  uint64_t time)
{
	uint32_t mask = 1 << (e->code - BTN_LEFT);

	/* Ignore other buttons on clickpads */
	if (tp->buttons.is_clickpad && e->code != BTN_LEFT) {
		log_bug("received non BTN_LEFT button event on a clickpad (kernel bug?)\n");
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

static void
tp_button_timeout_handler(void *data)
{
	struct tp_dispatch *tp = data;
	uint64_t expires;
	int len;
	struct timespec ts;
	uint64_t now;

	len = read(tp->buttons.timer_fd, &expires, sizeof expires);
	if (len != sizeof expires)
		/* This will only happen if the application made the fd
		 * non-blocking, but this function should only be called
		 * upon the timeout, so lets continue anyway. */
		log_error("timerfd read error: %s\n", strerror(errno));

	clock_gettime(CLOCK_MONOTONIC, &ts);
	now = ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000;

	tp_button_handle_timeout(tp, now);
}

int
tp_init_buttons(struct tp_dispatch *tp,
		struct evdev_device *device)
{
	int width, height;
	double diagonal;

	tp->buttons.is_clickpad = libevdev_has_property(device->evdev,
							INPUT_PROP_BUTTONPAD);

	if (libevdev_has_event_code(device->evdev, EV_KEY, BTN_MIDDLE) ||
	    libevdev_has_event_code(device->evdev, EV_KEY, BTN_RIGHT)) {
		if (tp->buttons.is_clickpad)
			log_bug("clickpad advertising right button (kernel bug?)\n");
	} else {
		if (!tp->buttons.is_clickpad)
			log_bug("non clickpad without right button (kernel bug)?\n");
	}

	width = abs(device->abs.max_x - device->abs.min_x);
	height = abs(device->abs.max_y - device->abs.min_y);
	diagonal = sqrt(width*width + height*height);

	tp->buttons.motion_dist = diagonal * DEFAULT_BUTTON_MOTION_THRESHOLD;

	if (libevdev_get_id_vendor(device->evdev) == 0x5ac) /* Apple */
		tp->buttons.use_clickfinger = true;

	if (tp->buttons.is_clickpad && !tp->buttons.use_clickfinger) {
		tp->buttons.area.top_edge = height * .8 + device->abs.min_y;
		tp->buttons.area.rightbutton_left_edge = width/2 + device->abs.min_x;
		tp->buttons.timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);

		if (tp->buttons.timer_fd == -1)
			return -1;

		tp->buttons.source =
			libinput_add_fd(tp->device->base.seat->libinput,
					tp->buttons.timer_fd,
					tp_button_timeout_handler,
					tp);
		if (tp->buttons.source == NULL)
			return -1;
	} else {
		tp->buttons.area.top_edge = INT_MAX;
	}

	return 0;
}

void
tp_destroy_buttons(struct tp_dispatch *tp)
{
	if (tp->buttons.source) {
		libinput_remove_source(tp->device->base.seat->libinput,
				       tp->buttons.source);
		tp->buttons.source = NULL;
	}
	if (tp->buttons.timer_fd > -1) {
		close(tp->buttons.timer_fd);
		tp->buttons.timer_fd = -1;
	}
}

static int
tp_post_clickfinger_buttons(struct tp_dispatch *tp, uint64_t time)
{
	uint32_t current, old, button;
	enum libinput_pointer_button_state state;

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
		state = LIBINPUT_POINTER_BUTTON_STATE_PRESSED;
	} else {
		button = tp->buttons.active;
		tp->buttons.active = 0;
		state = LIBINPUT_POINTER_BUTTON_STATE_RELEASED;
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
tp_post_softbutton_buttons(struct tp_dispatch *tp, uint64_t time)
{
	uint32_t current, old, button;
	enum libinput_pointer_button_state state;
	enum { AREA = 0x01, LEFT = 0x02, RIGHT = 0x04 };

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
				button |= LEFT;
				break;
			case BUTTON_EVENT_IN_BOTTOM_R:
				button |= RIGHT;
				break;
			default:
				break;
			}
		}

		switch (button) {
		case 0:
			/* No touches, wait for a touch before processing */
			tp->buttons.click_pending = true;
			return 0;
		case RIGHT:
		case RIGHT | AREA:
			/* Some touches in right, no touches in left */
			button = BTN_RIGHT;
			break;
		case LEFT | RIGHT:
		case LEFT | RIGHT | AREA:
			/* Some touches in left and some in right */
			button = BTN_MIDDLE;
			break;
		default:
			button = BTN_LEFT;
		}

		tp->buttons.active = button;
		state = LIBINPUT_POINTER_BUTTON_STATE_PRESSED;
	} else {
		button = tp->buttons.active;
		tp->buttons.active = 0;
		state = LIBINPUT_POINTER_BUTTON_STATE_RELEASED;
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
