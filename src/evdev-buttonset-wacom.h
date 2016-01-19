/*
 * Copyright © 2015 Red Hat, Inc.
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

#ifndef EVDEV_BUTTONSET_WACOM_H
#define EVDEV_BUTTONSET_WACOM_H

#include "evdev.h"

#define LIBINPUT_BUTTONSET_AXIS_NONE 0

enum buttonset_status {
	BUTTONSET_NONE = 0,
	BUTTONSET_AXES_UPDATED = 1 << 0,
	BUTTONSET_BUTTONS_PRESSED = 1 << 1,
	BUTTONSET_BUTTONS_RELEASED = 1 << 2,
};

struct button_state {
	/* Bitmask of pressed buttons. */
	unsigned long buttons[NLONGS(KEY_CNT)];
};

struct buttonset_axis {
	struct libinput_buttonset_axis base;
	unsigned int evcode;
};

struct buttonset_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;
	unsigned char status;
	unsigned int naxes;
	unsigned int evcode_map[ABS_CNT]; /* evcode to axis number */
	unsigned char changed_axes[NCHARS(LIBINPUT_BUTTONSET_MAX_NUM_AXES)];
	struct buttonset_axis axes[LIBINPUT_BUTTONSET_MAX_NUM_AXES];

	struct button_state button_state;
	struct button_state prev_button_state;

	bool have_abs_misc_terminator;

	struct {
		struct libinput_device_config_send_events config;
		enum libinput_config_send_events_mode current_mode;
	} sendevents;
};

#endif
