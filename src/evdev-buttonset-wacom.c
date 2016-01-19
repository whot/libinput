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
#include "evdev-buttonset-wacom.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#define buttonset_set_status(buttonset_,s_) (buttonset_)->status |= (s_)
#define buttonset_unset_status(buttonset_,s_) (buttonset_)->status &= ~(s_)
#define buttonset_has_status(buttonset_,s_) (!!((buttonset_)->status & (s_)))

static void
buttonset_get_buttons_pressed(struct buttonset_dispatch *buttonset,
			      unsigned long buttons_pressed[NLONGS(KEY_CNT)])
{
	struct button_state *state = &buttonset->button_state;
	struct button_state *prev_state = &buttonset->prev_button_state;
	unsigned int i;

	for (i = 0; i < NLONGS(KEY_CNT); i++)
		buttons_pressed[i] = state->buttons[i]
						& ~(prev_state->buttons[i]);
}

static void
buttonset_get_buttons_released(struct buttonset_dispatch *buttonset,
			       unsigned long buttons_released[NLONGS(KEY_CNT)])
{
	struct button_state *state = &buttonset->button_state;
	struct button_state *prev_state = &buttonset->prev_button_state;
	unsigned int i;

	for (i = 0; i < NLONGS(KEY_CNT); i++)
		buttons_released[i] = prev_state->buttons[i]
						& ~(state->buttons[i]);
}

static inline bool
buttonset_button_is_down(const struct buttonset_dispatch *buttonset,
			 uint32_t button)
{
	return long_bit_is_set(buttonset->button_state.buttons, button);
}

static inline void
buttonset_button_set_down(struct buttonset_dispatch *buttonset,
			  uint32_t button,
			  bool is_down)
{
	struct button_state *state = &buttonset->button_state;

	if (is_down) {
		long_set_bit(state->buttons, button);
		buttonset_set_status(buttonset, BUTTONSET_BUTTONS_PRESSED);
	} else {
		long_clear_bit(state->buttons, button);
		buttonset_set_status(buttonset, BUTTONSET_BUTTONS_RELEASED);
	}
}

static void
buttonset_process_absolute(struct buttonset_dispatch *buttonset,
			   struct evdev_device *device,
			   struct input_event *e,
			   uint32_t time)
{
	unsigned int axis;

	switch (e->code) {
	case ABS_WHEEL:
	case ABS_THROTTLE:
	case ABS_RX:
	case ABS_RY:
		axis = buttonset->evcode_map[e->code];
		if (axis == (unsigned int)-1) {
			log_bug_libinput(device->base.seat->libinput,
					 "Unhandled EV_ABS mapping for %#x\n",
					 e->code);
			break;
		}

		set_bit(buttonset->changed_axes, axis);
		buttonset_set_status(buttonset, BUTTONSET_AXES_UPDATED);
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
		buttonset->have_abs_misc_terminator = true;
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
buttonset_handle_ring(struct buttonset_dispatch *buttonset,
		      struct evdev_device *device,
		      unsigned int axis)
{
	unsigned int code;
	const struct input_absinfo *absinfo;

	code = buttonset->axes[axis].evcode;
	assert(code != 0);
	absinfo = libevdev_get_abs_info(device->evdev, code);
	assert(absinfo);

	return normalize_ring(absinfo);
}

static inline bool
buttonset_handle_strip(struct buttonset_dispatch *buttonset,
		       struct evdev_device *device,
		       unsigned int axis,
		       double *value)
{
	unsigned int code;
	const struct input_absinfo *absinfo;

	code = buttonset->axes[axis].evcode;
	assert(code != 0);
	absinfo = libevdev_get_abs_info(device->evdev, code);
	assert(absinfo);

	/* value 0 is a finger release, ignore it */
	if (absinfo->value == 0)
		return false;

	*value = normalize_strip(absinfo);

	return true;
}

static void
buttonset_check_notify_axes(struct buttonset_dispatch *buttonset,
			    struct evdev_device *device,
			    uint32_t time)
{
	struct libinput_device *base = &device->base;
	bool axis_update_needed = false;
	unsigned int a;
	double value;
	struct libinput_buttonset_axis axes[buttonset->naxes];

	/* Suppress the reset to 0 on finger up. See the
	   comment in buttonset_process_absolute */
	if (buttonset->have_abs_misc_terminator &&
	    libevdev_get_event_value(device->evdev, EV_ABS, ABS_MISC) == 0) {
		goto out;
	}

	for (a = 0; a < buttonset->naxes; a++) {
		if (!bit_is_set(buttonset->changed_axes, a)) {
			axes[a] = buttonset->axes[a].base;
			continue;
		}

		switch (buttonset->axes[a].base.type) {
		case LIBINPUT_BUTTONSET_AXIS_RING:
			value = buttonset_handle_ring(buttonset,
						      device,
						      a);
			buttonset->axes[a].base.value.degrees = value;
			buttonset->axes[a].base.source =
				LIBINPUT_BUTTONSET_AXIS_SOURCE_UNKNOWN;
			break;
		case LIBINPUT_BUTTONSET_AXIS_STRIP:
			if (buttonset_handle_strip(buttonset,
						   device,
						   a,
						   &value)) {
				buttonset->axes[a].base.value.normalized =
							value;
				buttonset->axes[a].base.source =
					LIBINPUT_BUTTONSET_AXIS_SOURCE_UNKNOWN;
			} else {
				clear_bit(buttonset->changed_axes, a);
				continue;
			}
			break;
		default:
			log_bug_libinput(device->base.seat->libinput,
					 "Invalid axis update: %u\n", a);
			break;
		}

		axis_update_needed = true;
		axes[a] = buttonset->axes[a].base;
	}

	if (axis_update_needed)
		buttonset_notify_axis(base,
				      time,
				      buttonset->changed_axes,
				      axes,
				      ARRAY_LENGTH(axes));

out:
	memset(buttonset->changed_axes, 0, sizeof(buttonset->changed_axes));
	buttonset->have_abs_misc_terminator = false;
}

static void
buttonset_process_key(struct buttonset_dispatch *buttonset,
		      struct evdev_device *device,
		      struct input_event *e,
		      uint32_t time)
{
	uint32_t button = e->code;
	uint32_t is_press = e->value != 0;

	buttonset_button_set_down(buttonset, button, is_press);
}

static void
buttonset_notify_button_mask(struct buttonset_dispatch *buttonset,
			     struct evdev_device *device,
			     uint32_t time,
			     unsigned long *buttons,
			     enum libinput_button_state state)
{
	struct libinput_device *base = &device->base;
	int32_t num_button;
	unsigned int i;
	struct libinput_buttonset_axis axes[buttonset->naxes];
	unsigned int a;

	for (a = 0; a < buttonset->naxes; a++)
		axes[a] = buttonset->axes[a].base;

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

			buttonset_notify_button(base,
						time,
						axes,
						ARRAY_LENGTH(axes),
						num_button - 1,
						state);
		}
	}
}

