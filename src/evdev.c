/*
 * Copyright © 2010 Intel Corporation
 * Copyright © 2013 Jonas Ådahl
 * Copyright © 2013-2015 Red Hat, Inc.
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

#define DEFAULT_WHEEL_CLICK_ANGLE 15
#define DEFAULT_MIDDLE_BUTTON_SCROLL_TIMEOUT 200

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
        EVDEV_UDEV_TAG_BUTTONSET = (1 << 8),
        EVDEV_UDEV_TAG_POINTINGSTICK = (1 << 9),
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
	{"ID_INPUT_TABLET_PAD",		EVDEV_UDEV_TAG_BUTTONSET},
	{"ID_INPUT_JOYSTICK",		EVDEV_UDEV_TAG_JOYSTICK},
	{"ID_INPUT_ACCELEROMETER",	EVDEV_UDEV_TAG_ACCELEROMETER},
	{"ID_INPUT_POINTINGSTICK",	EVDEV_UDEV_TAG_POINTINGSTICK},

	/* sentinel value */
	{ 0 },
};

static void
hw_set_key_down(struct evdev_device *device, int code, int pressed)
{
	long_set_bit_state(device->hw_key_mask, code, pressed);
}

static int
hw_is_key_down(struct evdev_device *device, int code)
{
	return long_bit_is_set(device->hw_key_mask, code);
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
		log_bug_libinput(device->base.seat->libinput,
				 "Key count for %s reached abnormal values\n",
				 libevdev_event_code_get_name(EV_KEY, code));
	}

	return key_count;
}

void
evdev_keyboard_notify_key(struct evdev_device *device,
			  uint32_t time,
			  int key,
			  enum libinput_key_state state)
{
	int down_count;

	down_count = update_key_down_count(device, key, state);

	if ((state == LIBINPUT_KEY_STATE_PRESSED && down_count == 1) ||
	    (state == LIBINPUT_KEY_STATE_RELEASED && down_count == 0))
		keyboard_notify_key(&device->base, time, key, state);
}

void
evdev_pointer_notify_physical_button(struct evdev_device *device,
				     uint32_t time,
				     int button,
				     enum libinput_button_state state)
{
	if (evdev_middlebutton_filter_button(device,
					     time,
					     button,
					     state))
			return;

	evdev_pointer_notify_button(device, time, button, state);
}

