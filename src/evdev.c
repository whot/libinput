/*
 * Copyright © 2010 Intel Corporation
 * Copyright © 2013 Jonas Ådahl
 * Copyright © 2013-2017 Red Hat, Inc.
 * Copyright © 2017 James Ye <jye836@gmail.com>
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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "linux/input.h"
#include <unistd.h>
#include <fcntl.h>
#include <mtdev-plumbing.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include "libinput.h"
#include "evdev.h"
#include "filter.h"
#include "libinput-private.h"

#if HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

#define DEFAULT_WHEEL_CLICK_ANGLE 15
#define DEFAULT_BUTTON_SCROLL_TIMEOUT ms2us(200)
#define	DEBOUNCE_TIME ms2us(12)

enum evdev_key_type {
	EVDEV_KEY_TYPE_NONE,
	EVDEV_KEY_TYPE_KEY,
	EVDEV_KEY_TYPE_BUTTON,
};

enum evdev_device_udev_tags {
        EVDEV_UDEV_TAG_INPUT = (1 << 0),
        EVDEV_UDEV_TAG_KEYBOARD = (1 << 1),
        EVDEV_UDEV_TAG_MOUSE = (1 << 2),
        EVDEV_UDEV_TAG_TOUCHPAD = (1 << 3),
        EVDEV_UDEV_TAG_TOUCHSCREEN = (1 << 4),
        EVDEV_UDEV_TAG_TABLET = (1 << 5),
        EVDEV_UDEV_TAG_JOYSTICK = (1 << 6),
        EVDEV_UDEV_TAG_ACCELEROMETER = (1 << 7),
        EVDEV_UDEV_TAG_TABLET_PAD = (1 << 8),
        EVDEV_UDEV_TAG_POINTINGSTICK = (1 << 9),
        EVDEV_UDEV_TAG_TRACKBALL = (1 << 10),
        EVDEV_UDEV_TAG_SWITCH = (1 << 11),
};

struct evdev_udev_tag_match {
	const char *name;
	enum evdev_device_udev_tags tag;
};

static const struct evdev_udev_tag_match evdev_udev_tag_matches[] = {
	{"ID_INPUT",			EVDEV_UDEV_TAG_INPUT},
	{"ID_INPUT_KEYBOARD",		EVDEV_UDEV_TAG_KEYBOARD},
	{"ID_INPUT_KEY",		EVDEV_UDEV_TAG_KEYBOARD},
	{"ID_INPUT_MOUSE",		EVDEV_UDEV_TAG_MOUSE},
	{"ID_INPUT_TOUCHPAD",		EVDEV_UDEV_TAG_TOUCHPAD},
	{"ID_INPUT_TOUCHSCREEN",	EVDEV_UDEV_TAG_TOUCHSCREEN},
	{"ID_INPUT_TABLET",		EVDEV_UDEV_TAG_TABLET},
	{"ID_INPUT_TABLET_PAD",		EVDEV_UDEV_TAG_TABLET_PAD},
	{"ID_INPUT_JOYSTICK",		EVDEV_UDEV_TAG_JOYSTICK},
	{"ID_INPUT_ACCELEROMETER",	EVDEV_UDEV_TAG_ACCELEROMETER},
	{"ID_INPUT_POINTINGSTICK",	EVDEV_UDEV_TAG_POINTINGSTICK},
	{"ID_INPUT_TRACKBALL",		EVDEV_UDEV_TAG_TRACKBALL},
	{"ID_INPUT_SWITCH",		EVDEV_UDEV_TAG_SWITCH},
};

static inline bool
parse_udev_flag(struct evdev_device *device,
		struct udev_device *udev_device,
		const char *property)
{
	const char *val;

	val = udev_device_get_property_value(udev_device, property);
	if (!val)
		return false;

	if (streq(val, "1"))
		return true;
	if (!streq(val, "0"))
		evdev_log_error(device,
				"property %s has invalid value '%s'\n",
				property,
				val);
	return false;
}

static void
hw_set_key_down(struct fallback_dispatch *dispatch, int code, int pressed)
{
	long_set_bit_state(dispatch->hw_key_mask, code, pressed);
}

static bool
hw_is_key_down(struct fallback_dispatch *dispatch, int code)
{
	return long_bit_is_set(dispatch->hw_key_mask, code);
}

static int
get_key_down_count(struct evdev_device *device, int code)
{
	return device->key_count[code];
}

static int
update_key_down_count(struct evdev_device *device, int code, int pressed)
{
	int key_count;
	assert(code >= 0 && code < KEY_CNT);

	if (pressed) {
		key_count = ++device->key_count[code];
	} else {
		assert(device->key_count[code] > 0);
		key_count = --device->key_count[code];
	}

	if (key_count > 32) {
		evdev_log_bug_libinput(device,
				       "key count for %s reached abnormal values\n",
				       libevdev_event_code_get_name(EV_KEY, code));
	}

	return key_count;
}

static void
fallback_keyboard_notify_key(struct fallback_dispatch *dispatch,
			     struct evdev_device *device,
			     uint64_t time,
			     int key,
			     enum libinput_key_state state)
{
	int down_count;

	down_count = update_key_down_count(device, key, state);

	if ((state == LIBINPUT_KEY_STATE_PRESSED && down_count == 1) ||
	    (state == LIBINPUT_KEY_STATE_RELEASED && down_count == 0))
		keyboard_notify_key(&device->base, time, key, state);
}

static void
fallback_lid_notify_toggle(struct fallback_dispatch *dispatch,
			   struct evdev_device *device,
			   uint64_t time)
{
	if (dispatch->lid.is_closed ^ dispatch->lid.is_closed_client_state) {
		switch_notify_toggle(&device->base,
				     time,
				     LIBINPUT_SWITCH_LID,
				     dispatch->lid.is_closed);
		dispatch->lid.is_closed_client_state = dispatch->lid.is_closed;
	}
}

enum libinput_switch_state
evdev_device_switch_get_state(struct evdev_device *device,
			      enum libinput_switch sw)
{
	struct fallback_dispatch *dispatch = fallback_dispatch(device->dispatch);

	switch (sw) {
	case LIBINPUT_SWITCH_TABLET_MODE:
		break;
	default:
		/* Internal function only, so we can abort here */
		abort();
	}

	return dispatch->tablet_mode.state ?
			LIBINPUT_SWITCH_STATE_ON :
			LIBINPUT_SWITCH_STATE_OFF;
}

void
evdev_pointer_notify_physical_button(struct evdev_device *device,
				     uint64_t time,
				     int button,
				     enum libinput_button_state state)
{
	if (evdev_middlebutton_filter_button(device,
					     time,
					     button,
					     state))
			return;

	evdev_pointer_notify_button(device,
				    time,
				    (unsigned int)button,
				    state);
}

static void
evdev_pointer_post_button(struct evdev_device *device,
			  uint64_t time,
			  unsigned int button,
			  enum libinput_button_state state)
{
	int down_count;

	down_count = update_key_down_count(device, button, state);

	if ((state == LIBINPUT_BUTTON_STATE_PRESSED && down_count == 1) ||
	    (state == LIBINPUT_BUTTON_STATE_RELEASED && down_count == 0)) {
		pointer_notify_button(&device->base, time, button, state);

		if (state == LIBINPUT_BUTTON_STATE_RELEASED) {
			if (device->left_handed.change_to_enabled)
				device->left_handed.change_to_enabled(device);

			if (device->scroll.change_scroll_method)
				device->scroll.change_scroll_method(device);
		}
	}

}

static void
evdev_button_scroll_timeout(uint64_t time, void *data)
{
	struct evdev_device *device = data;

	device->scroll.button_scroll_state = BUTTONSCROLL_READY;
}

static void
evdev_button_scroll_button(struct evdev_device *device,
			   uint64_t time, int is_press)
{
	if (is_press) {
		enum timer_flags flags = TIMER_FLAG_NONE;

		device->scroll.button_scroll_state = BUTTONSCROLL_BUTTON_DOWN;

		/* Special case: if middle button emulation is enabled and
		 * our scroll button is the left or right button, we only
		 * get here *after* the middle button timeout has expired
		 * for that button press. The time passed is the button-down
		 * time though (which is in the past), so we have to allow
		 * for a negative timer to be set.
		 */
		if (device->middlebutton.enabled &&
		    (device->scroll.button == BTN_LEFT ||
		     device->scroll.button == BTN_RIGHT)) {
			flags = TIMER_FLAG_ALLOW_NEGATIVE;
		}

		libinput_timer_set_flags(&device->scroll.timer,
					 time + DEFAULT_BUTTON_SCROLL_TIMEOUT,
					 flags);
		device->scroll.button_down_time = time;
		evdev_log_debug(device, "btnscroll: down\n");
	} else {
		libinput_timer_cancel(&device->scroll.timer);
		switch(device->scroll.button_scroll_state) {
		case BUTTONSCROLL_IDLE:
			evdev_log_bug_libinput(device,
				       "invalid state IDLE for button up\n");
			break;
		case BUTTONSCROLL_BUTTON_DOWN:
		case BUTTONSCROLL_READY:
			evdev_log_debug(device, "btnscroll: cancel\n");

			/* If the button is released quickly enough or
			 * without scroll events, emit the
			 * button press/release events. */
			evdev_pointer_post_button(device,
					device->scroll.button_down_time,
					device->scroll.button,
					LIBINPUT_BUTTON_STATE_PRESSED);
			evdev_pointer_post_button(device, time,
					device->scroll.button,
					LIBINPUT_BUTTON_STATE_RELEASED);
			break;
		case BUTTONSCROLL_SCROLLING:
			evdev_log_debug(device, "btnscroll: up\n");
			evdev_stop_scroll(device, time,
					  LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS);
			break;
		}

		device->scroll.button_scroll_state = BUTTONSCROLL_IDLE;
	}
}

void
evdev_pointer_notify_button(struct evdev_device *device,
			    uint64_t time,
			    unsigned int button,
			    enum libinput_button_state state)
{
	if (device->scroll.method == LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN &&
	    button == device->scroll.button) {
		evdev_button_scroll_button(device, time, state);
		return;
	}

	evdev_pointer_post_button(device, time, button, state);
}

void
evdev_device_led_update(struct evdev_device *device, enum libinput_led leds)
{
	static const struct {
		enum libinput_led libinput;
		int evdev;
	} map[] = {
		{ LIBINPUT_LED_NUM_LOCK, LED_NUML },
		{ LIBINPUT_LED_CAPS_LOCK, LED_CAPSL },
		{ LIBINPUT_LED_SCROLL_LOCK, LED_SCROLLL },
	};
	struct input_event ev[ARRAY_LENGTH(map) + 1];
	unsigned int i;

	if (!(device->seat_caps & EVDEV_DEVICE_KEYBOARD))
		return;

	memset(ev, 0, sizeof(ev));
	for (i = 0; i < ARRAY_LENGTH(map); i++) {
		ev[i].type = EV_LED;
		ev[i].code = map[i].evdev;
		ev[i].value = !!(leds & map[i].libinput);
	}
	ev[i].type = EV_SYN;
	ev[i].code = SYN_REPORT;

	i = write(device->fd, ev, sizeof ev);
	(void)i; /* no, we really don't care about the return value */
}

void
evdev_transform_absolute(struct evdev_device *device,
			 struct device_coords *point)
{
	if (!device->abs.apply_calibration)
		return;

	matrix_mult_vec(&device->abs.calibration, &point->x, &point->y);
}

void
evdev_transform_relative(struct evdev_device *device,
			 struct device_coords *point)
{
	struct matrix rel_matrix;

	if (!device->abs.apply_calibration)
		return;

	matrix_to_relative(&rel_matrix, &device->abs.calibration);
	matrix_mult_vec(&rel_matrix, &point->x, &point->y);
}

static inline double
scale_axis(const struct input_absinfo *absinfo, double val, double to_range)
{
	return (val - absinfo->minimum) * to_range /
		(absinfo->maximum - absinfo->minimum + 1);
}

double
evdev_device_transform_x(struct evdev_device *device,
			 double x,
			 uint32_t width)
{
	return scale_axis(device->abs.absinfo_x, x, width);
}

double
evdev_device_transform_y(struct evdev_device *device,
			 double y,
			 uint32_t height)
{
	return scale_axis(device->abs.absinfo_y, y, height);
}

static inline void
normalize_delta(struct evdev_device *device,
		const struct device_coords *delta,
		struct normalized_coords *normalized)
{
	normalized->x = delta->x * DEFAULT_MOUSE_DPI / (double)device->dpi;
	normalized->y = delta->y * DEFAULT_MOUSE_DPI / (double)device->dpi;
}

static inline bool
evdev_post_trackpoint_scroll(struct evdev_device *device,
			     struct normalized_coords unaccel,
			     uint64_t time)
{
	if (device->scroll.method != LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN)
		return false;

	switch(device->scroll.button_scroll_state) {
	case BUTTONSCROLL_IDLE:
		return false;
	case BUTTONSCROLL_BUTTON_DOWN:
		/* if the button is down but scroll is not active, we're within the
		   timeout where swallow motion events but don't post scroll buttons */
		evdev_log_debug(device, "btnscroll: discarding\n");
		return true;
	case BUTTONSCROLL_READY:
		device->scroll.button_scroll_state = BUTTONSCROLL_SCROLLING;
		/* fallthrough */
	case BUTTONSCROLL_SCROLLING:
		evdev_post_scroll(device, time,
				  LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS,
				  &unaccel);
		return true;
	}

	assert(!"invalid scroll button state");
}

static inline bool
fallback_filter_defuzz_touch(struct fallback_dispatch *dispatch,
			     struct evdev_device *device,
			     struct mt_slot *slot)
{
	struct device_coords point;

	if (!dispatch->mt.want_hysteresis)
		return false;

	point.x = evdev_hysteresis(slot->point.x,
				   slot->hysteresis_center.x,
				   dispatch->mt.hysteresis_margin.x);
	point.y = evdev_hysteresis(slot->point.y,
				   slot->hysteresis_center.y,
				   dispatch->mt.hysteresis_margin.y);

	slot->hysteresis_center = slot->point;
	if (point.x == slot->point.x && point.y == slot->point.y)
		return true;

	slot->point = point;

	return false;
}

static inline void
fallback_rotate_relative(struct fallback_dispatch *dispatch,
			 struct evdev_device *device)
{
	struct device_coords rel = dispatch->rel;

	if (!device->base.config.rotation)
		return;

	/* loss of precision for non-90 degrees, but we only support 90 deg
	 * right now anyway */
	matrix_mult_vec(&dispatch->rotation.matrix, &rel.x, &rel.y);

	dispatch->rel = rel;
}

static void
fallback_flush_relative_motion(struct fallback_dispatch *dispatch,
			       struct evdev_device *device,
			       uint64_t time)
{
	struct libinput_device *base = &device->base;
	struct normalized_coords accel, unaccel;
	struct device_float_coords raw;

	if (!(device->seat_caps & EVDEV_DEVICE_POINTER))
		return;

	fallback_rotate_relative(dispatch, device);

	normalize_delta(device, &dispatch->rel, &unaccel);
	raw.x = dispatch->rel.x;
	raw.y = dispatch->rel.y;
	dispatch->rel.x = 0;
	dispatch->rel.y = 0;

	/* Use unaccelerated deltas for pointing stick scroll */
	if (evdev_post_trackpoint_scroll(device, unaccel, time))
		return;

	if (device->pointer.filter) {
		/* Apply pointer acceleration. */
		accel = filter_dispatch(device->pointer.filter,
					&raw,
					device,
					time);
	} else {
		evdev_log_bug_libinput(device,
				       "accel filter missing\n");
		accel = unaccel;
	}

	if (normalized_is_zero(accel) && normalized_is_zero(unaccel))
		return;

	pointer_notify_motion(base, time, &accel, &raw);
}

