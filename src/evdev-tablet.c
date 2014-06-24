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
#include "config.h"
#include "evdev-tablet.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#define tablet_set_status(tablet_,s_) (tablet_)->status |= (s_)
#define tablet_unset_status(tablet_,s_) (tablet_)->status &= ~(s_)
#define tablet_has_status(tablet_,s_) (!!((tablet_)->status & (s_)))

#define tablet_get_pressed_buttons(tablet_,field_) \
       ((tablet_)->button_state.field_ & ~((tablet_)->prev_button_state.field_))
#define tablet_get_released_buttons(tablet_,field_) \
       ((tablet_)->prev_button_state.field_ & ~((tablet_)->button_state.field_))

static void
tablet_process_absolute(struct tablet_dispatch *tablet,
			struct evdev_device *device,
			struct input_event *e,
			uint32_t time)
{
	enum libinput_tablet_axis axis;

	switch (e->code) {
	case ABS_X:
	case ABS_Y:
	case ABS_PRESSURE:
	case ABS_TILT_X:
	case ABS_TILT_Y:
	case ABS_DISTANCE:
		axis = evcode_to_axis(e->code);
		if (axis == LIBINPUT_TABLET_AXIS_NONE) {
			log_bug_libinput("Invalid ABS event code %#x\n",
					 e->code);
			break;
		}

		set_bit(tablet->changed_axes, axis);
		tablet_set_status(tablet, TABLET_AXES_UPDATED);
		break;
	default:
		log_info("Unhandled ABS event code %#x\n", e->code);
		break;
	}
}

static void
tablet_mark_all_axes_changed(struct tablet_dispatch *tablet,
			     struct evdev_device *device)
{
	enum libinput_tablet_axis a;

	for (a = 0; a < LIBINPUT_TABLET_AXIS_CNT; a++) {
		if (libevdev_has_event_code(device->evdev,
					    EV_ABS,
					    axis_to_evcode(a)))
			set_bit(tablet->changed_axes, a);
	}

	tablet_set_status(tablet, TABLET_AXES_UPDATED);
}

static void
tablet_update_tool(struct tablet_dispatch *tablet,
		   struct evdev_device *device,
		   enum libinput_tool_type tool,
		   bool enabled)
{
	assert(tool != LIBINPUT_TOOL_NONE);

	if (enabled) {
		if (tool != tablet->current_tool_type) {
			tablet->current_tool_type = tool;
			tablet_set_status(tablet, TABLET_TOOL_UPDATED);
		}
		tablet_mark_all_axes_changed(tablet, device);
		tablet_unset_status(tablet, TABLET_TOOL_OUT_OF_PROXIMITY);
	}
	else
		tablet_set_status(tablet, TABLET_TOOL_OUT_OF_PROXIMITY);
}

static inline double
normalize_pressure_or_dist(const struct input_absinfo * absinfo) {
	double range = absinfo->maximum - absinfo->minimum + 1;
	double value = (absinfo->value + absinfo->minimum) / range;

	return value;
}

static inline double
normalize_tilt(const struct input_absinfo * absinfo) {
	double range = absinfo->maximum - absinfo->minimum + 1;
	double value = (absinfo->value + absinfo->minimum) / range;

	/* Map to the (-1, 1) range */
	return (value * 2) - 1;
}