void
evdev_pointer_notify_button(struct evdev_device *device,
			    uint32_t time,
			    int button,
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

void
evdev_device_led_update(struct evdev_device *device, enum libinput_led leds)
{
	static const struct {
		enum libinput_led weston;
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
		ev[i].value = !!(leds & map[i].weston);
	}
	ev[i].type = EV_SYN;
	ev[i].code = SYN_REPORT;

	i = write(device->fd, ev, sizeof ev);
	(void)i; /* no, we really don't care about the return value */
}

static void
transform_absolute(struct evdev_device *device,
		   struct device_coords *point)
{
	if (!device->abs.apply_calibration)
		return;

	matrix_mult_vec(&device->abs.calibration, &point->x, &point->y);
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
	if (device->scroll.method != LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN ||
	    !hw_is_key_down(device, device->scroll.button))
		return false;

	if (device->scroll.button_scroll_active)
		evdev_post_scroll(device, time,
				  LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS,
				  &unaccel);

	return true;
}

static void
evdev_flush_pending_event(struct evdev_device *device, uint64_t time)
{
	struct libinput *libinput = device->base.seat->libinput;
	int slot;
	int seat_slot;
	struct libinput_device *base = &device->base;
	struct libinput_seat *seat = base->seat;
	struct normalized_coords accel, unaccel;
	struct device_coords point;
	struct device_float_coords raw;

	slot = device->mt.slot;

	switch (device->pending_event) {
	case EVDEV_NONE:
		return;
	case EVDEV_RELATIVE_MOTION:
		normalize_delta(device, &device->rel, &unaccel);
		raw.x = device->rel.x;
		raw.y = device->rel.y;
		device->rel.x = 0;
		device->rel.y = 0;

		/* Use unaccelerated deltas for pointing stick scroll */
		if (evdev_post_trackpoint_scroll(device, unaccel, time))
		    break;

		/* Apply pointer acceleration. */
		accel = filter_dispatch(device->pointer.filter,
					&unaccel,
					device,
					time);

		if (normalized_is_zero(accel) && normalized_is_zero(unaccel))
			break;

		pointer_notify_motion(base, time, &accel, &raw);
		break;
	case EVDEV_ABSOLUTE_MT_DOWN:
		if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
			break;

		if (device->mt.slots[slot].seat_slot != -1) {
			log_bug_kernel(libinput,
				       "%s: Driver sent multiple touch down for the "
				       "same slot",
				       udev_device_get_devnode(device->udev_device));
			break;
		}

		seat_slot = ffs(~seat->slot_map) - 1;
		device->mt.slots[slot].seat_slot = seat_slot;

		if (seat_slot == -1)
			break;

		seat->slot_map |= 1 << seat_slot;
		point = device->mt.slots[slot].point;
		transform_absolute(device, &point);

		touch_notify_touch_down(base, time, slot, seat_slot,
					&point);
		break;
	case EVDEV_ABSOLUTE_MT_MOTION:
		if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
			break;

		seat_slot = device->mt.slots[slot].seat_slot;
		point = device->mt.slots[slot].point;

		if (seat_slot == -1)
			break;

		transform_absolute(device, &point);
		touch_notify_touch_motion(base, time, slot, seat_slot,
					  &point);
		break;
	case EVDEV_ABSOLUTE_MT_UP:
		if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
			break;

		seat_slot = device->mt.slots[slot].seat_slot;
		device->mt.slots[slot].seat_slot = -1;

		if (seat_slot == -1)
			break;

		seat->slot_map &= ~(1 << seat_slot);

		touch_notify_touch_up(base, time, slot, seat_slot);
		break;
	case EVDEV_ABSOLUTE_TOUCH_DOWN:
		if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
			break;

		if (device->abs.seat_slot != -1) {
			log_bug_kernel(libinput,
				       "%s: Driver sent multiple touch down for the "
				       "same slot",
				       udev_device_get_devnode(device->udev_device));
			break;
		}

		seat_slot = ffs(~seat->slot_map) - 1;
		device->abs.seat_slot = seat_slot;

		if (seat_slot == -1)
			break;

		seat->slot_map |= 1 << seat_slot;

		point = device->abs.point;
		transform_absolute(device, &point);

		touch_notify_touch_down(base, time, -1, seat_slot, &point);
		break;
	case EVDEV_ABSOLUTE_MOTION:
		point = device->abs.point;
		transform_absolute(device, &point);

		if (device->seat_caps & EVDEV_DEVICE_TOUCH) {
			seat_slot = device->abs.seat_slot;

			if (seat_slot == -1)
				break;

			touch_notify_touch_motion(base, time, -1, seat_slot,
						  &point);
		} else if (device->seat_caps & EVDEV_DEVICE_POINTER) {
			pointer_notify_motion_absolute(base, time, &point);
		}
		break;
	case EVDEV_ABSOLUTE_TOUCH_UP:
		if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
			break;

		seat_slot = device->abs.seat_slot;
		device->abs.seat_slot = -1;

		if (seat_slot == -1)
			break;

		seat->slot_map &= ~(1 << seat_slot);

		touch_notify_touch_up(base, time, -1, seat_slot);
		break;
	default:
		assert(0 && "Unknown pending event type");
		break;
	}

	device->pending_event = EVDEV_NONE;
}

static enum evdev_key_type
get_key_type(uint16_t code)
{
	if (code == BTN_TOUCH)
		return EVDEV_KEY_TYPE_NONE;

	if (code >= KEY_ESC && code <= KEY_MICMUTE)
		return EVDEV_KEY_TYPE_KEY;
	if (code >= BTN_MISC && code <= BTN_GEAR_UP)
		return EVDEV_KEY_TYPE_BUTTON;
	if (code >= KEY_OK && code <= KEY_LIGHTS_TOGGLE)
		return EVDEV_KEY_TYPE_KEY;
	if (code >= BTN_DPAD_UP && code <= BTN_TRIGGER_HAPPY40)
		return EVDEV_KEY_TYPE_BUTTON;
	return EVDEV_KEY_TYPE_NONE;
}

static void
evdev_button_scroll_timeout(uint64_t time, void *data)
{
	struct evdev_device *device = data;

	device->scroll.button_scroll_active = true;
}

static void
evdev_button_scroll_button(struct evdev_device *device,
			   uint64_t time, int is_press)
{
	if (is_press) {
		libinput_timer_set(&device->scroll.timer,
				time + DEFAULT_MIDDLE_BUTTON_SCROLL_TIMEOUT);
		device->scroll.button_down_time = time;
	} else {
		libinput_timer_cancel(&device->scroll.timer);
		if (device->scroll.button_scroll_active) {
			evdev_stop_scroll(device, time,
					  LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS);
			device->scroll.button_scroll_active = false;
		} else {
			/* If the button is released quickly enough emit the
			 * button press/release events. */
			evdev_pointer_notify_physical_button(device,
					device->scroll.button_down_time,
					device->scroll.button,
					LIBINPUT_BUTTON_STATE_PRESSED);
			evdev_pointer_notify_physical_button(device, time,
					device->scroll.button,
					LIBINPUT_BUTTON_STATE_RELEASED);
		}
	}
}

static void
evdev_process_touch_button(struct evdev_device *device,
			   uint64_t time, int value)
{
	if (device->pending_event != EVDEV_NONE &&
	    device->pending_event != EVDEV_ABSOLUTE_MOTION)
		evdev_flush_pending_event(device, time);

	device->pending_event = (value ?
				 EVDEV_ABSOLUTE_TOUCH_DOWN :
				 EVDEV_ABSOLUTE_TOUCH_UP);
}

static inline void
evdev_process_key(struct evdev_device *device,
		  struct input_event *e, uint64_t time)
{
	enum evdev_key_type type;

	/* ignore kernel key repeat */
	if (e->value == 2)
		return;

	if (e->code == BTN_TOUCH) {
		if (!device->is_mt)
			evdev_process_touch_button(device, time, e->value);
		return;
	}

	evdev_flush_pending_event(device, time);

	type = get_key_type(e->code);

	/* Ignore key release events from the kernel for keys that libinput
	 * never got a pressed event for. */
	if (e->value == 0) {
		switch (type) {
		case EVDEV_KEY_TYPE_NONE:
			break;
		case EVDEV_KEY_TYPE_KEY:
		case EVDEV_KEY_TYPE_BUTTON:
			if (!hw_is_key_down(device, e->code))
				return;
		}
	}

	hw_set_key_down(device, e->code, e->value);

	switch (type) {
	case EVDEV_KEY_TYPE_NONE:
		break;
	case EVDEV_KEY_TYPE_KEY:
		evdev_keyboard_notify_key(
			device,
			time,
			e->code,
			e->value ? LIBINPUT_KEY_STATE_PRESSED :
				   LIBINPUT_KEY_STATE_RELEASED);
		break;
	case EVDEV_KEY_TYPE_BUTTON:
		if (device->scroll.method == LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN &&
		    e->code == device->scroll.button) {
			evdev_button_scroll_button(device, time, e->value);
			break;
		}
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
evdev_process_touch(struct evdev_device *device,
		    struct input_event *e,
		    uint64_t time)
{
	switch (e->code) {
	case ABS_MT_SLOT:
		if ((size_t)e->value >= device->mt.slots_len) {
			log_bug_libinput(device->base.seat->libinput,
					 "%s exceeds slots (%d of %zd)\n",
					 device->devname,
					 e->value,
					 device->mt.slots_len);
			e->value = device->mt.slots_len - 1;
		}
		evdev_flush_pending_event(device, time);
		device->mt.slot = e->value;
		break;
	case ABS_MT_TRACKING_ID:
		if (device->pending_event != EVDEV_NONE &&
		    device->pending_event != EVDEV_ABSOLUTE_MT_MOTION)
			evdev_flush_pending_event(device, time);
		if (e->value >= 0)
			device->pending_event = EVDEV_ABSOLUTE_MT_DOWN;
		else
			device->pending_event = EVDEV_ABSOLUTE_MT_UP;
		break;
	case ABS_MT_POSITION_X:
		device->mt.slots[device->mt.slot].point.x = e->value;
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MT_MOTION;
		break;
	case ABS_MT_POSITION_Y:
		device->mt.slots[device->mt.slot].point.y = e->value;
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MT_MOTION;
		break;
	}
}

static inline void
evdev_process_absolute_motion(struct evdev_device *device,
			      struct input_event *e)
{
	switch (e->code) {
	case ABS_X:
		device->abs.point.x = e->value;
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MOTION;
		break;
	case ABS_Y:
		device->abs.point.y = e->value;
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MOTION;
		break;
	}
}

static void
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

static inline void
evdev_process_relative(struct evdev_device *device,
		       struct input_event *e, uint64_t time)
{
	struct normalized_coords wheel_degrees = { 0.0, 0.0 };
	struct discrete_coords discrete = { 0.0, 0.0 };

	switch (e->code) {
	case REL_X:
		if (device->pending_event != EVDEV_RELATIVE_MOTION)
			evdev_flush_pending_event(device, time);
		device->rel.x += e->value;
		device->pending_event = EVDEV_RELATIVE_MOTION;
		break;
	case REL_Y:
		if (device->pending_event != EVDEV_RELATIVE_MOTION)
			evdev_flush_pending_event(device, time);
		device->rel.y += e->value;
		device->pending_event = EVDEV_RELATIVE_MOTION;
		break;
	case REL_WHEEL:
		evdev_flush_pending_event(device, time);
		wheel_degrees.y = -1 * e->value *
					device->scroll.wheel_click_angle;
		discrete.y = -1 * e->value;
		evdev_notify_axis(
			device,
			time,
			AS_MASK(LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL),
			LIBINPUT_POINTER_AXIS_SOURCE_WHEEL,
			&wheel_degrees,
			&discrete);
		break;
	case REL_HWHEEL:
		evdev_flush_pending_event(device, time);
		wheel_degrees.x = e->value * device->scroll.wheel_click_angle;
		discrete.x = e->value;
		evdev_notify_axis(
			device,
			time,
			AS_MASK(LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL),
			LIBINPUT_POINTER_AXIS_SOURCE_WHEEL,
			&wheel_degrees,
			&discrete);
		break;
	}
}

static inline void
evdev_process_absolute(struct evdev_device *device,
		       struct input_event *e,
		       uint64_t time)
{
	if (device->is_mt) {
		evdev_process_touch(device, e, time);
	} else {
		evdev_process_absolute_motion(device, e);
	}
}

static inline bool
evdev_any_button_down(struct evdev_device *device)
{
	unsigned int button;

	for (button = BTN_LEFT; button < BTN_JOYSTICK; button++) {
		if (libevdev_has_event_code(device->evdev, EV_KEY, button) &&
		    hw_is_key_down(device, button))
			return true;
	}
	return false;
}

static inline bool
evdev_need_touch_frame(struct evdev_device *device)
{
	if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
		return false;

	switch (device->pending_event) {
	case EVDEV_NONE:
	case EVDEV_RELATIVE_MOTION:
		break;
	case EVDEV_ABSOLUTE_MT_DOWN:
	case EVDEV_ABSOLUTE_MT_MOTION:
	case EVDEV_ABSOLUTE_MT_UP:
	case EVDEV_ABSOLUTE_TOUCH_DOWN:
	case EVDEV_ABSOLUTE_TOUCH_UP:
	case EVDEV_ABSOLUTE_MOTION:
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
	    udev_device_get_property_value(udev_device,
					   "ID_INPUT_POINTINGSTICK"))
		device->tags |= EVDEV_TAG_TRACKPOINT;
}

static void
evdev_tag_keyboard(struct evdev_device *device,
		   struct udev_device *udev_device)
{
	int code;

	if (!libevdev_has_event_type(device->evdev, EV_KEY))
		return;

	for (code = KEY_Q; code <= KEY_P; code++) {
		if (!libevdev_has_event_code(device->evdev,
					     EV_KEY,
					     code))
			return;
	}

	device->tags |= EVDEV_TAG_KEYBOARD;
}

static void
fallback_process(struct evdev_dispatch *dispatch,
		 struct evdev_device *device,
		 struct input_event *event,
		 uint64_t time)
{
	bool need_frame = false;

	switch (event->type) {
	case EV_REL:
		evdev_process_relative(device, event, time);
		break;
	case EV_ABS:
		evdev_process_absolute(device, event, time);
		break;
	case EV_KEY:
		evdev_process_key(device, event, time);
		break;
	case EV_SYN:
		need_frame = evdev_need_touch_frame(device);
		evdev_flush_pending_event(device, time);
		if (need_frame)
			touch_notify_frame(&device->base, time);
		break;
	}
}

static void
release_pressed_keys(struct evdev_device *device)
{
	struct libinput *libinput = device->base.seat->libinput;
	uint64_t time;
	int code;

	if ((time = libinput_now(libinput)) == 0)
		return;

	for (code = 0; code < KEY_CNT; code++) {
		int count = get_key_down_count(device, code);

		if (count == 0)
			continue;

		if (count > 1) {
			log_bug_libinput(libinput,
					 "Key %d is down %d times.\n",
					 code,
					 count);
		}

		switch (get_key_type(code)) {
		case EVDEV_KEY_TYPE_NONE:
			break;
		case EVDEV_KEY_TYPE_KEY:
			evdev_keyboard_notify_key(
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
			log_bug_libinput(libinput,
					 "Releasing key %d failed.\n",
					 code);
			break;
		}
	}
}

static void
fallback_suspend(struct evdev_dispatch *dispatch,
		 struct evdev_device *device)
{
	release_pressed_keys(device);
}

static void
fallback_destroy(struct evdev_dispatch *dispatch)
{
	free(dispatch);
}

static int
evdev_calibration_has_matrix(struct libinput_device *libinput_device)
{
	struct evdev_device *device = (struct evdev_device*)libinput_device;

	return device->abs.absinfo_x && device->abs.absinfo_y;
}

static enum libinput_config_status
evdev_calibration_set_matrix(struct libinput_device *libinput_device,
			     const float matrix[6])
{
	struct evdev_device *device = (struct evdev_device*)libinput_device;

	evdev_device_calibrate(device, matrix);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static int
evdev_calibration_get_matrix(struct libinput_device *libinput_device,
			     float matrix[6])
{
	struct evdev_device *device = (struct evdev_device*)libinput_device;

	matrix_to_farray6(&device->abs.usermatrix, matrix);

	return !matrix_is_identity(&device->abs.usermatrix);
}

static int
evdev_calibration_get_default_matrix(struct libinput_device *libinput_device,
				     float matrix[6])
{
	struct evdev_device *device = (struct evdev_device*)libinput_device;

	matrix_to_farray6(&device->abs.default_calibration, matrix);

	return !matrix_is_identity(&device->abs.default_calibration);
}

struct evdev_dispatch_interface fallback_interface = {
	fallback_process,
	fallback_suspend,
	NULL, /* remove */
	fallback_destroy,
	NULL, /* device_added */
	NULL, /* device_removed */
	NULL, /* device_suspended */
	NULL, /* device_resumed */
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
	struct evdev_device *evdev = (struct evdev_device*)device;
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
	struct evdev_device *evdev = (struct evdev_device*)device;
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
	if (device->left_handed.want_enabled == device->left_handed.enabled)
		return;

	if (evdev_any_button_down(device))
		return;

	device->left_handed.enabled = device->left_handed.want_enabled;
}

static enum libinput_config_status
evdev_left_handed_set(struct libinput_device *device, int left_handed)
{
	struct evdev_device *evdev_device = (struct evdev_device *)device;

	evdev_device->left_handed.want_enabled = left_handed ? true : false;

	evdev_device->left_handed.change_to_enabled(evdev_device);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static int
evdev_left_handed_get(struct libinput_device *device)
{
	struct evdev_device *evdev_device = (struct evdev_device *)device;

	/* return the wanted configuration, even if it hasn't taken
	 * effect yet! */
	return evdev_device->left_handed.want_enabled;
}

static int
evdev_left_handed_get_default(struct libinput_device *device)
{
	return 0;
}

int
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

	return 0;
}

static uint32_t
evdev_scroll_get_methods(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
}

static void
evdev_change_scroll_method(struct evdev_device *device)
{
	if (device->scroll.want_method == device->scroll.method &&
	    device->scroll.want_button == device->scroll.button)
		return;

	if (evdev_any_button_down(device))
		return;

	device->scroll.method = device->scroll.want_method;
	device->scroll.button = device->scroll.want_button;
}

static enum libinput_config_status
evdev_scroll_set_method(struct libinput_device *device,
			enum libinput_config_scroll_method method)
{
	struct evdev_device *evdev = (struct evdev_device*)device;

	evdev->scroll.want_method = method;
	evdev->scroll.change_scroll_method(evdev);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_scroll_method
evdev_scroll_get_method(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device *)device;

	/* return the wanted configuration, even if it hasn't taken
	 * effect yet! */
	return evdev->scroll.want_method;
}

static enum libinput_config_scroll_method
evdev_scroll_get_default_method(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device *)device;

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
	struct evdev_device *evdev = (struct evdev_device*)device;

	evdev->scroll.want_button = button;
	evdev->scroll.change_scroll_method(evdev);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static uint32_t
evdev_scroll_get_button(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device *)device;

	/* return the wanted configuration, even if it hasn't taken
	 * effect yet! */
	return evdev->scroll.want_button;
}

static uint32_t
evdev_scroll_get_default_button(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device *)device;

	if( libevdev_has_event_code(evdev->evdev, EV_KEY, BTN_MIDDLE))
		return BTN_MIDDLE;

	return 0;
}

static int
evdev_init_button_scroll(struct evdev_device *device,
			 void (*change_scroll_method)(struct evdev_device *))
{
	libinput_timer_init(&device->scroll.timer, device->base.seat->libinput,
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

	return 0;
}

static void
evdev_init_calibration(struct evdev_device *device,
		       struct evdev_dispatch *dispatch)
{
	device->base.config.calibration = &dispatch->calibration;

	dispatch->calibration.has_matrix = evdev_calibration_has_matrix;
	dispatch->calibration.set_matrix = evdev_calibration_set_matrix;
	dispatch->calibration.get_matrix = evdev_calibration_get_matrix;
	dispatch->calibration.get_default_matrix = evdev_calibration_get_default_matrix;
}

static void
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
	struct evdev_device *dev = (struct evdev_device *)device;

	dev->scroll.natural_scrolling_enabled = enabled ? true : false;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static int
evdev_scroll_config_natural_get(struct libinput_device *device)
{
	struct evdev_device *dev = (struct evdev_device *)device;

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

static struct evdev_dispatch *
fallback_dispatch_create(struct libinput_device *device)
{
	struct evdev_dispatch *dispatch = zalloc(sizeof *dispatch);
	struct evdev_device *evdev_device = (struct evdev_device *)device;

	if (dispatch == NULL)
		return NULL;

	dispatch->interface = &fallback_interface;

	if (evdev_device->left_handed.want_enabled &&
	    evdev_init_left_handed(evdev_device,
				   evdev_change_to_left_handed) == -1) {
		free(dispatch);
		return NULL;
	}

	if (evdev_device->scroll.want_button &&
	    evdev_init_button_scroll(evdev_device,
				     evdev_change_scroll_method) == -1) {
		free(dispatch);
		return NULL;
	}

	if (evdev_device->scroll.natural_scrolling_enabled)
		evdev_init_natural_scroll(evdev_device);

	evdev_init_calibration(evdev_device, dispatch);
	evdev_init_sendevents(evdev_device, dispatch);

	/* BTN_MIDDLE is set on mice even when it's not present. So
	 * we can only use the absense of BTN_MIDDLE to mean something, i.e.
	 * we enable it by default on anything that only has L&R.
	 * If we have L&R and no middle, we don't expose it as config
	 * option */
	if (libevdev_has_event_code(evdev_device->evdev, EV_KEY, BTN_LEFT) &&
	    libevdev_has_event_code(evdev_device->evdev, EV_KEY, BTN_RIGHT)) {
		bool has_middle = libevdev_has_event_code(evdev_device->evdev,
							  EV_KEY,
							  BTN_MIDDLE);
		bool want_config = has_middle;
		bool enable_by_default = !has_middle;

		evdev_init_middlebutton(evdev_device,
					enable_by_default,
					want_config);
	}

	return dispatch;
}

static inline void
evdev_process_event(struct evdev_device *device, struct input_event *e)
{
	struct evdev_dispatch *dispatch = device->dispatch;
	uint64_t time = e->time.tv_sec * 1000ULL + e->time.tv_usec / 1000;

#if 0
	if (libevdev_event_is_code(e, EV_SYN, SYN_REPORT))
		log_debug(device->base.seat->libinput,
			  "-------------- EV_SYN ------------\n");
	else
		log_debug(device->base.seat->libinput,
			  "%-7s %-16s %-20s %4d\n",
			  evdev_device_get_sysname(device),
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
	struct libinput *libinput = device->base.seat->libinput;
	struct input_event ev;
	int rc;

	/* If the compositor is repainting, this function is called only once
	 * per frame and we have to process all the events available on the
	 * fd, otherwise there will be input lag. */
	do {
		rc = libevdev_next_event(device->evdev,
					 LIBEVDEV_READ_FLAG_NORMAL, &ev);
		if (rc == LIBEVDEV_READ_STATUS_SYNC) {
			switch (ratelimit_test(&device->syn_drop_limit)) {
			case RATELIMIT_PASS:
				log_info(libinput, "SYN_DROPPED event from "
					 "\"%s\" - some input events have "
					 "been lost.\n", device->devname);
				break;
			case RATELIMIT_THRESHOLD:
				log_info(libinput, "SYN_DROPPED flood "
					 "from \"%s\"\n",
					 device->devname);
				break;
			case RATELIMIT_EXCEEDED:
				break;
			}

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
	struct evdev_device *dev = (struct evdev_device *)device;

	if (!filter_set_speed(dev->pointer.filter, speed))
		return LIBINPUT_CONFIG_STATUS_INVALID;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static double
evdev_accel_config_get_speed(struct libinput_device *device)
{
	struct evdev_device *dev = (struct evdev_device *)device;

	return filter_get_speed(dev->pointer.filter);
}

static double
evdev_accel_config_get_default_speed(struct libinput_device *device)
{
	return 0.0;
}

int
evdev_device_init_pointer_acceleration(struct evdev_device *device,
				       accel_profile_func_t profile)
{
	device->pointer.filter = create_pointer_accelerator_filter(profile,
								   device->dpi);
	if (!device->pointer.filter)
		return -1;

	device->pointer.config.available = evdev_accel_config_available;
	device->pointer.config.set_speed = evdev_accel_config_set_speed;
	device->pointer.config.get_speed = evdev_accel_config_get_speed;
	device->pointer.config.get_default_speed = evdev_accel_config_get_default_speed;
	device->base.config.accel = &device->pointer.config;

	evdev_accel_config_set_speed(&device->base,
		     evdev_accel_config_get_default_speed(&device->base));

	return 0;
}

static inline int
evdev_need_mtdev(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;

	return (libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) &&
		libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y) &&
		!libevdev_has_event_code(evdev, EV_ABS, ABS_MT_SLOT));
}

static inline int
evdev_read_wheel_click_prop(struct evdev_device *device)
{
	struct libinput *libinput = device->base.seat->libinput;
	const char *prop;
	int angle = DEFAULT_WHEEL_CLICK_ANGLE;

	prop = udev_device_get_property_value(device->udev_device,
					      "MOUSE_WHEEL_CLICK_ANGLE");
	if (prop) {
		angle = parse_mouse_wheel_click_angle_property(prop);
		if (!angle) {
			log_error(libinput,
				  "Mouse wheel click angle '%s' is present but invalid,"
				  "using %d degrees instead\n",
				  device->devname,
				  DEFAULT_WHEEL_CLICK_ANGLE);
			angle = DEFAULT_WHEEL_CLICK_ANGLE;
		}
	}

	return angle;
}

static inline int
evdev_get_trackpoint_dpi(struct evdev_device *device)
{
	struct libinput *libinput = device->base.seat->libinput;
	const char *trackpoint_accel;
	double accel = DEFAULT_TRACKPOINT_ACCEL;

	trackpoint_accel = udev_device_get_property_value(
				device->udev_device, "POINTINGSTICK_CONST_ACCEL");
	if (trackpoint_accel) {
		accel = parse_trackpoint_accel_property(trackpoint_accel);
		if (accel == 0.0) {
			log_error(libinput, "Trackpoint accel property for "
					    "'%s' is present but invalid, "
					    "using %.2f instead\n",
					    device->devname,
					    DEFAULT_TRACKPOINT_ACCEL);
			accel = DEFAULT_TRACKPOINT_ACCEL;
		}
		log_info(libinput,
			  "Device '%s' set to const accel %.2f\n",
			  device->devname,
			  accel);
	}

	return DEFAULT_MOUSE_DPI / accel;
}

static inline int
evdev_read_dpi_prop(struct evdev_device *device)
{
	struct libinput *libinput = device->base.seat->libinput;
	const char *mouse_dpi;
	int dpi = DEFAULT_MOUSE_DPI;

	/*
	 * Trackpoints do not have dpi, instead hwdb may contain a
	 * POINTINGSTICK_CONST_ACCEL value to compensate for sensitivity
	 * differences between models, we translate this to a fake dpi.
	 */
	if (device->tags & EVDEV_TAG_TRACKPOINT)
		return evdev_get_trackpoint_dpi(device);

	mouse_dpi = udev_device_get_property_value(device->udev_device,
						   "MOUSE_DPI");
	if (mouse_dpi) {
		dpi = parse_mouse_dpi_property(mouse_dpi);
		if (!dpi) {
			log_error(libinput, "Mouse DPI property for '%s' is "
					    "present but invalid, using %d "
					    "DPI instead\n",
					    device->devname,
					    DEFAULT_MOUSE_DPI);
			dpi = DEFAULT_MOUSE_DPI;
		}
		log_info(libinput,
			 "Device '%s' set to %d DPI\n",
			 device->devname,
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
		{ "LIBINPUT_MODEL_LENOVO_X230", EVDEV_MODEL_LENOVO_X230 },
		{ "LIBINPUT_MODEL_CHROMEBOOK", EVDEV_MODEL_CHROMEBOOK },
		{ "LIBINPUT_MODEL_SYSTEM76_BONOBO", EVDEV_MODEL_SYSTEM76_BONOBO },
		{ "LIBINPUT_MODEL_SYSTEM76_GALAGO", EVDEV_MODEL_SYSTEM76_GALAGO },
		{ "LIBINPUT_MODEL_SYSTEM76_KUDU", EVDEV_MODEL_SYSTEM76_KUDU },
		{ "LIBINPUT_MODEL_CLEVO_W740SU", EVDEV_MODEL_CLEVO_W740SU },
		{ "LIBINPUT_MODEL_APPLE_TOUCHPAD", EVDEV_MODEL_APPLE_TOUCHPAD },
		{ "LIBINPUT_MODEL_WACOM_TOUCHPAD", EVDEV_MODEL_WACOM_TOUCHPAD },
		{ "LIBINPUT_MODEL_ALPS_TOUCHPAD", EVDEV_MODEL_ALPS_TOUCHPAD },
		{ "LIBINPUT_MODEL_SYNAPTICS_SERIAL_TOUCHPAD", EVDEV_MODEL_SYNAPTICS_SERIAL_TOUCHPAD },
		{ NULL, EVDEV_MODEL_DEFAULT },
	};
	const struct model_map *m = model_map;
	uint32_t model_flags = 0;

	while (m->property) {
		if (!!udev_device_get_property_value(device->udev_device,
						     m->property))
			model_flags |= m->model;
		m++;
	}

	return model_flags;
}

static inline int
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

static inline int
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
	struct libinput *libinput = device->base.seat->libinput;
	struct libevdev *evdev = device->evdev;
	const struct input_absinfo *absx, *absy;
	size_t widthmm = 0, heightmm = 0;
	size_t xres = EVDEV_FAKE_RESOLUTION,
	       yres = EVDEV_FAKE_RESOLUTION;

	if (!(xcode == ABS_X && ycode == ABS_Y)  &&
	    !(xcode == ABS_MT_POSITION_X && ycode == ABS_MT_POSITION_Y)) {
		log_bug_libinput(libinput,
				 "Invalid x/y code combination %d/%d\n",
				 xcode, ycode);
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
	const char *prop;
	enum evdev_device_udev_tags tags = 0;
	const struct evdev_udev_tag_match *match;
	int i;

	for (i = 0; i < 2 && udev_device; i++) {
		match = evdev_udev_tag_matches;
		while (match->name) {
			prop = udev_device_get_property_value(
						      udev_device,
						      match->name);
			if (prop)
				tags |= match->tag;

			match++;
		}
		udev_device = udev_device_get_parent(udev_device);
	}

	return tags;
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

static inline int
evdev_check_min_max(struct evdev_device *device, unsigned int code)
{
	struct libevdev *evdev = device->evdev;
	const struct input_absinfo *absinfo;

	if (!libevdev_has_event_code(evdev, EV_ABS, code))
		return 0;

	absinfo = libevdev_get_abs_info(evdev, code);
	if (absinfo->minimum == absinfo->maximum) {
		/* Some devices have a sort-of legitimate min/max of 0 for
		 * ABS_MISC and above (e.g. Roccat Kone XTD). Don't ignore
		 * them, simply disable the axes so we won't get events,
		 * we don't know what to do with them anyway.
		 */
		if (absinfo->minimum == 0 &&
		    code >= ABS_MISC && code < ABS_MT_SLOT) {
			log_info(device->base.seat->libinput,
				 "Disabling EV_ABS %#x on device '%s' (min == max == 0)\n",
				 code,
				 device->devname);
			libevdev_disable_event_code(device->evdev,
						    EV_ABS,
						    code);
		} else {
			log_bug_kernel(device->base.seat->libinput,
				       "Device '%s' has min == max on %s\n",
				       device->devname,
				       libevdev_event_code_get_name(EV_ABS, code));
			return -1;
		}
	}

	return 0;
}

static int
evdev_reject_device(struct evdev_device *device)
{
	struct libinput *libinput = device->base.seat->libinput;
	struct libevdev *evdev = device->evdev;
	unsigned int code;
	const struct input_absinfo *absx, *absy;

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_X) ^
	    libevdev_has_event_code(evdev, EV_ABS, ABS_Y))
		return -1;

	if (libevdev_has_event_code(evdev, EV_REL, REL_X) ^
	    libevdev_has_event_code(evdev, EV_REL, REL_Y))
		return -1;

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) ^
	    libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y))
		return -1;

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_X)) {
		absx = libevdev_get_abs_info(evdev, ABS_X);
		absy = libevdev_get_abs_info(evdev, ABS_Y);
		if ((absx->resolution == 0 && absy->resolution != 0) ||
		    (absx->resolution != 0 && absy->resolution == 0)) {
			log_bug_kernel(libinput,
				       "Kernel has only x or y resolution, not both.\n");
			return -1;
		}
	}

	if (!evdev_is_fake_mt_device(device) &&
	    libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X)) {
		absx = libevdev_get_abs_info(evdev, ABS_MT_POSITION_X);
		absy = libevdev_get_abs_info(evdev, ABS_MT_POSITION_Y);
		if ((absx->resolution == 0 && absy->resolution != 0) ||
		    (absx->resolution != 0 && absy->resolution == 0)) {
			log_bug_kernel(libinput,
				       "Kernel has only x or y MT resolution, not both.\n");
			return -1;
		}
	}

	for (code = 0; code < ABS_CNT; code++) {
		switch (code) {
		case ABS_MISC:
		case ABS_MT_SLOT:
		case ABS_MT_TOOL_TYPE:
			break;
		default:
			if (evdev_check_min_max(device, code) == -1)
				return -1;
		}
	}

	return 0;
}

static int
evdev_configure_mt_device(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;
	struct mt_slot *slots;
	int num_slots;
	int active_slot;
	int slot;

	if (!libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) ||
	    !libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y))
		 return 0;

	if (evdev_fix_abs_resolution(device,
				     ABS_MT_POSITION_X,
				     ABS_MT_POSITION_Y))
		device->abs.fake_resolution = 1;

	device->abs.absinfo_x = libevdev_get_abs_info(evdev, ABS_MT_POSITION_X);
	device->abs.absinfo_y = libevdev_get_abs_info(evdev, ABS_MT_POSITION_Y);
	device->abs.dimensions.x = abs(device->abs.absinfo_x->maximum -
				       device->abs.absinfo_x->minimum);
	device->abs.dimensions.y = abs(device->abs.absinfo_y->maximum -
				       device->abs.absinfo_y->minimum);
	device->is_mt = 1;

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

	slots = calloc(num_slots, sizeof(struct mt_slot));
	if (!slots)
		return -1;

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
	device->mt.slots = slots;
	device->mt.slots_len = num_slots;
	device->mt.slot = active_slot;

	return 0;
}

