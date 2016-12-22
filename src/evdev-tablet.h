/*
 * Copyright © 2014 Red Hat, Inc.
 * Copyright © 2014 Stephen Chandler "Lyude" Paul
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

#ifndef EVDEV_TABLET_H
#define EVDEV_TABLET_H

#include "evdev.h"

#define LIBINPUT_TABLET_TOOL_AXIS_NONE 0
#define LIBINPUT_TOOL_NONE 0
#define LIBINPUT_TABLET_TOOL_TYPE_MAX LIBINPUT_TABLET_TOOL_TYPE_LENS

#define TABLET_HISTORY_LENGTH 4

enum tablet_status {
	TABLET_NONE = 0,
	TABLET_AXES_UPDATED = 1 << 0,
	TABLET_BUTTONS_PRESSED = 1 << 1,
	TABLET_BUTTONS_RELEASED = 1 << 2,
	TABLET_TOOL_IN_CONTACT = 1 << 3,
	TABLET_TOOL_LEAVING_PROXIMITY = 1 << 4,
	TABLET_TOOL_OUT_OF_PROXIMITY = 1 << 5,
	TABLET_TOOL_ENTERING_PROXIMITY = 1 << 6,
	TABLET_TOOL_ENTERING_CONTACT = 1 << 7,
	TABLET_TOOL_LEAVING_CONTACT = 1 << 8,
	TABLET_TOOL_OUT_OF_RANGE = 1 << 9,
};

struct button_state {
	unsigned char bits[NCHARS(KEY_CNT)];
};

struct tablet_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;
	unsigned int status;
	unsigned char changed_axes[NCHARS(LIBINPUT_TABLET_TOOL_AXIS_MAX + 1)];
	struct tablet_axes axes; /* for assembling the current state */
	struct device_coords last_smooth_point;
	struct {
		unsigned int index;
		unsigned int count;
		struct tablet_axes samples[TABLET_HISTORY_LENGTH];
	} history;

	unsigned char axis_caps[NCHARS(LIBINPUT_TABLET_TOOL_AXIS_MAX + 1)];
	int current_value[LIBINPUT_TABLET_TOOL_AXIS_MAX + 1];
	int prev_value[LIBINPUT_TABLET_TOOL_AXIS_MAX + 1];

	/* Only used for tablets that don't report serial numbers */
	struct list tool_list;

	struct button_state button_state;
	struct button_state prev_button_state;

	enum libinput_tablet_tool_type current_tool_type;
	uint32_t current_tool_id;
	uint32_t current_tool_serial;

	uint32_t cursor_proximity_threshold;

	struct libinput_device_config_calibration calibration;

	/* The paired touch device on devices with both pen & touch */
	struct evdev_device *touch_device;

	struct {
		bool need_to_force_prox_out;
		struct libinput_timer prox_out_timer;
		bool proximity_out_forced;
		uint64_t last_event_time;
	} quirks;
};

static inline struct tablet_dispatch*
tablet_dispatch(struct evdev_dispatch *dispatch)
{
	evdev_verify_dispatch_type(dispatch, DISPATCH_TABLET);

	return container_of(dispatch, struct tablet_dispatch, base);
}

static inline enum libinput_tablet_tool_axis
evcode_to_axis(const uint32_t evcode)
{
	enum libinput_tablet_tool_axis axis;

	switch (evcode) {
	case ABS_X:
		axis = LIBINPUT_TABLET_TOOL_AXIS_X;
		break;
	case ABS_Y:
		axis = LIBINPUT_TABLET_TOOL_AXIS_Y;
		break;
	case ABS_Z:
		axis = LIBINPUT_TABLET_TOOL_AXIS_ROTATION_Z;
		break;
	case ABS_DISTANCE:
		axis = LIBINPUT_TABLET_TOOL_AXIS_DISTANCE;
		break;
	case ABS_PRESSURE:
		axis = LIBINPUT_TABLET_TOOL_AXIS_PRESSURE;
		break;
	case ABS_TILT_X:
		axis = LIBINPUT_TABLET_TOOL_AXIS_TILT_X;
		break;
	case ABS_TILT_Y:
		axis = LIBINPUT_TABLET_TOOL_AXIS_TILT_Y;
		break;
	case ABS_WHEEL:
		axis = LIBINPUT_TABLET_TOOL_AXIS_SLIDER;
		break;
	default:
		axis = LIBINPUT_TABLET_TOOL_AXIS_NONE;
		break;
	}

	return axis;
}

