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
#include "timer.h"

#define TOUCHPAD_HISTORY_LENGTH 4
#define TOUCHPAD_MIN_SAMPLES 4

#define VENDOR_ID_APPLE 0x5ac

/* Touchpad slowdown factor, see the FIXME in tp_init_accel() */
#define TP_MAGIC_SLOWDOWN 0.4
/* Convert mm to a distance normalized to DEFAULT_MOUSE_DPI */
#define TP_MM_TO_DPI_NORMALIZED(mm) (DEFAULT_MOUSE_DPI/25.4 * TP_MAGIC_SLOWDOWN  * mm)

enum touchpad_event {
	TOUCHPAD_EVENT_NONE		= 0,
	TOUCHPAD_EVENT_MOTION		= (1 << 0),
	TOUCHPAD_EVENT_BUTTON_PRESS	= (1 << 1),
	TOUCHPAD_EVENT_BUTTON_RELEASE	= (1 << 2),
};

enum touchpad_model {
	MODEL_UNKNOWN = 0,
	MODEL_SYNAPTICS,
	MODEL_ALPS,
	MODEL_APPLETOUCH,
	MODEL_ELANTECH,
	MODEL_UNIBODY_MACBOOK
};

enum touch_state {
	TOUCH_NONE = 0,
	TOUCH_HOVERING,
	TOUCH_BEGIN,
	TOUCH_UPDATE,
	TOUCH_END
};

enum button_event {
	BUTTON_EVENT_IN_BOTTOM_R = 30,
	BUTTON_EVENT_IN_BOTTOM_L,
	BUTTON_EVENT_IN_TOP_R,
	BUTTON_EVENT_IN_TOP_M,
	BUTTON_EVENT_IN_TOP_L,
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
	BUTTON_STATE_TOP,
	BUTTON_STATE_TOP_NEW,
	BUTTON_STATE_TOP_TO_IGNORE,
	BUTTON_STATE_IGNORE,
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

enum tp_tap_touch_state {
	TAP_TOUCH_STATE_IDLE = 16,	/**< not in touch */
	TAP_TOUCH_STATE_TOUCH,		/**< touching, may tap */
	TAP_TOUCH_STATE_DEAD,		/**< exceeded motion/timeout */
};

/* For edge scrolling, so we only care about right and bottom */
enum tp_edge {
	EDGE_NONE = 0,
	EDGE_RIGHT = (1 << 0),
	EDGE_BOTTOM = (1 << 1),
};

enum tp_edge_scroll_touch_state {
	EDGE_SCROLL_TOUCH_STATE_NONE,
	EDGE_SCROLL_TOUCH_STATE_EDGE_NEW,
	EDGE_SCROLL_TOUCH_STATE_EDGE,
	EDGE_SCROLL_TOUCH_STATE_AREA,
};

struct tp_touch {
	struct tp_dispatch *tp;
	enum touch_state state;
	bool has_ended;				/* TRACKING_ID == -1 */
	bool dirty;
	struct device_coords point;
	uint64_t millis;

	struct {
		struct device_coords samples[TOUCHPAD_HISTORY_LENGTH];
		unsigned int index;
		unsigned int count;
	} history;

	struct device_coords hysteresis_center;

	/* A pinned touchpoint is the one that pressed the physical button
	 * on a clickpad. After the release, it won't move until the center
	 * moves more than a threshold away from the original coordinates
	 */
	struct {
		bool is_pinned;
		struct device_coords center;
	} pinned;

	/* Software-button state and timeout if applicable */
	struct {
		enum button_state state;
		/* We use button_event here so we can use == on events */
		enum button_event curr;
		struct libinput_timer timer;
	} button;

	struct {
		enum tp_tap_touch_state state;
		struct device_coords initial;
	} tap;

	struct {
		enum tp_edge_scroll_touch_state edge_state;
		uint32_t edge;
		int direction;
		struct libinput_timer timer;
		struct device_coords initial;
	} scroll;

	struct {
		bool is_palm;
		struct device_coords first; /* first coordinates if is_palm == true */
		uint32_t time; /* first timestamp if is_palm == true */
	} palm;
};

struct tp_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;
	unsigned int nfingers_down;		/* number of fingers down */
	unsigned int old_nfingers_down;		/* previous no fingers down */
	unsigned int slot;			/* current slot */
	bool has_mt;
	bool semi_mt;
	enum touchpad_model model;

	unsigned int real_touches;		/* number of slots */
	unsigned int ntouches;			/* no slots inc. fakes */
	struct tp_touch *touches;		/* len == ntouches */
	/* bit 0: BTN_TOUCH
	 * bit 1: BTN_TOOL_FINGER
	 * bit 2: BTN_TOOL_DOUBLETAP
	 * ...
	 */
	unsigned int fake_touches;

	struct device_coords hysteresis_margin;

	struct {
		double x_scale_coeff;
		double y_scale_coeff;
	} accel;

	struct {
		bool started;
		unsigned int finger_count;
		unsigned int finger_count_pending;
		struct libinput_timer finger_count_switch_timer;
	} gesture;

