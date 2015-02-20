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

#if HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

#define tablet_set_status(tablet_,s_) (tablet_)->status |= (s_)
#define tablet_unset_status(tablet_,s_) (tablet_)->status &= ~(s_)
#define tablet_has_status(tablet_,s_) (!!((tablet_)->status & (s_)))

static inline void
tablet_get_pressed_buttons(struct tablet_dispatch *tablet,
			   unsigned char *buttons,
			   unsigned int buttons_len)
{
	size_t i;
	const struct button_state *state = &tablet->button_state,
			          *prev_state = &tablet->prev_button_state;

	assert(buttons_len <= ARRAY_LENGTH(state->stylus_buttons));

	for (i = 0; i < buttons_len; i++)
		buttons[i] = state->stylus_buttons[i] &
					~(prev_state->stylus_buttons[i]);
}

static inline void
tablet_get_released_buttons(struct tablet_dispatch *tablet,
			    unsigned char *buttons,
			    unsigned int buttons_len)
{
	size_t i;
	const struct button_state *state = &tablet->button_state,
			          *prev_state = &tablet->prev_button_state;

	assert(buttons_len <= ARRAY_LENGTH(state->stylus_buttons));

	for (i = 0; i < buttons_len; i++)
		buttons[i] = prev_state->stylus_buttons[i] &
					~(state->stylus_buttons[i]);
}

static int
tablet_device_has_axis(struct tablet_dispatch *tablet,
		       enum libinput_tablet_axis axis)
{
	struct libevdev *evdev = tablet->device->evdev;
	bool has_axis = false;
	unsigned int code;

	if (axis == LIBINPUT_TABLET_AXIS_ROTATION_Z) {
		has_axis = (libevdev_has_event_code(evdev,
						    EV_ABS,
						    ABS_TILT_X) &&
			    libevdev_has_event_code(evdev,
						    EV_ABS,
						    ABS_TILT_Y));
	} else if (axis == LIBINPUT_TABLET_AXIS_REL_WHEEL) {
		has_axis = libevdev_has_event_code(evdev,
						   EV_REL,
						   REL_WHEEL);
	} else {
		code = axis_to_evcode(axis);
		has_axis = libevdev_has_event_code(evdev,
						   EV_ABS,
						   code);
	}

	return has_axis;
}

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
	case ABS_Z:
	case ABS_PRESSURE:
	case ABS_TILT_X:
	case ABS_TILT_Y:
	case ABS_DISTANCE:
	case ABS_WHEEL:
		axis = evcode_to_axis(e->code);
		if (axis == LIBINPUT_TABLET_AXIS_NONE) {
			log_bug_libinput(device->base.seat->libinput,
					 "Invalid ABS event code %#x\n",
					 e->code);
			break;
		}

		set_bit(tablet->changed_axes, axis);
		tablet_set_status(tablet, TABLET_AXES_UPDATED);
		break;
	/* tool_id is the identifier for the tool we can use in libwacom
	 * to identify it (if we have one anyway) */
	case ABS_MISC:
		tablet->current_tool_id = e->value;
		break;
	/* Intuos 3 strip data. Should only happen on the Pad device, not on
	   the Pen device. */
	case ABS_RX:
	case ABS_RY:
	/* Only on the 4D mouse (Intuos2), obsolete */
	case ABS_RZ:
	/* Only on the 4D mouse (Intuos2), obsolete.
	   The 24HD sends ABS_THROTTLE on the Pad device for the second
	   wheel but we shouldn't get here on kernel >= 3.17.
	   */
	case ABS_THROTTLE:
	default:
		log_info(device->base.seat->libinput,
			 "Unhandled ABS event code %#x\n", e->code);
		break;
	}
}

static void
tablet_mark_all_axes_changed(struct tablet_dispatch *tablet,
			     struct evdev_device *device)
{
	enum libinput_tablet_axis a;