static inline int
evdev_init_accel(struct evdev_device *device)
{
	accel_profile_func_t profile;

	if (device->dpi < DEFAULT_MOUSE_DPI)
		profile = pointer_accel_profile_linear_low_dpi;
	else
		profile = pointer_accel_profile_linear;

	return evdev_device_init_pointer_acceleration(device, profile);
}

static int
evdev_configure_device(struct evdev_device *device)
{
	struct libinput *libinput = device->base.seat->libinput;
	struct libevdev *evdev = device->evdev;
	const char *devnode = udev_device_get_devnode(device->udev_device);
	enum evdev_device_udev_tags udev_tags;

	udev_tags = evdev_device_get_udev_tags(device, device->udev_device);

	if ((udev_tags & EVDEV_UDEV_TAG_INPUT) == 0 ||
	    (udev_tags & ~EVDEV_UDEV_TAG_INPUT) == 0) {
		log_info(libinput,
			 "input device '%s', %s not tagged as input device\n",
			 device->devname, devnode);
		return -1;
	}

	log_info(libinput,
		 "input device '%s', %s is tagged by udev as:%s%s%s%s%s%s%s%s%s\n",
		 device->devname, devnode,
		 udev_tags & EVDEV_UDEV_TAG_KEYBOARD ? " Keyboard" : "",
		 udev_tags & EVDEV_UDEV_TAG_MOUSE ? " Mouse" : "",
		 udev_tags & EVDEV_UDEV_TAG_TOUCHPAD ? " Touchpad" : "",
		 udev_tags & EVDEV_UDEV_TAG_TOUCHSCREEN ? " Touchscreen" : "",
		 udev_tags & EVDEV_UDEV_TAG_TABLET ? " Tablet" : "",
		 udev_tags & EVDEV_UDEV_TAG_POINTINGSTICK ? " Pointingstick" : "",
		 udev_tags & EVDEV_UDEV_TAG_JOYSTICK ? " Joystick" : "",
		 udev_tags & EVDEV_UDEV_TAG_ACCELEROMETER ? " Accelerometer" : "",
		 udev_tags & EVDEV_UDEV_TAG_BUTTONSET ? " Buttonset" : "");

	/* libwacom *adds* TABLET, TOUCHPAD but leaves JOYSTICK in place, so
	   make sure we only ignore real joystick devices */
	if ((udev_tags & EVDEV_UDEV_TAG_JOYSTICK) == udev_tags) {
		log_info(libinput,
			 "input device '%s', %s is a joystick, ignoring\n",
			 device->devname, devnode);
		return -1;
	}

	/* libwacom assigns tablet _and_ tablet_pad to the pad devices */
	if (udev_tags & EVDEV_UDEV_TAG_BUTTONSET) {
		log_info(libinput,
			 "input device '%s', %s is a buttonset, ignoring\n",
			 device->devname, devnode);
		return -1;
	}

	if (evdev_reject_device(device) == -1) {
		log_info(libinput,
			 "input device '%s', %s was rejected.\n",
			 device->devname, devnode);
		return -1;
	}

	if (!evdev_is_fake_mt_device(device))
		evdev_fix_android_mt(device);

	if (libevdev_has_event_code(evdev, EV_ABS, ABS_X)) {
		if (evdev_fix_abs_resolution(device, ABS_X, ABS_Y))
			device->abs.fake_resolution = 1;
		device->abs.absinfo_x = libevdev_get_abs_info(evdev, ABS_X);
		device->abs.absinfo_y = libevdev_get_abs_info(evdev, ABS_Y);
		device->abs.point.x = device->abs.absinfo_x->value;
		device->abs.point.y = device->abs.absinfo_y->value;
		device->abs.dimensions.x = abs(device->abs.absinfo_x->maximum -
					       device->abs.absinfo_x->minimum);
		device->abs.dimensions.y = abs(device->abs.absinfo_y->maximum -
					       device->abs.absinfo_y->minimum);

		if (evdev_is_fake_mt_device(device)) {
			udev_tags &= ~EVDEV_UDEV_TAG_TOUCHSCREEN;
		} else if (evdev_configure_mt_device(device) == -1) {
			return -1;
		}
	}

	if (udev_tags & EVDEV_UDEV_TAG_TOUCHPAD) {
		device->dispatch = evdev_mt_touchpad_create(device);
		device->seat_caps |= EVDEV_DEVICE_GESTURE;
		log_info(libinput,
			 "input device '%s', %s is a touchpad\n",
			 device->devname, devnode);

		evdev_tag_touchpad(device, device->udev_device);
		return device->dispatch == NULL ? -1 : 0;
	}

	if (udev_tags & EVDEV_UDEV_TAG_MOUSE ||
	    udev_tags & EVDEV_UDEV_TAG_POINTINGSTICK) {
		evdev_tag_external_mouse(device, device->udev_device);
		evdev_tag_trackpoint(device, device->udev_device);
		device->dpi = evdev_read_dpi_prop(device);

		if (libevdev_has_event_code(evdev, EV_REL, REL_X) &&
		    libevdev_has_event_code(evdev, EV_REL, REL_Y) &&
		    evdev_init_accel(device) == -1)
			return -1;

		device->seat_caps |= EVDEV_DEVICE_POINTER;

		log_info(libinput,
			 "input device '%s', %s is a pointer caps\n",
			 device->devname, devnode);

		/* want left-handed config option */
		device->left_handed.want_enabled = true;
		/* want natural-scroll config option */
		device->scroll.natural_scrolling_enabled = true;
		/* want button scrolling config option */
		device->scroll.want_button = 1;
	}

	if (udev_tags & EVDEV_UDEV_TAG_KEYBOARD) {
		device->seat_caps |= EVDEV_DEVICE_KEYBOARD;
		log_info(libinput,
			 "input device '%s', %s is a keyboard\n",
			 device->devname, devnode);

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
		log_info(libinput,
			 "input device '%s', %s is a touch device\n",
			 device->devname, devnode);
	}

	return 0;
}