static void
buttonset_notify_buttons(struct buttonset_dispatch *buttonset,
			 struct evdev_device *device,
			 uint32_t time,
			 enum libinput_button_state state)
{
	unsigned long buttons[NLONGS(KEY_CNT)];

	if (state == LIBINPUT_BUTTON_STATE_PRESSED)
		buttonset_get_buttons_pressed(buttonset,
					      buttons);
	else
		buttonset_get_buttons_released(buttonset,
					       buttons);

	buttonset_notify_button_mask(buttonset,
				     device,
				     time,
				     buttons,
				     state);
}

static void
sanitize_buttonset_axes(struct buttonset_dispatch *buttonset)
{
}

static void
buttonset_flush(struct buttonset_dispatch *buttonset,
		struct evdev_device *device,
		uint32_t time)
{
	if (buttonset_has_status(buttonset, BUTTONSET_AXES_UPDATED)) {
		sanitize_buttonset_axes(buttonset);
		buttonset_check_notify_axes(buttonset, device, time);
		buttonset_unset_status(buttonset, BUTTONSET_AXES_UPDATED);
	}

	if (buttonset_has_status(buttonset, BUTTONSET_BUTTONS_RELEASED)) {
		buttonset_notify_buttons(buttonset,
					 device,
					 time,
					 LIBINPUT_BUTTON_STATE_RELEASED);
		buttonset_unset_status(buttonset, BUTTONSET_BUTTONS_RELEASED);
	}

	if (buttonset_has_status(buttonset, BUTTONSET_BUTTONS_PRESSED)) {
		buttonset_notify_buttons(buttonset,
					 device,
					 time,
					 LIBINPUT_BUTTON_STATE_PRESSED);
		buttonset_unset_status(buttonset, BUTTONSET_BUTTONS_PRESSED);
	}

	/* Update state */
	memcpy(&buttonset->prev_button_state,
	       &buttonset->button_state,
	       sizeof(buttonset->button_state));
}

