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

#include "config.h"
#include "evdev-tablet-pad.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#define tablet_pad_set_status(tablet_pad_,s_) (tablet_pad_)->status |= (s_)
#define tablet_pad_unset_status(tablet_pad_,s_) (tablet_pad_)->status &= ~(s_)
#define tablet_pad_has_status(tablet_pad_,s_) (!!((tablet_pad_)->status & (s_)))

static void
tablet_pad_get_buttons_pressed(struct tablet_pad_dispatch *tablet_pad,
			      unsigned long buttons_pressed[NLONGS(KEY_CNT)])
{
	struct button_state *state = &tablet_pad->button_state;
	struct button_state *prev_state = &tablet_pad->prev_button_state;
	unsigned int i;

	for (i = 0; i < NLONGS(KEY_CNT); i++)
		buttons_pressed[i] = state->buttons[i]
						& ~(prev_state->buttons[i]);
}

static void
tablet_pad_get_buttons_released(struct tablet_pad_dispatch *tablet_pad,
			       unsigned long buttons_released[NLONGS(KEY_CNT)])
{
	struct button_state *state = &tablet_pad->button_state;
	struct button_state *prev_state = &tablet_pad->prev_button_state;
	unsigned int i;

	for (i = 0; i < NLONGS(KEY_CNT); i++)
		buttons_released[i] = prev_state->buttons[i]
						& ~(state->buttons[i]);
}

static inline bool
tablet_pad_button_is_down(const struct tablet_pad_dispatch *tablet_pad,
			 uint32_t button)
{
	return long_bit_is_set(tablet_pad->button_state.buttons, button);
}

static inline void
tablet_pad_button_set_down(struct tablet_pad_dispatch *tablet_pad,
			  uint32_t button,
			  bool is_down)
{
	struct button_state *state = &tablet_pad->button_state;

	if (is_down) {
		long_set_bit(state->buttons, button);
		tablet_pad_set_status(tablet_pad, TABLET_PAD_BUTTONS_PRESSED);
	} else {
		long_clear_bit(state->buttons, button);
		tablet_pad_set_status(tablet_pad, TABLET_PAD_BUTTONS_RELEASED);
	}
}

static void
tablet_pad_process_absolute(struct tablet_pad_dispatch *tablet_pad,
			   struct evdev_device *device,
			   struct input_event *e,
			   uint64_t time)
{
	switch (e->code) {
	case ABS_WHEEL:
		tablet_pad->changed_axes |= TABLET_PAD_AXIS_RING1;
		tablet_pad_set_status(tablet_pad, TABLET_PAD_AXES_UPDATED);
		break;
	case ABS_THROTTLE:
		tablet_pad->changed_axes |= TABLET_PAD_AXIS_RING2;
		tablet_pad_set_status(tablet_pad, TABLET_PAD_AXES_UPDATED);
		break;
	case ABS_RX:
		tablet_pad->changed_axes |= TABLET_PAD_AXIS_STRIP1;
		tablet_pad_set_status(tablet_pad, TABLET_PAD_AXES_UPDATED);
		break;
	case ABS_RY:
		tablet_pad->changed_axes |= TABLET_PAD_AXIS_STRIP2;
		tablet_pad_set_status(tablet_pad, TABLET_PAD_AXES_UPDATED);
		break;
	case ABS_MISC:
		/* The wacom driver always sends a 0 axis event on finger
		   up, but we also get an ABS_MISC 15 on touch down and
		   ABS_MISC 0 on touch up, on top of the actual event. This
		   is kernel behavior for xf86-input-wacom backwards
		   compatibility after the 3.17 wacom HID move.

		   We use that event to tell when we truly went a full
		   rotation around the wheel vs. a finger release.

		   FIXME: On the Intuos5 and later the kernel merges all
		   states into that event, so if any finger is down on any
		   button, the wheel release won't trigger the ABS_MISC 0
		   but still send a 0 event. We can't currently detect this.
		 */
		tablet_pad->have_abs_misc_terminator = true;
		break;
	default:
		log_info(device->base.seat->libinput,
			 "Unhandled EV_ABS event code %#x\n", e->code);
		break;
	}
}

