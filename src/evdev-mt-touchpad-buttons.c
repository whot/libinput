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

#include <math.h>

#include "evdev-mt-touchpad.h"

#define DEFAULT_BUTTON_MOTION_THRESHOLD 0.02 /* in percent of size */

int
tp_process_button(struct tp_dispatch *tp,
		  const struct input_event *e,
		  uint32_t time)
{
	uint32_t mask = 1 << (e->code - BTN_LEFT);
	if (e->value) {
		tp->buttons.state |= mask;
		tp->queued |= TOUCHPAD_EVENT_BUTTON_PRESS;
	} else {
		tp->buttons.state &= ~mask;
		tp->queued |= TOUCHPAD_EVENT_BUTTON_RELEASE;
	}

	return 0;
}

int
tp_init_buttons(struct tp_dispatch *tp,
		struct evdev_device *device)
{
	int width, height;
	double diagonal;

	if (libevdev_has_event_code(device->evdev, EV_KEY, BTN_MIDDLE) ||
	    libevdev_has_event_code(device->evdev, EV_KEY, BTN_RIGHT))
		tp->buttons.has_buttons = true;

	width = abs(device->abs.max_x - device->abs.min_x);
	height = abs(device->abs.max_y - device->abs.min_y);
	diagonal = sqrt(width*width + height*height);

	tp->buttons.motion_dist = diagonal * DEFAULT_BUTTON_MOTION_THRESHOLD;

	if (libevdev_get_id_vendor(device->evdev) == 0x5ac) /* Apple */
		tp->buttons.use_clickfinger = true;
	else
		tp->buttons.use_clickfinger = false;

	return 0;
}

static int
tp_post_clickfinger_buttons(struct tp_dispatch *tp, uint32_t time)
{
	uint32_t current, old, button;
	enum libinput_pointer_button_state state;

	current = tp->buttons.state;
	old = tp->buttons.old_state;

	if (current == old)
		return 0;

	if (current) {
		switch (tp->nfingers_down) {
		case 1: button = BTN_LEFT; break;
		case 2: button = BTN_RIGHT; break;
		case 3: button = BTN_MIDDLE; break;
		default:
			return 0;
		}
		tp->buttons.active = button;
		state = LIBINPUT_POINTER_BUTTON_STATE_PRESSED;
	} else {
		button = tp->buttons.active;
		tp->buttons.active = 0;
		state = LIBINPUT_POINTER_BUTTON_STATE_RELEASED;
	}

	if (button)
		pointer_notify_button(&tp->device->base,
				      time,
				      button,
				      state);
	return 1;
}

static int
tp_post_physical_buttons(struct tp_dispatch *tp, uint32_t time)
{
	uint32_t current, old, button;

	current = tp->buttons.state;
	old = tp->buttons.old_state;
	button = BTN_LEFT;

	while (current || old) {
		enum libinput_pointer_button_state state;

		if ((current & 0x1) ^ (old & 0x1)) {
			if (!!(current & 0x1))
				state = LIBINPUT_POINTER_BUTTON_STATE_PRESSED;
			else
				state = LIBINPUT_POINTER_BUTTON_STATE_RELEASED;

			pointer_notify_button(&tp->device->base,
					      time,
					      button,
					      state);
		}

		button++;
		current >>= 1;
		old >>= 1;
	}

	return 0;
}

int
tp_post_button_events(struct tp_dispatch *tp, uint32_t time)
{
	int rc;

	if ((tp->queued &
		(TOUCHPAD_EVENT_BUTTON_PRESS|TOUCHPAD_EVENT_BUTTON_RELEASE)) == 0)
				return 0;

	if (tp->buttons.has_buttons)
		rc = tp_post_physical_buttons(tp, time);
	else if (tp->buttons.use_clickfinger)
		rc = tp_post_clickfinger_buttons(tp, time);

	return rc;
}