static void
tablet_check_notify_axes(struct tablet_dispatch *tablet,
			 struct evdev_device *device,
			 uint32_t time)
{
	struct libinput_device *base = &device->base;
	bool axis_update_needed = false;
	int a;
	double dx, dy;

	for (a = 0; a < LIBINPUT_TABLET_AXIS_CNT; a++) {
		const struct input_absinfo *absinfo;

		if (!bit_is_set(tablet->changed_axes, a))
			continue;

		absinfo = libevdev_get_abs_info(device->evdev,
						axis_to_evcode(a));

		switch (a) {
		case LIBINPUT_TABLET_AXIS_X:
			dx = -tablet->axes[a];
			tablet->axes[a] = evdev_convert_to_mm(absinfo,
							      absinfo->value);
			dx += tablet->axes[a];
			break;
		case LIBINPUT_TABLET_AXIS_Y:
			dy = -tablet->axes[a];
			tablet->axes[a] = evdev_convert_to_mm(absinfo,
							      absinfo->value);
			dy += tablet->axes[a];
			break;
		case LIBINPUT_TABLET_AXIS_DISTANCE:
		case LIBINPUT_TABLET_AXIS_PRESSURE:
			tablet->axes[a] = normalize_pressure_or_dist(absinfo);
			break;
		case LIBINPUT_TABLET_AXIS_TILT_VERTICAL:
		case LIBINPUT_TABLET_AXIS_TILT_HORIZONTAL:
			tablet->axes[a] = normalize_tilt(absinfo);
			break;
		default:
			log_bug_libinput("Invalid axis update: %d\n", a);
			break;
		}

		axis_update_needed = true;
	}

	if (axis_update_needed &&
	    !tablet_has_status(tablet, TABLET_TOOL_OUT_OF_PROXIMITY)) {
		if (tablet->current_tool_type != LIBINPUT_TOOL_MOUSE) {
			tablet_notify_axis(base,
					   time,
					   tablet->changed_axes,
					   tablet->axes);
		} else {
			tablet_notify_axis_relative(base,
						    time,
						    tablet->changed_axes,
						    dx,
						    dy,
						    tablet->axes);
		}
	}

	memset(tablet->changed_axes, 0, sizeof(tablet->changed_axes));
}

static void
tablet_update_button(struct tablet_dispatch *tablet,
		     uint32_t evcode,
		     uint32_t enable)
{
	uint32_t button, *mask;

	/* XXX: This really depends on the expected buttons fitting in the mask */
	if (evcode >= BTN_MISC && evcode <= BTN_TASK) {
		return;
	} else if (evcode >= BTN_TOUCH && evcode <= BTN_STYLUS2) {
		mask = &tablet->button_state.stylus_buttons;
		button = evcode - BTN_TOUCH;
	} else {
		log_info("Unhandled button %s (%#x)\n",
			 libevdev_event_code_get_name(EV_KEY, evcode), evcode);
		return;
	}

	assert(button < 32);

	if (enable) {
		(*mask) |= 1 << button;
		tablet_set_status(tablet, TABLET_BUTTONS_PRESSED);
	} else {
		(*mask) &= ~(1 << button);
		tablet_set_status(tablet, TABLET_BUTTONS_RELEASED);
	}
}

static void
tablet_process_key(struct tablet_dispatch *tablet,
		   struct evdev_device *device,
		   struct input_event *e,
		   uint32_t time)
{
	switch (e->code) {
	case BTN_TOOL_PEN:
	case BTN_TOOL_RUBBER:
	case BTN_TOOL_BRUSH:
	case BTN_TOOL_PENCIL:
	case BTN_TOOL_AIRBRUSH:
	case BTN_TOOL_FINGER:
	case BTN_TOOL_MOUSE:
	case BTN_TOOL_LENS:
		/* These codes have an equivalent libinput_tool value */
		tablet_update_tool(tablet, device, e->code, e->value);
		break;
	case BTN_TOUCH:
		if (e->value)
			tablet_set_status(tablet, TABLET_STYLUS_IN_CONTACT);
		else
			tablet_unset_status(tablet, TABLET_STYLUS_IN_CONTACT);

		/* Fall through */
	case BTN_STYLUS:
	case BTN_STYLUS2:
	default:
		tablet_update_button(tablet, e->code, e->value);
		break;
	}
}