static inline double
normalize_ring(const struct input_absinfo *absinfo)
{
	/* libinput has 0 as the ring's northernmost point in the device's
	   current logical rotation, increasing clockwise to 1. Wacom has
	   0 on the left-most wheel position.
	 */
	double range = absinfo->maximum - absinfo->minimum + 1;
	double value = (absinfo->value - absinfo->minimum) / range - 0.25;
	if (value < 0.0)
		value += 1.0;

	return value;
}

static inline double
normalize_strip(const struct input_absinfo *absinfo)
{
	/* strip axes don't use a proper value, they just shift the bit left
	 * for each position. 0 isn't a real value either, it's only sent on
	 * finger release */
	double min = 0,
	       max = log2(absinfo->maximum);
	double range = max - min;
	double value = (log2(absinfo->value) - min) / range;

	return value;
}

/* FIXME:
 * - switch the loop in check_notify_axes to a similar approach as used
 * in the tablet source
 * - switch all special axis handling out into helper functions
 * - switch everything over to the new axis struct
 */
static inline double
tablet_pad_handle_ring(struct tablet_pad_dispatch *tablet_pad,
		      struct evdev_device *device,
		      unsigned int code)
{
	const struct input_absinfo *absinfo;

	absinfo = libevdev_get_abs_info(device->evdev, code);
	assert(absinfo);

	return normalize_ring(absinfo);
}

static inline bool
tablet_pad_handle_strip(struct tablet_pad_dispatch *tablet_pad,
		       struct evdev_device *device,
		       unsigned int code,
		       double *value)
{
	const struct input_absinfo *absinfo;

	absinfo = libevdev_get_abs_info(device->evdev, code);
	assert(absinfo);

	/* value 0 is a finger release, ignore it */
	if (absinfo->value == 0)
		return false;

	*value = normalize_strip(absinfo);

	return true;
}

static void
tablet_pad_check_notify_axes(struct tablet_pad_dispatch *tablet_pad,
			    struct evdev_device *device,
			    uint64_t time)
{
	struct libinput_device *base = &device->base;
	double value;

	/* Suppress the reset to 0 on finger up. See the
	   comment in tablet_pad_process_absolute */
	if (tablet_pad->have_abs_misc_terminator &&
	    libevdev_get_event_value(device->evdev, EV_ABS, ABS_MISC) == 0) {
		goto out;
	}

	if (tablet_pad->changed_axes & TABLET_PAD_AXIS_RING1) {
		value = tablet_pad_handle_ring(tablet_pad,
					       device,
					       ABS_WHEEL);
		tablet_pad_notify_ring(base,
				       time,
				       0,
				       value,
				       LIBINPUT_TABLET_PAD_RING_SOURCE_UNKNOWN);
	}

	if (tablet_pad->changed_axes & TABLET_PAD_AXIS_RING2) {
		value = tablet_pad_handle_ring(tablet_pad,
					       device,
					       ABS_THROTTLE);
		tablet_pad_notify_ring(base,
				       time,
				       1,
				       value,
				       LIBINPUT_TABLET_PAD_RING_SOURCE_UNKNOWN);
	}

	if (tablet_pad->changed_axes & TABLET_PAD_AXIS_STRIP1 &&
	    tablet_pad_handle_strip(tablet_pad,
				    device,
				    ABS_RX,
				    &value)) {
		tablet_pad_notify_strip(base,
					time,
					1,
					value,
					LIBINPUT_TABLET_PAD_STRIP_SOURCE_UNKNOWN);
	}

	if (tablet_pad->changed_axes & TABLET_PAD_AXIS_STRIP2 &&
	    tablet_pad_handle_strip(tablet_pad,
				    device,
				    ABS_RY,
				    &value)) {
		tablet_pad_notify_strip(base,
					time,
					1,
					value,
					LIBINPUT_TABLET_PAD_STRIP_SOURCE_UNKNOWN);
	}

out:
	tablet_pad->changed_axes = TABLET_PAD_AXIS_NONE;
	tablet_pad->have_abs_misc_terminator = false;
}

static void
tablet_pad_process_key(struct tablet_pad_dispatch *tablet_pad,
		       struct evdev_device *device,
		       struct input_event *e,
		       uint64_t time)
{
	uint32_t button = e->code;
	uint32_t is_press = e->value != 0;

	tablet_pad_button_set_down(tablet_pad, button, is_press);
}

