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

enum button_event {
       BUTTON_EVENT_IN_BOTTOM_R = 30,
       BUTTON_EVENT_IN_BOTTOM_L,
       BUTTON_EVENT_IN_AREA,
       BUTTON_EVENT_UP,
       BUTTON_EVENT_PRESS,
       BUTTON_EVENT_RELEASE,
       BUTTON_EVENT_TIMEOUT,
};

enum button_state {
       BUTTON_STATE_NONE,
       BUTTON_STATE_AREA,
       BUTTON_STATE_BOTTOM,
       BUTTON_STATE_BOTTOM_NEW,
       BUTTON_STATE_BOTTOM_TO_AREA,
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

	/* A pinned touchpoint is the one that pressed the physical button
	 * on a clickpad. After the release, it won't move until the center
	 * moves more than a threshold away from the original coordinates
	 */
	struct {
		bool is_pinned;
		int32_t center_x;
		int32_t center_y;
	} pinned;

	/* Software-button state and timeout if applicable */
	struct {
		enum button_state state;
		/* We use button_event here so we can use == on events */
		enum button_event curr;
		uint32_t timeout;
	} button;
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
		bool is_clickpad;		/* true for clickpads */
		bool use_clickfinger;		/* number of fingers decides button number */
		uint32_t state;
		uint32_t old_state;
		uint32_t motion_dist;		/* for pinned touches */
		unsigned int active;		/* currently active button, for release event */

		/* Only used for clickpads. The software button area is always
		 * a horizontal strip across the touchpad. Depending on the
		 * rightbutton_left_edge value, the buttons are split according to the
		 * edge settings.
		 */
		struct {
			int32_t top_edge;
			int32_t rightbutton_left_edge;
		} area;

		unsigned int timeout;		/* current timeout in ms */

		int timer_fd;
		struct libinput_source *source;
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

void
tp_destroy_tap(struct tp_dispatch *tp);

int
tp_init_buttons(struct tp_dispatch *tp, struct evdev_device *device);

void
tp_destroy_buttons(struct tp_dispatch *tp);

int
tp_process_button(struct tp_dispatch *tp,
		  const struct input_event *e,
		  uint32_t time);

int
tp_post_button_events(struct tp_dispatch *tp, uint32_t time);

int
tp_button_handle_state(struct tp_dispatch *tp, uint32_t time);

int
tp_button_touch_active(struct tp_dispatch *tp, struct tp_touch *t);

#endif
