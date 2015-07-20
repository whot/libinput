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
	return t->point.y >= tp->buttons.bottom_area.top_edge;
}

static inline bool
is_inside_bottom_right_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_bottom_button_area(tp, t) &&
	       t->point.x > tp->buttons.bottom_area.rightbutton_left_edge;
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
	return t->point.y <= tp->buttons.top_area.bottom_edge;
}

static inline bool
is_inside_top_right_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_top_button_area(tp, t) &&
	       t->point.x > tp->buttons.top_area.rightbutton_left_edge;
}

static inline bool
is_inside_top_left_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_top_button_area(tp, t) &&
	       t->point.x < tp->buttons.top_area.leftbutton_right_edge;
}

static inline bool
is_inside_top_middle_area(struct tp_dispatch *tp, struct tp_touch *t)
{
	return is_inside_top_button_area(tp, t) &&
	       t->point.x >= tp->buttons.top_area.leftbutton_right_edge &&
	       t->point.x <= tp->buttons.top_area.rightbutton_left_edge;
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
tp_button_set_state(struct tp_dispatch *tp,
		    struct tp_touch *t,
		    enum button_state new_state,
		    enum button_event event)
{
	libinput_timer_cancel(&t->button.timer);

	t->button.state = new_state;

	switch (t->button.state) {
	case BUTTON_STATE_NONE:
		t->button.curr = 0;
		break;
	case BUTTON_STATE_AREA:
		t->button.curr = BUTTON_EVENT_IN_AREA;
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
			tp_button_set_state(tp,
					    t,
					    BUTTON_STATE_BOTTOM,
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
			tp_button_set_state(tp,
					    t,
					    BUTTON_STATE_TOP_NEW,
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
			tp_button_set_state(tp,
					    t,
					    BUTTON_STATE_TOP_NEW,
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
			tp_button_set_state(tp,
					    t,
					    BUTTON_STATE_TOP,
					    event);
		else
			tp_button_set_state(tp,
					    t,
					    BUTTON_STATE_TOP_NEW,
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
	struct libinput *libinput = tp_libinput_context(tp);
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
			enum button_event event;

			if (is_inside_bottom_right_area(tp, t))
				event = BUTTON_EVENT_IN_BOTTOM_R;
			else if (is_inside_bottom_left_area(tp, t))
				event = BUTTON_EVENT_IN_BOTTOM_L;
			else if (is_inside_top_right_area(tp, t))
				event = BUTTON_EVENT_IN_TOP_R;
			else if (is_inside_top_middle_area(tp, t))
				event = BUTTON_EVENT_IN_TOP_M;
			else if (is_inside_top_left_area(tp, t))
				event = BUTTON_EVENT_IN_TOP_L;
			else
				event = BUTTON_EVENT_IN_AREA;

			tp_button_handle_event(tp, t, event, time);
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
	struct libinput *libinput = tp_libinput_context(tp);
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

void
tp_release_all_buttons(struct tp_dispatch *tp,
		       uint64_t time)
{
	if (tp->buttons.state) {
		tp->buttons.state = 0;
		tp->queued |= TOUCHPAD_EVENT_BUTTON_RELEASE;
	}
}

static void
tp_init_softbuttons(struct tp_dispatch *tp,
		    struct evdev_device *device)
{
	int width, height;
	const struct input_absinfo *absinfo_x, *absinfo_y;
	int xoffset, yoffset;
	int yres;

	absinfo_x = device->abs.absinfo_x;
	absinfo_y = device->abs.absinfo_y;

	xoffset = absinfo_x->minimum,
	yoffset = absinfo_y->minimum,
	yres = absinfo_y->resolution;
	width = device->abs.dimensions.x;
	height = device->abs.dimensions.y;

	/* button height: 10mm or 15% or the touchpad height,
	   whichever is smaller */
	if ((height * 0.15)/yres > 10) {
		tp->buttons.bottom_area.top_edge =
			absinfo_y->maximum - 10 * yres;
	} else {
		tp->buttons.bottom_area.top_edge = height * .85 + yoffset;
	}
	tp->buttons.bottom_area.rightbutton_left_edge = width/2 + xoffset;
}

void
tp_init_top_softbuttons(struct tp_dispatch *tp,
			struct evdev_device *device,
			double topbutton_size_mult)
{
	int width;
	const struct input_absinfo *absinfo_x, *absinfo_y;
	int xoffset, yoffset;
	int yres;

	absinfo_x = device->abs.absinfo_x;
	absinfo_y = device->abs.absinfo_y;

	xoffset = absinfo_x->minimum,
	yoffset = absinfo_y->minimum;
	yres = absinfo_y->resolution;
	width = device->abs.dimensions.x;

	if (tp->buttons.has_topbuttons) {
		/* T440s has the top button line 5mm from the top, event
		   analysis has shown events to start down to ~10mm from the
		   top - which maps to 15%.  We allow the caller to enlarge the
		   area using a multiplier for the touchpad disabled case. */
		double topsize_mm = 10 * topbutton_size_mult;

		tp->buttons.top_area.bottom_edge = yoffset + topsize_mm * yres;
		tp->buttons.top_area.rightbutton_left_edge = width * .58 + xoffset;
		tp->buttons.top_area.leftbutton_right_edge = width * .42 + xoffset;
	} else {
		tp->buttons.top_area.bottom_edge = INT_MIN;
	}
}

static inline uint32_t
tp_button_config_click_get_methods(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;
	uint32_t methods = LIBINPUT_CONFIG_CLICK_METHOD_NONE;

	if (tp->buttons.is_clickpad) {
		methods |= LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
		if (tp->has_mt)
			methods |= LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
	}

	return methods;
}

static void
tp_switch_click_method(struct tp_dispatch *tp)
{
	/*
	 * All we need to do when switching click methods is to change the
	 * bottom_area.top_edge so that when in clickfinger mode the bottom
	 * touchpad area is not dead wrt finger movement starting there.
	 *
	 * We do not need to take any state into account, fingers which are
	 * already down will simply keep the state / area they have assigned
	 * until they are released, and the post_button_events path is state
	 * agnostic.
	 */

	switch (tp->buttons.click_method) {
	case LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS:
		tp_init_softbuttons(tp, tp->device);
		break;
	case LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER:
	case LIBINPUT_CONFIG_CLICK_METHOD_NONE:
		tp->buttons.bottom_area.top_edge = INT_MAX;
		break;
	}
}

static enum libinput_config_status
tp_button_config_click_set_method(struct libinput_device *device,
				  enum libinput_config_click_method method)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	tp->buttons.click_method = method;
	tp_switch_click_method(tp);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_click_method
tp_button_config_click_get_method(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	return tp->buttons.click_method;
}

static enum libinput_config_click_method
tp_click_get_default_method(struct tp_dispatch *tp)
{
	struct evdev_device *device = tp->device;
	uint32_t clickfinger_models = EVDEV_MODEL_CHROMEBOOK |
				      EVDEV_MODEL_SYSTEM76_BONOBO |
				      EVDEV_MODEL_SYSTEM76_GALAGO |
				      EVDEV_MODEL_SYSTEM76_KUDU |
				      EVDEV_MODEL_CLEVO_W740SU;

	if (!tp->buttons.is_clickpad)
		return LIBINPUT_CONFIG_CLICK_METHOD_NONE;
	else if (libevdev_get_id_vendor(tp->device->evdev) == VENDOR_ID_APPLE)
		return LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;

	if (device->model_flags & clickfinger_models)
		return LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;

	return LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
}

static enum libinput_config_click_method
tp_button_config_click_get_default_method(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tp_dispatch *tp = (struct tp_dispatch*)evdev->dispatch;

	return tp_click_get_default_method(tp);
}

static inline void
tp_init_middlebutton_emulation(struct tp_dispatch *tp,
			       struct evdev_device *device)
{
	bool enable_by_default,
	     want_config_option;

	if (tp->buttons.is_clickpad)
		return;

	/* init middle button emulation on non-clickpads, but only if we
	 * don't have a middle button. Exception: ALPS touchpads don't know
	 * if they have a middle button, so we always want the option there
	 * and enabled by default.
	 */
	if (!libevdev_has_event_code(device->evdev, EV_KEY, BTN_MIDDLE)) {
		enable_by_default = true;
		want_config_option = false;
	} else if (device->model_flags & EVDEV_MODEL_ALPS_TOUCHPAD) {
		enable_by_default = true;
		want_config_option = true;
	} else
		return;

	evdev_init_middlebutton(tp->device,
				enable_by_default,
				want_config_option);
}

int
tp_init_buttons(struct tp_dispatch *tp,
		struct evdev_device *device)
{
	struct libinput *libinput = tp_libinput_context(tp);
	struct tp_touch *t;
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
				       device->devname);
	} else if (libevdev_has_event_code(device->evdev, EV_KEY, BTN_LEFT) &&
		   !tp->buttons.is_clickpad) {
			log_bug_kernel(libinput,
				       "%s: non clickpad without right button?\n",
				       device->devname);
	}

	absinfo_x = device->abs.absinfo_x;
	absinfo_y = device->abs.absinfo_y;

	/* pinned-finger motion threshold, see tp_unpin_finger. */
	tp->buttons.motion_dist.x_scale_coeff = 1.0/absinfo_x->resolution;
	tp->buttons.motion_dist.y_scale_coeff = 1.0/absinfo_y->resolution;

	tp->buttons.config_method.get_methods = tp_button_config_click_get_methods;
	tp->buttons.config_method.set_method = tp_button_config_click_set_method;
	tp->buttons.config_method.get_method = tp_button_config_click_get_method;
	tp->buttons.config_method.get_default_method = tp_button_config_click_get_default_method;
	tp->device->base.config.click_method = &tp->buttons.config_method;

	tp->buttons.click_method = tp_click_get_default_method(tp);
	tp_switch_click_method(tp);

	tp_init_top_softbuttons(tp, device, 1.0);

	tp_init_middlebutton_emulation(tp, device);

	tp_for_each_touch(tp, t) {
		t->button.state = BUTTON_STATE_NONE;
		libinput_timer_init(&t->button.timer,
				    tp_libinput_context(tp),
				    tp_button_handle_timeout, t);
	}

	return 0;
}

void
tp_remove_buttons(struct tp_dispatch *tp)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t)
		libinput_timer_cancel(&t->button.timer);
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
			uint32_t b;

			if (!!(current & 0x1))
				state = LIBINPUT_BUTTON_STATE_PRESSED;
			else
				state = LIBINPUT_BUTTON_STATE_RELEASED;

			b = evdev_to_left_handed(tp->device, button);
			evdev_pointer_notify_physical_button(tp->device,
							     time,
							     b,
							     state);
		}

		button++;
		current >>= 1;
		old >>= 1;
	}

	return 0;
}

static inline int
tp_check_clickfinger_distance(struct tp_dispatch *tp,
			      struct tp_touch *t1,
			      struct tp_touch *t2)
{
	double x, y;
	int within_distance = 0;
	int xres, yres;
	int bottom_threshold;

	if (!t1 || !t2)
		return 0;

	if (t1->is_thumb || t2->is_thumb)
		return 0;

	x = abs(t1->point.x - t2->point.x);
	y = abs(t1->point.y - t2->point.y);

	xres = tp->device->abs.absinfo_x->resolution;
	yres = tp->device->abs.absinfo_y->resolution;
	x /= xres;
	y /= yres;

	/* maximum horiz spread is 40mm horiz, 30mm vert, anything wider
	 * than that is probably a gesture. */
	if (x > 40 || y > 30)
		goto out;

	within_distance = 1;

	/* if y spread is <= 20mm, they're definitely together. */
	if (y <= 20)
		goto out;

	/* if they're vertically spread between 20-40mm, they're not
	 * together if:
	 * - the touchpad's vertical size is >50mm, anything smaller is
	 *   unlikely to have a thumb resting on it
	 * - and one of the touches is in the bottom 20mm of the touchpad
	 *   and the other one isn't
	 */

	if (tp->device->abs.dimensions.y/yres < 50)
		goto out;

	bottom_threshold = tp->device->abs.absinfo_y->maximum - 20 * yres;
	if ((t1->point.y > bottom_threshold) !=
		    (t2->point.y > bottom_threshold))
		within_distance = 0;

out:
	return within_distance;
}

static uint32_t
tp_clickfinger_set_button(struct tp_dispatch *tp)
{
	uint32_t button;
	unsigned int nfingers = tp->nfingers_down;
	struct tp_touch *t;
	struct tp_touch *first = NULL,
			*second = NULL,
			*third = NULL;
	uint32_t close_touches = 0;

	if (nfingers < 2 || nfingers > 3)
		goto out;

	/* two or three fingers down on the touchpad. Check for distance
	 * between the fingers. */
	tp_for_each_touch(tp, t) {
		if (t->state != TOUCH_BEGIN && t->state != TOUCH_UPDATE)
			continue;

		if (!first)
			first = t;
		else if (!second)
			second = t;
		else if (!third) {
			third = t;
			break;
		}
	}

	if (!first || !second) {
		nfingers = 1;
		goto out;
	}

	close_touches |= tp_check_clickfinger_distance(tp, first, second) << 0;
	close_touches |= tp_check_clickfinger_distance(tp, second, third) << 1;
	close_touches |= tp_check_clickfinger_distance(tp, first, third) << 2;

	switch(__builtin_popcount(close_touches)) {
	case 0: nfingers = 1; break;
	case 1: nfingers = 2; break;
	default: nfingers = 3; break;
	}

out:
	switch (nfingers) {
	case 0:
	case 1: button = BTN_LEFT; break;
	case 2: button = BTN_RIGHT; break;
	case 3: button = BTN_MIDDLE; break;
	default:
		button = 0;
		break;
	}

	return button;
}

static int
tp_notify_clickpadbutton(struct tp_dispatch *tp,
			 uint64_t time,
			 uint32_t button,
			 uint32_t is_topbutton,
			 enum libinput_button_state state)
{
	/* If we've a trackpoint, send top buttons through the trackpoint */
	if (is_topbutton && tp->buttons.trackpoint) {
		struct evdev_dispatch *dispatch = tp->buttons.trackpoint->dispatch;
		struct input_event event;

		event.time.tv_sec = time/1000;
		event.time.tv_usec = (time % 1000) * 1000;
		event.type = EV_KEY;
		event.code = button;
		event.value = (state == LIBINPUT_BUTTON_STATE_PRESSED) ? 1 : 0;
		dispatch->interface->process(dispatch,
					     tp->buttons.trackpoint,
					     &event,
					     time);
		return 1;
	}

	/* Ignore button events not for the trackpoint while suspended */
	if (tp->device->suspended)
		return 0;

	/* A button click always terminates edge scrolling, even if we
	 * don't end up sending a button event. */
	tp_edge_scroll_stop_events(tp, time);

	/*
	 * If the user has requested clickfinger replace the button chosen
	 * by the softbutton code with one based on the number of fingers.
	 */
	if (tp->buttons.click_method == LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER &&
	    state == LIBINPUT_BUTTON_STATE_PRESSED) {
		button = tp_clickfinger_set_button(tp);
		tp->buttons.active = button;

		if (!button)
			return 0;
	}

	evdev_pointer_notify_button(tp->device, time, button, state);
	return 1;
}

static int
tp_post_clickpadbutton_buttons(struct tp_dispatch *tp, uint64_t time)
{
	uint32_t current, old, button, is_top;
	enum libinput_button_state state;
	enum { AREA = 0x01, LEFT = 0x02, MIDDLE = 0x04, RIGHT = 0x08 };

	current = tp->buttons.state;
	old = tp->buttons.old_state;
	is_top = 0;

	if (!tp->buttons.click_pending && current == old)
		return 0;

	if (current) {
		struct tp_touch *t;
		uint32_t area = 0;

		tp_for_each_touch(tp, t) {
			switch (t->button.curr) {
			case BUTTON_EVENT_IN_AREA:
				area |= AREA;
				break;
			case BUTTON_EVENT_IN_TOP_L:
				is_top = 1;
				/* fallthrough */
			case BUTTON_EVENT_IN_BOTTOM_L:
				area |= LEFT;
				break;
			case BUTTON_EVENT_IN_TOP_M:
				is_top = 1;
				area |= MIDDLE;
				break;
			case BUTTON_EVENT_IN_TOP_R:
				is_top = 1;
				/* fallthrough */
			case BUTTON_EVENT_IN_BOTTOM_R:
				area |= RIGHT;
				break;
			default:
				break;
			}
		}

		if (area == 0 &&
		    tp->buttons.click_method != LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER) {
			/* No touches, wait for a touch before processing */
			tp->buttons.click_pending = true;
			return 0;
		}

		if ((area & MIDDLE) || ((area & LEFT) && (area & RIGHT)))
			button = evdev_to_left_handed(tp->device, BTN_MIDDLE);
		else if (area & RIGHT)
			button = evdev_to_left_handed(tp->device, BTN_RIGHT);
		else if (area & LEFT)
			button = evdev_to_left_handed(tp->device, BTN_LEFT);
		else /* main or no area (for clickfinger) is always BTN_LEFT */
			button = BTN_LEFT;

		tp->buttons.active = button;
		tp->buttons.active_is_topbutton = is_top;
		state = LIBINPUT_BUTTON_STATE_PRESSED;
	} else {
		button = tp->buttons.active;
		is_top = tp->buttons.active_is_topbutton;
		tp->buttons.active = 0;
		tp->buttons.active_is_topbutton = 0;
		state = LIBINPUT_BUTTON_STATE_RELEASED;
	}

	tp->buttons.click_pending = false;

	if (button)
		return tp_notify_clickpadbutton(tp,
						time,
						button,
						is_top,
						state);
	return 0;
}

int
tp_post_button_events(struct tp_dispatch *tp, uint64_t time)
{
	if (tp->buttons.is_clickpad)
		return tp_post_clickpadbutton_buttons(tp, time);
	else
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
	return is_inside_top_button_area(tp, t) ||
	       is_inside_bottom_button_area(tp, t);
}