	for (a = LIBINPUT_TABLET_AXIS_X; a <= LIBINPUT_TABLET_AXIS_MAX; a++) {
		if (tablet_device_has_axis(tablet, a))
			set_bit(tablet->changed_axes, a);
	}

	tablet_set_status(tablet, TABLET_AXES_UPDATED);
}

static void
tablet_change_to_left_handed(struct evdev_device *device)
{
	struct tablet_dispatch *tablet =
		(struct tablet_dispatch*)device->dispatch;

	if (device->left_handed.enabled == device->left_handed.want_enabled)
		return;

	if (!tablet_has_status(tablet, TABLET_TOOL_OUT_OF_PROXIMITY))
		return;

	device->left_handed.enabled = device->left_handed.want_enabled;
}

static void
tablet_update_tool(struct tablet_dispatch *tablet,
		   struct evdev_device *device,
		   enum libinput_tool_type tool,
		   bool enabled)
{
	assert(tool != LIBINPUT_TOOL_NONE);

	if (enabled) {
		tablet->current_tool_type = tool;
		tablet_mark_all_axes_changed(tablet, device);
		tablet_set_status(tablet, TABLET_TOOL_ENTERING_PROXIMITY);
		tablet_unset_status(tablet, TABLET_TOOL_OUT_OF_PROXIMITY);
	}
	else
		tablet_set_status(tablet, TABLET_TOOL_LEAVING_PROXIMITY);
}

static inline double
normalize_pressure_dist_slider(const struct input_absinfo *absinfo)
{
	double range = absinfo->maximum - absinfo->minimum;
	double value = (absinfo->value - absinfo->minimum) / range;

	return value;
}

static inline double
normalize_tilt(const struct input_absinfo *absinfo)
{
	double range = absinfo->maximum - absinfo->minimum;
	double value = (absinfo->value - absinfo->minimum) / range;

	/* Map to the (-1, 1) range */
	return (value * 2) - 1;
}

static inline int32_t
invert_axis(const struct input_absinfo *absinfo)
{
	return absinfo->maximum - (absinfo->value - absinfo->minimum);
}

static void
convert_tilt_to_rotation(struct tablet_dispatch *tablet)
{
	const int offset = 5;
	double x, y;
	double angle = 0.0;

	/* Wacom Intuos 4, 5, Pro mouse calculates rotation from the x/y tilt
	   values. The device has a 175 degree CCW hardware offset but since we use
	   atan2 the effective offset is just 5 degrees.
	   */
	x = tablet->axes[LIBINPUT_TABLET_AXIS_TILT_X];
	y = tablet->axes[LIBINPUT_TABLET_AXIS_TILT_Y];
	clear_bit(tablet->changed_axes, LIBINPUT_TABLET_AXIS_TILT_X);
	clear_bit(tablet->changed_axes, LIBINPUT_TABLET_AXIS_TILT_Y);

	/* atan2 is CCW, we want CW -> negate x */
	if (x || y)
		angle = ((180.0 * atan2(-x, y)) / M_PI);

	angle = fmod(360 + angle - offset, 360);

	tablet->axes[LIBINPUT_TABLET_AXIS_ROTATION_Z] = angle;
	set_bit(tablet->changed_axes, LIBINPUT_TABLET_AXIS_ROTATION_Z);
}

static double
convert_to_degrees(const struct input_absinfo *absinfo, double offset)
{
	/* range is [0, 360[, i.e. range + 1 */
	double range = absinfo->maximum - absinfo->minimum + 1;
	double value = (absinfo->value - absinfo->minimum) / range;

	return fmod(value * 360.0 + offset, 360.0);
}

static inline double
normalize_wheel(struct tablet_dispatch *tablet,
		int value)
{
	struct evdev_device *device = tablet->device;

	return value * device->scroll.wheel_click_angle;
}

