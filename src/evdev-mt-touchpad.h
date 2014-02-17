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
#define TOUCHPAD_MIN_SAMPLES 4

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

enum scroll_state {
	SCROLL_STATE_NONE,
	SCROLL_STATE_SCROLLING
};

enum tp_tap_state {
	TAP_STATE_IDLE = 4,
	TAP_STATE_TOUCH,
	TAP_STATE_HOLD,
	TAP_STATE_TAPPED,
	TAP_STATE_TOUCH_2,
	TAP_STATE_TOUCH_2_HOLD,
	TAP_STATE_TOUCH_3,
	TAP_STATE_TOUCH_3_HOLD,
	TAP_STATE_DRAGGING_OR_DOUBLETAP,
	TAP_STATE_DRAGGING,
	TAP_STATE_DRAGGING_WAIT,
	TAP_STATE_DRAGGING_2,
	TAP_STATE_DEAD, /**< finger count exceeded */
};

struct tp_motion {
	int32_t x;
	int32_t y;
};

struct tp_touch {
	enum touch_state state;
	bool dirty;
	bool fake;				/* a fake touch */
	bool is_pointer;			/* the pointer-controlling touch */
	bool is_pinned;				/* holds the phys. button */
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
	bool has_mt;

	unsigned int ntouches;			/* number of slots */
	struct tp_touch *touches;		/* len == ntouches */
	unsigned int fake_touches;		/* fake touch mask */

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

	struct {
		bool has_buttons;		/* true for physical LMR buttons */
		uint32_t state;
		uint32_t old_state;
	} buttons;				/* physical buttons */

	struct {
		enum scroll_state state;
		enum libinput_pointer_axis direction;
	} scroll;

	enum touchpad_event queued;

	struct {
		bool enabled;
		int timer_fd;
		struct libinput_source *source;
		unsigned int timeout;
		enum tp_tap_state state;
	} tap;
};

#define tp_for_each_touch(_tp, _t) \
	for (unsigned int _i = 0; _i < (_tp)->ntouches && (_t = &(_tp)->touches[_i]); _i++)

void
tp_get_delta(struct tp_touch *t, double *dx, double *dy);

int
tp_tap_handle_state(struct tp_dispatch *tp, uint32_t time);

unsigned int
tp_tap_handle_timeout(struct tp_dispatch *tp, uint32_t time);

int
tp_init_tap(struct tp_dispatch *tp);

#endif