static void
evdev_notify_added_device(struct evdev_device *device)
{
	struct libinput_device *dev;

	list_for_each(dev, &device->base.seat->devices_list, link) {
		struct evdev_device *d = (struct evdev_device*)dev;
		if (dev == &device->base)
			continue;

		/* Notify existing device d about addition of device device */
		if (d->dispatch->interface->device_added)
			d->dispatch->interface->device_added(d, device);

		/* Notify new device device about existing device d */
		if (device->dispatch->interface->device_added)
			device->dispatch->interface->device_added(device, d);

		/* Notify new device device if existing device d is suspended */
		if (d->suspended && device->dispatch->interface->device_suspended)
			device->dispatch->interface->device_suspended(device, d);
	}

	notify_added_device(&device->base);
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

static int
evdev_set_device_group(struct evdev_device *device,
		       struct udev_device *udev_device)
{
	struct libinput_device_group *group = NULL;
	const char *udev_group;

	udev_group = udev_device_get_property_value(udev_device,
						    "LIBINPUT_DEVICE_GROUP");
	if (udev_group) {
		struct libinput_device *d;

		list_for_each(d, &device->base.seat->devices_list, link) {
			const char *identifier = d->group->identifier;

			if (identifier &&
			    strcmp(identifier, udev_group) == 0) {
				group = d->group;
				break;
			}
		}
	}

	if (!group) {
		group = libinput_device_group_create(udev_group);
		if (!group)
			return 1;
		libinput_device_set_device_group(&device->base, group);
		libinput_device_group_unref(group);
	} else {
		libinput_device_set_device_group(&device->base, group);
	}

	return 0;
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

	/* Use non-blocking mode so that we can loop on read on
	 * evdev_device_data() until all events on the fd are
	 * read.  mtdev_get() also expects this. */
	fd = open_restricted(libinput, devnode,
			     O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		log_info(libinput,
			 "opening input device '%s' failed (%s).\n",
			 devnode, strerror(-fd));
		return NULL;
	}

	if (!evdev_device_have_same_syspath(udev_device, fd))
		goto err;

	device = zalloc(sizeof *device);
	if (device == NULL)
		goto err;

	libinput_device_init(&device->base, seat);
	libinput_seat_ref(seat);

	rc = libevdev_new_from_fd(fd, &device->evdev);
	if (rc != 0)
		goto err;

	libevdev_set_clock_id(device->evdev, CLOCK_MONOTONIC);

	device->seat_caps = 0;
	device->is_mt = 0;
	device->mtdev = NULL;
	device->udev_device = udev_device_ref(udev_device);
	device->rel.x = 0;
	device->rel.y = 0;
	device->abs.seat_slot = -1;
	device->dispatch = NULL;
	device->fd = fd;
	device->pending_event = EVDEV_NONE;
	device->devname = libevdev_get_name(device->evdev);
	device->scroll.threshold = 5.0; /* Default may be overridden */
	device->scroll.direction = 0;
	device->scroll.wheel_click_angle =
		evdev_read_wheel_click_prop(device);
	device->model_flags = evdev_read_model_flags(device);
	device->dpi = DEFAULT_MOUSE_DPI;

	/* at most 5 SYN_DROPPED log-messages per 30s */
	ratelimit_init(&device->syn_drop_limit, 30ULL * 1000, 5);

	matrix_init_identity(&device->abs.calibration);
	matrix_init_identity(&device->abs.usermatrix);
	matrix_init_identity(&device->abs.default_calibration);

	if (evdev_configure_device(device) == -1)
		goto err;

	if (device->seat_caps == 0) {
		unhandled_device = 1;
		goto err;
	}

	/* If the dispatch was not set up use the fallback. */
	if (device->dispatch == NULL)
		device->dispatch = fallback_dispatch_create(&device->base);
	if (device->dispatch == NULL)
		goto err;

	device->source =
		libinput_add_fd(libinput, fd, evdev_device_dispatch, device);
	if (!device->source)
		goto err;

	if (evdev_set_device_group(device, udev_device))
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

int
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
	default:
		return 0;
	}
}