static void
fallback_flush_absolute_motion(struct fallback_dispatch *dispatch,
			       struct evdev_device *device,
			       uint64_t time)
{
	struct libinput_device *base = &device->base;
	struct device_coords point;

	if (!(device->seat_caps & EVDEV_DEVICE_POINTER))
		return;

	point = dispatch->abs.point;
	evdev_transform_absolute(device, &point);

	pointer_notify_motion_absolute(base, time, &point);
}

static bool
fallback_flush_mt_down(struct fallback_dispatch *dispatch,
		       struct evdev_device *device,
		       int slot_idx,
		       uint64_t time)
{
	struct libinput_device *base = &device->base;
	struct libinput_seat *seat = base->seat;
	struct device_coords point;
	struct mt_slot *slot;
	int seat_slot;

	if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
		return false;

	slot = &dispatch->mt.slots[slot_idx];
	if (slot->seat_slot != -1) {
		evdev_log_bug_kernel(device,
				     "driver sent multiple touch down for the same slot");
		return false;
	}

	seat_slot = ffs(~seat->slot_map) - 1;
	slot->seat_slot = seat_slot;

	if (seat_slot == -1)
		return false;

	seat->slot_map |= 1 << seat_slot;
	point = slot->point;
	slot->hysteresis_center = point;
	evdev_transform_absolute(device, &point);

	touch_notify_touch_down(base, time, slot_idx, seat_slot,
				&point);

	return true;
}

static bool
fallback_flush_mt_motion(struct fallback_dispatch *dispatch,
			 struct evdev_device *device,
			 int slot_idx,
			 uint64_t time)
{
	struct libinput_device *base = &device->base;
	struct device_coords point;
	struct mt_slot *slot;
	int seat_slot;

	if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
		return false;

	slot = &dispatch->mt.slots[slot_idx];
	seat_slot = slot->seat_slot;
	point = slot->point;

	if (seat_slot == -1)
		return false;

	if (fallback_filter_defuzz_touch(dispatch, device, slot))
		return false;

	evdev_transform_absolute(device, &point);
	touch_notify_touch_motion(base, time, slot_idx, seat_slot,
				  &point);

	return true;
}

static bool
fallback_flush_mt_up(struct fallback_dispatch *dispatch,
		     struct evdev_device *device,
		     int slot_idx,
		     uint64_t time)
{
	struct libinput_device *base = &device->base;
	struct libinput_seat *seat = base->seat;
	struct mt_slot *slot;
	int seat_slot;

	if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
		return false;

	slot = &dispatch->mt.slots[slot_idx];
	seat_slot = slot->seat_slot;
	slot->seat_slot = -1;

	if (seat_slot == -1)
		return false;

	seat->slot_map &= ~(1 << seat_slot);

	touch_notify_touch_up(base, time, slot_idx, seat_slot);

	return true;
}

static bool
fallback_flush_st_down(struct fallback_dispatch *dispatch,
		       struct evdev_device *device,
		       uint64_t time)
{
	struct libinput_device *base = &device->base;
	struct libinput_seat *seat = base->seat;
	struct device_coords point;
	int seat_slot;

	if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
		return false;

	if (dispatch->abs.seat_slot != -1) {
		evdev_log_bug_kernel(device,
				     "driver sent multiple touch down for the same slot");
		return false;
	}

	seat_slot = ffs(~seat->slot_map) - 1;
	dispatch->abs.seat_slot = seat_slot;

	if (seat_slot == -1)
		return false;

	seat->slot_map |= 1 << seat_slot;

	point = dispatch->abs.point;
	evdev_transform_absolute(device, &point);

	touch_notify_touch_down(base, time, -1, seat_slot, &point);

	return true;
}

static bool
fallback_flush_st_motion(struct fallback_dispatch *dispatch,
			 struct evdev_device *device,
			 uint64_t time)
{
	struct libinput_device *base = &device->base;
	struct device_coords point;
	int seat_slot;

	point = dispatch->abs.point;
	evdev_transform_absolute(device, &point);

	seat_slot = dispatch->abs.seat_slot;

	if (seat_slot == -1)
		return false;

	touch_notify_touch_motion(base, time, -1, seat_slot, &point);

	return true;
}

static bool
fallback_flush_st_up(struct fallback_dispatch *dispatch,
		     struct evdev_device *device,
		     uint64_t time)
{
	struct libinput_device *base = &device->base;
	struct libinput_seat *seat = base->seat;
	int seat_slot;

	if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
		return false;

	seat_slot = dispatch->abs.seat_slot;
	dispatch->abs.seat_slot = -1;

	if (seat_slot == -1)
		return false;

	seat->slot_map &= ~(1 << seat_slot);

	touch_notify_touch_up(base, time, -1, seat_slot);

	return true;
}

static enum evdev_event_type
fallback_flush_pending_event(struct fallback_dispatch *dispatch,
			     struct evdev_device *device,
			     uint64_t time)
{
	enum evdev_event_type sent_event;
	int slot_idx;

	sent_event = dispatch->pending_event;

	switch (dispatch->pending_event) {
	case EVDEV_NONE:
		break;
	case EVDEV_RELATIVE_MOTION:
		fallback_flush_relative_motion(dispatch, device, time);
		break;
	case EVDEV_ABSOLUTE_MT_DOWN:
		slot_idx = dispatch->mt.slot;
		if (!fallback_flush_mt_down(dispatch,
					    device,
					    slot_idx,
					    time))
			sent_event = EVDEV_NONE;
		break;
	case EVDEV_ABSOLUTE_MT_MOTION:
		slot_idx = dispatch->mt.slot;
		if (!fallback_flush_mt_motion(dispatch,
					      device,
					      slot_idx,
					      time))
			sent_event = EVDEV_NONE;
		break;
	case EVDEV_ABSOLUTE_MT_UP:
		slot_idx = dispatch->mt.slot;
		if (!fallback_flush_mt_up(dispatch,
					  device,
					  slot_idx,
					  time))
			sent_event = EVDEV_NONE;
		break;
	case EVDEV_ABSOLUTE_TOUCH_DOWN:
		if (!fallback_flush_st_down(dispatch, device, time))
			sent_event = EVDEV_NONE;
		break;
	case EVDEV_ABSOLUTE_MOTION:
		if (device->seat_caps & EVDEV_DEVICE_TOUCH) {
			if (fallback_flush_st_motion(dispatch,
						     device,
						     time))
				sent_event = EVDEV_ABSOLUTE_MT_MOTION;
			else
				sent_event = EVDEV_NONE;
		} else if (device->seat_caps & EVDEV_DEVICE_POINTER) {
			fallback_flush_absolute_motion(dispatch,
						       device,
						       time);
		}
		break;
	case EVDEV_ABSOLUTE_TOUCH_UP:
		if (!fallback_flush_st_up(dispatch, device, time))
			sent_event = EVDEV_NONE;
		break;
	default:
		assert(0 && "Unknown pending event type");
		break;
	}

	dispatch->pending_event = EVDEV_NONE;

	return sent_event;
}

static enum evdev_key_type
get_key_type(uint16_t code)
{
	switch (code) {
	case BTN_TOOL_PEN:
	case BTN_TOOL_RUBBER:
	case BTN_TOOL_BRUSH:
	case BTN_TOOL_PENCIL:
	case BTN_TOOL_AIRBRUSH:
	case BTN_TOOL_MOUSE:
	case BTN_TOOL_LENS:
	case BTN_TOOL_QUINTTAP:
	case BTN_TOOL_DOUBLETAP:
	case BTN_TOOL_TRIPLETAP:
	case BTN_TOOL_QUADTAP:
	case BTN_TOOL_FINGER:
	case BTN_TOUCH:
		return EVDEV_KEY_TYPE_NONE;
	}

	if (code >= KEY_ESC && code <= KEY_MICMUTE)
		return EVDEV_KEY_TYPE_KEY;
	if (code >= BTN_MISC && code <= BTN_GEAR_UP)
		return EVDEV_KEY_TYPE_BUTTON;
	if (code >= KEY_OK && code <= KEY_LIGHTS_TOGGLE)
		return EVDEV_KEY_TYPE_KEY;
	if (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT)
		return EVDEV_KEY_TYPE_BUTTON;
	if (code >= KEY_ALS_TOGGLE && code <= KEY_KBDINPUTASSIST_CANCEL)
		return EVDEV_KEY_TYPE_KEY;
	if (code >= BTN_TRIGGER_HAPPY && code <= BTN_TRIGGER_HAPPY40)
		return EVDEV_KEY_TYPE_BUTTON;
	return EVDEV_KEY_TYPE_NONE;
}

static void
fallback_process_touch_button(struct fallback_dispatch *dispatch,
			      struct evdev_device *device,
			      uint64_t time, int value)
{
	if (dispatch->pending_event != EVDEV_NONE &&
	    dispatch->pending_event != EVDEV_ABSOLUTE_MOTION)
		fallback_flush_pending_event(dispatch, device, time);

	dispatch->pending_event = (value ?
				 EVDEV_ABSOLUTE_TOUCH_DOWN :
				 EVDEV_ABSOLUTE_TOUCH_UP);
}

static inline void
fallback_flush_debounce(struct fallback_dispatch *dispatch,
			struct evdev_device *device)
{
	int code = dispatch->debounce.button_code;
	int button;

	if (dispatch->debounce.state != DEBOUNCE_ACTIVE)
		return;

	if (hw_is_key_down(dispatch, code)) {
		button = evdev_to_left_handed(device, code);
		evdev_pointer_notify_physical_button(device,
						     dispatch->debounce.button_up_time,
						     button,
						     LIBINPUT_BUTTON_STATE_RELEASED);
		hw_set_key_down(dispatch, code, 0);
	}

	dispatch->debounce.state = DEBOUNCE_ON;
}

static void
fallback_debounce_timeout(uint64_t now, void *data)
{
	struct evdev_device *device = data;
	struct fallback_dispatch *dispatch =
		fallback_dispatch(device->dispatch);

	fallback_flush_debounce(dispatch, device);
}

static bool
fallback_filter_debounce_press(struct fallback_dispatch *dispatch,
			       struct evdev_device *device,
			       struct input_event *e,
			       uint64_t time)
{
	bool filter = false;
	uint64_t tdelta;

	/* If other button is pressed while we're holding back the release,
	 * flush the pending release (if any) and continue. We don't handle
	 * this situation, if you have a mouse that needs per-button
	 * debouncing, consider writing to santa for a new mouse.
	 */
	if (e->code != dispatch->debounce.button_code) {
		if (dispatch->debounce.state == DEBOUNCE_ACTIVE) {
			libinput_timer_cancel(&dispatch->debounce.timer);
			fallback_flush_debounce(dispatch, device);
		}
		return false;
	}

	tdelta = time - dispatch->debounce.button_up_time;
	assert((int64_t)tdelta >= 0);

	if (tdelta < DEBOUNCE_TIME) {
		switch (dispatch->debounce.state) {
		case DEBOUNCE_INIT:
			/* This is the first time we debounce, enable proper debouncing
			   from now on but filter this press event */
			filter = true;
			evdev_log_info(device,
				       "Enabling button debouncing, "
				       "see %sbutton_debouncing.html for details\n",
				       HTTP_DOC_LINK);
			dispatch->debounce.state = DEBOUNCE_NEEDED;
			break;
		case DEBOUNCE_NEEDED:
		case DEBOUNCE_ON:
			break;
		/* If a release event is pending and, filter press
		 * events until we flushed the release */
		case DEBOUNCE_ACTIVE:
			filter = true;
			break;
		}
	} else if (dispatch->debounce.state == DEBOUNCE_ACTIVE) {
		/* call libinput_dispatch() more frequently */
		evdev_log_bug_client(device,
				     "Debouncing still active past timeout\n");
	}

	return filter;
}

static bool
fallback_filter_debounce_release(struct fallback_dispatch *dispatch,
				 struct input_event *e,
				 uint64_t time)
{
	bool filter = false;

	dispatch->debounce.button_code = e->code;
	dispatch->debounce.button_up_time = time;

	switch (dispatch->debounce.state) {
	case DEBOUNCE_INIT:
		break;
	case DEBOUNCE_NEEDED:
		filter = true;
		dispatch->debounce.state = DEBOUNCE_ON;
		break;
	case DEBOUNCE_ON:
		libinput_timer_set(&dispatch->debounce.timer,
				   time + DEBOUNCE_TIME);
		filter = true;
		dispatch->debounce.state = DEBOUNCE_ACTIVE;
		break;
	case DEBOUNCE_ACTIVE:
		filter = true;
		break;
	}

	return filter;
}

static bool
fallback_filter_debounce(struct fallback_dispatch *dispatch,
			 struct evdev_device *device,
			 struct input_event *e, uint64_t time)
{
	bool filter = false;

	/* Behavior: we monitor the time deltas between release and press
	 * events. Proper debouncing is disabled on init, but the first
	 * time we see a bouncing press event we enable it.
	 *
	 * The first bounced event is simply discarded, which ends up in the
	 * button being released sooner than it should be. Subsequent button
	 * presses are timer-based and thus released a bit later because we
	 * then wait for a timeout before we post the release event.
	 */
	if (e->value)
		filter = fallback_filter_debounce_press(dispatch, device, e, time);
	else
		filter = fallback_filter_debounce_release(dispatch, e, time);

	return filter;
}

static inline void
fallback_process_key(struct fallback_dispatch *dispatch,
		     struct evdev_device *device,
		     struct input_event *e, uint64_t time)
{
	enum evdev_key_type type;

	/* ignore kernel key repeat */
	if (e->value == 2)
		return;

	if (e->code == BTN_TOUCH) {
		if (!device->is_mt)
			fallback_process_touch_button(dispatch,
						      device,
						      time,
						      e->value);
		return;
	}

	fallback_flush_pending_event(dispatch, device, time);

	type = get_key_type(e->code);

	/* Ignore key release events from the kernel for keys that libinput
	 * never got a pressed event for or key presses for keys that we
	 * think are still down */
	switch (type) {
	case EVDEV_KEY_TYPE_NONE:
		break;
	case EVDEV_KEY_TYPE_KEY:
		if ((e->value && hw_is_key_down(dispatch, e->code)) ||
		    (e->value == 0 && !hw_is_key_down(dispatch, e->code)))
			return;
		break;
	case EVDEV_KEY_TYPE_BUTTON:
		if (fallback_filter_debounce(dispatch, device, e, time))
			return;

		if ((e->value && hw_is_key_down(dispatch, e->code)) ||
		    (e->value == 0 && !hw_is_key_down(dispatch, e->code)))
			return;
		break;
	}

	hw_set_key_down(dispatch, e->code, e->value);

	switch (type) {
	case EVDEV_KEY_TYPE_NONE:
		break;
	case EVDEV_KEY_TYPE_KEY:
		fallback_keyboard_notify_key(
			dispatch,
			device,
			time,
			e->code,
			e->value ? LIBINPUT_KEY_STATE_PRESSED :
				   LIBINPUT_KEY_STATE_RELEASED);
		break;
	case EVDEV_KEY_TYPE_BUTTON:
		evdev_pointer_notify_physical_button(
			device,
			time,
			evdev_to_left_handed(device, e->code),
			e->value ? LIBINPUT_BUTTON_STATE_PRESSED :
				   LIBINPUT_BUTTON_STATE_RELEASED);
		break;
	}
}

