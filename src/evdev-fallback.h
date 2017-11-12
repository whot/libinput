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

		struct {
			struct device_coords min, max;
			struct ratelimit range_warn_limit;
		} warning_range;
	} abs;

	struct {
		int slot;
		struct mt_slot *slots;
		size_t slots_len;
		bool want_hysteresis;
		struct device_coords hysteresis_margin;
	} mt;

	struct device_coords rel;

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

	enum evdev_event_type pending_event;

	/* true if we're reading events (i.e. not suspended) but we're
	   ignoring them */
	bool ignore_events;

	struct {
		enum evdev_debounce_state state;
		unsigned int button_code;
		uint64_t button_up_time;
		struct libinput_timer timer;
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

#endif