static inline double
guess_wheel_delta(double current, double old)
{
       double d1, d2, d3;

       d1 = current - old;
       d2 = (current + 360.0) - old;
       d3 = current - (old + 360.0);

       if (fabs(d2) < fabs(d1))
               d1 = d2;

       if (fabs(d3) < fabs(d1))
               d1 = d3;

       return d1;
}

static inline double
get_delta(enum libinput_tablet_axis axis, double current, double old)
{
	double delta = 0;

	switch (axis) {
	case LIBINPUT_TABLET_AXIS_X:
	case LIBINPUT_TABLET_AXIS_Y:
	case LIBINPUT_TABLET_AXIS_DISTANCE:
	case LIBINPUT_TABLET_AXIS_PRESSURE:
	case LIBINPUT_TABLET_AXIS_SLIDER:
	case LIBINPUT_TABLET_AXIS_TILT_X:
	case LIBINPUT_TABLET_AXIS_TILT_Y:
		delta = current - old;
		break;
	case LIBINPUT_TABLET_AXIS_ROTATION_Z:
		delta = guess_wheel_delta(current, old);
		break;
	default:
		abort();
	}
	return delta;
}

static void
tablet_check_notify_axes(struct tablet_dispatch *tablet,
			 struct evdev_device *device,
			 uint32_t time,
			 struct libinput_tool *tool)
{
	struct libinput_device *base = &device->base;
	bool axis_update_needed = false;
	int a;
	double axes[LIBINPUT_TABLET_AXIS_MAX + 1] = {0};
	double deltas[LIBINPUT_TABLET_AXIS_MAX + 1] = {0};
	double oldval;

	for (a = LIBINPUT_TABLET_AXIS_X; a <= LIBINPUT_TABLET_AXIS_MAX; a++) {
		const struct input_absinfo *absinfo;

		if (!bit_is_set(tablet->changed_axes, a)) {
			axes[a] = tablet->axes[a];
			continue;
		}

		axis_update_needed = true;
		oldval = tablet->axes[a];

		/* ROTATION_Z is higher than TILT_X/Y so we know that the
		   tilt axes are already normalized and set */
		if (a == LIBINPUT_TABLET_AXIS_ROTATION_Z &&
		   (tablet->current_tool_type == LIBINPUT_TOOL_MOUSE ||
		    tablet->current_tool_type == LIBINPUT_TOOL_LENS)) {
			convert_tilt_to_rotation(tablet);
			axes[LIBINPUT_TABLET_AXIS_TILT_X] = 0;
			axes[LIBINPUT_TABLET_AXIS_TILT_Y] = 0;
			axes[a] = tablet->axes[a];
			deltas[a] = get_delta(a, tablet->axes[a], oldval);
			continue;
		} else if (a == LIBINPUT_TABLET_AXIS_REL_WHEEL) {
			deltas[a] = normalize_wheel(tablet,
						    tablet->deltas[a]);
			axes[a] = 0;
			continue;
		}

		absinfo = libevdev_get_abs_info(device->evdev,
						axis_to_evcode(a));

		switch (a) {
		case LIBINPUT_TABLET_AXIS_X:
		case LIBINPUT_TABLET_AXIS_Y:
			if (device->left_handed.enabled)
				tablet->axes[a] = invert_axis(absinfo);
			else
				tablet->axes[a] = absinfo->value;
			break;
		case LIBINPUT_TABLET_AXIS_DISTANCE:
		case LIBINPUT_TABLET_AXIS_PRESSURE:
		case LIBINPUT_TABLET_AXIS_SLIDER:
			tablet->axes[a] = normalize_pressure_dist_slider(absinfo);
			break;
		case LIBINPUT_TABLET_AXIS_TILT_X:
		case LIBINPUT_TABLET_AXIS_TILT_Y:
			tablet->axes[a] = normalize_tilt(absinfo);
			break;
		case LIBINPUT_TABLET_AXIS_ROTATION_Z:
			/* artpen has 0 with buttons pointing east */
			tablet->axes[a] = convert_to_degrees(absinfo, 90);
			break;
		default:
			log_bug_libinput(device->base.seat->libinput,
					 "Invalid axis update: %d\n", a);
			break;
		}

		axes[a] = tablet->axes[a];
		deltas[a] = get_delta(a, tablet->axes[a], oldval);
	}