static void
fallback_process_touch(struct fallback_dispatch *dispatch,
		       struct evdev_device *device,
		       struct input_event *e,
		       uint64_t time)
{
	switch (e->code) {
	case ABS_MT_SLOT:
		if ((size_t)e->value >= dispatch->mt.slots_len) {
			evdev_log_bug_libinput(device,
					 "exceeded slot count (%d of max %zd)\n",
					 e->value,
					 dispatch->mt.slots_len);
			e->value = dispatch->mt.slots_len - 1;
		}
		fallback_flush_pending_event(dispatch, device, time);
		dispatch->mt.slot = e->value;
		break;
	case ABS_MT_TRACKING_ID:
		if (dispatch->pending_event != EVDEV_NONE &&
		    dispatch->pending_event != EVDEV_ABSOLUTE_MT_MOTION)
			fallback_flush_pending_event(dispatch, device, time);
		if (e->value >= 0)
			dispatch->pending_event = EVDEV_ABSOLUTE_MT_DOWN;
		else
			dispatch->pending_event = EVDEV_ABSOLUTE_MT_UP;
		break;
	case ABS_MT_POSITION_X:
		evdev_device_check_abs_axis_range(device, e->code, e->value);
		dispatch->mt.slots[dispatch->mt.slot].point.x = e->value;
		if (dispatch->pending_event == EVDEV_NONE)
			dispatch->pending_event = EVDEV_ABSOLUTE_MT_MOTION;
		break;
	case ABS_MT_POSITION_Y:
		evdev_device_check_abs_axis_range(device, e->code, e->value);
		dispatch->mt.slots[dispatch->mt.slot].point.y = e->value;
		if (dispatch->pending_event == EVDEV_NONE)
			dispatch->pending_event = EVDEV_ABSOLUTE_MT_MOTION;
		break;
	}
}
static inline void
fallback_process_absolute_motion(struct fallback_dispatch *dispatch,
				 struct evdev_device *device,
				 struct input_event *e)
{
	switch (e->code) {
	case ABS_X:
		evdev_device_check_abs_axis_range(device, e->code, e->value);
		dispatch->abs.point.x = e->value;
		if (dispatch->pending_event == EVDEV_NONE)
			dispatch->pending_event = EVDEV_ABSOLUTE_MOTION;
		break;
	case ABS_Y:
		evdev_device_check_abs_axis_range(device, e->code, e->value);
		dispatch->abs.point.y = e->value;
		if (dispatch->pending_event == EVDEV_NONE)
			dispatch->pending_event = EVDEV_ABSOLUTE_MOTION;
		break;
	}
}

static void
fallback_lid_keyboard_event(uint64_t time,
			    struct libinput_event *event,
			    void *data)
{
	struct fallback_dispatch *dispatch = fallback_dispatch(data);

	if (!dispatch->lid.is_closed)
		return;

	if (event->type != LIBINPUT_EVENT_KEYBOARD_KEY)
		return;

	if (dispatch->lid.reliability == RELIABILITY_WRITE_OPEN) {
		int fd = libevdev_get_fd(dispatch->device->evdev);
		struct input_event ev[2] = {
			{{ 0, 0 }, EV_SW, SW_LID, 0 },
			{{ 0, 0 }, EV_SYN, SYN_REPORT, 0 },
		};

		(void)write(fd, ev, sizeof(ev));
		/* In case write() fails, we sync the lid state manually
		 * regardless. */
	}

	/* Posting the event here means we preempt the keyboard events that
	 * caused us to wake up, so the lid event is always passed on before
	 * the key event.
	 */
	dispatch->lid.is_closed = false;
	fallback_lid_notify_toggle(dispatch, dispatch->device, time);
}

static void
fallback_lid_toggle_keyboard_listener(struct fallback_dispatch *dispatch,
				      bool is_closed)
{
	if (!dispatch->lid.keyboard)
		return;

	if (is_closed) {
		libinput_device_add_event_listener(
					&dispatch->lid.keyboard->base,
					&dispatch->lid.listener,
					fallback_lid_keyboard_event,
					dispatch);
	} else {
		libinput_device_remove_event_listener(
					&dispatch->lid.listener);
		libinput_device_init_event_listener(
					&dispatch->lid.listener);
	}
}

static inline void
fallback_process_switch(struct fallback_dispatch *dispatch,
			struct evdev_device *device,
			struct input_event *e,
			uint64_t time)
{
	enum libinput_switch_state state;
	bool is_closed;

	switch (e->code) {
	case SW_LID:
		is_closed = !!e->value;

		if (dispatch->lid.is_closed == is_closed)
			return;
		fallback_lid_toggle_keyboard_listener(dispatch, is_closed);

		dispatch->lid.is_closed = is_closed;
		fallback_lid_notify_toggle(dispatch, device, time);
		break;
	case SW_TABLET_MODE:
		if (dispatch->tablet_mode.state == e->value)
			return;

		dispatch->tablet_mode.state = e->value;
		if (e->value)
			state = LIBINPUT_SWITCH_STATE_ON;
		else
			state = LIBINPUT_SWITCH_STATE_OFF;
		switch_notify_toggle(&device->base,
				     time,
				     LIBINPUT_SWITCH_TABLET_MODE,
				     state);
		break;
	}
}

void
evdev_notify_axis(struct evdev_device *device,
		  uint64_t time,
		  uint32_t axes,
		  enum libinput_pointer_axis_source source,
		  const struct normalized_coords *delta_in,
		  const struct discrete_coords *discrete_in)
{
	struct normalized_coords delta = *delta_in;
	struct discrete_coords discrete = *discrete_in;

	if (device->scroll.natural_scrolling_enabled) {
		delta.x *= -1;
		delta.y *= -1;
		discrete.x *= -1;
		discrete.y *= -1;
	}

	pointer_notify_axis(&device->base,
			    time,
			    axes,
			    source,
			    &delta,
			    &discrete);
}

static inline bool
fallback_reject_relative(struct evdev_device *device,
			 const struct input_event *e,
			 uint64_t time)
{
	if ((e->code == REL_X || e->code == REL_Y) &&
	    (device->seat_caps & EVDEV_DEVICE_POINTER) == 0) {
		evdev_log_bug_libinput_ratelimit(device,
						 &device->nonpointer_rel_limit,
						 "REL_X/Y from a non-pointer device\n");
		return true;
	}

	return false;
}

static inline void
fallback_process_relative(struct fallback_dispatch *dispatch,
			  struct evdev_device *device,
			  struct input_event *e, uint64_t time)
{
	struct normalized_coords wheel_degrees = { 0.0, 0.0 };
	struct discrete_coords discrete = { 0.0, 0.0 };
	enum libinput_pointer_axis_source source;

	if (fallback_reject_relative(device, e, time))
		return;

	switch (e->code) {
	case REL_X:
		if (dispatch->pending_event != EVDEV_RELATIVE_MOTION)
			fallback_flush_pending_event(dispatch, device, time);
		dispatch->rel.x += e->value;
		dispatch->pending_event = EVDEV_RELATIVE_MOTION;
		break;
	case REL_Y:
		if (dispatch->pending_event != EVDEV_RELATIVE_MOTION)
			fallback_flush_pending_event(dispatch, device, time);
		dispatch->rel.y += e->value;
		dispatch->pending_event = EVDEV_RELATIVE_MOTION;
		break;
	case REL_WHEEL:
		fallback_flush_pending_event(dispatch, device, time);
		wheel_degrees.y = -1 * e->value *
					device->scroll.wheel_click_angle.x;
		discrete.y = -1 * e->value;

		source = device->scroll.is_tilt.vertical ?
				LIBINPUT_POINTER_AXIS_SOURCE_WHEEL_TILT:
				LIBINPUT_POINTER_AXIS_SOURCE_WHEEL;

		evdev_notify_axis(
			device,
			time,
			AS_MASK(LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL),
			source,
			&wheel_degrees,
			&discrete);
		break;
	case REL_HWHEEL:
		fallback_flush_pending_event(dispatch, device, time);
		wheel_degrees.x = e->value *
					device->scroll.wheel_click_angle.y;
		discrete.x = e->value;

		source = device->scroll.is_tilt.horizontal ?
				LIBINPUT_POINTER_AXIS_SOURCE_WHEEL_TILT:
				LIBINPUT_POINTER_AXIS_SOURCE_WHEEL;

		evdev_notify_axis(
			device,
			time,
			AS_MASK(LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL),
			source,
			&wheel_degrees,
			&discrete);
		break;
	}
}

static inline void
fallback_process_absolute(struct fallback_dispatch *dispatch,
			  struct evdev_device *device,
			  struct input_event *e,
			  uint64_t time)
{
	if (device->is_mt) {
		fallback_process_touch(dispatch, device, e, time);
	} else {
		fallback_process_absolute_motion(dispatch, device, e);
	}
}

static inline bool
fallback_any_button_down(struct fallback_dispatch *dispatch,
		      struct evdev_device *device)
{
	unsigned int button;

	for (button = BTN_LEFT; button < BTN_JOYSTICK; button++) {
		if (libevdev_has_event_code(device->evdev, EV_KEY, button) &&
		    hw_is_key_down(dispatch, button))
			return true;
	}
	return false;
}

static void
evdev_tag_external_mouse(struct evdev_device *device,
			 struct udev_device *udev_device)
{
	int bustype;

	bustype = libevdev_get_id_bustype(device->evdev);
	if (bustype == BUS_USB || bustype == BUS_BLUETOOTH)
		device->tags |= EVDEV_TAG_EXTERNAL_MOUSE;
}

static void
evdev_tag_trackpoint(struct evdev_device *device,
		     struct udev_device *udev_device)
{
	if (libevdev_has_property(device->evdev,
				  INPUT_PROP_POINTING_STICK) ||
	    parse_udev_flag(device, udev_device, "ID_INPUT_POINTINGSTICK"))
		device->tags |= EVDEV_TAG_TRACKPOINT;
}

static inline void
evdev_tag_keyboard_internal(struct evdev_device *device)
{
	device->tags |= EVDEV_TAG_INTERNAL_KEYBOARD;
	device->tags &= ~EVDEV_TAG_EXTERNAL_KEYBOARD;
}

static inline void
evdev_tag_keyboard_external(struct evdev_device *device)
{
	device->tags |= EVDEV_TAG_EXTERNAL_KEYBOARD;
	device->tags &= ~EVDEV_TAG_INTERNAL_KEYBOARD;
}

static void
evdev_tag_keyboard(struct evdev_device *device,
		   struct udev_device *udev_device)
{
	const char *prop;
	int code;

	if (!libevdev_has_event_type(device->evdev, EV_KEY))
		return;

	for (code = KEY_Q; code <= KEY_P; code++) {
		if (!libevdev_has_event_code(device->evdev,
					     EV_KEY,
					     code))
			return;
	}

	/* This should eventually become ID_INPUT_KEYBOARD_INTEGRATION */
	prop = udev_device_get_property_value(udev_device,
					      "LIBINPUT_ATTR_KEYBOARD_INTEGRATION");
	if (prop) {
		if (streq(prop, "internal")) {
			evdev_tag_keyboard_internal(device);
		} else if (streq(prop, "external")) {
			evdev_tag_keyboard_external(device);
		} else {
			evdev_log_info(device,
				       "tagged with unknown value %s\n",
				       prop);
		}
	}

	device->tags |= EVDEV_TAG_KEYBOARD;
}

static void
fallback_process(struct evdev_dispatch *evdev_dispatch,
		 struct evdev_device *device,
		 struct input_event *event,
		 uint64_t time)
{
	struct fallback_dispatch *dispatch = fallback_dispatch(evdev_dispatch);
	enum evdev_event_type sent;

	if (dispatch->ignore_events)
		return;

	switch (event->type) {
	case EV_REL:
		fallback_process_relative(dispatch, device, event, time);
		break;
	case EV_ABS:
		fallback_process_absolute(dispatch, device, event, time);
		break;
	case EV_KEY:
		fallback_process_key(dispatch, device, event, time);
		break;
	case EV_SW:
		fallback_process_switch(dispatch, device, event, time);
		break;
	case EV_SYN:
		sent = fallback_flush_pending_event(dispatch, device, time);
		switch (sent) {
		case EVDEV_ABSOLUTE_TOUCH_DOWN:
		case EVDEV_ABSOLUTE_TOUCH_UP:
		case EVDEV_ABSOLUTE_MT_DOWN:
		case EVDEV_ABSOLUTE_MT_MOTION:
		case EVDEV_ABSOLUTE_MT_UP:
			touch_notify_frame(&device->base, time);
			break;
		case EVDEV_ABSOLUTE_MOTION:
		case EVDEV_RELATIVE_MOTION:
		case EVDEV_NONE:
			break;
		}
		break;
	}
}

static void
release_touches(struct fallback_dispatch *dispatch,
		struct evdev_device *device,
		uint64_t time)
{
	unsigned int idx;
	bool need_frame = false;

	need_frame = fallback_flush_st_up(dispatch, device, time);

	for (idx = 0; idx < dispatch->mt.slots_len; idx++) {
		struct mt_slot *slot = &dispatch->mt.slots[idx];

		if (slot->seat_slot == -1)
			continue;

		if (fallback_flush_mt_up(dispatch, device, idx, time))
			need_frame = true;
	}

	if (need_frame)
		touch_notify_frame(&device->base, time);
}

static void
release_pressed_keys(struct fallback_dispatch *dispatch,
		     struct evdev_device *device,
		     uint64_t time)
{
	int code;

	for (code = 0; code < KEY_CNT; code++) {
		int count = get_key_down_count(device, code);

		if (count == 0)
			continue;

		if (count > 1) {
			evdev_log_bug_libinput(device,
					       "key %d is down %d times.\n",
					       code,
					       count);
		}

		switch (get_key_type(code)) {
		case EVDEV_KEY_TYPE_NONE:
			break;
		case EVDEV_KEY_TYPE_KEY:
			fallback_keyboard_notify_key(
				dispatch,
				device,
				time,
				code,
				LIBINPUT_KEY_STATE_RELEASED);
			break;
		case EVDEV_KEY_TYPE_BUTTON:
			evdev_pointer_notify_physical_button(
				device,
				time,
				evdev_to_left_handed(device, code),
				LIBINPUT_BUTTON_STATE_RELEASED);
			break;
		}

		count = get_key_down_count(device, code);
		if (count != 0) {
			evdev_log_bug_libinput(device,
					       "releasing key %d failed.\n",
					       code);
			break;
		}
	}
}

static void
fallback_return_to_neutral_state(struct fallback_dispatch *dispatch,
				 struct evdev_device *device)
{
	struct libinput *libinput = evdev_libinput_context(device);
	uint64_t time;

	if ((time = libinput_now(libinput)) == 0)
		return;

	release_touches(dispatch, device, time);
	release_pressed_keys(dispatch, device, time);
	memset(dispatch->hw_key_mask, 0, sizeof(dispatch->hw_key_mask));
}

static void
fallback_suspend(struct evdev_dispatch *evdev_dispatch,
		 struct evdev_device *device)
{
	struct fallback_dispatch *dispatch = fallback_dispatch(evdev_dispatch);

	fallback_return_to_neutral_state(dispatch, device);
}

static void
fallback_remove(struct evdev_dispatch *evdev_dispatch)
{
	struct fallback_dispatch *dispatch = fallback_dispatch(evdev_dispatch);

	if (!dispatch->lid.keyboard)
		return;

	libinput_device_remove_event_listener(&dispatch->lid.listener);
}

static void
fallback_sync_initial_state(struct evdev_device *device,
			    struct evdev_dispatch *evdev_dispatch)
{
	struct fallback_dispatch *dispatch = fallback_dispatch(evdev_dispatch);
	uint64_t time = libinput_now(evdev_libinput_context(device));