static void
buttonset_process(struct evdev_dispatch *dispatch,
		  struct evdev_device *device,
		  struct input_event *e,
		  uint64_t time)
{
	struct buttonset_dispatch *buttonset =
		(struct buttonset_dispatch *)dispatch;

	switch (e->type) {
	case EV_ABS:
		buttonset_process_absolute(buttonset, device, e, time);
		break;
	case EV_KEY:
		buttonset_process_key(buttonset, device, e, time);
		break;
	case EV_SYN:
		buttonset_flush(buttonset, device, time);
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
buttonset_suspend(struct evdev_dispatch *dispatch,
		  struct evdev_device *device)
{
	struct buttonset_dispatch *buttonset =
		(struct buttonset_dispatch *)dispatch;
	struct libinput *libinput = device->base.seat->libinput;
	unsigned int code;

	for (code = KEY_ESC; code < KEY_CNT; code++) {
		if (buttonset_button_is_down(buttonset, code))
			buttonset_button_set_down(buttonset, code, false);
	}

	buttonset_flush(buttonset, device, libinput_now(libinput));
}

static void
buttonset_destroy(struct evdev_dispatch *dispatch)
{
	struct buttonset_dispatch *buttonset =
		(struct buttonset_dispatch*)dispatch;

	free(buttonset);
}

static unsigned int
buttonset_get_num_axes(struct evdev_device *device)
{
	struct buttonset_dispatch *buttonset =
				(struct buttonset_dispatch*)device->dispatch;

	return buttonset->naxes;
}

static enum libinput_buttonset_axis_type
buttonset_get_axis_type(struct evdev_device *device, unsigned int axis)
{
	struct buttonset_dispatch *buttonset =
				(struct buttonset_dispatch*)device->dispatch;

	if (axis < buttonset->naxes)
		return buttonset->axes[axis].base.type;

	log_bug_client(device->base.seat->libinput,
		       "Axis %d does not exist on device %s\n",
		       axis,
		       device->devname);

	return 0;
}

static struct evdev_dispatch_interface buttonset_interface = {
	buttonset_process,
	buttonset_suspend, /* suspend */
	NULL, /* remove */
	buttonset_destroy,
	NULL, /* device_added */
	NULL, /* device_removed */
	NULL, /* device_suspended */
	NULL, /* device_resumed */
	NULL, /* post_added */
	buttonset_get_num_axes,
	buttonset_get_axis_type,
};

static enum libinput_buttonset_axis_type
buttonset_guess_axis_type(struct evdev_device *device,
			  unsigned int evcode)
{
	switch (evcode) {
	case ABS_WHEEL:
	case ABS_THROTTLE:
		return LIBINPUT_BUTTONSET_AXIS_RING;
	case ABS_RX:
	case ABS_RY:
		return LIBINPUT_BUTTONSET_AXIS_STRIP;
	default:
		return LIBINPUT_BUTTONSET_AXIS_NONE;
	}
}

static int
buttonset_init(struct buttonset_dispatch *buttonset,
	       struct evdev_device *device)
{
	unsigned int naxes = 0;
	int code;
	enum libinput_buttonset_axis_type type;

	buttonset->base.interface = &buttonset_interface;
	buttonset->device = device;
	buttonset->status = BUTTONSET_NONE;

	/* We intentionally skip X/Y/Z, they're dead on most wacom pads and
	   the 27QHD sends accelerometer data through those three */
	for (code = ABS_RX; code <= ABS_MAX; code++) {
		buttonset->evcode_map[code] = -1;

		if (!libevdev_has_event_code(device->evdev, EV_ABS, code))
			continue;

		/* Ignore axes we don't know about */
		type = buttonset_guess_axis_type(device, code);
		if (type == LIBINPUT_BUTTONSET_AXIS_NONE)
			continue;

		buttonset->evcode_map[code] = naxes;
		buttonset->axes[naxes].base.type = type;
		buttonset->axes[naxes].evcode = code;
		naxes++;
	}

	buttonset->naxes = naxes;

	return 0;
}

static uint32_t
bs_sendevents_get_modes(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
}

static enum libinput_config_status
bs_sendevents_set_mode(struct libinput_device *device,
		       enum libinput_config_send_events_mode mode)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct buttonset_dispatch *buttonset =
			(struct buttonset_dispatch*)evdev->dispatch;

	if (mode == buttonset->sendevents.current_mode)
		return LIBINPUT_CONFIG_STATUS_SUCCESS;

	switch(mode) {
	case LIBINPUT_CONFIG_SEND_EVENTS_ENABLED:
		break;
	case LIBINPUT_CONFIG_SEND_EVENTS_DISABLED:
		buttonset_suspend(evdev->dispatch, evdev);
		break;
	default:
		return LIBINPUT_CONFIG_STATUS_UNSUPPORTED;
	}

	buttonset->sendevents.current_mode = mode;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_send_events_mode
bs_sendevents_get_mode(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct buttonset_dispatch *dispatch =
			(struct buttonset_dispatch*)evdev->dispatch;

	return dispatch->sendevents.current_mode;
}

static enum libinput_config_send_events_mode
bs_sendevents_get_default_mode(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
}

struct evdev_dispatch *
evdev_buttonset_create(struct evdev_device *device)
{
	struct buttonset_dispatch *buttonset;

	buttonset = zalloc(sizeof *buttonset);
	if (!buttonset)
		return NULL;

	if (buttonset_init(buttonset, device) != 0) {
		buttonset_destroy(&buttonset->base);
		return NULL;
	}

	device->base.config.sendevents = &buttonset->sendevents.config;
	buttonset->sendevents.current_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	buttonset->sendevents.config.get_modes = bs_sendevents_get_modes;
	buttonset->sendevents.config.set_mode = bs_sendevents_set_mode;
	buttonset->sendevents.config.get_mode = bs_sendevents_get_mode;
	buttonset->sendevents.config.get_default_mode = bs_sendevents_get_default_mode;

	return &buttonset->base;
}