static inline enum libinput_tablet_tool_axis
rel_evcode_to_axis(const uint32_t evcode)
{
	enum libinput_tablet_tool_axis axis;

	switch (evcode) {
	case REL_WHEEL:
		axis = LIBINPUT_TABLET_TOOL_AXIS_REL_WHEEL;
		break;
	default:
		axis = LIBINPUT_TABLET_TOOL_AXIS_NONE;
		break;
	}

	return axis;
}

static inline uint32_t
axis_to_evcode(const enum libinput_tablet_tool_axis axis)
{
	uint32_t evcode;

	switch (axis) {
	case LIBINPUT_TABLET_TOOL_AXIS_X:
		evcode = ABS_X;
		break;
	case LIBINPUT_TABLET_TOOL_AXIS_Y:
		evcode = ABS_Y;
		break;
	case LIBINPUT_TABLET_TOOL_AXIS_DISTANCE:
		evcode = ABS_DISTANCE;
		break;
	case LIBINPUT_TABLET_TOOL_AXIS_PRESSURE:
		evcode = ABS_PRESSURE;
		break;
	case LIBINPUT_TABLET_TOOL_AXIS_TILT_X:
		evcode = ABS_TILT_X;
		break;
	case LIBINPUT_TABLET_TOOL_AXIS_TILT_Y:
		evcode = ABS_TILT_Y;
		break;
	case LIBINPUT_TABLET_TOOL_AXIS_ROTATION_Z:
		evcode = ABS_Z;
		break;
	case LIBINPUT_TABLET_TOOL_AXIS_SLIDER:
		evcode = ABS_WHEEL;
		break;
	default:
		abort();
	}

	return evcode;
}

static inline int
tablet_tool_to_evcode(enum libinput_tablet_tool_type type)
{
	int code;

	switch (type) {
	case LIBINPUT_TABLET_TOOL_TYPE_PEN:	  code = BTN_TOOL_PEN;		break;
	case LIBINPUT_TABLET_TOOL_TYPE_ERASER:	  code = BTN_TOOL_RUBBER;	break;
	case LIBINPUT_TABLET_TOOL_TYPE_BRUSH:	  code = BTN_TOOL_BRUSH;	break;
	case LIBINPUT_TABLET_TOOL_TYPE_PENCIL:	  code = BTN_TOOL_PENCIL;	break;
	case LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH:  code = BTN_TOOL_AIRBRUSH;	break;
	case LIBINPUT_TABLET_TOOL_TYPE_MOUSE:	  code = BTN_TOOL_MOUSE;	break;
	case LIBINPUT_TABLET_TOOL_TYPE_LENS:	  code = BTN_TOOL_LENS;		break;
	default:
		abort();
	}

	return code;
}

static inline const char *
tablet_tool_type_to_string(enum libinput_tablet_tool_type type)
{
	const char *str;

	switch (type) {
	case LIBINPUT_TABLET_TOOL_TYPE_PEN:	  str = "pen";		break;
	case LIBINPUT_TABLET_TOOL_TYPE_ERASER:	  str = "eraser";	break;
	case LIBINPUT_TABLET_TOOL_TYPE_BRUSH:	  str = "brush";	break;
	case LIBINPUT_TABLET_TOOL_TYPE_PENCIL:	  str = "pencil";	break;
	case LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH:  str = "airbrush";	break;
	case LIBINPUT_TABLET_TOOL_TYPE_MOUSE:	  str = "mouse";	break;
	case LIBINPUT_TABLET_TOOL_TYPE_LENS:	  str = "lens";		break;
	default:
		abort();
	}

	return str;
}

static inline struct libinput *
tablet_libinput_context(const struct tablet_dispatch *tablet)
{
	return evdev_libinput_context(tablet->device);
}

#endif