	if (device->tags & EVDEV_TAG_LID_SWITCH) {
		struct libevdev *evdev = device->evdev;

		dispatch->lid.is_closed = libevdev_get_event_value(evdev,
								   EV_SW,
								   SW_LID);
		dispatch->lid.is_closed_client_state = false;

		/* For the initial state sync, we depend on whether the lid switch
		 * is reliable. If we know it's reliable, we sync as expected.
		 * If we're not sure, we ignore the initial state and only sync on
		 * the first future lid close event. Laptops with a broken switch
		 * that always have the switch in 'on' state thus don't mess up our
		 * touchpad.
		 */
		if (dispatch->lid.is_closed &&
		    dispatch->lid.reliability == RELIABILITY_RELIABLE) {
			fallback_lid_notify_toggle(dispatch, device, time);
		}
	}

	if (dispatch->tablet_mode.state) {
		switch_notify_toggle(&device->base,
				     time,
				     LIBINPUT_SWITCH_TABLET_MODE,
				     LIBINPUT_SWITCH_STATE_ON);
	}
}

static void
fallback_toggle_touch(struct evdev_dispatch *evdev_dispatch,
		      struct evdev_device *device,
		      bool enable)
{
	struct fallback_dispatch *dispatch = fallback_dispatch(evdev_dispatch);
	bool ignore_events = !enable;

	if (ignore_events == dispatch->ignore_events)
		return;

	if (ignore_events)
		fallback_return_to_neutral_state(dispatch, device);

	dispatch->ignore_events = ignore_events;
}

static void
fallback_destroy(struct evdev_dispatch *evdev_dispatch)
{
	struct fallback_dispatch *dispatch = fallback_dispatch(evdev_dispatch);

	libinput_timer_cancel(&dispatch->debounce.timer);
	libinput_timer_destroy(&dispatch->debounce.timer);
	free(dispatch->mt.slots);
	free(dispatch);
}

static int
evdev_calibration_has_matrix(struct libinput_device *libinput_device)
{
	struct evdev_device *device = evdev_device(libinput_device);

	return device->abs.absinfo_x && device->abs.absinfo_y;
}

static enum libinput_config_status
evdev_calibration_set_matrix(struct libinput_device *libinput_device,
			     const float matrix[6])
{
	struct evdev_device *device = evdev_device(libinput_device);

	evdev_device_calibrate(device, matrix);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static int
evdev_calibration_get_matrix(struct libinput_device *libinput_device,
			     float matrix[6])
{
	struct evdev_device *device = evdev_device(libinput_device);

	matrix_to_farray6(&device->abs.usermatrix, matrix);

	return !matrix_is_identity(&device->abs.usermatrix);
}

static int
evdev_calibration_get_default_matrix(struct libinput_device *libinput_device,
				     float matrix[6])
{
	struct evdev_device *device = evdev_device(libinput_device);

	matrix_to_farray6(&device->abs.default_calibration, matrix);

	return !matrix_is_identity(&device->abs.default_calibration);
}

static void
fallback_lid_pair_keyboard(struct evdev_device *lid_switch,
			   struct evdev_device *keyboard)
{
	struct fallback_dispatch *dispatch =
		fallback_dispatch(lid_switch->dispatch);

	if ((keyboard->tags & EVDEV_TAG_KEYBOARD) == 0 ||
	    (lid_switch->tags & EVDEV_TAG_LID_SWITCH) == 0)
		return;

	if (dispatch->lid.keyboard)
		return;

	if (keyboard->tags & EVDEV_TAG_INTERNAL_KEYBOARD) {
		dispatch->lid.keyboard = keyboard;
		evdev_log_debug(lid_switch,
				"lid: keyboard paired with %s<->%s\n",
				lid_switch->devname,
				keyboard->devname);

		/* We need to init the event listener now only if the reported state
		 * is closed. */
		if (dispatch->lid.is_closed)
			fallback_lid_toggle_keyboard_listener(dispatch,
						    dispatch->lid.is_closed);
	}
}

static void
fallback_interface_device_added(struct evdev_device *device,
				struct evdev_device *added_device)
{
	fallback_lid_pair_keyboard(device, added_device);
}

static void
fallback_interface_device_removed(struct evdev_device *device,
				  struct evdev_device *removed_device)
{
	struct fallback_dispatch *dispatch =
			fallback_dispatch(device->dispatch);

	if (removed_device == dispatch->lid.keyboard) {
		libinput_device_remove_event_listener(
					&dispatch->lid.listener);
		libinput_device_init_event_listener(
					&dispatch->lid.listener);
		dispatch->lid.keyboard = NULL;
	}
}

struct evdev_dispatch_interface fallback_interface = {
	fallback_process,
	fallback_suspend,
	fallback_remove,
	fallback_destroy,
	fallback_interface_device_added,
	fallback_interface_device_removed,
	fallback_interface_device_removed, /* device_suspended, treat as remove */
	fallback_interface_device_added,   /* device_resumed, treat as add */
	fallback_sync_initial_state, /* post_added */
	fallback_toggle_touch, /* toggle_touch */
};

static uint32_t
evdev_sendevents_get_modes(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
}

static enum libinput_config_status
evdev_sendevents_set_mode(struct libinput_device *device,
			  enum libinput_config_send_events_mode mode)
{
	struct evdev_device *evdev = evdev_device(device);
	struct evdev_dispatch *dispatch = evdev->dispatch;

	if (mode == dispatch->sendevents.current_mode)
		return LIBINPUT_CONFIG_STATUS_SUCCESS;

	switch(mode) {
	case LIBINPUT_CONFIG_SEND_EVENTS_ENABLED:
		evdev_device_resume(evdev);
		break;
	case LIBINPUT_CONFIG_SEND_EVENTS_DISABLED:
		evdev_device_suspend(evdev);
		break;
	default: /* no support for combined modes yet */
		return LIBINPUT_CONFIG_STATUS_UNSUPPORTED;
	}

	dispatch->sendevents.current_mode = mode;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_send_events_mode
evdev_sendevents_get_mode(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);
	struct evdev_dispatch *dispatch = evdev->dispatch;

	return dispatch->sendevents.current_mode;
}

static enum libinput_config_send_events_mode
evdev_sendevents_get_default_mode(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
}

static int
evdev_left_handed_has(struct libinput_device *device)
{
	/* This is only hooked up when we have left-handed configuration, so we
	 * can hardcode 1 here */
	return 1;
}

static void
evdev_change_to_left_handed(struct evdev_device *device)
{
	struct fallback_dispatch *dispatch = fallback_dispatch(device->dispatch);

	if (device->left_handed.want_enabled == device->left_handed.enabled)
		return;

	if (fallback_any_button_down(dispatch, device))
		return;

	device->left_handed.enabled = device->left_handed.want_enabled;
}

static enum libinput_config_status
evdev_left_handed_set(struct libinput_device *device, int left_handed)
{
	struct evdev_device *evdev = evdev_device(device);

	evdev->left_handed.want_enabled = left_handed ? true : false;

	evdev->left_handed.change_to_enabled(evdev);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static int
evdev_left_handed_get(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);

	/* return the wanted configuration, even if it hasn't taken
	 * effect yet! */
	return evdev->left_handed.want_enabled;
}

static int
evdev_left_handed_get_default(struct libinput_device *device)
{
	return 0;
}

void
evdev_init_left_handed(struct evdev_device *device,
		       void (*change_to_left_handed)(struct evdev_device *))
{
	device->left_handed.config.has = evdev_left_handed_has;
	device->left_handed.config.set = evdev_left_handed_set;
	device->left_handed.config.get = evdev_left_handed_get;
	device->left_handed.config.get_default = evdev_left_handed_get_default;
	device->base.config.left_handed = &device->left_handed.config;
	device->left_handed.enabled = false;
	device->left_handed.want_enabled = false;
	device->left_handed.change_to_enabled = change_to_left_handed;
}

static uint32_t
evdev_scroll_get_methods(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
}

static void
evdev_change_scroll_method(struct evdev_device *device)
{
	struct fallback_dispatch *dispatch = fallback_dispatch(device->dispatch);

	if (device->scroll.want_method == device->scroll.method &&
	    device->scroll.want_button == device->scroll.button)
		return;

	if (fallback_any_button_down(dispatch, device))
		return;

	device->scroll.method = device->scroll.want_method;
	device->scroll.button = device->scroll.want_button;
}

static enum libinput_config_status
evdev_scroll_set_method(struct libinput_device *device,
			enum libinput_config_scroll_method method)
{
	struct evdev_device *evdev = evdev_device(device);

	evdev->scroll.want_method = method;
	evdev->scroll.change_scroll_method(evdev);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_scroll_method
evdev_scroll_get_method(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);

	/* return the wanted configuration, even if it hasn't taken
	 * effect yet! */
	return evdev->scroll.want_method;
}

static enum libinput_config_scroll_method
evdev_scroll_get_default_method(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);

	if (evdev->tags & EVDEV_TAG_TRACKPOINT)
		return LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;

	/* Mice without a scroll wheel but with middle button have on-button
	 * scrolling by default */
	if (!libevdev_has_event_code(evdev->evdev, EV_REL, REL_WHEEL) &&
	    !libevdev_has_event_code(evdev->evdev, EV_REL, REL_HWHEEL) &&
	    libevdev_has_event_code(evdev->evdev, EV_KEY, BTN_MIDDLE))
		return LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;

	return LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
}

static enum libinput_config_status
evdev_scroll_set_button(struct libinput_device *device,
			uint32_t button)
{
	struct evdev_device *evdev = evdev_device(device);

	evdev->scroll.want_button = button;
	evdev->scroll.change_scroll_method(evdev);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static uint32_t
evdev_scroll_get_button(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);

	/* return the wanted configuration, even if it hasn't taken
	 * effect yet! */
	return evdev->scroll.want_button;
}

static uint32_t
evdev_scroll_get_default_button(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);
	unsigned int code;

	if (libevdev_has_event_code(evdev->evdev, EV_KEY, BTN_MIDDLE))
		return BTN_MIDDLE;

	for (code = BTN_SIDE; code <= BTN_TASK; code++) {
		if (libevdev_has_event_code(evdev->evdev, EV_KEY, code))
			return code;
	}

	if (libevdev_has_event_code(evdev->evdev, EV_KEY, BTN_RIGHT))
		return BTN_RIGHT;

	return 0;
}

static void
evdev_init_button_scroll(struct evdev_device *device,
			 void (*change_scroll_method)(struct evdev_device *))
{
	char timer_name[64];

	snprintf(timer_name,
		 sizeof(timer_name),
		 "%s btnscroll",
		 evdev_device_get_sysname(device));
	libinput_timer_init(&device->scroll.timer,
			    evdev_libinput_context(device),
			    timer_name,
			    evdev_button_scroll_timeout, device);
	device->scroll.config.get_methods = evdev_scroll_get_methods;
	device->scroll.config.set_method = evdev_scroll_set_method;
	device->scroll.config.get_method = evdev_scroll_get_method;
	device->scroll.config.get_default_method = evdev_scroll_get_default_method;
	device->scroll.config.set_button = evdev_scroll_set_button;
	device->scroll.config.get_button = evdev_scroll_get_button;
	device->scroll.config.get_default_button = evdev_scroll_get_default_button;
	device->base.config.scroll_method = &device->scroll.config;
	device->scroll.method = evdev_scroll_get_default_method((struct libinput_device *)device);
	device->scroll.want_method = device->scroll.method;
	device->scroll.button = evdev_scroll_get_default_button((struct libinput_device *)device);
	device->scroll.want_button = device->scroll.button;
	device->scroll.change_scroll_method = change_scroll_method;
}

void
evdev_init_calibration(struct evdev_device *device,
		       struct libinput_device_config_calibration *calibration)
{
	device->base.config.calibration = calibration;

	calibration->has_matrix = evdev_calibration_has_matrix;
	calibration->set_matrix = evdev_calibration_set_matrix;
	calibration->get_matrix = evdev_calibration_get_matrix;
	calibration->get_default_matrix = evdev_calibration_get_default_matrix;
}

void
evdev_init_sendevents(struct evdev_device *device,
		      struct evdev_dispatch *dispatch)
{
	device->base.config.sendevents = &dispatch->sendevents.config;

	dispatch->sendevents.current_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	dispatch->sendevents.config.get_modes = evdev_sendevents_get_modes;
	dispatch->sendevents.config.set_mode = evdev_sendevents_set_mode;
	dispatch->sendevents.config.get_mode = evdev_sendevents_get_mode;
	dispatch->sendevents.config.get_default_mode = evdev_sendevents_get_default_mode;
}

static int
evdev_scroll_config_natural_has(struct libinput_device *device)
{
	return 1;
}

static enum libinput_config_status
evdev_scroll_config_natural_set(struct libinput_device *device,
				int enabled)
{
	struct evdev_device *dev = evdev_device(device);

	dev->scroll.natural_scrolling_enabled = enabled ? true : false;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static int
evdev_scroll_config_natural_get(struct libinput_device *device)
{
	struct evdev_device *dev = evdev_device(device);

	return dev->scroll.natural_scrolling_enabled ? 1 : 0;
}

static int
evdev_scroll_config_natural_get_default(struct libinput_device *device)
{
	/* could enable this on Apple touchpads. could do that, could
	 * very well do that... */
	return 0;
}

void
evdev_init_natural_scroll(struct evdev_device *device)
{
	device->scroll.config_natural.has = evdev_scroll_config_natural_has;
	device->scroll.config_natural.set_enabled = evdev_scroll_config_natural_set;
	device->scroll.config_natural.get_enabled = evdev_scroll_config_natural_get;
	device->scroll.config_natural.get_default_enabled = evdev_scroll_config_natural_get_default;
	device->scroll.natural_scrolling_enabled = false;
	device->base.config.natural_scroll = &device->scroll.config_natural;
}

static int
evdev_rotation_config_is_available(struct libinput_device *device)
{
	/* This function only gets called when we support rotation */
	return 1;
}

static enum libinput_config_status
evdev_rotation_config_set_angle(struct libinput_device *libinput_device,
				unsigned int degrees_cw)
{
	struct evdev_device *device = evdev_device(libinput_device);
	struct fallback_dispatch *dispatch = fallback_dispatch(device->dispatch);

	dispatch->rotation.angle = degrees_cw;
	matrix_init_rotate(&dispatch->rotation.matrix, degrees_cw);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static unsigned int
evdev_rotation_config_get_angle(struct libinput_device *libinput_device)
{
	struct evdev_device *device = evdev_device(libinput_device);
	struct fallback_dispatch *dispatch = fallback_dispatch(device->dispatch);

	return dispatch->rotation.angle;
}

static unsigned int
evdev_rotation_config_get_default_angle(struct libinput_device *device)
{
	return 0;
}

static void
evdev_init_rotation(struct evdev_device *device,
		    struct fallback_dispatch *dispatch)
{
	if ((device->model_flags & EVDEV_MODEL_TRACKBALL) == 0)
		return;

	dispatch->rotation.config.is_available = evdev_rotation_config_is_available;
	dispatch->rotation.config.set_angle = evdev_rotation_config_set_angle;
	dispatch->rotation.config.get_angle = evdev_rotation_config_get_angle;
	dispatch->rotation.config.get_default_angle = evdev_rotation_config_get_default_angle;
	dispatch->rotation.is_enabled = false;
	matrix_init_identity(&dispatch->rotation.matrix);
	device->base.config.rotation = &dispatch->rotation.config;
}

static inline int
evdev_need_mtdev(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;

	return (libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) &&
		libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y) &&
		!libevdev_has_event_code(evdev, EV_ABS, ABS_MT_SLOT));
}

/* Fake MT devices have the ABS_MT_SLOT bit set because of
   the limited ABS_* range - they aren't MT devices, they
   just have too many ABS_ axes */
static inline bool
evdev_is_fake_mt_device(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;

	return libevdev_has_event_code(evdev, EV_ABS, ABS_MT_SLOT) &&
		libevdev_get_num_slots(evdev) == -1;
}

