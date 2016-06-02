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

enum pad_status {
	PAD_NONE = 0,
	PAD_AXES_UPDATED = 1 << 0,
	PAD_BUTTONS_PRESSED = 1 << 1,
	PAD_BUTTONS_RELEASED = 1 << 2,
};

enum pad_axes {
	PAD_AXIS_NONE = 0,
	PAD_AXIS_RING1 = 1 << 0,
	PAD_AXIS_RING2 = 1 << 1,
	PAD_AXIS_STRIP1 = 1 << 2,
	PAD_AXIS_STRIP2 = 1 << 3,
};

struct button_state {
	unsigned char bits[NCHARS(KEY_CNT)];
};

struct pad_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;
	unsigned char status;
	uint32_t changed_axes;

	struct button_state button_state;
	struct button_state prev_button_state;

	char button_map[KEY_CNT];
	unsigned int nbuttons;

	bool have_abs_misc_terminator;

	struct {
		struct libinput_device_config_send_events config;
		enum libinput_config_send_events_mode current_mode;
	} sendevents;

	struct {
		struct list mode_group_list;
	} modes;
};

static inline struct libinput *
pad_libinput_context(const struct pad_dispatch *pad)
{
	return evdev_libinput_context(pad->device);
}

int
pad_init_leds(struct pad_dispatch *pad, struct evdev_device *device);
void
pad_destroy_leds(struct pad_dispatch *pad);
void
pad_button_update_mode(struct libinput_tablet_pad_mode_group *g,
		       unsigned int pressed_button,
		       enum libinput_button_state state);
#endif