static void
tablet_process_misc(struct tablet_dispatch *tablet,
		    struct evdev_device *device,
		    struct input_event *e,
		    uint32_t time)
{
	switch (e->code) {
	case MSC_SERIAL:
		if (e->value != (int32_t)tablet->current_tool_serial &&
		    e->value != -1) {
			tablet->current_tool_serial = e->value;
			tablet_set_status(tablet, TABLET_TOOL_UPDATED);
			tablet_unset_status(tablet,
					    TABLET_TOOL_OUT_OF_PROXIMITY);
		}
		break;
	default:
		log_info("Unhandled MSC event code %s (%#x)\n",
			 libevdev_event_code_get_name(EV_MSC, e->code),
			 e->code);
		break;
	}
}

static void
tablet_notify_tool(struct tablet_dispatch *tablet,
		   struct evdev_device *device,
		   uint32_t time)
{
	struct libinput_device *base = &device->base;
	struct libinput_tool *tool;
	struct libinput_tool *new_tool = NULL;

	/* Check if we already have the tool in our list of tools */
	list_for_each(tool, &base->seat->libinput->tool_list, link) {
		if (tablet->current_tool_type == tool->type &&
		    tablet->current_tool_serial == tool->serial) {
			new_tool = tool;
			break;
		}
	}

	/* If we didn't already have the tool in our list of tools, add it */
	if (new_tool == NULL) {
		new_tool = zalloc(sizeof *new_tool);
		*new_tool = (struct libinput_tool) {
			.type = tablet->current_tool_type,
			.serial = tablet->current_tool_serial,
			.refcount = 1,
		};

		list_insert(&base->seat->libinput->tool_list, &new_tool->link);
	}

	tablet_notify_tool_update(base, time, new_tool);
}

static void
tablet_notify_button_mask(struct tablet_dispatch *tablet,
			  struct evdev_device *device,
			  uint32_t time,
			  uint32_t buttons,
			  uint32_t button_base,
			  enum libinput_button_state state)
{
	struct libinput_device *base = &device->base;
	int32_t num_button = 0;

	while (buttons) {
		int enabled;

		num_button++;
		enabled = (buttons & 1);
		buttons >>= 1;

		if (!enabled)
			continue;

		tablet_notify_button(base,
				     time,
				     num_button + button_base - 1,
				     state);
	}
}

static void
tablet_notify_buttons(struct tablet_dispatch *tablet,
		      struct evdev_device *device,
		      uint32_t time,
		      enum libinput_button_state state)
{
	uint32_t stylus_buttons;

	if (state == LIBINPUT_BUTTON_STATE_PRESSED)
		stylus_buttons =
			tablet_get_pressed_buttons(tablet, stylus_buttons);
	else
		stylus_buttons =
			tablet_get_released_buttons(tablet, stylus_buttons);

	tablet_notify_button_mask(tablet, device, time,
				  stylus_buttons, BTN_TOUCH, state);
}

static void
sanitize_tablet_axes(struct tablet_dispatch *tablet)
{
	const struct input_absinfo *distance,
	                           *pressure;

	distance = libevdev_get_abs_info(tablet->device->evdev, ABS_DISTANCE);
	pressure = libevdev_get_abs_info(tablet->device->evdev, ABS_PRESSURE);

	/* Keep distance and pressure mutually exclusive. In addition, filter
	 * out invalid distance events that can occur when the tablet tool is
	 * close enough for the tablet to detect that's something's there, but
	 * not close enough for it to actually receive data from the tool
	 * properly
	 */
	if (bit_is_set(tablet->changed_axes, LIBINPUT_TABLET_AXIS_DISTANCE) &&
	    distance->value > distance->minimum &&
	    pressure->value > pressure->minimum) {
		clear_bit(tablet->changed_axes, LIBINPUT_TABLET_AXIS_DISTANCE);
		tablet->axes[LIBINPUT_TABLET_AXIS_DISTANCE] = 0;
	} else if (bit_is_set(tablet->changed_axes, LIBINPUT_TABLET_AXIS_PRESSURE) &&
		   !tablet_has_status(tablet, TABLET_STYLUS_IN_CONTACT)) {
		/* Make sure that the last axis value sent to the caller is a 0 */
		if (tablet->axes[LIBINPUT_TABLET_AXIS_PRESSURE] == 0)
			clear_bit(tablet->changed_axes,
				  LIBINPUT_TABLET_AXIS_PRESSURE);
		else
			tablet->axes[LIBINPUT_TABLET_AXIS_PRESSURE] = 0;
	}
}