static inline int
fallback_dispatch_init_slots(struct fallback_dispatch *dispatch,
			     struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;
	struct mt_slot *slots;
	int num_slots;
	int active_slot;
	int slot;

	if (evdev_is_fake_mt_device(device) ||
	    !libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) ||
	    !libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y))
		 return 0;

	/* We only handle the slotted Protocol B in libinput.
	   Devices with ABS_MT_POSITION_* but not ABS_MT_SLOT
	   require mtdev for conversion. */
	if (evdev_need_mtdev(device)) {
		device->mtdev = mtdev_new_open(device->fd);
		if (!device->mtdev)
			return -1;

		/* pick 10 slots as default for type A
		   devices. */
		num_slots = 10;
		active_slot = device->mtdev->caps.slot.value;
	} else {
		num_slots = libevdev_get_num_slots(device->evdev);
		active_slot = libevdev_get_current_slot(evdev);
	}

	slots = zalloc(num_slots * sizeof(struct mt_slot));

	for (slot = 0; slot < num_slots; ++slot) {
		slots[slot].seat_slot = -1;

		if (evdev_need_mtdev(device))
			continue;

		slots[slot].point.x = libevdev_get_slot_value(evdev,
							      slot,
							      ABS_MT_POSITION_X);
		slots[slot].point.y = libevdev_get_slot_value(evdev,
							      slot,
							      ABS_MT_POSITION_Y);
	}
	dispatch->mt.slots = slots;
	dispatch->mt.slots_len = num_slots;
	dispatch->mt.slot = active_slot;

	if (device->abs.absinfo_x->fuzz || device->abs.absinfo_y->fuzz) {
		dispatch->mt.want_hysteresis = true;
		dispatch->mt.hysteresis_margin.x = device->abs.absinfo_x->fuzz/2;
		dispatch->mt.hysteresis_margin.y = device->abs.absinfo_y->fuzz/2;
	}

	return 0;
}

static inline void
fallback_dispatch_init_rel(struct fallback_dispatch *dispatch,
			   struct evdev_device *device)
{
	dispatch->rel.x = 0;
	dispatch->rel.y = 0;
}

static inline void
fallback_dispatch_init_abs(struct fallback_dispatch *dispatch,
			   struct evdev_device *device)
{
	if (!libevdev_has_event_code(device->evdev, EV_ABS, ABS_X))
		return;

	dispatch->abs.point.x = device->abs.absinfo_x->value;
	dispatch->abs.point.y = device->abs.absinfo_y->value;
	dispatch->abs.seat_slot = -1;

	evdev_device_init_abs_range_warnings(device);
}

static inline enum switch_reliability
evdev_read_switch_reliability_prop(struct evdev_device *device)
{
	const char *prop;
	enum switch_reliability r;

	prop = udev_device_get_property_value(device->udev_device,
					      "LIBINPUT_ATTR_LID_SWITCH_RELIABILITY");
	if (!parse_switch_reliability_property(prop, &r)) {
		evdev_log_error(device,
				"%s: switch reliability set to unknown value '%s'\n",
				device->devname,
				prop);
		r =  RELIABILITY_UNKNOWN;
	} else if (r == RELIABILITY_WRITE_OPEN) {
		evdev_log_info(device,
			       "%s: will write switch open events\n",
			       device->devname);
	}

	return r;
}

static inline void
fallback_dispatch_init_switch(struct fallback_dispatch *dispatch,
			      struct evdev_device *device)
{
	int val;

	if (device->tags & EVDEV_TAG_LID_SWITCH) {
		libinput_device_init_event_listener(&dispatch->lid.listener);
		dispatch->lid.reliability = evdev_read_switch_reliability_prop(device);
		dispatch->lid.is_closed = false;
	}

	if (device->tags & EVDEV_TAG_TABLET_MODE_SWITCH) {
		val = libevdev_get_event_value(device->evdev,
					       EV_SW,
					       SW_TABLET_MODE);
		dispatch->tablet_mode.state = val;
	}
}

static struct evdev_dispatch *
fallback_dispatch_create(struct libinput_device *libinput_device)
{
	struct evdev_device *device = evdev_device(libinput_device);
	struct fallback_dispatch *dispatch;
	char timer_name[64];

	dispatch = zalloc(sizeof *dispatch);
	dispatch->device = evdev_device(libinput_device);
	dispatch->base.dispatch_type = DISPATCH_FALLBACK;
	dispatch->base.interface = &fallback_interface;
	dispatch->pending_event = EVDEV_NONE;

	fallback_dispatch_init_rel(dispatch, device);
	fallback_dispatch_init_abs(dispatch, device);
	if (fallback_dispatch_init_slots(dispatch, device) == -1) {
		free(dispatch);
		return NULL;
	}

	fallback_dispatch_init_switch(dispatch, device);

	if (device->left_handed.want_enabled)
		evdev_init_left_handed(device,
				       evdev_change_to_left_handed);

	if (device->scroll.want_button)
		evdev_init_button_scroll(device,
					 evdev_change_scroll_method);

	if (device->scroll.natural_scrolling_enabled)
		evdev_init_natural_scroll(device);

	evdev_init_calibration(device, &dispatch->calibration);
	evdev_init_sendevents(device, &dispatch->base);
	evdev_init_rotation(device, dispatch);

	/* BTN_MIDDLE is set on mice even when it's not present. So
	 * we can only use the absence of BTN_MIDDLE to mean something, i.e.
	 * we enable it by default on anything that only has L&R.
	 * If we have L&R and no middle, we don't expose it as config
	 * option */
	if (libevdev_has_event_code(device->evdev, EV_KEY, BTN_LEFT) &&
	    libevdev_has_event_code(device->evdev, EV_KEY, BTN_RIGHT)) {
		bool has_middle = libevdev_has_event_code(device->evdev,
							  EV_KEY,
							  BTN_MIDDLE);
		bool want_config = has_middle;
		bool enable_by_default = !has_middle;

		evdev_init_middlebutton(device,
					enable_by_default,
					want_config);
	}

	snprintf(timer_name,
		 sizeof(timer_name),
		 "%s debounce",
		 evdev_device_get_sysname(device));
	libinput_timer_init(&dispatch->debounce.timer,
			    evdev_libinput_context(device),
			    timer_name,
			    fallback_debounce_timeout,
			    device);
	return &dispatch->base;
}

static inline void
evdev_process_event(struct evdev_device *device, struct input_event *e)
{
	struct evdev_dispatch *dispatch = device->dispatch;
	uint64_t time = tv2us(&e->time);

#if 0
	if (libevdev_event_is_code(e, EV_SYN, SYN_REPORT))
		evdev_log_debug(device,
			  "-------------- EV_SYN ------------\n");
	else
		evdev_log_debug(device,
			  "%-16s %-20s %4d\n",
			  libevdev_event_type_get_name(e->type),
			  libevdev_event_code_get_name(e->type, e->code),
			  e->value);
#endif

	dispatch->interface->process(dispatch, device, e, time);
}

static inline void
evdev_device_dispatch_one(struct evdev_device *device,
			  struct input_event *ev)
{
	if (!device->mtdev) {
		evdev_process_event(device, ev);
	} else {
		mtdev_put_event(device->mtdev, ev);
		if (libevdev_event_is_code(ev, EV_SYN, SYN_REPORT)) {
			while (!mtdev_empty(device->mtdev)) {
				struct input_event e;
				mtdev_get_event(device->mtdev, &e);
				evdev_process_event(device, &e);
			}
		}
	}
}

static int
evdev_sync_device(struct evdev_device *device)
{
	struct input_event ev;
	int rc;

	do {
		rc = libevdev_next_event(device->evdev,
					 LIBEVDEV_READ_FLAG_SYNC, &ev);
		if (rc < 0)
			break;
		evdev_device_dispatch_one(device, &ev);
	} while (rc == LIBEVDEV_READ_STATUS_SYNC);

	return rc == -EAGAIN ? 0 : rc;
}

static void
evdev_device_dispatch(void *data)
{
	struct evdev_device *device = data;
	struct libinput *libinput = evdev_libinput_context(device);
	struct input_event ev;
	int rc;

	/* If the compositor is repainting, this function is called only once
	 * per frame and we have to process all the events available on the
	 * fd, otherwise there will be input lag. */
	do {
		rc = libevdev_next_event(device->evdev,
					 LIBEVDEV_READ_FLAG_NORMAL, &ev);
		if (rc == LIBEVDEV_READ_STATUS_SYNC) {
			evdev_log_info_ratelimit(device,
						 &device->syn_drop_limit,
						 "SYN_DROPPED event - some input events have been lost.\n");

			/* send one more sync event so we handle all
			   currently pending events before we sync up
			   to the current state */
			ev.code = SYN_REPORT;
			evdev_device_dispatch_one(device, &ev);

			rc = evdev_sync_device(device);
			if (rc == 0)
				rc = LIBEVDEV_READ_STATUS_SUCCESS;
		} else if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
			evdev_device_dispatch_one(device, &ev);
		}
	} while (rc == LIBEVDEV_READ_STATUS_SUCCESS);

	if (rc != -EAGAIN && rc != -EINTR) {
		libinput_remove_source(libinput, device->source);
		device->source = NULL;
	}
}

static inline bool
evdev_init_accel(struct evdev_device *device,
		 enum libinput_config_accel_profile which)
{
	struct motion_filter *filter;

	if (which == LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT)
		filter = create_pointer_accelerator_filter_flat(device->dpi);
	else if (device->tags & EVDEV_TAG_TRACKPOINT)
		filter = create_pointer_accelerator_filter_trackpoint(device->trackpoint_range);
	else if (device->dpi < DEFAULT_MOUSE_DPI)
		filter = create_pointer_accelerator_filter_linear_low_dpi(device->dpi);
	else
		filter = create_pointer_accelerator_filter_linear(device->dpi);

	if (!filter)
		return false;

	evdev_device_init_pointer_acceleration(device, filter);

	return true;
}

static int
evdev_accel_config_available(struct libinput_device *device)
{
	/* this function is only called if we set up ptraccel, so we can
	   reply with a resounding "Yes" */
	return 1;
}

