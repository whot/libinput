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
		if (libevdev_has_event_code(device->evdev,
					    EV_ABS,
					    axis_to_evcode(a)))
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
normalize_pressure_or_dist(const struct input_absinfo *absinfo)
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
tablet_check_notify_axes(struct tablet_dispatch *tablet,
			 struct evdev_device *device,
			 uint32_t time,
			 struct libinput_tool *tool)
{
	struct libinput_device *base = &device->base;
	bool axis_update_needed = false;
	int a;

	for (a = LIBINPUT_TABLET_AXIS_X; a <= LIBINPUT_TABLET_AXIS_MAX; a++) {
		const struct input_absinfo *absinfo;

		if (!bit_is_set(tablet->changed_axes, a))
			continue;

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
			tablet->axes[a] = normalize_pressure_or_dist(absinfo);
			break;
		case LIBINPUT_TABLET_AXIS_TILT_X:
		case LIBINPUT_TABLET_AXIS_TILT_Y:
			tablet->axes[a] = normalize_tilt(absinfo);
			break;
		default:
			log_bug_libinput(device->base.seat->libinput,
					 "Invalid axis update: %d\n", a);
			break;
		}

		axis_update_needed = true;
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
						tablet->axes);
		else
			tablet_notify_axis(base,
					   time,
					   tool,
					   tablet->changed_axes,
					   tablet->axes);
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
		log_info(tablet->device->base.seat->libinput,
			 "Unhandled button %s (%#x)\n",
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

		/* Determine the axis capabilities of the tool. Here's a break
		 * down of the heuristics used here:
		 * - The Wacom art pen supports all of the extra axes, along
		 *   with rotation
		 * - All of normal pens and the airbrush support all of the
		 *   extra axes if the tablet can report them
		 * - All of the mouse like devices don't really report any of
		 *   the extra axes except for rotation.
		 * (as of writing this comment, rotation isn't supported, so you
		 * won't see the mouse or art pen here)
		 */
		switch (type) {
		case LIBINPUT_TOOL_PEN:
		case LIBINPUT_TOOL_ERASER:
		case LIBINPUT_TOOL_PENCIL:
		case LIBINPUT_TOOL_BRUSH:
		case LIBINPUT_TOOL_AIRBRUSH:
			if (bit_is_set(tablet->axis_caps,
				       LIBINPUT_TABLET_AXIS_PRESSURE))
				set_bit(tool->axis_caps,
					LIBINPUT_TABLET_AXIS_PRESSURE);
			if (bit_is_set(tablet->axis_caps,
				       LIBINPUT_TABLET_AXIS_DISTANCE))
				set_bit(tool->axis_caps,
					LIBINPUT_TABLET_AXIS_DISTANCE);
			if (bit_is_set(tablet->axis_caps,
				       LIBINPUT_TABLET_AXIS_TILT_X))
				set_bit(tool->axis_caps,
					LIBINPUT_TABLET_AXIS_TILT_X);
			if (bit_is_set(tablet->axis_caps,
				       LIBINPUT_TABLET_AXIS_TILT_Y))
				set_bit(tool->axis_caps,
					LIBINPUT_TABLET_AXIS_TILT_Y);
			break;
		default:
			break;
		}

		list_insert(tool_list, &tool->link);
	}

	return tool;
}

static void
tablet_notify_button_mask(struct tablet_dispatch *tablet,
			  struct evdev_device *device,
			  uint32_t time,
			  struct libinput_tool *tool,
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
				     tool,
				     tablet->axes,
				     num_button + button_base - 1,
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
	uint32_t stylus_buttons;

	if (state == LIBINPUT_BUTTON_STATE_PRESSED)
		stylus_buttons =
			tablet_get_pressed_buttons(tablet, stylus_buttons);
	else
		stylus_buttons =
			tablet_get_released_buttons(tablet, stylus_buttons);

	tablet_notify_button_mask(tablet,
				  device,
				  time,
				  tool,
				  stylus_buttons,
				  BTN_TOUCH,
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
		tablet->button_state.stylus_buttons = 0;
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
		if (libevdev_has_event_code(device->evdev,
					    EV_ABS,
					    axis_to_evcode(axis)))
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
