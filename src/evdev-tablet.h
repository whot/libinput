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

#define LIBINPUT_TABLET_AXIS_NONE 0
#define LIBINPUT_TOOL_NONE 0

enum tablet_status {
	TABLET_NONE = 0,
	TABLET_AXES_UPDATED = 1 << 0,
	TABLET_BUTTONS_PRESSED = 1 << 1,
	TABLET_BUTTONS_RELEASED = 1 << 2,
	TABLET_STYLUS_IN_CONTACT = 1 << 3,
	TABLET_TOOL_LEAVING_PROXIMITY = 1 << 4,
	TABLET_TOOL_OUT_OF_PROXIMITY = 1 << 5,
	TABLET_TOOL_ENTERING_PROXIMITY = 1 << 6
};

struct button_state {
	uint32_t stylus_buttons; /* bitmask of evcode - BTN_TOUCH */
};

struct tablet_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;
	unsigned char status;
	unsigned char changed_axes[NCHARS(LIBINPUT_TABLET_AXIS_MAX + 1)];
	double axes[LIBINPUT_TABLET_AXIS_MAX + 1];
	unsigned char axis_caps[NCHARS(LIBINPUT_TABLET_AXIS_MAX + 1)];

	/* Only used for tablets that don't report serial numbers */
	struct list tool_list;

	struct button_state button_state;
	struct button_state prev_button_state;

	enum libinput_tool_type current_tool_type;
	uint32_t current_tool_serial;
};

static inline enum libinput_tablet_axis
evcode_to_axis(const uint32_t evcode)
{
	enum libinput_tablet_axis axis;

	switch (evcode) {
	case ABS_X:
		axis = LIBINPUT_TABLET_AXIS_X;
		break;
	case ABS_Y:
		axis = LIBINPUT_TABLET_AXIS_Y;
		break;
	case ABS_DISTANCE:
		axis = LIBINPUT_TABLET_AXIS_DISTANCE;
		break;
	case ABS_PRESSURE:
		axis = LIBINPUT_TABLET_AXIS_PRESSURE;
		break;
	case ABS_TILT_X:
		axis = LIBINPUT_TABLET_AXIS_TILT_X;
		break;
	case ABS_TILT_Y:
		axis = LIBINPUT_TABLET_AXIS_TILT_Y;
		break;
	default:
		axis = LIBINPUT_TABLET_AXIS_NONE;
		break;
	}

	return axis;
}

static inline uint32_t
axis_to_evcode(const enum libinput_tablet_axis axis)
{
	uint32_t evcode;

	switch (axis) {
	case LIBINPUT_TABLET_AXIS_X:
		evcode = ABS_X;
		break;
	case LIBINPUT_TABLET_AXIS_Y:
		evcode = ABS_Y;
		break;
	case LIBINPUT_TABLET_AXIS_DISTANCE:
		evcode = ABS_DISTANCE;
		break;
	case LIBINPUT_TABLET_AXIS_PRESSURE:
		evcode = ABS_PRESSURE;
		break;
	case LIBINPUT_TABLET_AXIS_TILT_X:
		evcode = ABS_TILT_X;
		break;
	case LIBINPUT_TABLET_AXIS_TILT_Y:
		evcode = ABS_TILT_Y;
		break;
	default:
		abort();
	}

	return evcode;
}

#endif