static enum libinput_config_status
evdev_accel_config_set_speed(struct libinput_device *device, double speed)
{
	struct evdev_device *dev = evdev_device(device);

	if (!filter_set_speed(dev->pointer.filter, speed))
		return LIBINPUT_CONFIG_STATUS_INVALID;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static double
evdev_accel_config_get_speed(struct libinput_device *device)
{
	struct evdev_device *dev = evdev_device(device);

	return filter_get_speed(dev->pointer.filter);
}

static double
evdev_accel_config_get_default_speed(struct libinput_device *device)
{
	return 0.0;
}

static uint32_t
evdev_accel_config_get_profiles(struct libinput_device *libinput_device)
{
	struct evdev_device *device = evdev_device(libinput_device);

	if (!device->pointer.filter)
		return LIBINPUT_CONFIG_ACCEL_PROFILE_NONE;

	return LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE |
		LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
}

static enum libinput_config_status
evdev_accel_config_set_profile(struct libinput_device *libinput_device,
			       enum libinput_config_accel_profile profile)
{
	struct evdev_device *device = evdev_device(libinput_device);
	struct motion_filter *filter;
	double speed;

	filter = device->pointer.filter;
	if (filter_get_type(filter) == profile)
		return LIBINPUT_CONFIG_STATUS_SUCCESS;

	speed = filter_get_speed(filter);
	device->pointer.filter = NULL;

	if (evdev_init_accel(device, profile)) {
		evdev_accel_config_set_speed(libinput_device, speed);
		filter_destroy(filter);
	} else {
		device->pointer.filter = filter;
	}

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_accel_profile
evdev_accel_config_get_profile(struct libinput_device *libinput_device)
{
	struct evdev_device *device = evdev_device(libinput_device);

	return filter_get_type(device->pointer.filter);
}

static enum libinput_config_accel_profile
evdev_accel_config_get_default_profile(struct libinput_device *libinput_device)
{
	struct evdev_device *device = evdev_device(libinput_device);

	if (!device->pointer.filter)
		return LIBINPUT_CONFIG_ACCEL_PROFILE_NONE;

	/* No device has a flat profile as default */
	return LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
}

void
evdev_device_init_pointer_acceleration(struct evdev_device *device,
				       struct motion_filter *filter)
{
	device->pointer.filter = filter;

	if (device->base.config.accel == NULL) {
		device->pointer.config.available = evdev_accel_config_available;
		device->pointer.config.set_speed = evdev_accel_config_set_speed;
		device->pointer.config.get_speed = evdev_accel_config_get_speed;
		device->pointer.config.get_default_speed = evdev_accel_config_get_default_speed;
		device->pointer.config.get_profiles = evdev_accel_config_get_profiles;
		device->pointer.config.set_profile = evdev_accel_config_set_profile;
		device->pointer.config.get_profile = evdev_accel_config_get_profile;
		device->pointer.config.get_default_profile = evdev_accel_config_get_default_profile;
		device->base.config.accel = &device->pointer.config;

		evdev_accel_config_set_speed(&device->base,
			     evdev_accel_config_get_default_speed(&device->base));
	}
}

static inline bool
evdev_read_wheel_click_prop(struct evdev_device *device,
			    const char *prop,
			    double *angle)
{
	int val;

	*angle = DEFAULT_WHEEL_CLICK_ANGLE;
	prop = udev_device_get_property_value(device->udev_device, prop);
	if (!prop)
		return false;

	val = parse_mouse_wheel_click_angle_property(prop);
	if (val) {
		*angle = val;
		return true;
	}

	evdev_log_error(device,
		  "mouse wheel click angle is present but invalid, "
		  "using %d degrees instead\n",
		  DEFAULT_WHEEL_CLICK_ANGLE);

	return false;
}

static inline bool
evdev_read_wheel_click_count_prop(struct evdev_device *device,
				  const char *prop,
				  double *angle)
{
	int val;

	prop = udev_device_get_property_value(device->udev_device, prop);
	if (!prop)
		return false;

	val = parse_mouse_wheel_click_angle_property(prop);
	if (val) {
		*angle = 360.0/val;
		return true;
	}

	evdev_log_error(device,
		  "mouse wheel click count is present but invalid, "
		  "using %d degrees for angle instead instead\n",
		  DEFAULT_WHEEL_CLICK_ANGLE);
	*angle = DEFAULT_WHEEL_CLICK_ANGLE;

	return false;
}

static inline struct wheel_angle
evdev_read_wheel_click_props(struct evdev_device *device)
{
	struct wheel_angle angles;

	/* CLICK_COUNT overrides CLICK_ANGLE */
	if (!evdev_read_wheel_click_count_prop(device,
					      "MOUSE_WHEEL_CLICK_COUNT",
					      &angles.x))
		evdev_read_wheel_click_prop(device,
					    "MOUSE_WHEEL_CLICK_ANGLE",
					    &angles.x);
	if (!evdev_read_wheel_click_count_prop(device,
					      "MOUSE_WHEEL_CLICK_COUNT_HORIZONTAL",
					      &angles.y)) {
		if (!evdev_read_wheel_click_prop(device,
						 "MOUSE_WHEEL_CLICK_ANGLE_HORIZONTAL",
						 &angles.y))
			angles.y = angles.x;
	}

	return angles;
}

static inline struct wheel_tilt_flags
evdev_read_wheel_tilt_props(struct evdev_device *device)
{
	struct wheel_tilt_flags flags;

	flags.vertical = parse_udev_flag(device,
					 device->udev_device,
					 "MOUSE_WHEEL_TILT_VERTICAL");

	flags.horizontal = parse_udev_flag(device,
					 device->udev_device,
					 "MOUSE_WHEEL_TILT_HORIZONTAL");
	return flags;
}

static inline int
evdev_get_trackpoint_range(struct evdev_device *device)
{
	const char *prop;
	int range = DEFAULT_TRACKPOINT_RANGE;

	if (!(device->tags & EVDEV_TAG_TRACKPOINT))
		return DEFAULT_TRACKPOINT_RANGE;

	prop = udev_device_get_property_value(device->udev_device,
					      "LIBINPUT_ATTR_TRACKPOINT_RANGE");
	if (prop) {
		if (!safe_atoi(prop, &range) ||
		    (range < 0.0 || range > 100)) {
			evdev_log_error(device,
					"trackpoint range property is present but invalid, "
					"using %d instead\n",
					DEFAULT_TRACKPOINT_RANGE);
			range = DEFAULT_TRACKPOINT_RANGE;
		}
		goto out;
	}

	prop = udev_device_get_property_value(device->udev_device,
					      "POINTINGSTICK_SENSITIVITY");
	if (prop) {
		int sensitivity;

		if (!safe_atoi(prop, &sensitivity) ||
		    (sensitivity < 0.0 || sensitivity > 255)) {
			evdev_log_error(device,
					"trackpoint sensitivity property is present but invalid, "
					"using %d instead\n",
					DEFAULT_TRACKPOINT_SENSITIVITY);
			sensitivity = DEFAULT_TRACKPOINT_SENSITIVITY;
		}
		range = 1.0 * DEFAULT_TRACKPOINT_RANGE *
			sensitivity/DEFAULT_TRACKPOINT_SENSITIVITY;
	}

out:
	evdev_log_info(device, "trackpoint device set to range %d\n", range);
	return range;
}

static inline int
evdev_read_dpi_prop(struct evdev_device *device)
{
	const char *mouse_dpi;
	int dpi = DEFAULT_MOUSE_DPI;

	if (device->tags & EVDEV_TAG_TRACKPOINT)
		return DEFAULT_MOUSE_DPI;

	mouse_dpi = udev_device_get_property_value(device->udev_device,
						   "MOUSE_DPI");
	if (mouse_dpi) {
		dpi = parse_mouse_dpi_property(mouse_dpi);
		if (!dpi) {
			evdev_log_error(device,
					"mouse DPI property is present but invalid, "
					"using %d DPI instead\n",
					DEFAULT_MOUSE_DPI);
			dpi = DEFAULT_MOUSE_DPI;
		}
		evdev_log_info(device,
			       "device set to %d DPI\n",
			       dpi);
	}

	return dpi;
}

static inline uint32_t
evdev_read_model_flags(struct evdev_device *device)
{
	const struct model_map {
		const char *property;
		enum evdev_device_model model;
	} model_map[] = {
#define MODEL(name) { "LIBINPUT_MODEL_" #name, EVDEV_MODEL_##name }
		MODEL(LENOVO_X230),
		MODEL(LENOVO_X230),
		MODEL(LENOVO_X220_TOUCHPAD_FW81),
		MODEL(CHROMEBOOK),
		MODEL(SYSTEM76_BONOBO),
		MODEL(SYSTEM76_GALAGO),
		MODEL(SYSTEM76_KUDU),
		MODEL(CLEVO_W740SU),
		MODEL(APPLE_TOUCHPAD),
		MODEL(WACOM_TOUCHPAD),
		MODEL(ALPS_TOUCHPAD),
		MODEL(SYNAPTICS_SERIAL_TOUCHPAD),
		MODEL(JUMPING_SEMI_MT),
		MODEL(CYBORG_RAT),
		MODEL(HP_STREAM11_TOUCHPAD),
		MODEL(LENOVO_T450_TOUCHPAD),
		MODEL(TOUCHPAD_VISIBLE_MARKER),
		MODEL(TRACKBALL),
		MODEL(APPLE_MAGICMOUSE),
		MODEL(HP8510_TOUCHPAD),
		MODEL(HP6910_TOUCHPAD),
		MODEL(HP_ZBOOK_STUDIO_G3),
		MODEL(HP_PAVILION_DM4_TOUCHPAD),
		MODEL(APPLE_TOUCHPAD_ONEBUTTON),
		MODEL(LOGITECH_MARBLE_MOUSE),
#undef MODEL
		{ "ID_INPUT_TRACKBALL", EVDEV_MODEL_TRACKBALL },
		{ NULL, EVDEV_MODEL_DEFAULT },
	};
	const struct model_map *m = model_map;
	uint32_t model_flags = 0;

	while (m->property) {
		if (parse_udev_flag(device,
				    device->udev_device,
				    m->property)) {
			evdev_log_debug(device, "tagged as %s\n", m->property);
			model_flags |= m->model;
		}
		m++;
	}

	return model_flags;
}

static inline bool
evdev_read_attr_res_prop(struct evdev_device *device,
			 size_t *xres,
			 size_t *yres)
{
	struct udev_device *udev;
	const char *res_prop;

	udev = device->udev_device;
	res_prop = udev_device_get_property_value(udev,
						   "LIBINPUT_ATTR_RESOLUTION_HINT");
	if (!res_prop)
		return false;

	return parse_dimension_property(res_prop, xres, yres);
}

static inline bool
evdev_read_attr_size_prop(struct evdev_device *device,
			  size_t *size_x,
			  size_t *size_y)
{
	struct udev_device *udev;
	const char *size_prop;

	udev = device->udev_device;
	size_prop = udev_device_get_property_value(udev,
						   "LIBINPUT_ATTR_SIZE_HINT");
	if (!size_prop)
		return false;

	return parse_dimension_property(size_prop, size_x, size_y);
}

/* Return 1 if the device is set to the fake resolution or 0 otherwise */
static inline int
evdev_fix_abs_resolution(struct evdev_device *device,
			 unsigned int xcode,
			 unsigned int ycode)
{
	struct libevdev *evdev = device->evdev;
	const struct input_absinfo *absx, *absy;
	size_t widthmm = 0, heightmm = 0;
	size_t xres = EVDEV_FAKE_RESOLUTION,
	       yres = EVDEV_FAKE_RESOLUTION;

	if (!(xcode == ABS_X && ycode == ABS_Y)  &&
	    !(xcode == ABS_MT_POSITION_X && ycode == ABS_MT_POSITION_Y)) {
		evdev_log_bug_libinput(device,
				       "invalid x/y code combination %d/%d\n",
				       xcode,
				       ycode);
		return 0;
	}

	absx = libevdev_get_abs_info(evdev, xcode);
	absy = libevdev_get_abs_info(evdev, ycode);

	if (absx->resolution != 0 || absy->resolution != 0)
		return 0;

	/* Note: we *do not* override resolutions if provided by the kernel.
	 * If a device needs this, add it to 60-evdev.hwdb. The libinput
	 * property is only for general size hints where we can make
	 * educated guesses but don't know better.
	 */
	if (!evdev_read_attr_res_prop(device, &xres, &yres) &&
	    evdev_read_attr_size_prop(device, &widthmm, &heightmm)) {
		xres = (absx->maximum - absx->minimum)/widthmm;
		yres = (absy->maximum - absy->minimum)/heightmm;
	}

	/* libevdev_set_abs_resolution() changes the absinfo we already
	   have a pointer to, no need to fetch it again */
	libevdev_set_abs_resolution(evdev, xcode, xres);
	libevdev_set_abs_resolution(evdev, ycode, yres);

	return xres == EVDEV_FAKE_RESOLUTION;
}

static enum evdev_device_udev_tags
evdev_device_get_udev_tags(struct evdev_device *device,
			   struct udev_device *udev_device)
{
	enum evdev_device_udev_tags tags = 0;
	int i;

	for (i = 0; i < 2 && udev_device; i++) {
		unsigned j;
		for (j = 0; j < ARRAY_LENGTH(evdev_udev_tag_matches); j++) {
			const struct evdev_udev_tag_match match = evdev_udev_tag_matches[j];
			if (parse_udev_flag(device,
					    udev_device,
					    match.name))
				tags |= match.tag;
		}
		udev_device = udev_device_get_parent(udev_device);
	}

	return tags;
}

static inline void
evdev_fix_android_mt(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_X) ||
	    libevdev_has_event_code(evdev, EV_ABS, ABS_Y))
		return;

	if (!libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) ||
	    !libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y) ||
	    evdev_is_fake_mt_device(device))
		return;

	libevdev_enable_event_code(evdev, EV_ABS, ABS_X,
		      libevdev_get_abs_info(evdev, ABS_MT_POSITION_X));
	libevdev_enable_event_code(evdev, EV_ABS, ABS_Y,
		      libevdev_get_abs_info(evdev, ABS_MT_POSITION_Y));
}

static inline bool
evdev_check_min_max(struct evdev_device *device, unsigned int code)
{
	struct libevdev *evdev = device->evdev;
	const struct input_absinfo *absinfo;

	if (!libevdev_has_event_code(evdev, EV_ABS, code))
		return true;

	absinfo = libevdev_get_abs_info(evdev, code);
	if (absinfo->minimum == absinfo->maximum) {
		/* Some devices have a sort-of legitimate min/max of 0 for
		 * ABS_MISC and above (e.g. Roccat Kone XTD). Don't ignore
		 * them, simply disable the axes so we won't get events,
		 * we don't know what to do with them anyway.
		 */
		if (absinfo->minimum == 0 &&
		    code >= ABS_MISC && code < ABS_MT_SLOT) {
			evdev_log_info(device,
				       "disabling EV_ABS %#x on device (min == max == 0)\n",
				       code);
			libevdev_disable_event_code(device->evdev,
						    EV_ABS,
						    code);
		} else {
			evdev_log_bug_kernel(device,
					     "device has min == max on %s\n",
					     libevdev_event_code_get_name(EV_ABS, code));
			return false;
		}
	}

	return true;
}

static bool
evdev_reject_device(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;
	unsigned int code;
	const struct input_absinfo *absx, *absy;

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_X) ^
	    libevdev_has_event_code(evdev, EV_ABS, ABS_Y))
		return true;

	if (libevdev_has_event_code(evdev, EV_REL, REL_X) ^
	    libevdev_has_event_code(evdev, EV_REL, REL_Y))
		return true;

	if (!evdev_is_fake_mt_device(device) &&
	    libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) ^
	    libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y))
		return true;

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_X)) {
		absx = libevdev_get_abs_info(evdev, ABS_X);
		absy = libevdev_get_abs_info(evdev, ABS_Y);
		if ((absx->resolution == 0 && absy->resolution != 0) ||
		    (absx->resolution != 0 && absy->resolution == 0)) {
			evdev_log_bug_kernel(device,
				       "kernel has only x or y resolution, not both.\n");
			return true;
		}
	}

	if (!evdev_is_fake_mt_device(device) &&
	    libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X)) {
		absx = libevdev_get_abs_info(evdev, ABS_MT_POSITION_X);
		absy = libevdev_get_abs_info(evdev, ABS_MT_POSITION_Y);
		if ((absx->resolution == 0 && absy->resolution != 0) ||
		    (absx->resolution != 0 && absy->resolution == 0)) {
			evdev_log_bug_kernel(device,
				       "kernel has only x or y MT resolution, not both.\n");
			return true;
		}
	}

	for (code = 0; code < ABS_CNT; code++) {
		switch (code) {
		case ABS_MISC:
		case ABS_MT_SLOT:
		case ABS_MT_TOOL_TYPE:
			break;
		default:
			if (!evdev_check_min_max(device, code))
				return true;
		}
	}

	return false;
}

static void
evdev_extract_abs_axes(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;

	if (!libevdev_has_event_code(evdev, EV_ABS, ABS_X) ||
	    !libevdev_has_event_code(evdev, EV_ABS, ABS_Y))
		 return;

	if (evdev_fix_abs_resolution(device, ABS_X, ABS_Y))
		device->abs.is_fake_resolution = true;
	device->abs.absinfo_x = libevdev_get_abs_info(evdev, ABS_X);
	device->abs.absinfo_y = libevdev_get_abs_info(evdev, ABS_Y);
	device->abs.dimensions.x = abs(device->abs.absinfo_x->maximum -
				       device->abs.absinfo_x->minimum);
	device->abs.dimensions.y = abs(device->abs.absinfo_y->maximum -
				       device->abs.absinfo_y->minimum);

	if (evdev_is_fake_mt_device(device) ||
	    !libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) ||
	    !libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y))
		 return;

	if (evdev_fix_abs_resolution(device,
				     ABS_MT_POSITION_X,
				     ABS_MT_POSITION_Y))
		device->abs.is_fake_resolution = true;

	device->abs.absinfo_x = libevdev_get_abs_info(evdev, ABS_MT_POSITION_X);
	device->abs.absinfo_y = libevdev_get_abs_info(evdev, ABS_MT_POSITION_Y);
	device->abs.dimensions.x = abs(device->abs.absinfo_x->maximum -
				       device->abs.absinfo_x->minimum);
	device->abs.dimensions.y = abs(device->abs.absinfo_y->maximum -
				       device->abs.absinfo_y->minimum);
	device->is_mt = 1;
}

static void
evdev_disable_accelerometer_axes(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;

	libevdev_disable_event_code(evdev, EV_ABS, ABS_X);
	libevdev_disable_event_code(evdev, EV_ABS, ABS_Y);
	libevdev_disable_event_code(evdev, EV_ABS, ABS_Z);

	libevdev_disable_event_code(evdev, EV_ABS, REL_X);
	libevdev_disable_event_code(evdev, EV_ABS, REL_Y);
	libevdev_disable_event_code(evdev, EV_ABS, REL_Z);
}

