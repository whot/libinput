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

#ifndef EVDEV_FALLBACK_H
#define EVDEV_FALLBACK_H

#include "evdev.h"

enum debounce_state {
	DEBOUNCE_STATE_IS_UP = 100,
	DEBOUNCE_STATE_IS_DOWN,
	DEBOUNCE_STATE_DOWN_WAITING,
	DEBOUNCE_STATE_RELEASE_PENDING,
	DEBOUNCE_STATE_RELEASE_DELAYED,
	DEBOUNCE_STATE_RELEASE_WAITING,
	DEBOUNCE_STATE_MAYBE_SPURIOUS,
	DEBOUNCE_STATE_RELEASED,
	DEBOUNCE_STATE_PRESS_PENDING,
};

struct fallback_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;

	struct libinput_device_config_calibration calibration;

	struct {
		bool is_enabled;
		int angle;
		struct matrix matrix;
		struct libinput_device_config_rotation config;
	} rotation;

	struct {
		struct device_coords point;
		int32_t seat_slot;
	} abs;

	struct {
		int slot;
		struct mt_slot *slots;
		size_t slots_len;
		bool want_hysteresis;
		struct device_coords hysteresis_margin;
	} mt;

	struct device_coords rel;
	struct device_coords wheel;

	struct {
		/* The struct for the tablet mode switch device itself */
		struct {
			int state;
		} sw;
		/* The struct for other devices listening to the tablet mode
		   switch */
		struct {
			struct evdev_device *sw_device;
			struct libinput_event_listener listener;
		} other;
	} tablet_mode;

	/* Bitmask of pressed keys used to ignore initial release events from
	 * the kernel. */
	unsigned long hw_key_mask[NLONGS(KEY_CNT)];
	unsigned long last_hw_key_mask[NLONGS(KEY_CNT)];

	enum evdev_event_type pending_event;

	/* true if we're reading events (i.e. not suspended) but we're
	   ignoring them */
	bool ignore_events;

	struct {
#if 0
		enum evdev_debounce_state state;
		uint64_t button_up_time;
#endif
		unsigned int button_code;
		uint64_t button_time;
		struct libinput_timer timer;
		struct libinput_timer timer_short;
		enum debounce_state state;
		bool spurious_enabled;
	} debounce;

	struct {
		enum switch_reliability reliability;

		bool is_closed;
		bool is_closed_client_state;

		/* We allow up to 3 paired keyboards for the lid switch
		 * listener. Only one keyboard should exist, but that can
		 * have more than one event node.
		 *
		 * Note: this is a sparse list, any element may have a
		 * non-NULL device.
		 */
		struct paired_keyboard {
			struct evdev_device *device;
			struct libinput_event_listener listener;
		} paired_keyboard[3];
	} lid;
};

static inline struct fallback_dispatch*
fallback_dispatch(struct evdev_dispatch *dispatch)
{
	evdev_verify_dispatch_type(dispatch, DISPATCH_FALLBACK);

	return container_of(dispatch, struct fallback_dispatch, base);
}

enum key_type {
	KEY_TYPE_NONE,
	KEY_TYPE_KEY,
	KEY_TYPE_BUTTON,
};

static inline enum key_type
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
		return KEY_TYPE_NONE;
	}

	if (code >= KEY_ESC && code <= KEY_MICMUTE)
		return KEY_TYPE_KEY;
	if (code >= BTN_MISC && code <= BTN_GEAR_UP)
		return KEY_TYPE_BUTTON;
	if (code >= KEY_OK && code <= KEY_LIGHTS_TOGGLE)
		return KEY_TYPE_KEY;
	if (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT)
		return KEY_TYPE_BUTTON;
	if (code >= KEY_ALS_TOGGLE && code <= KEY_ONSCREEN_KEYBOARD)
		return KEY_TYPE_KEY;
	if (code >= BTN_TRIGGER_HAPPY && code <= BTN_TRIGGER_HAPPY40)
		return KEY_TYPE_BUTTON;
	return KEY_TYPE_NONE;
}

static inline void
hw_set_key_down(struct fallback_dispatch *dispatch, int code, int pressed)
{
	long_set_bit_state(dispatch->hw_key_mask, code, pressed);
}

static inline bool
hw_key_has_changed(struct fallback_dispatch *dispatch, int code)
{
	return long_bit_is_set(dispatch->hw_key_mask, code) !=
		long_bit_is_set(dispatch->last_hw_key_mask, code);
}

static inline void
hw_key_update_last_state(struct fallback_dispatch *dispatch)
{
	static_assert(sizeof(dispatch->hw_key_mask) ==
		      sizeof(dispatch->last_hw_key_mask),
		      "Mismatching key mask size");

	memcpy(dispatch->last_hw_key_mask,
	       dispatch->hw_key_mask,
	       sizeof(dispatch->hw_key_mask));
}

static inline bool
hw_is_key_down(struct fallback_dispatch *dispatch, int code)
{
	return long_bit_is_set(dispatch->hw_key_mask, code);
}

static inline int
get_key_down_count(struct evdev_device *device, int code)
{
	return device->key_count[code];
}

void fallback_init_debounce(struct fallback_dispatch *dispatch);
void fallback_debounce_handle_state(struct fallback_dispatch *dispatch,
				    uint64_t time);

#endif