	/* We need to make sure that we check that the tool is not out of
	 * proximity before we send any axis updates. This is because many
	 * tablets will send axis events with incorrect values if the tablet
	 * tool is close enough so that the tablet can partially detect that
	 * it's there, but can't properly receive any data from the tool. */
	if (axis_update_needed &&
	    !tablet_has_status(tablet, TABLET_TOOL_OUT_OF_PROXIMITY) &&
	    !tablet_has_status(tablet, TABLET_TOOL_LEAVING_PROXIMITY)) {
		if (tablet_has_status(tablet, TABLET_TOOL_ENTERING_PROXIMITY))
			tablet_notify_proximity(&device->base,
						time,
						tool,
						LIBINPUT_TOOL_PROXIMITY_IN,
						tablet->changed_axes,
						axes);
		else
			tablet_notify_axis(base,
					   time,
					   tool,
					   tablet->changed_axes,
					   axes,
					   deltas);
	}

	memset(tablet->changed_axes, 0, sizeof(tablet->changed_axes));
}

static void
tablet_update_button(struct tablet_dispatch *tablet,
		     uint32_t evcode,
		     uint32_t enable)
{
	switch (evcode) {
	case BTN_LEFT:
	case BTN_RIGHT:
	case BTN_MIDDLE:
	case BTN_SIDE:
	case BTN_EXTRA:
	case BTN_FORWARD:
	case BTN_BACK:
	case BTN_TASK:
	case BTN_TOUCH:
	case BTN_STYLUS:
	case BTN_STYLUS2:
		break;
	default:
		log_info(tablet->device->base.seat->libinput,
			 "Unhandled button %s (%#x)\n",
			 libevdev_event_code_get_name(EV_KEY, evcode), evcode);
		return;
	}

	if (enable) {
		set_bit(tablet->button_state.stylus_buttons, evcode);
		tablet_set_status(tablet, TABLET_BUTTONS_PRESSED);
	} else {
		clear_bit(tablet->button_state.stylus_buttons, evcode);
		tablet_set_status(tablet, TABLET_BUTTONS_RELEASED);
	}
}

static inline enum libinput_tool_type
tablet_evcode_to_tool(int code)
{
	enum libinput_tool_type type;

	switch (code) {
	case BTN_TOOL_PEN:	type = LIBINPUT_TOOL_PEN;	break;
	case BTN_TOOL_RUBBER:	type = LIBINPUT_TOOL_ERASER;	break;
	case BTN_TOOL_BRUSH:	type = LIBINPUT_TOOL_BRUSH;	break;
	case BTN_TOOL_PENCIL:	type = LIBINPUT_TOOL_PENCIL;	break;
	case BTN_TOOL_AIRBRUSH:	type = LIBINPUT_TOOL_AIRBRUSH;	break;
	case BTN_TOOL_FINGER:	type = LIBINPUT_TOOL_FINGER;	break;
	case BTN_TOOL_MOUSE:	type = LIBINPUT_TOOL_MOUSE;	break;
	case BTN_TOOL_LENS:	type = LIBINPUT_TOOL_LENS;	break;
	default:
		abort();
	}

	return type;
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
		tablet_update_tool(tablet,
				   device,
				   tablet_evcode_to_tool(e->code),
				   e->value);
		break;
	case BTN_TOUCH:
		if (e->value)
			tablet_set_status(tablet, TABLET_STYLUS_IN_CONTACT);
		else
			tablet_unset_status(tablet, TABLET_STYLUS_IN_CONTACT);

		/* Fall through */
	case BTN_LEFT:
	case BTN_RIGHT:
	case BTN_MIDDLE:
	case BTN_SIDE:
	case BTN_EXTRA:
	case BTN_FORWARD:
	case BTN_BACK:
	case BTN_TASK:
	case BTN_STYLUS:
	case BTN_STYLUS2:
	default:
		tablet_update_button(tablet, e->code, e->value);
		break;
	}
}