static struct evdev_dispatch *
evdev_configure_device(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;
	enum evdev_device_udev_tags udev_tags;
	unsigned int tablet_tags;
	struct evdev_dispatch *dispatch;

	udev_tags = evdev_device_get_udev_tags(device, device->udev_device);

	if ((udev_tags & EVDEV_UDEV_TAG_INPUT) == 0 ||
	    (udev_tags & ~EVDEV_UDEV_TAG_INPUT) == 0) {
		evdev_log_info(device,
			       "not tagged as supported input device\n");
		return NULL;
	}

	evdev_log_info(device,
		 "is tagged by udev as:%s%s%s%s%s%s%s%s%s%s%s\n",
		 udev_tags & EVDEV_UDEV_TAG_KEYBOARD ? " Keyboard" : "",
		 udev_tags & EVDEV_UDEV_TAG_MOUSE ? " Mouse" : "",
		 udev_tags & EVDEV_UDEV_TAG_TOUCHPAD ? " Touchpad" : "",
		 udev_tags & EVDEV_UDEV_TAG_TOUCHSCREEN ? " Touchscreen" : "",
		 udev_tags & EVDEV_UDEV_TAG_TABLET ? " Tablet" : "",
		 udev_tags & EVDEV_UDEV_TAG_POINTINGSTICK ? " Pointingstick" : "",
		 udev_tags & EVDEV_UDEV_TAG_JOYSTICK ? " Joystick" : "",
		 udev_tags & EVDEV_UDEV_TAG_ACCELEROMETER ? " Accelerometer" : "",
		 udev_tags & EVDEV_UDEV_TAG_TABLET_PAD ? " TabletPad" : "",
		 udev_tags & EVDEV_UDEV_TAG_TRACKBALL ? " Trackball" : "",
		 udev_tags & EVDEV_UDEV_TAG_SWITCH ? " Switch" : "");

	/* Ignore pure accelerometers, but accept devices that are
	 * accelerometers with other axes */
	if (udev_tags == (EVDEV_UDEV_TAG_INPUT|EVDEV_UDEV_TAG_ACCELEROMETER)) {
		evdev_log_info(device,
			 "device is an accelerometer, ignoring\n");
		return NULL;
	} else if (udev_tags & EVDEV_UDEV_TAG_ACCELEROMETER) {
		evdev_disable_accelerometer_axes(device);
	}

	/* libwacom *adds* TABLET, TOUCHPAD but leaves JOYSTICK in place, so
	   make sure we only ignore real joystick devices */
	if (udev_tags == (EVDEV_UDEV_TAG_INPUT|EVDEV_UDEV_TAG_JOYSTICK)) {
		evdev_log_info(device,
			       "device is a joystick, ignoring\n");
		return NULL;
	}

	if (evdev_reject_device(device)) {
		evdev_log_info(device, "was rejected\n");
		return NULL;
	}

	if (!evdev_is_fake_mt_device(device))
		evdev_fix_android_mt(device);

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_X)) {
		evdev_extract_abs_axes(device);

		if (evdev_is_fake_mt_device(device))
			udev_tags &= ~EVDEV_UDEV_TAG_TOUCHSCREEN;
	}

	/* libwacom assigns touchpad (or touchscreen) _and_ tablet to the
	   tablet touch bits, so make sure we don't initialize the tablet
	   interface for the touch device */
	tablet_tags = EVDEV_UDEV_TAG_TABLET |
		      EVDEV_UDEV_TAG_TOUCHPAD |
		      EVDEV_UDEV_TAG_TOUCHSCREEN;

	/* libwacom assigns tablet _and_ tablet_pad to the pad devices */
	if (udev_tags & EVDEV_UDEV_TAG_TABLET_PAD) {
		dispatch = evdev_tablet_pad_create(device);
		device->seat_caps |= EVDEV_DEVICE_TABLET_PAD;
		evdev_log_info(device, "device is a tablet pad\n");
		return dispatch;

	} else if ((udev_tags & tablet_tags) == EVDEV_UDEV_TAG_TABLET) {
		dispatch = evdev_tablet_create(device);
		device->seat_caps |= EVDEV_DEVICE_TABLET;
		evdev_log_info(device, "device is a tablet\n");
		return dispatch;
	}

	if (udev_tags & EVDEV_UDEV_TAG_TOUCHPAD) {
		dispatch = evdev_mt_touchpad_create(device);
		evdev_log_info(device, "device is a touchpad\n");
		return dispatch;
	}

	if (udev_tags & EVDEV_UDEV_TAG_MOUSE ||
	    udev_tags & EVDEV_UDEV_TAG_POINTINGSTICK) {
		evdev_tag_external_mouse(device, device->udev_device);
		evdev_tag_trackpoint(device, device->udev_device);
		device->dpi = evdev_read_dpi_prop(device);
		device->trackpoint_range = evdev_get_trackpoint_range(device);

		device->seat_caps |= EVDEV_DEVICE_POINTER;

		evdev_log_info(device, "device is a pointer\n");

		/* want left-handed config option */
		device->left_handed.want_enabled = true;
		/* want natural-scroll config option */
		device->scroll.natural_scrolling_enabled = true;
		/* want button scrolling config option */
		if (libevdev_has_event_code(evdev, EV_REL, REL_X) ||
		    libevdev_has_event_code(evdev, EV_REL, REL_Y))
			device->scroll.want_button = 1;
	}

	if (udev_tags & EVDEV_UDEV_TAG_KEYBOARD) {
		device->seat_caps |= EVDEV_DEVICE_KEYBOARD;
		evdev_log_info(device, "device is a keyboard\n");

		/* want natural-scroll config option */
		if (libevdev_has_event_code(evdev, EV_REL, REL_WHEEL) ||
		    libevdev_has_event_code(evdev, EV_REL, REL_HWHEEL)) {
			device->scroll.natural_scrolling_enabled = true;
			device->seat_caps |= EVDEV_DEVICE_POINTER;
		}

		evdev_tag_keyboard(device, device->udev_device);
	}

	if (udev_tags & EVDEV_UDEV_TAG_TOUCHSCREEN) {
		device->seat_caps |= EVDEV_DEVICE_TOUCH;
		evdev_log_info(device, "device is a touch device\n");
	}

	if (udev_tags & EVDEV_UDEV_TAG_SWITCH) {
		if (libevdev_has_event_code(evdev, EV_SW, SW_LID)) {
			device->seat_caps |= EVDEV_DEVICE_SWITCH;
			device->tags |= EVDEV_TAG_LID_SWITCH;
			evdev_log_info(device, "device is a switch device\n");
		}

		if (libevdev_has_event_code(evdev, EV_SW, SW_TABLET_MODE)) {
			device->seat_caps |= EVDEV_DEVICE_SWITCH;
			device->tags |= EVDEV_TAG_TABLET_MODE_SWITCH;
			evdev_log_info(device, "device is a switch device\n");
		}
	}

	if (device->seat_caps & EVDEV_DEVICE_POINTER &&
	    libevdev_has_event_code(evdev, EV_REL, REL_X) &&
	    libevdev_has_event_code(evdev, EV_REL, REL_Y) &&
	    !evdev_init_accel(device, LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE)) {
		evdev_log_error(device,
				"failed to initialize pointer acceleration\n");
		return NULL;
	}

	return fallback_dispatch_create(&device->base);
}

static void
evdev_notify_added_device(struct evdev_device *device)
{
	struct libinput_device *dev;

	list_for_each(dev, &device->base.seat->devices_list, link) {
		struct evdev_device *d = evdev_device(dev);
		if (dev == &device->base)
			continue;

		/* Notify existing device d about addition of device */
		if (d->dispatch->interface->device_added)
			d->dispatch->interface->device_added(d, device);

		/* Notify new device about existing device d */
		if (device->dispatch->interface->device_added)
			device->dispatch->interface->device_added(device, d);

		/* Notify new device if existing device d is suspended */
		if (d->is_suspended &&
		    device->dispatch->interface->device_suspended)
			device->dispatch->interface->device_suspended(device, d);
	}

	notify_added_device(&device->base);

	if (device->dispatch->interface->post_added)
		device->dispatch->interface->post_added(device,
							device->dispatch);
}

static bool
evdev_device_have_same_syspath(struct udev_device *udev_device, int fd)
{
	struct udev *udev = udev_device_get_udev(udev_device);
	struct udev_device *udev_device_new = NULL;
	struct stat st;
	bool rc = false;

	if (fstat(fd, &st) < 0)
		goto out;

	udev_device_new = udev_device_new_from_devnum(udev, 'c', st.st_rdev);
	if (!udev_device_new)
		goto out;

	rc = streq(udev_device_get_syspath(udev_device_new),
		   udev_device_get_syspath(udev_device));
out:
	if (udev_device_new)
		udev_device_unref(udev_device_new);
	return rc;
}

static bool
evdev_set_device_group(struct evdev_device *device,
		       struct udev_device *udev_device)
{
	struct libinput *libinput = evdev_libinput_context(device);
	struct libinput_device_group *group = NULL;
	const char *udev_group;

	udev_group = udev_device_get_property_value(udev_device,
						    "LIBINPUT_DEVICE_GROUP");
	if (udev_group)
		group = libinput_device_group_find_group(libinput, udev_group);

	if (!group) {
		group = libinput_device_group_create(libinput, udev_group);
		if (!group)
			return false;
		libinput_device_set_device_group(&device->base, group);
		libinput_device_group_unref(group);
	} else {
		libinput_device_set_device_group(&device->base, group);
	}

	return true;
}

static inline void
evdev_drain_fd(int fd)
{
	struct input_event ev[24];
	size_t sz = sizeof ev;

	while (read(fd, &ev, sz) == (int)sz) {
		/* discard all pending events */
	}
}

static inline void
evdev_pre_configure_model_quirks(struct evdev_device *device)
{
	/* The Cyborg RAT has a mode button that cycles through event codes.
	 * On press, we get a release for the current mode and a press for the
	 * next mode:
	 * E: 0.000001 0004 0004 589833	# EV_MSC / MSC_SCAN             589833
	 * E: 0.000001 0001 0118 0000	# EV_KEY / (null)               0
	 * E: 0.000001 0004 0004 589834	# EV_MSC / MSC_SCAN             589834
	 * E: 0.000001 0001 0119 0001	# EV_KEY / (null)               1
	 * E: 0.000001 0000 0000 0000	# ------------ SYN_REPORT (0) ---------- +0ms
	 * E: 0.705000 0004 0004 589834	# EV_MSC / MSC_SCAN             589834
	 * E: 0.705000 0001 0119 0000	# EV_KEY / (null)               0
	 * E: 0.705000 0004 0004 589835	# EV_MSC / MSC_SCAN             589835
	 * E: 0.705000 0001 011a 0001	# EV_KEY / (null)               1
	 * E: 0.705000 0000 0000 0000	# ------------ SYN_REPORT (0) ---------- +705ms
	 * E: 1.496995 0004 0004 589833	# EV_MSC / MSC_SCAN             589833
	 * E: 1.496995 0001 0118 0001	# EV_KEY / (null)               1
	 * E: 1.496995 0004 0004 589835	# EV_MSC / MSC_SCAN             589835
	 * E: 1.496995 0001 011a 0000	# EV_KEY / (null)               0
	 * E: 1.496995 0000 0000 0000	# ------------ SYN_REPORT (0) ---------- +791ms
	 *
	 * https://bugs.freedesktop.org/show_bug.cgi?id=92127
	 *
	 * Disable the event codes to avoid stuck buttons.
	 */
	if (device->model_flags & EVDEV_MODEL_CYBORG_RAT) {
		libevdev_disable_event_code(device->evdev, EV_KEY, 0x118);
		libevdev_disable_event_code(device->evdev, EV_KEY, 0x119);
		libevdev_disable_event_code(device->evdev, EV_KEY, 0x11a);
	}
	/* The Apple MagicMouse has a touchpad built-in but the kernel still
	 * emulates a full 2/3 button mouse for us. Ignore anything from the
	 * ABS interface
	 */
	if (device->model_flags & EVDEV_MODEL_APPLE_MAGICMOUSE)
		libevdev_disable_event_type(device->evdev, EV_ABS);

	/* Claims to have double/tripletap but doesn't actually send it
	 * https://bugzilla.redhat.com/show_bug.cgi?id=1351285 and
	 * https://bugs.freedesktop.org/show_bug.cgi?id=98538
	 */
	if (device->model_flags &
	    (EVDEV_MODEL_HP8510_TOUCHPAD|EVDEV_MODEL_HP6910_TOUCHPAD)) {
		libevdev_disable_event_code(device->evdev, EV_KEY, BTN_TOOL_DOUBLETAP);
		libevdev_disable_event_code(device->evdev, EV_KEY, BTN_TOOL_TRIPLETAP);
	}

	/* Touchpad is a clickpad but INPUT_PROP_BUTTONPAD is not set, see
	 * fdo bug 97147. Remove when RMI4 is commonplace */
	if (device->model_flags & EVDEV_MODEL_HP_STREAM11_TOUCHPAD)
		libevdev_enable_property(device->evdev,
					 INPUT_PROP_BUTTONPAD);

	/* Touchpad claims to have 4 slots but only ever sends 2
	 * https://bugs.freedesktop.org/show_bug.cgi?id=98100 */
	if (device->model_flags & EVDEV_MODEL_HP_ZBOOK_STUDIO_G3)
		libevdev_set_abs_maximum(device->evdev, ABS_MT_SLOT, 1);

	/* Logitech Marble Mouse claims to have a middle button */
	if (device->model_flags & EVDEV_MODEL_LOGITECH_MARBLE_MOUSE)
		libevdev_disable_event_code(device->evdev, EV_KEY, BTN_MIDDLE);
}

static void
libevdev_log_func(const struct libevdev *evdev,
		  enum libevdev_log_priority priority,
		  void *data,
		  const char *file,
		  int line,
		  const char *func,
		  const char *format,
		  va_list args)
{
	struct libinput *libinput = data;
	enum libinput_log_priority pri = LIBEVDEV_LOG_ERROR;
	const char prefix[] = "libevdev: ";
	char fmt[strlen(format) + strlen(prefix) + 1];

	switch (priority) {
	case LIBEVDEV_LOG_ERROR:
		pri = LIBINPUT_LOG_PRIORITY_ERROR;
		break;
	case LIBEVDEV_LOG_INFO:
		pri = LIBINPUT_LOG_PRIORITY_INFO;
		break;
	case LIBEVDEV_LOG_DEBUG:
		pri = LIBINPUT_LOG_PRIORITY_DEBUG;
		break;
	}

	snprintf(fmt, sizeof(fmt), "%s%s", prefix, format);

	log_msg_va(libinput, pri, fmt, args);
}

static bool
udev_device_should_be_ignored(struct udev_device *udev_device)
{
	const char *value;

	value = udev_device_get_property_value(udev_device,
					       "LIBINPUT_IGNORE_DEVICE");

	return value && !streq(value, "0");
}

struct evdev_device *
evdev_device_create(struct libinput_seat *seat,
		    struct udev_device *udev_device)
{
	struct libinput *libinput = seat->libinput;
	struct evdev_device *device = NULL;
	int rc;
	int fd;
	int unhandled_device = 0;
	const char *devnode = udev_device_get_devnode(udev_device);
	const char *sysname = udev_device_get_sysname(udev_device);

	if (udev_device_should_be_ignored(udev_device)) {
		log_debug(libinput, "%s: device is ignored\n", sysname);
		return NULL;
	}

	/* Use non-blocking mode so that we can loop on read on
	 * evdev_device_data() until all events on the fd are
	 * read.  mtdev_get() also expects this. */
	fd = open_restricted(libinput, devnode,
			     O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		log_info(libinput,
			 "%s: opening input device '%s' failed (%s).\n",
			 sysname,
			 devnode,
			 strerror(-fd));
		return NULL;
	}

	if (!evdev_device_have_same_syspath(udev_device, fd))
		goto err;

	device = zalloc(sizeof *device);

	libinput_device_init(&device->base, seat);
	libinput_seat_ref(seat);

	evdev_drain_fd(fd);

	rc = libevdev_new_from_fd(fd, &device->evdev);
	if (rc != 0)
		goto err;

	libevdev_set_clock_id(device->evdev, CLOCK_MONOTONIC);
	libevdev_set_device_log_function(device->evdev,
					 libevdev_log_func,
					 LIBEVDEV_LOG_ERROR,
					 libinput);
	device->seat_caps = 0;
	device->is_mt = 0;
	device->mtdev = NULL;
	device->udev_device = udev_device_ref(udev_device);
	device->dispatch = NULL;
	device->fd = fd;
	device->devname = libevdev_get_name(device->evdev);
	device->scroll.threshold = 5.0; /* Default may be overridden */
	device->scroll.direction_lock_threshold = 5.0; /* Default may be overridden */
	device->scroll.direction = 0;
	device->scroll.wheel_click_angle =
		evdev_read_wheel_click_props(device);
	device->scroll.is_tilt = evdev_read_wheel_tilt_props(device);
	device->model_flags = evdev_read_model_flags(device);
	device->dpi = DEFAULT_MOUSE_DPI;

	/* at most 5 SYN_DROPPED log-messages per 30s */
	ratelimit_init(&device->syn_drop_limit, s2us(30), 5);
	/* at most 5 log-messages per 5s */
	ratelimit_init(&device->nonpointer_rel_limit, s2us(5), 5);

	matrix_init_identity(&device->abs.calibration);
	matrix_init_identity(&device->abs.usermatrix);
	matrix_init_identity(&device->abs.default_calibration);

	evdev_pre_configure_model_quirks(device);

	device->dispatch = evdev_configure_device(device);
	if (device->dispatch == NULL) {
		if (device->seat_caps == 0)
			unhandled_device = 1;
		goto err;
	}

	device->source =
		libinput_add_fd(libinput, fd, evdev_device_dispatch, device);
	if (!device->source)
		goto err;

	if (!evdev_set_device_group(device, udev_device))
		goto err;

	list_insert(seat->devices_list.prev, &device->base.link);

	evdev_notify_added_device(device);