static void
tablet_pad_notify_button_mask(struct tablet_pad_dispatch *tablet_pad,
			      struct evdev_device *device,
			      uint64_t time,
			      unsigned long *buttons,
			      enum libinput_button_state state)
{
	struct libinput_device *base = &device->base;
	int32_t num_button;
	unsigned int i;

	for (i = 0; i < NLONGS(KEY_CNT); i++) {
		unsigned long buttons_slice = buttons[i];

		num_button = i * LONG_BITS;
		while (buttons_slice) {
			int enabled;

			num_button++;
			enabled = (buttons_slice & 1);
			buttons_slice >>= 1;

			if (!enabled)
				continue;

			tablet_pad_notify_button(base,
						 time,
						 num_button - 1,
						 state);
		}
	}
}

static void
tablet_pad_notify_buttons(struct tablet_pad_dispatch *tablet_pad,
			  struct evdev_device *device,
			  uint64_t time,
			  enum libinput_button_state state)
{
	unsigned long buttons[NLONGS(KEY_CNT)];

	if (state == LIBINPUT_BUTTON_STATE_PRESSED)
		tablet_pad_get_buttons_pressed(tablet_pad,
					      buttons);
	else
		tablet_pad_get_buttons_released(tablet_pad,
					       buttons);

	tablet_pad_notify_button_mask(tablet_pad,
				     device,
				     time,
				     buttons,
				     state);
}

static void
sanitize_tablet_pad_axes(struct tablet_pad_dispatch *tablet_pad)
{
}

static void
tablet_pad_flush(struct tablet_pad_dispatch *tablet_pad,
		 struct evdev_device *device,
		 uint64_t time)
{
	if (tablet_pad_has_status(tablet_pad, TABLET_PAD_AXES_UPDATED)) {
		sanitize_tablet_pad_axes(tablet_pad);
		tablet_pad_check_notify_axes(tablet_pad, device, time);
		tablet_pad_unset_status(tablet_pad, TABLET_PAD_AXES_UPDATED);
	}

	if (tablet_pad_has_status(tablet_pad, TABLET_PAD_BUTTONS_RELEASED)) {
		tablet_pad_notify_buttons(tablet_pad,
					 device,
					 time,
					 LIBINPUT_BUTTON_STATE_RELEASED);
		tablet_pad_unset_status(tablet_pad, TABLET_PAD_BUTTONS_RELEASED);
	}

	if (tablet_pad_has_status(tablet_pad, TABLET_PAD_BUTTONS_PRESSED)) {
		tablet_pad_notify_buttons(tablet_pad,
					 device,
					 time,
					 LIBINPUT_BUTTON_STATE_PRESSED);
		tablet_pad_unset_status(tablet_pad, TABLET_PAD_BUTTONS_PRESSED);
	}

	/* Update state */
	memcpy(&tablet_pad->prev_button_state,
	       &tablet_pad->button_state,
	       sizeof(tablet_pad->button_state));
}

static void
tablet_pad_process(struct evdev_dispatch *dispatch,
		  struct evdev_device *device,
		  struct input_event *e,
		  uint64_t time)
{
	struct tablet_pad_dispatch *tablet_pad =
		(struct tablet_pad_dispatch *)dispatch;

	switch (e->type) {
	case EV_ABS:
		tablet_pad_process_absolute(tablet_pad, device, e, time);
		break;
	case EV_KEY:
		tablet_pad_process_key(tablet_pad, device, e, time);
		break;
	case EV_SYN:
		tablet_pad_flush(tablet_pad, device, time);
		break;
	default:
		log_error(device->base.seat->libinput,
			  "Unexpected event type %s (%#x)\n",
			  libevdev_event_type_get_name(e->type),
			  e->type);
		break;
	}
}

static void
tablet_pad_suspend(struct evdev_dispatch *dispatch,
		  struct evdev_device *device)
{
	struct tablet_pad_dispatch *tablet_pad =
		(struct tablet_pad_dispatch *)dispatch;
	struct libinput *libinput = device->base.seat->libinput;
	unsigned int code;

	for (code = KEY_ESC; code < KEY_CNT; code++) {
		if (tablet_pad_button_is_down(tablet_pad, code))
			tablet_pad_button_set_down(tablet_pad, code, false);
	}