static void
tablet_process_relative(struct tablet_dispatch *tablet,
			struct evdev_device *device,
			struct input_event *e,
			uint32_t time)
{
	enum libinput_tablet_axis axis;
	switch (e->code) {
	case REL_WHEEL:
		axis = rel_evcode_to_axis(e->code);
		if (axis == LIBINPUT_TABLET_AXIS_NONE) {
			log_bug_libinput(device->base.seat->libinput,
					 "Invalid ABS event code %#x\n",
					 e->code);
			break;
		}
		set_bit(tablet->changed_axes, axis);
		tablet->deltas[axis] = -1 * e->value;
		tablet_set_status(tablet, TABLET_AXES_UPDATED);
		break;
	default:
		log_info(tablet->device->base.seat->libinput,
			 "Unhandled relative axis %s (%#x)\n",
			 libevdev_event_code_get_name(EV_REL, e->code),
			 e->code);
		return;
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
		if (e->value != -1)
			tablet->current_tool_serial = e->value;

		break;
	default:
		log_info(device->base.seat->libinput,
			 "Unhandled MSC event code %s (%#x)\n",
			 libevdev_event_code_get_name(EV_MSC, e->code),
			 e->code);
		break;
	}
}

static inline void
copy_axis_cap(const struct tablet_dispatch *tablet,
	      struct libinput_tool *tool,
	      enum libinput_tablet_axis axis)
{
	if (bit_is_set(tablet->axis_caps, axis))
		set_bit(tool->axis_caps, axis);
}

static inline void
copy_button_cap(const struct tablet_dispatch *tablet,
		struct libinput_tool *tool,
		uint32_t button)
{
	struct libevdev *evdev = tablet->device->evdev;
	if (libevdev_has_event_code(evdev, EV_KEY, button))
		set_bit(tool->buttons, button);
}

static inline int
tool_set_bits_from_libwacom(const struct tablet_dispatch *tablet,
			    struct libinput_tool *tool)
{
	int rc = 1;

#if HAVE_LIBWACOM
	WacomDeviceDatabase *db;
	const WacomStylus *s = NULL;
	int code;
	WacomStylusType type;

	db = libwacom_database_new();
	if (!db)
		goto out;
	s = libwacom_stylus_get_for_id(db, tool->tool_id);
	if (!s)
		goto out;

	type = libwacom_stylus_get_type(s);
	if (type == WSTYLUS_PUCK) {
		for (code = BTN_LEFT;
		     code < BTN_LEFT + libwacom_stylus_get_num_buttons(s);
		     code++)
			copy_button_cap(tablet, tool, code);
	} else {
		if (libwacom_stylus_get_num_buttons(s) >= 2)
			copy_button_cap(tablet, tool, BTN_STYLUS2);
		if (libwacom_stylus_get_num_buttons(s) >= 1)
			copy_button_cap(tablet, tool, BTN_STYLUS);
		copy_button_cap(tablet, tool, BTN_TOUCH);
	}

	/* Eventually we want libwacom to tell us each axis on each device
	   separately. */
	switch(type) {
	case WSTYLUS_AIRBRUSH:
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_SLIDER);
		/* fall-through */
	case WSTYLUS_MARKER:
		if (type == WSTYLUS_MARKER)
			copy_axis_cap(tablet, tool,
				      LIBINPUT_TABLET_AXIS_ROTATION_Z);
		/* fallthrough */
	case WSTYLUS_GENERAL:
	case WSTYLUS_INKING:
	case WSTYLUS_CLASSIC:
	case WSTYLUS_STROKE:
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_PRESSURE);
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_DISTANCE);
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_TILT_X);
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_TILT_Y);
		break;
	case WSTYLUS_PUCK:
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_ROTATION_Z);
		/* lens cursors don't have a wheel */
		if (!libwacom_stylus_has_lens(s))
			copy_axis_cap(tablet,
				      tool,
				      LIBINPUT_TABLET_AXIS_REL_WHEEL);
		break;
	default:
		break;
	}

	rc = 0;
