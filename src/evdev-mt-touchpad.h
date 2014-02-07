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


#ifndef EVDEV_MT_TOUCHPAD_H
#define EVDEV_MT_TOUCHPAD_H

#include <stdbool.h>

#include "evdev.h"
#include "filter.h"

#define TOUCHPAD_HISTORY_LENGTH 4

enum touchpad_event {
	TOUCHPAD_EVENT_NONE		= 0,
	TOUCHPAD_EVENT_MOTION		= (1 << 0),
	TOUCHPAD_EVENT_BUTTON_PRESS	= (1 << 1),
	TOUCHPAD_EVENT_BUTTON_RELEASE	= (1 << 2),
};

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

	struct {
		int32_t center_x;
		int32_t center_y;
	} hysteresis;
};

struct tp_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;
	unsigned int nfingers_down;		/* number of fingers down */
	unsigned int slot;			/* current slot */

	unsigned int ntouches;			/* number of slots */
	struct tp_touch *touches;		/* len == ntouches */

	struct {
		int32_t margin_x;
		int32_t margin_y;
	} hysteresis;

	struct motion_filter *filter;

	struct {
		double constant_factor;
		double min_factor;
		double max_factor;
	} accel;

	enum touchpad_event queued;
};

#define tp_for_each_touch(_tp, _t) \
	for (unsigned int _i = 0; _i < (_tp)->ntouches && (_t = &(_tp)->touches[_i]); _i++)

#endif