int
evdev_device_get_size(struct evdev_device *device,
		      double *width,
		      double *height)
{
	const struct input_absinfo *x, *y;

	x = libevdev_get_abs_info(device->evdev, ABS_X);
	y = libevdev_get_abs_info(device->evdev, ABS_Y);

	if (!x || !y || device->abs.fake_resolution ||
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
		if (fabs(delta->y) >= device->scroll.threshold)
			evdev_start_scrolling(device,
				      LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
	} else if (!evdev_is_scrolling(device,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
		if (fabs(delta->x) >= device->scroll.threshold)
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
		evdev_notify_axis(device,
				  time,
				  device->scroll.direction,
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

	if (device->suspended)
		return;

	list_for_each(it, &device->base.seat->devices_list, link) {
		struct evdev_device *d = (struct evdev_device*)it;
		if (it == &device->base)
			continue;

		if (d->dispatch->interface->device_suspended)
			d->dispatch->interface->device_suspended(d, device);
	}

	device->suspended = 1;
}

void
evdev_notify_resumed_device(struct evdev_device *device)
{
	struct libinput_device *it;

	if (!device->suspended)
		return;

	list_for_each(it, &device->base.seat->devices_list, link) {
		struct evdev_device *d = (struct evdev_device*)it;
		if (it == &device->base)
			continue;

		if (d->dispatch->interface->device_resumed)
			d->dispatch->interface->device_resumed(d, device);
	}

	device->suspended = 0;
}

int
evdev_device_suspend(struct evdev_device *device)
{
	evdev_notify_suspended_device(device);

	if (device->dispatch->interface->suspend)
		device->dispatch->interface->suspend(device->dispatch,
						     device);

	if (device->source) {
		libinput_remove_source(device->base.seat->libinput,
				       device->source);
		device->source = NULL;
	}

	if (device->mtdev) {
		mtdev_close_delete(device->mtdev);
		device->mtdev = NULL;
	}

	if (device->fd != -1) {
		close_restricted(device->base.seat->libinput, device->fd);
		device->fd = -1;
	}

	return 0;
}

int
evdev_device_resume(struct evdev_device *device)
{
	struct libinput *libinput = device->base.seat->libinput;
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

	memset(device->hw_key_mask, 0, sizeof(device->hw_key_mask));

	evdev_notify_resumed_device(device);

	return 0;
}

void
evdev_device_remove(struct evdev_device *device)
{
	struct libinput_device *dev;

	list_for_each(dev, &device->base.seat->devices_list, link) {
		struct evdev_device *d = (struct evdev_device*)dev;
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

	filter_destroy(device->pointer.filter);
	libinput_seat_unref(device->base.seat);
	libevdev_free(device->evdev);
	udev_device_unref(device->udev_device);
	free(device->mt.slots);
	free(device);
}