out:
	if (db)
		libwacom_database_destroy(db);
#endif
	return rc;
}

static void
tool_set_bits(const struct tablet_dispatch *tablet,
	      struct libinput_tool *tool)
{
	enum libinput_tool_type type = tool->type;

#if HAVE_LIBWACOM
	if (tool_set_bits_from_libwacom(tablet, tool) == 0)
		return;
#endif
	/* If we don't have libwacom, we simply copy any axis we have on the
	   tablet onto the tool. Except we know that mice only have rotation
	   anyway.
	 */
	switch (type) {
	case LIBINPUT_TOOL_PEN:
	case LIBINPUT_TOOL_ERASER:
	case LIBINPUT_TOOL_PENCIL:
	case LIBINPUT_TOOL_BRUSH:
	case LIBINPUT_TOOL_AIRBRUSH:
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_PRESSURE);
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_DISTANCE);
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_TILT_X);
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_TILT_Y);
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_SLIDER);
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_ROTATION_Z);
		break;
	case LIBINPUT_TOOL_MOUSE:
	case LIBINPUT_TOOL_LENS:
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_ROTATION_Z);
		copy_axis_cap(tablet, tool, LIBINPUT_TABLET_AXIS_REL_WHEEL);
		break;
	default:
		break;
	}

	/* If we don't have libwacom, copy all pen-related ones from the
	   tablet vs all mouse-related ones */
	switch (type) {
	case LIBINPUT_TOOL_PEN:
	case LIBINPUT_TOOL_BRUSH:
	case LIBINPUT_TOOL_AIRBRUSH:
	case LIBINPUT_TOOL_PENCIL:
	case LIBINPUT_TOOL_ERASER:
		copy_button_cap(tablet, tool, BTN_STYLUS);
		copy_button_cap(tablet, tool, BTN_STYLUS2);
		copy_button_cap(tablet, tool, BTN_TOUCH);
		break;
	case LIBINPUT_TOOL_MOUSE:
	case LIBINPUT_TOOL_LENS:
		copy_button_cap(tablet, tool, BTN_LEFT);
		copy_button_cap(tablet, tool, BTN_MIDDLE);
		copy_button_cap(tablet, tool, BTN_RIGHT);
		copy_button_cap(tablet, tool, BTN_SIDE);
		copy_button_cap(tablet, tool, BTN_EXTRA);
		break;
	default:
		break;
	}
}

static struct libinput_tool *
tablet_get_tool(struct tablet_dispatch *tablet,
		enum libinput_tool_type type,
		uint32_t tool_id,
		uint32_t serial)
{
	struct libinput_tool *tool = NULL, *t;
	struct list *tool_list;

	if (serial) {
		tool_list = &tablet->device->base.seat->libinput->tool_list;

		/* Check if we already have the tool in our list of tools */
		list_for_each(t, tool_list, link) {
			if (type == t->type && serial == t->serial) {
				tool = t;
				break;
			}
		}
	} else {
		/* We can't guarantee that tools without serial numbers are
		 * unique, so we keep them local to the tablet that they come
		 * into proximity of instead of storing them in the global tool
		 * list */
		tool_list = &tablet->tool_list;

		/* Same as above, but don't bother checking the serial number */
		list_for_each(t, tool_list, link) {
			if (type == t->type) {
				tool = t;
				break;
			}
		}
	}

	/* If we didn't already have the new_tool in our list of tools,
	 * add it */
	if (!tool) {
		tool = zalloc(sizeof *tool);
		*tool = (struct libinput_tool) {
			.type = type,
			.serial = serial,
			.tool_id = tool_id,
			.refcount = 1,
		};

		tool_set_bits(tablet, tool);

		list_insert(tool_list, &tool->link);
	}

	return tool;
}