static void
tablet_flush(struct tablet_dispatch *tablet,
	     struct evdev_device *device,
	     uint32_t time)
{
	if (tablet_has_status(tablet, TABLET_TOOL_OUT_OF_PROXIMITY)) {
		/* Release all stylus buttons */
		tablet->button_state.stylus_buttons = 0;
		tablet_set_status(tablet, TABLET_BUTTONS_RELEASED);
	} else if (tablet_has_status(tablet, TABLET_TOOL_UPDATED)) {
		tablet_notify_tool(tablet, device, time);
		tablet_unset_status(tablet, TABLET_TOOL_UPDATED);
	}

	if (tablet_has_status(tablet, TABLET_AXES_UPDATED)) {
		sanitize_tablet_axes(tablet);
		tablet_check_notify_axes(tablet, device, time);
		tablet_unset_status(tablet, TABLET_AXES_UPDATED);
	}

	if (tablet_has_status(tablet, TABLET_BUTTONS_RELEASED)) {
		tablet_notify_buttons(tablet, device, time,
				      LIBINPUT_BUTTON_STATE_RELEASED);
		tablet_unset_status(tablet, TABLET_BUTTONS_RELEASED);
	}

	if (tablet_has_status(tablet, TABLET_BUTTONS_PRESSED)) {
		tablet_notify_buttons(tablet, device, time,
				      LIBINPUT_BUTTON_STATE_PRESSED);
		tablet_unset_status(tablet, TABLET_BUTTONS_PRESSED);
	}

	if (tablet_has_status(tablet, TABLET_TOOL_OUT_OF_PROXIMITY))
		tablet_notify_proximity_out(&device->base, time);

	/* Update state */
	memcpy(&tablet->prev_button_state,
	       &tablet->button_state,
	       sizeof(tablet->button_state));
}

static void
tablet_process(struct evdev_dispatch *dispatch,
	       struct evdev_device *device,
	       struct input_event *e,
	       uint64_t time)
{
	struct tablet_dispatch *tablet =
		(struct tablet_dispatch *)dispatch;

	switch (e->type) {
	case EV_ABS:
		tablet_process_absolute(tablet, device, e, time);
		break;
	case EV_KEY:
		tablet_process_key(tablet, device, e, time);
		break;
	case EV_MSC:
		tablet_process_misc(tablet, device, e, time);
		break;
	case EV_SYN:
		tablet_flush(tablet, device, time);
		break;
	default:
		log_error("Unexpected event type %s (%#x)\n",
			  libevdev_event_type_get_name(e->type),
			  e->type);
		break;
	}
}

static void
tablet_destroy(struct evdev_dispatch *dispatch)
{
	struct tablet_dispatch *tablet =
		(struct tablet_dispatch*)dispatch;

	free(tablet);
}

static struct evdev_dispatch_interface tablet_interface = {
	tablet_process,
	tablet_destroy
};

static int
tablet_init(struct tablet_dispatch *tablet,
	    struct evdev_device *device)
{
	tablet->base.interface = &tablet_interface;
	tablet->device = device;
	tablet->status = TABLET_NONE;
	tablet->current_tool_type = LIBINPUT_TOOL_NONE;

	tablet_mark_all_axes_changed(tablet, device);

	return 0;
}

struct evdev_dispatch *
evdev_tablet_create(struct evdev_device *device)
{
	struct tablet_dispatch *tablet;

	tablet = zalloc(sizeof *tablet);
	if (!tablet)
		return NULL;

	if (tablet_init(tablet, device) != 0) {
		tablet_destroy(&tablet->base);
		return NULL;
	}

	return &tablet->base;
}