	tablet_pad_flush(tablet_pad, device, libinput_now(libinput));
}

static void
tablet_pad_destroy(struct evdev_dispatch *dispatch)
{
	struct tablet_pad_dispatch *tablet_pad =
		(struct tablet_pad_dispatch*)dispatch;

	free(tablet_pad);
}

static struct evdev_dispatch_interface tablet_pad_interface = {
	tablet_pad_process,
	tablet_pad_suspend, /* suspend */
	NULL, /* remove */
	tablet_pad_destroy,
	NULL, /* device_added */
	NULL, /* device_removed */
	NULL, /* device_suspended */
	NULL, /* device_resumed */
	NULL, /* post_added */
};

static int
tablet_pad_init(struct tablet_pad_dispatch *tablet_pad,
		struct evdev_device *device)
{
	tablet_pad->base.interface = &tablet_pad_interface;
	tablet_pad->device = device;
	tablet_pad->status = TABLET_PAD_NONE;
	tablet_pad->changed_axes = TABLET_PAD_AXIS_NONE;

	return 0;
}

static uint32_t
tablet_pad_sendevents_get_modes(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
}

static enum libinput_config_status
tablet_pad_sendevents_set_mode(struct libinput_device *device,
			       enum libinput_config_send_events_mode mode)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tablet_pad_dispatch *tablet_pad =
			(struct tablet_pad_dispatch*)evdev->dispatch;

	if (mode == tablet_pad->sendevents.current_mode)
		return LIBINPUT_CONFIG_STATUS_SUCCESS;

	switch(mode) {
	case LIBINPUT_CONFIG_SEND_EVENTS_ENABLED:
		break;
	case LIBINPUT_CONFIG_SEND_EVENTS_DISABLED:
		tablet_pad_suspend(evdev->dispatch, evdev);
		break;
	default:
		return LIBINPUT_CONFIG_STATUS_UNSUPPORTED;
	}

	tablet_pad->sendevents.current_mode = mode;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_send_events_mode
tablet_pad_sendevents_get_mode(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct tablet_pad_dispatch *dispatch =
			(struct tablet_pad_dispatch*)evdev->dispatch;

	return dispatch->sendevents.current_mode;
}

static enum libinput_config_send_events_mode
tablet_pad_sendevents_get_default_mode(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
}

struct evdev_dispatch *
evdev_tablet_pad_create(struct evdev_device *device)
{
	struct tablet_pad_dispatch *tablet_pad;

	tablet_pad = zalloc(sizeof *tablet_pad);
	if (!tablet_pad)
		return NULL;

	if (tablet_pad_init(tablet_pad, device) != 0) {
		tablet_pad_destroy(&tablet_pad->base);
		return NULL;
	}

	device->base.config.sendevents = &tablet_pad->sendevents.config;
	tablet_pad->sendevents.current_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	tablet_pad->sendevents.config.get_modes = tablet_pad_sendevents_get_modes;
	tablet_pad->sendevents.config.set_mode = tablet_pad_sendevents_set_mode;
	tablet_pad->sendevents.config.get_mode = tablet_pad_sendevents_get_mode;
	tablet_pad->sendevents.config.get_default_mode = tablet_pad_sendevents_get_default_mode;

	return &tablet_pad->base;
}

int
evdev_device_tablet_pad_has_button(struct evdev_device *device, uint32_t code)
{
	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return -1;

	return libevdev_has_event_code(device->evdev, EV_KEY, code);
}

int
evdev_device_tablet_pad_get_num_rings(struct evdev_device *device)
{
	int nrings = 0;

	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return -1;

	if (libevdev_has_event_code(device->evdev, EV_ABS, ABS_WHEEL)) {
		nrings++;
		if (libevdev_has_event_code(device->evdev,
					    EV_ABS,
					    ABS_THROTTLE))
			nrings++;
	}

	return nrings;
}

int
evdev_device_tablet_pad_get_num_strips(struct evdev_device *device)
{
	int nstrips = 0;

	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return -1;

	if (libevdev_has_event_code(device->evdev, EV_ABS, ABS_RX)) {
		nstrips++;
		if (libevdev_has_event_code(device->evdev,
					    EV_ABS,
					    ABS_RY))
			nstrips++;
	}

	return nstrips;
}