static void
tablet_notify_button_mask(struct tablet_dispatch *tablet,
			  struct evdev_device *device,
			  uint32_t time,
			  struct libinput_tool *tool,
			  const unsigned char *buttons,
			  unsigned int buttons_len,
			  enum libinput_button_state state)
{
	struct libinput_device *base = &device->base;
	size_t i;
	size_t nbits = 8 * sizeof(buttons[0]) * buttons_len;

	for (i = 0; i < nbits; i++) {
		if (!bit_is_set(buttons, i))
			continue;

		tablet_notify_button(base,
				     time,
				     tool,
				     tablet->axes,
				     i,
				     state);
	}
}

static void
tablet_notify_buttons(struct tablet_dispatch *tablet,
		      struct evdev_device *device,
		      uint32_t time,
		      struct libinput_tool *tool,
		      enum libinput_button_state state)
{
	unsigned char buttons[ARRAY_LENGTH(tablet->button_state.stylus_buttons)];

	if (state == LIBINPUT_BUTTON_STATE_PRESSED)
		tablet_get_pressed_buttons(tablet, buttons, sizeof(buttons));
	else
		tablet_get_released_buttons(tablet,
					    buttons,
					    sizeof(buttons));

	tablet_notify_button_mask(tablet,
				  device,
				  time,
				  tool,
				  buttons,
				  sizeof(buttons),
				  state);
}

static void
sanitize_tablet_axes(struct tablet_dispatch *tablet)
{
	const struct input_absinfo *distance,
	                           *pressure;

	distance = libevdev_get_abs_info(tablet->device->evdev, ABS_DISTANCE);
	pressure = libevdev_get_abs_info(tablet->device->evdev, ABS_PRESSURE);

	/* Keep distance and pressure mutually exclusive */
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

	/* If we have a mouse/lens cursor and the tilt changed, the rotation
	   changed. Mark this, calculate the angle later */
	if ((tablet->current_tool_type == LIBINPUT_TOOL_MOUSE ||
	    tablet->current_tool_type == LIBINPUT_TOOL_LENS) &&
	    (bit_is_set(tablet->changed_axes, LIBINPUT_TABLET_AXIS_TILT_X) ||
	     bit_is_set(tablet->changed_axes, LIBINPUT_TABLET_AXIS_TILT_Y)))
		set_bit(tablet->changed_axes, LIBINPUT_TABLET_AXIS_ROTATION_Z);
}