	struct {
		bool is_clickpad;		/* true for clickpads */
		bool has_topbuttons;
		bool use_clickfinger;		/* number of fingers decides button number */
		bool click_pending;
		uint32_t state;
		uint32_t old_state;
		uint32_t motion_dist;		/* for pinned touches */
		unsigned int active;		/* currently active button, for release event */
		bool active_is_topbutton;	/* is active a top button? */

		/* Only used for clickpads. The software button areas are
		 * always 2 horizontal stripes across the touchpad.
		 * The buttons are split according to the edge settings.
		 */
		struct {
			int32_t top_edge;	/* in device coordinates */
			int32_t rightbutton_left_edge; /* in device coordinates */
		} bottom_area;

		struct {
			int32_t bottom_edge;	/* in device coordinates */
			int32_t rightbutton_left_edge; /* in device coordinates */
			int32_t leftbutton_right_edge; /* in device coordinates */
		} top_area;

		struct evdev_device *trackpoint;

		enum libinput_config_click_method click_method;
		struct libinput_device_config_click_method config_method;
	} buttons;

	struct {
		struct libinput_device_config_scroll_method config_method;
		enum libinput_config_scroll_method method;
		int32_t right_edge;		/* in device coordinates */
		int32_t bottom_edge;		/* in device coordinates */
	} scroll;

	enum touchpad_event queued;

	struct {
		struct libinput_device_config_tap config;
		bool enabled;
		bool suspended;
		struct libinput_timer timer;
		enum tp_tap_state state;
		uint32_t buttons_pressed;
	} tap;

	struct {
		int32_t right_edge;		/* in device coordinates */
		int32_t left_edge;		/* in device coordinates */
	} palm;

	struct {
		struct libinput_device_config_send_events config;
		enum libinput_config_send_events_mode current_mode;
		bool trackpoint_active;
		struct libinput_event_listener trackpoint_listener;
		struct libinput_timer trackpoint_timer;
	} sendevents;
};

#define tp_for_each_touch(_tp, _t) \
	for (unsigned int _i = 0; _i < (_tp)->ntouches && (_t = &(_tp)->touches[_i]); _i++)

static inline void
tp_normalize_delta(struct tp_dispatch *tp, double *dx, double *dy)
{
	*dx = *dx * tp->accel.x_scale_coeff;
	*dy = *dy * tp->accel.y_scale_coeff;
}

void
tp_get_delta(struct tp_touch *t, double *dx, double *dy);

void
tp_filter_motion(struct tp_dispatch *tp,
	         double *dx, double *dy,
	         double *dx_unaccel, double *dy_unaccel,
		 uint64_t time);

int
tp_touch_active(struct tp_dispatch *tp, struct tp_touch *t);

int
tp_tap_handle_state(struct tp_dispatch *tp, uint64_t time);

int
tp_init_tap(struct tp_dispatch *tp);

void
tp_remove_tap(struct tp_dispatch *tp);

int
tp_init_buttons(struct tp_dispatch *tp, struct evdev_device *device);

void
tp_init_top_softbuttons(struct tp_dispatch *tp,
			struct evdev_device *device,
			double topbutton_size_mult);

void
tp_remove_buttons(struct tp_dispatch *tp);

int
tp_process_button(struct tp_dispatch *tp,
		  const struct input_event *e,
		  uint64_t time);

void
tp_release_all_buttons(struct tp_dispatch *tp,
		       uint64_t time);

int
tp_post_button_events(struct tp_dispatch *tp, uint64_t time);

int
tp_button_handle_state(struct tp_dispatch *tp, uint64_t time);

int
tp_button_touch_active(struct tp_dispatch *tp, struct tp_touch *t);

bool
tp_button_is_inside_softbutton_area(struct tp_dispatch *tp, struct tp_touch *t);

void
tp_release_all_taps(struct tp_dispatch *tp,
		    uint64_t time);

void
tp_tap_suspend(struct tp_dispatch *tp, uint64_t time);

void
tp_tap_resume(struct tp_dispatch *tp, uint64_t time);

bool
tp_tap_dragging(struct tp_dispatch *tp);

int
tp_edge_scroll_init(struct tp_dispatch *tp, struct evdev_device *device);

void
tp_remove_edge_scroll(struct tp_dispatch *tp);

void
tp_edge_scroll_handle_state(struct tp_dispatch *tp, uint64_t time);

int
tp_edge_scroll_post_events(struct tp_dispatch *tp, uint64_t time);

void
tp_edge_scroll_stop_events(struct tp_dispatch *tp, uint64_t time);

int
tp_edge_scroll_touch_active(struct tp_dispatch *tp, struct tp_touch *t);

int
tp_init_gesture(struct tp_dispatch *tp);

void
tp_remove_gesture(struct tp_dispatch *tp);

void
tp_gesture_stop(struct tp_dispatch *tp, uint64_t time);

void
tp_gesture_handle_state(struct tp_dispatch *tp, uint64_t time);

void
tp_gesture_post_events(struct tp_dispatch *tp, uint64_t time);

void
tp_gesture_stop_twofinger_scroll(struct tp_dispatch *tp, uint64_t time);

#endif