	return device;

err:
	if (fd >= 0)
		close_restricted(libinput, fd);
	if (device)
		evdev_device_destroy(device);

	return unhandled_device ? EVDEV_UNHANDLED_DEVICE :  NULL;
}

const char *
evdev_device_get_output(struct evdev_device *device)
{
	return device->output_name;
}

const char *
evdev_device_get_sysname(struct evdev_device *device)
{
	return udev_device_get_sysname(device->udev_device);
}

const char *
evdev_device_get_name(struct evdev_device *device)
{
	return device->devname;
}

unsigned int
evdev_device_get_id_product(struct evdev_device *device)
{
	return libevdev_get_id_product(device->evdev);
}

unsigned int
evdev_device_get_id_vendor(struct evdev_device *device)
{
	return libevdev_get_id_vendor(device->evdev);
}

struct udev_device *
evdev_device_get_udev_device(struct evdev_device *device)
{
	return udev_device_ref(device->udev_device);
}

void
evdev_device_set_default_calibration(struct evdev_device *device,
				     const float calibration[6])
{
	matrix_from_farray6(&device->abs.default_calibration, calibration);
	evdev_device_calibrate(device, calibration);
}

void
evdev_device_calibrate(struct evdev_device *device,
		       const float calibration[6])
{
	struct matrix scale,
		      translate,
		      transform;
	double sx, sy;

	matrix_from_farray6(&transform, calibration);
	device->abs.apply_calibration = !matrix_is_identity(&transform);

	if (!device->abs.apply_calibration) {
		matrix_init_identity(&device->abs.calibration);
		return;
	}

	sx = device->abs.absinfo_x->maximum - device->abs.absinfo_x->minimum + 1;
	sy = device->abs.absinfo_y->maximum - device->abs.absinfo_y->minimum + 1;

	/* The transformation matrix is in the form:
	 *  [ a b c ]
	 *  [ d e f ]
	 *  [ 0 0 1 ]
	 * Where a, e are the scale components, a, b, d, e are the rotation
	 * component (combined with scale) and c and f are the translation
	 * component. The translation component in the input matrix must be
	 * normalized to multiples of the device width and height,
	 * respectively. e.g. c == 1 shifts one device-width to the right.
	 *
	 * We pre-calculate a single matrix to apply to event coordinates:
	 *     M = Un-Normalize * Calibration * Normalize
	 *
	 * Normalize: scales the device coordinates to [0,1]
	 * Calibration: user-supplied matrix
	 * Un-Normalize: scales back up to device coordinates
	 * Matrix maths requires the normalize/un-normalize in reverse
	 * order.
	 */

	/* back up the user matrix so we can return it on request */
	matrix_from_farray6(&device->abs.usermatrix, calibration);

	/* Un-Normalize */
	matrix_init_translate(&translate,
			      device->abs.absinfo_x->minimum,
			      device->abs.absinfo_y->minimum);
	matrix_init_scale(&scale, sx, sy);
	matrix_mult(&scale, &translate, &scale);

	/* Calibration */
	matrix_mult(&transform, &scale, &transform);

	/* Normalize */
	matrix_init_translate(&translate,
			      -device->abs.absinfo_x->minimum/sx,
			      -device->abs.absinfo_y->minimum/sy);
	matrix_init_scale(&scale, 1.0/sx, 1.0/sy);
	matrix_mult(&scale, &translate, &scale);

	/* store final matrix in device */
	matrix_mult(&device->abs.calibration, &transform, &scale);
}

void
evdev_read_calibration_prop(struct evdev_device *device)
{
	const char *prop;
	float calibration[6];

	prop = udev_device_get_property_value(device->udev_device,
					      "LIBINPUT_CALIBRATION_MATRIX");

	if (prop == NULL)
		return;

	if (!device->abs.absinfo_x || !device->abs.absinfo_y)
		return;

	if (!parse_calibration_property(prop, calibration))
		return;

	evdev_device_set_default_calibration(device, calibration);
	evdev_log_info(device,
		       "applying calibration: %f %f %f %f %f %f\n",
		       calibration[0],
		       calibration[1],
		       calibration[2],
		       calibration[3],
		       calibration[4],
		       calibration[5]);
}

bool
evdev_device_has_capability(struct evdev_device *device,
			    enum libinput_device_capability capability)
{
	switch (capability) {
	case LIBINPUT_DEVICE_CAP_POINTER:
		return !!(device->seat_caps & EVDEV_DEVICE_POINTER);
	case LIBINPUT_DEVICE_CAP_KEYBOARD:
		return !!(device->seat_caps & EVDEV_DEVICE_KEYBOARD);
	case LIBINPUT_DEVICE_CAP_TOUCH:
		return !!(device->seat_caps & EVDEV_DEVICE_TOUCH);
	case LIBINPUT_DEVICE_CAP_GESTURE:
		return !!(device->seat_caps & EVDEV_DEVICE_GESTURE);
	case LIBINPUT_DEVICE_CAP_TABLET_TOOL:
		return !!(device->seat_caps & EVDEV_DEVICE_TABLET);
	case LIBINPUT_DEVICE_CAP_TABLET_PAD:
		return !!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD);
	case LIBINPUT_DEVICE_CAP_SWITCH:
		return !!(device->seat_caps & EVDEV_DEVICE_SWITCH);
	default:
		return false;
	}
}

int
evdev_device_get_size(const struct evdev_device *device,
		      double *width,
		      double *height)
{
	const struct input_absinfo *x, *y;

	x = libevdev_get_abs_info(device->evdev, ABS_X);
	y = libevdev_get_abs_info(device->evdev, ABS_Y);

	if (!x || !y || device->abs.is_fake_resolution ||
	    !x->resolution || !y->resolution)
		return -1;

	*width = evdev_convert_to_mm(x, x->maximum);
	*height = evdev_convert_to_mm(y, y->maximum);

	return 0;
}

int
evdev_device_has_button(struct evdev_device *device, uint32_t code)
{
	if (!(device->seat_caps & EVDEV_DEVICE_POINTER))
		return -1;

	return libevdev_has_event_code(device->evdev, EV_KEY, code);
}

int
evdev_device_has_key(struct evdev_device *device, uint32_t code)
{
	if (!(device->seat_caps & EVDEV_DEVICE_KEYBOARD))
		return -1;

	return libevdev_has_event_code(device->evdev, EV_KEY, code);
}

int
evdev_device_has_switch(struct evdev_device *device,
			enum libinput_switch sw)
{
	unsigned int code;

	if (!(device->seat_caps & EVDEV_DEVICE_SWITCH))
		return -1;

	switch (sw) {
	case LIBINPUT_SWITCH_LID:
		code = SW_LID;
		break;
	case LIBINPUT_SWITCH_TABLET_MODE:
		code = SW_TABLET_MODE;
		break;
	default:
		return -1;
	}

	return libevdev_has_event_code(device->evdev, EV_SW, code);
}

static inline bool
evdev_is_scrolling(const struct evdev_device *device,
		   enum libinput_pointer_axis axis)
{
	assert(axis == LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL ||
	       axis == LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

	return (device->scroll.direction & AS_MASK(axis)) != 0;
}

static inline void
evdev_start_scrolling(struct evdev_device *device,
		      enum libinput_pointer_axis axis)
{
	assert(axis == LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL ||
	       axis == LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

	device->scroll.direction |= AS_MASK(axis);
}

void
evdev_post_scroll(struct evdev_device *device,
		  uint64_t time,
		  enum libinput_pointer_axis_source source,
		  const struct normalized_coords *delta)
{
	const struct normalized_coords *trigger;
	struct normalized_coords event;

	if (!evdev_is_scrolling(device,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
		device->scroll.buildup.y += delta->y;
	if (!evdev_is_scrolling(device,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
		device->scroll.buildup.x += delta->x;

	trigger = &device->scroll.buildup;

	/* If we're not scrolling yet, use a distance trigger: moving
	   past a certain distance starts scrolling */
	if (!evdev_is_scrolling(device,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL) &&
	    !evdev_is_scrolling(device,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
		if (fabs(trigger->y) >= device->scroll.threshold)
			evdev_start_scrolling(device,
					      LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		if (fabs(trigger->x) >= device->scroll.threshold)
			evdev_start_scrolling(device,
					      LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
	/* We're already scrolling in one direction. Require some
	   trigger speed to start scrolling in the other direction */
	} else if (!evdev_is_scrolling(device,
			       LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
		if (fabs(delta->y) >= device->scroll.direction_lock_threshold)
			evdev_start_scrolling(device,
				      LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
	} else if (!evdev_is_scrolling(device,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
		if (fabs(delta->x) >= device->scroll.direction_lock_threshold)
			evdev_start_scrolling(device,
				      LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
	}

	event = *delta;

	/* We use the trigger to enable, but the delta from this event for
	 * the actual scroll movement. Otherwise we get a jump once
	 * scrolling engages */
	if (!evdev_is_scrolling(device,
			       LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
		event.y = 0.0;

	if (!evdev_is_scrolling(device,
			       LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
		event.x = 0.0;

	if (!normalized_is_zero(event)) {
		const struct discrete_coords zero_discrete = { 0.0, 0.0 };
		uint32_t axes = device->scroll.direction;

		if (event.y == 0.0)
			axes &= ~AS_MASK(LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		if (event.x == 0.0)
			axes &= ~AS_MASK(LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);

		evdev_notify_axis(device,
				  time,
				  axes,
				  source,
				  &event,
				  &zero_discrete);
	}
}

void
evdev_stop_scroll(struct evdev_device *device,
		  uint64_t time,
		  enum libinput_pointer_axis_source source)
{
	const struct normalized_coords zero = { 0.0, 0.0 };
	const struct discrete_coords zero_discrete = { 0.0, 0.0 };

	/* terminate scrolling with a zero scroll event */
	if (device->scroll.direction != 0)
		pointer_notify_axis(&device->base,
				    time,
				    device->scroll.direction,
				    source,
				    &zero,
				    &zero_discrete);

	device->scroll.buildup.x = 0;
	device->scroll.buildup.y = 0;
	device->scroll.direction = 0;
}

void
evdev_notify_suspended_device(struct evdev_device *device)
{
	struct libinput_device *it;

	if (device->is_suspended)
		return;

	list_for_each(it, &device->base.seat->devices_list, link) {
		struct evdev_device *d = evdev_device(it);
		if (it == &device->base)
			continue;

		if (d->dispatch->interface->device_suspended)
			d->dispatch->interface->device_suspended(d, device);
	}

	device->is_suspended = true;
}

void
evdev_notify_resumed_device(struct evdev_device *device)
{
	struct libinput_device *it;

	if (!device->is_suspended)
		return;

	list_for_each(it, &device->base.seat->devices_list, link) {
		struct evdev_device *d = evdev_device(it);
		if (it == &device->base)
			continue;

		if (d->dispatch->interface->device_resumed)
			d->dispatch->interface->device_resumed(d, device);
	}

	device->is_suspended = false;
}

void
evdev_device_suspend(struct evdev_device *device)
{
	struct libinput *libinput = evdev_libinput_context(device);

	evdev_notify_suspended_device(device);

	if (device->dispatch->interface->suspend)
		device->dispatch->interface->suspend(device->dispatch,
						     device);

	if (device->source) {
		libinput_remove_source(libinput, device->source);
		device->source = NULL;
	}

	if (device->mtdev) {
		mtdev_close_delete(device->mtdev);
		device->mtdev = NULL;
	}

	if (device->fd != -1) {
		close_restricted(libinput, device->fd);
		device->fd = -1;
	}
}

int
evdev_device_resume(struct evdev_device *device)
{
	struct libinput *libinput = evdev_libinput_context(device);
	int fd;
	const char *devnode;
	struct input_event ev;
	enum libevdev_read_status status;

	if (device->fd != -1)
		return 0;

	if (device->was_removed)
		return -ENODEV;

	devnode = udev_device_get_devnode(device->udev_device);
	fd = open_restricted(libinput, devnode,
			     O_RDWR | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0)
		return -errno;

	if (!evdev_device_have_same_syspath(device->udev_device, fd)) {
		close_restricted(libinput, fd);
		return -ENODEV;
	}

	evdev_drain_fd(fd);

	device->fd = fd;

	if (evdev_need_mtdev(device)) {
		device->mtdev = mtdev_new_open(device->fd);
		if (!device->mtdev)
			return -ENODEV;
	}

	libevdev_change_fd(device->evdev, fd);
	libevdev_set_clock_id(device->evdev, CLOCK_MONOTONIC);

	/* re-sync libevdev's view of the device, but discard the actual
	   events. Our device is in a neutral state already */
	libevdev_next_event(device->evdev,
			    LIBEVDEV_READ_FLAG_FORCE_SYNC,
			    &ev);
	do {
		status = libevdev_next_event(device->evdev,
					     LIBEVDEV_READ_FLAG_SYNC,
					     &ev);
	} while (status == LIBEVDEV_READ_STATUS_SYNC);

	device->source =
		libinput_add_fd(libinput, fd, evdev_device_dispatch, device);
	if (!device->source) {
		mtdev_close_delete(device->mtdev);
		return -ENOMEM;
	}

	evdev_notify_resumed_device(device);

	return 0;
}

void
evdev_device_remove(struct evdev_device *device)
{
	struct libinput_device *dev;

	evdev_log_info(device, "device removed\n");

	list_for_each(dev, &device->base.seat->devices_list, link) {
		struct evdev_device *d = evdev_device(dev);
		if (dev == &device->base)
			continue;

		if (d->dispatch->interface->device_removed)
			d->dispatch->interface->device_removed(d, device);
	}

	evdev_device_suspend(device);

	if (device->dispatch->interface->remove)
		device->dispatch->interface->remove(device->dispatch);

	/* A device may be removed while suspended, mark it to
	 * skip re-opening a different device with the same node */
	device->was_removed = true;

	list_remove(&device->base.link);

	notify_removed_device(&device->base);
	libinput_device_unref(&device->base);
}

void
evdev_device_destroy(struct evdev_device *device)
{
	struct evdev_dispatch *dispatch;

	dispatch = device->dispatch;
	if (dispatch)
		dispatch->interface->destroy(dispatch);

	if (device->base.group)
		libinput_device_group_unref(device->base.group);

	free(device->output_name);
	filter_destroy(device->pointer.filter);
	libinput_timer_destroy(&device->scroll.timer);
	libinput_timer_destroy(&device->middlebutton.timer);
	libinput_seat_unref(device->base.seat);
	libevdev_free(device->evdev);
	udev_device_unref(device->udev_device);
	free(device);
}

bool
evdev_tablet_has_left_handed(struct evdev_device *device)
{
	bool has_left_handed = false;
#if HAVE_LIBWACOM
	WacomDeviceDatabase *db;
	WacomDevice *d = NULL;
	WacomError *error;
	const char *devnode;

	db = libwacom_database_new();
	if (!db) {
		evdev_log_info(device,
			       "failed to initialize libwacom context.\n");
		goto out;
	}

	error = libwacom_error_new();
	devnode = udev_device_get_devnode(device->udev_device);

	d = libwacom_new_from_path(db,
				   devnode,
				   WFALLBACK_NONE,
				   error);

	if (d) {
		if (libwacom_is_reversible(d))
			has_left_handed = true;
	} else if (libwacom_error_get_code(error) == WERROR_UNKNOWN_MODEL) {
		evdev_log_info(device,
			       "tablet '%s' unknown to libwacom\n",
			       device->devname);
	} else {
		evdev_log_error(device,
				"libwacom error: %s\n",
				libwacom_error_get_message(error));
	}

	if (error)
		libwacom_error_free(&error);
	if (d)
		libwacom_destroy(d);
	libwacom_database_destroy(db);

out:
#endif
	return has_left_handed;
}