static void
tablet_flush(struct tablet_dispatch *tablet,
	     struct evdev_device *device,
	     uint32_t time)
{
	struct libinput_tool *tool =
		tablet_get_tool(tablet,
				tablet->current_tool_type,
				tablet->current_tool_id,
				tablet->current_tool_serial);

	if (tablet_has_status(tablet, TABLET_TOOL_LEAVING_PROXIMITY)) {
		/* Release all stylus buttons */
		memset(tablet->button_state.stylus_buttons,
		       0,
		       sizeof(tablet->button_state.stylus_buttons));
		tablet_set_status(tablet, TABLET_BUTTONS_RELEASED);
	} else if (tablet_has_status(tablet, TABLET_AXES_UPDATED) ||
		   tablet_has_status(tablet, TABLET_TOOL_ENTERING_PROXIMITY)) {
		sanitize_tablet_axes(tablet);
		tablet_check_notify_axes(tablet, device, time, tool);

		tablet_unset_status(tablet, TABLET_TOOL_ENTERING_PROXIMITY);
		tablet_unset_status(tablet, TABLET_AXES_UPDATED);
	}

	if (tablet_has_status(tablet, TABLET_BUTTONS_RELEASED)) {
		tablet_notify_buttons(tablet,
				      device,
				      time,
				      tool,
				      LIBINPUT_BUTTON_STATE_RELEASED);
		tablet_unset_status(tablet, TABLET_BUTTONS_RELEASED);
	}

	if (tablet_has_status(tablet, TABLET_BUTTONS_PRESSED)) {
		tablet_notify_buttons(tablet,
				      device,
				      time,
				      tool,
				      LIBINPUT_BUTTON_STATE_PRESSED);
		tablet_unset_status(tablet, TABLET_BUTTONS_PRESSED);
	}

	if (tablet_has_status(tablet, TABLET_TOOL_LEAVING_PROXIMITY)) {
		memset(tablet->changed_axes, 0, sizeof(tablet->changed_axes));
		tablet_notify_proximity(&device->base,
					time,
					tool,
					LIBINPUT_TOOL_PROXIMITY_OUT,
					tablet->changed_axes,
					tablet->axes);

		tablet_set_status(tablet, TABLET_TOOL_OUT_OF_PROXIMITY);
		tablet_unset_status(tablet, TABLET_TOOL_LEAVING_PROXIMITY);

		tablet_change_to_left_handed(device);
	}

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
	case EV_REL:
		tablet_process_relative(tablet, device, e, time);
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
		log_error(device->base.seat->libinput,
			  "Unexpected event type %s (%#x)\n",
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
	struct libinput_tool *tool, *tmp;

	list_for_each_safe(tool, tmp, &tablet->tool_list, link) {
		libinput_tool_unref(tool);
	}

	free(tablet);
}

static struct evdev_dispatch_interface tablet_interface = {
	tablet_process,
	NULL, /* remove */
	tablet_destroy,
	NULL, /* device_added */
	NULL, /* device_removed */
	NULL, /* device_suspended */
	NULL, /* device_resumed */
	NULL, /* tag_device */
};

static int
tablet_init(struct tablet_dispatch *tablet,
	    struct evdev_device *device)
{
	enum libinput_tablet_axis axis;

	tablet->base.interface = &tablet_interface;
	tablet->device = device;
	tablet->status = TABLET_NONE;
	tablet->current_tool_type = LIBINPUT_TOOL_NONE;
	list_init(&tablet->tool_list);

	for (axis = LIBINPUT_TABLET_AXIS_X;
	     axis <= LIBINPUT_TABLET_AXIS_MAX;
	     axis++) {
		if (tablet_device_has_axis(tablet, axis))
			set_bit(tablet->axis_caps, axis);
	}

	tablet_mark_all_axes_changed(tablet, device);

	tablet_set_status(tablet, TABLET_TOOL_OUT_OF_PROXIMITY);

	return 0;
}

static void
tablet_init_left_handed(struct evdev_device *device)
{
#if HAVE_LIBWACOM
	struct libinput *libinput = device->base.seat->libinput;
	WacomDeviceDatabase *db;
	WacomDevice *d = NULL;
	WacomError *error;
	int vid, pid;

	vid = evdev_device_get_id_vendor(device);
	pid = evdev_device_get_id_product(device);

	db = libwacom_database_new();
	if (!db)
		return;
	error = libwacom_error_new();
	d = libwacom_new_from_usbid(db, vid, pid, error);

	if (d) {
		if (libwacom_is_reversible(d))
		    evdev_init_left_handed(device,
					   tablet_change_to_left_handed);
	} else if (libwacom_error_get_code(error) == WERROR_UNKNOWN_MODEL) {
		log_info(libinput, "Tablet unknown to libwacom\n");
	} else {
		log_error(libinput,
			  "libwacom error: %s\n",
			  libwacom_error_get_message(error));
	}

	if (error)
		libwacom_error_free(&error);
	if (d)
		libwacom_destroy(d);
	libwacom_database_destroy(db);
#endif
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

	tablet_init_left_handed(device);

	return &tablet->base;
}
