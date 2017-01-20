/*
 * Copyright © 2014 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libudev.h>
#include "linux/input.h"
#include <sys/ioctl.h>

#include <libinput.h>
#include <libevdev/libevdev.h>

#include "shared.h"

uint32_t start_time;
static const uint32_t screen_width = 100;
static const uint32_t screen_height = 100;
struct tools_context context;
static unsigned int stop = 0;

static void
print_event_header(struct libinput_event *ev)
{
	/* use for pointer value only, do not dereference */
	static void *last_device = NULL;
	struct libinput_device *dev = libinput_event_get_device(ev);
	const char *type = NULL;
	char prefix;

	switch(libinput_event_get_type(ev)) {
	case LIBINPUT_EVENT_NONE:
		abort();
	case LIBINPUT_EVENT_DEVICE_ADDED:
		type = "DEVICE_ADDED";
		break;
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		type = "DEVICE_REMOVED";
		break;
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		type = "KEYBOARD_KEY";
		break;
	case LIBINPUT_EVENT_POINTER_MOTION:
		type = "POINTER_MOTION";
		break;
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		type = "POINTER_MOTION_ABSOLUTE";
		break;
	case LIBINPUT_EVENT_POINTER_BUTTON:
		type = "POINTER_BUTTON";
		break;
	case LIBINPUT_EVENT_POINTER_AXIS:
		type = "POINTER_AXIS";
		break;
	case LIBINPUT_EVENT_TOUCH_DOWN:
		type = "TOUCH_DOWN";
		break;
	case LIBINPUT_EVENT_TOUCH_MOTION:
		type = "TOUCH_MOTION";
		break;
	case LIBINPUT_EVENT_TOUCH_UP:
		type = "TOUCH_UP";
		break;
	case LIBINPUT_EVENT_TOUCH_CANCEL:
		type = "TOUCH_CANCEL";
		break;
	case LIBINPUT_EVENT_TOUCH_FRAME:
		type = "TOUCH_FRAME";
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
		type = "GESTURE_SWIPE_BEGIN";
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
		type = "GESTURE_SWIPE_UPDATE";
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_END:
		type = "GESTURE_SWIPE_END";
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
		type = "GESTURE_PINCH_BEGIN";
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
		type = "GESTURE_PINCH_UPDATE";
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_END:
		type = "GESTURE_PINCH_END";
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
		type = "TABLET_TOOL_AXIS";
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
		type = "TABLET_TOOL_PROXIMITY";
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_TIP:
		type = "TABLET_TOOL_TIP";
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
		type = "TABLET_TOOL_BUTTON";
		break;
	case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
		type = "TABLET_PAD_BUTTON";
		break;
	case LIBINPUT_EVENT_TABLET_PAD_RING:
		type = "TABLET_PAD_RING";
		break;
	case LIBINPUT_EVENT_TABLET_PAD_STRIP:
		type = "TABLET_PAD_STRIP";
		break;
	case LIBINPUT_EVENT_SWITCH_TOGGLE:
		type = "SWITCH_TOGGLE";
		break;
	}

	prefix = (last_device != dev) ? '-' : ' ';

	printf("%c%-7s  %-16s ",
	       prefix,
	       libinput_device_get_sysname(dev),
	       type);

	last_device = dev;
}

static void
print_event_time(uint32_t time)
{
	printf("%+6.2fs	", (time - start_time) / 1000.0);
}

static void
print_device_notify(struct libinput_event *ev)
{
	struct libinput_device *dev = libinput_event_get_device(ev);
	struct libinput_seat *seat = libinput_device_get_seat(dev);
	struct libinput_device_group *group;
	double w, h;
	uint32_t scroll_methods, click_methods;
	static int next_group_id = 0;
	intptr_t group_id;

	group = libinput_device_get_device_group(dev);
	group_id = (intptr_t)libinput_device_group_get_user_data(group);
	if (!group_id) {
		group_id = ++next_group_id;
		libinput_device_group_set_user_data(group, (void*)group_id);
	}

	printf("%-33s %5s %7s group%-2d",
	       libinput_device_get_name(dev),
	       libinput_seat_get_physical_name(seat),
	       libinput_seat_get_logical_name(seat),
	       (int)group_id);

	printf(" cap:");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_KEYBOARD))
		printf("k");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_POINTER))
		printf("p");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_TOUCH))
		printf("t");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_GESTURE))
		printf("g");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_TABLET_TOOL))
		printf("T");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_TABLET_PAD))
		printf("P");
	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_SWITCH))
		printf("S");

	if (libinput_device_get_size(dev, &w, &h) == 0)
		printf("  size %.0fx%.0fmm", w, h);

	if (libinput_device_config_tap_get_finger_count(dev)) {
	    printf(" tap");
	    if (libinput_device_config_tap_get_drag_lock_enabled(dev))
		    printf("(dl on)");
	    else
		    printf("(dl off)");
	}
	if (libinput_device_config_left_handed_is_available(dev))
	    printf(" left");
	if (libinput_device_config_scroll_has_natural_scroll(dev))
	    printf(" scroll-nat");
	if (libinput_device_config_calibration_has_matrix(dev))
	    printf(" calib");

	scroll_methods = libinput_device_config_scroll_get_methods(dev);
	if (scroll_methods != LIBINPUT_CONFIG_SCROLL_NO_SCROLL) {
		printf(" scroll");
		if (scroll_methods & LIBINPUT_CONFIG_SCROLL_2FG)
			printf("-2fg");
		if (scroll_methods & LIBINPUT_CONFIG_SCROLL_EDGE)
			printf("-edge");
		if (scroll_methods & LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN)
			printf("-button");
	}

	click_methods = libinput_device_config_click_get_methods(dev);
	if (click_methods != LIBINPUT_CONFIG_CLICK_METHOD_NONE) {
		printf(" click");
		if (click_methods & LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS)
			printf("-buttonareas");
		if (click_methods & LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER)
			printf("-clickfinger");
	}

	if (libinput_device_config_dwt_is_available(dev)) {
		if (libinput_device_config_dwt_get_enabled(dev) ==
		    LIBINPUT_CONFIG_DWT_ENABLED)
			printf(" dwt-on");
		else
			printf(" dwt-off)");
	}

	if (libinput_device_has_capability(dev,
					   LIBINPUT_DEVICE_CAP_TABLET_PAD)) {
		int nbuttons, nstrips, nrings, ngroups;

		nbuttons = libinput_device_tablet_pad_get_num_buttons(dev);
		nstrips = libinput_device_tablet_pad_get_num_strips(dev);
		nrings = libinput_device_tablet_pad_get_num_rings(dev);
		ngroups = libinput_device_tablet_pad_get_num_mode_groups(dev);

		printf(" buttons:%d strips:%d rings:%d mode groups:%d",
		       nbuttons,
		       nstrips,
		       nrings,
		       ngroups);
	}

	printf("\n");

}

static void
print_key_event(struct libinput_event *ev)
{
	struct libinput_event_keyboard *k = libinput_event_get_keyboard_event(ev);
	enum libinput_key_state state;
	uint32_t key;
	const char *keyname;

	print_event_time(libinput_event_keyboard_get_time(k));
	state = libinput_event_keyboard_get_key_state(k);

	key = libinput_event_keyboard_get_key(k);
	keyname = libevdev_event_code_get_name(EV_KEY, key);
	printf("%s (%d) %s\n",
	       keyname ? keyname : "???",
	       key,
	       state == LIBINPUT_KEY_STATE_PRESSED ? "pressed" : "released");
}

static void
print_motion_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	double x = libinput_event_pointer_get_dx(p);
	double y = libinput_event_pointer_get_dy(p);

	print_event_time(libinput_event_pointer_get_time(p));

	printf("%6.2f/%6.2f\n", x, y);
}

static void
print_absmotion_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	double x = libinput_event_pointer_get_absolute_x_transformed(
		p, screen_width);
	double y = libinput_event_pointer_get_absolute_y_transformed(
		p, screen_height);

	print_event_time(libinput_event_pointer_get_time(p));
	printf("%6.2f/%6.2f\n", x, y);
}

static void
print_pointer_button_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	enum libinput_button_state state;
	const char *buttonname;
	int button;

	print_event_time(libinput_event_pointer_get_time(p));

	button = libinput_event_pointer_get_button(p);
	buttonname = libevdev_event_code_get_name(EV_KEY, button);

	state = libinput_event_pointer_get_button_state(p);
	printf("%s (%d) %s, seat count: %u\n",
	       buttonname ? buttonname : "???",
	       button,
	       state == LIBINPUT_BUTTON_STATE_PRESSED ? "pressed" : "released",
	       libinput_event_pointer_get_seat_button_count(p));
}

static void
print_tablet_tip_event(struct libinput_event *ev)
{
	struct libinput_event_tablet_tool *p = libinput_event_get_tablet_tool_event(ev);
	enum libinput_tablet_tool_tip_state state;

	print_event_time(libinput_event_tablet_tool_get_time(p));

	state = libinput_event_tablet_tool_get_tip_state(p);
	printf("%s\n", state == LIBINPUT_TABLET_TOOL_TIP_DOWN ? "down" : "up");
}

static void
print_tablet_button_event(struct libinput_event *ev)
{
	struct libinput_event_tablet_tool *p = libinput_event_get_tablet_tool_event(ev);
	enum libinput_button_state state;
	const char *buttonname;
	int button;

	print_event_time(libinput_event_tablet_tool_get_time(p));

	button = libinput_event_tablet_tool_get_button(p);
	buttonname = libevdev_event_code_get_name(EV_KEY, button);

	state = libinput_event_tablet_tool_get_button_state(p);
	printf("%3d (%s) %s, seat count: %u\n",
	       button,
	       buttonname ? buttonname : "???",
	       state == LIBINPUT_BUTTON_STATE_PRESSED ? "pressed" : "released",
	       libinput_event_tablet_tool_get_seat_button_count(p));
}

static void
print_pointer_axis_event(struct libinput_event *ev)
{
	struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
	double v = 0, h = 0;
	const char *have_vert = "",
		   *have_horiz = "";
	const char *source = "invalid";

	switch (libinput_event_pointer_get_axis_source(p)) {
	case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL:
		source = "wheel";
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_FINGER:
		source = "finger";
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS:
		source = "continuous";
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL_TILT:
		source = "tilt";
		break;
	}

	if (libinput_event_pointer_has_axis(p,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
		v = libinput_event_pointer_get_axis_value(p,
			      LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		have_vert = "*";
	}
	if (libinput_event_pointer_has_axis(p,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
		h = libinput_event_pointer_get_axis_value(p,
			      LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
		have_horiz = "*";
	}
	print_event_time(libinput_event_pointer_get_time(p));
	printf("vert %.2f%s horiz %.2f%s (%s)\n",
	       v, have_vert, h, have_horiz, source);
}

static void
print_tablet_axes(struct libinput_event_tablet_tool *t)
{
	struct libinput_tablet_tool *tool = libinput_event_tablet_tool_get_tool(t);
	double x, y;
	double dist, pressure;
	double rotation, slider, wheel;
	double delta;

#define changed_sym(ev, ax) \
	(libinput_event_tablet_tool_##ax##_has_changed(ev) ? "*" : "")

	x = libinput_event_tablet_tool_get_x(t);
	y = libinput_event_tablet_tool_get_x(t);
	printf("\t%.2f%s/%.2f%s",
	       x, changed_sym(t, x),
	       y, changed_sym(t, y));

	if (libinput_tablet_tool_has_tilt(tool)) {
		x = libinput_event_tablet_tool_get_tilt_x(t);
		y = libinput_event_tablet_tool_get_tilt_y(t);
		printf("\ttilt: %.2f%s/%.2f%s",
		       x, changed_sym(t, tilt_x),
		       y, changed_sym(t, tilt_y));
	}

	if (libinput_tablet_tool_has_distance(tool) ||
	    libinput_tablet_tool_has_pressure(tool)) {
		dist = libinput_event_tablet_tool_get_distance(t);
		pressure = libinput_event_tablet_tool_get_pressure(t);
		if (dist)
			printf("\tdistance: %.2f%s",
			       dist, changed_sym(t, distance));
		else
			printf("\tpressure: %.2f%s",
			       pressure, changed_sym(t, pressure));
	}

	if (libinput_tablet_tool_has_rotation(tool)) {
		rotation = libinput_event_tablet_tool_get_rotation(t);
		printf("\trotation: %.2f%s",
		       rotation, changed_sym(t, rotation));
	}

	if (libinput_tablet_tool_has_slider(tool)) {
		slider = libinput_event_tablet_tool_get_slider_position(t);
		printf("\tslider: %.2f%s",
		       slider, changed_sym(t, slider));
	}

	if (libinput_tablet_tool_has_wheel(tool)) {
		wheel = libinput_event_tablet_tool_get_wheel_delta(t);
		delta = libinput_event_tablet_tool_get_wheel_delta_discrete(t);
		printf("\twheel: %.2f%s (%d)",
		       wheel, changed_sym(t, wheel),
		       (int)delta);
	}
}

static void
print_tablet_axis_event(struct libinput_event *ev)
{
	struct libinput_event_tablet_tool *t = libinput_event_get_tablet_tool_event(ev);

	print_event_time(libinput_event_tablet_tool_get_time(t));
	print_tablet_axes(t);
	printf("\n");
}

static void
print_touch_event_without_coords(struct libinput_event *ev)
{
	struct libinput_event_touch *t = libinput_event_get_touch_event(ev);

	print_event_time(libinput_event_touch_get_time(t));
	printf("\n");
}

static void
print_proximity_event(struct libinput_event *ev)
{
	struct libinput_event_tablet_tool *t = libinput_event_get_tablet_tool_event(ev);
	struct libinput_tablet_tool *tool = libinput_event_tablet_tool_get_tool(t);
	enum libinput_tablet_tool_proximity_state state;
	const char *tool_str,
	           *state_str;

	switch (libinput_tablet_tool_get_type(tool)) {
	case LIBINPUT_TABLET_TOOL_TYPE_PEN:
		tool_str = "pen";
		break;
	case LIBINPUT_TABLET_TOOL_TYPE_ERASER:
		tool_str = "eraser";
		break;
	case LIBINPUT_TABLET_TOOL_TYPE_BRUSH:
		tool_str = "brush";
		break;
	case LIBINPUT_TABLET_TOOL_TYPE_PENCIL:
		tool_str = "pencil";
		break;
	case LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH:
		tool_str = "airbrush";
		break;
	case LIBINPUT_TABLET_TOOL_TYPE_MOUSE:
		tool_str = "mouse";
		break;
	case LIBINPUT_TABLET_TOOL_TYPE_LENS:
		tool_str = "lens";
		break;
	default:
		abort();
	}

	state = libinput_event_tablet_tool_get_proximity_state(t);

	print_event_time(libinput_event_tablet_tool_get_time(t));

	if (state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN) {
		print_tablet_axes(t);
		state_str = "proximity-in";
	} else if (state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT) {
		state_str = "proximity-out";
		printf("\t");
	} else {
		abort();
	}

	printf("\t%s (%#" PRIx64 ", id %#" PRIx64 ") %s",
	       tool_str,
	       libinput_tablet_tool_get_serial(tool),
	       libinput_tablet_tool_get_tool_id(tool),
	       state_str);

	printf("\taxes:");
	if (libinput_tablet_tool_has_distance(tool))
		printf("d");
	if (libinput_tablet_tool_has_pressure(tool))
		printf("p");
	if (libinput_tablet_tool_has_tilt(tool))
		printf("t");
	if (libinput_tablet_tool_has_rotation(tool))
		printf("r");
	if (libinput_tablet_tool_has_slider(tool))
		printf("s");
	if (libinput_tablet_tool_has_wheel(tool))
		printf("w");

	printf("\tbtn:");
	if (libinput_tablet_tool_has_button(tool, BTN_TOUCH))
		printf("T");
	if (libinput_tablet_tool_has_button(tool, BTN_STYLUS))
		printf("S");
	if (libinput_tablet_tool_has_button(tool, BTN_STYLUS2))
		printf("S2");
	if (libinput_tablet_tool_has_button(tool, BTN_LEFT))
		printf("L");
	if (libinput_tablet_tool_has_button(tool, BTN_MIDDLE))
		printf("M");
	if (libinput_tablet_tool_has_button(tool, BTN_RIGHT))
		printf("R");
	if (libinput_tablet_tool_has_button(tool, BTN_SIDE))
		printf("Sd");
	if (libinput_tablet_tool_has_button(tool, BTN_EXTRA))
		printf("Ex");

	printf("\n");
}

static void
print_touch_event_with_coords(struct libinput_event *ev)
{
	struct libinput_event_touch *t = libinput_event_get_touch_event(ev);
	double x = libinput_event_touch_get_x_transformed(t, screen_width);
	double y = libinput_event_touch_get_y_transformed(t, screen_height);
	double xmm = libinput_event_touch_get_x(t);
	double ymm = libinput_event_touch_get_y(t);

	print_event_time(libinput_event_touch_get_time(t));

	printf("%d (%d) %5.2f/%5.2f (%5.2f/%5.2fmm)\n",
	       libinput_event_touch_get_slot(t),
	       libinput_event_touch_get_seat_slot(t),
	       x, y,
	       xmm, ymm);
}

static void
print_gesture_event_without_coords(struct libinput_event *ev)
{
	struct libinput_event_gesture *t = libinput_event_get_gesture_event(ev);
	int finger_count = libinput_event_gesture_get_finger_count(t);
	int cancelled = 0;
	enum libinput_event_type type;

	type = libinput_event_get_type(ev);

	if (type == LIBINPUT_EVENT_GESTURE_SWIPE_END ||
	    type == LIBINPUT_EVENT_GESTURE_PINCH_END)
	    cancelled = libinput_event_gesture_get_cancelled(t);

	print_event_time(libinput_event_gesture_get_time(t));
	printf("%d%s\n", finger_count, cancelled ? " cancelled" : "");
}

static void
print_gesture_event_with_coords(struct libinput_event *ev)
{
	struct libinput_event_gesture *t = libinput_event_get_gesture_event(ev);
	double dx = libinput_event_gesture_get_dx(t);
	double dy = libinput_event_gesture_get_dy(t);
	double dx_unaccel = libinput_event_gesture_get_dx_unaccelerated(t);
	double dy_unaccel = libinput_event_gesture_get_dy_unaccelerated(t);

	print_event_time(libinput_event_gesture_get_time(t));

	printf("%d %5.2f/%5.2f (%5.2f/%5.2f unaccelerated)",
	       libinput_event_gesture_get_finger_count(t),
	       dx, dy, dx_unaccel, dy_unaccel);

	if (libinput_event_get_type(ev) ==
	    LIBINPUT_EVENT_GESTURE_PINCH_UPDATE) {
		double scale = libinput_event_gesture_get_scale(t);
		double angle = libinput_event_gesture_get_angle_delta(t);

		printf(" %5.2f @ %5.2f\n", scale, angle);
	} else {
		printf("\n");
	}
}

static void
print_tablet_pad_button_event(struct libinput_event *ev)
{
	struct libinput_event_tablet_pad *p = libinput_event_get_tablet_pad_event(ev);
	struct libinput_tablet_pad_mode_group *group;
	enum libinput_button_state state;
	unsigned int button, mode;

	print_event_time(libinput_event_tablet_pad_get_time(p));

	button = libinput_event_tablet_pad_get_button_number(p),
	state = libinput_event_tablet_pad_get_button_state(p);
	mode = libinput_event_tablet_pad_get_mode(p);
	printf("%3d %s (mode %d)",
	       button,
	       state == LIBINPUT_BUTTON_STATE_PRESSED ? "pressed" : "released",
	       mode);

	group = libinput_event_tablet_pad_get_mode_group(p);
	if (libinput_tablet_pad_mode_group_button_is_toggle(group, button))
		printf(" <mode toggle>");

	printf("\n");
}

static void
print_tablet_pad_ring_event(struct libinput_event *ev)
{
	struct libinput_event_tablet_pad *p = libinput_event_get_tablet_pad_event(ev);
	const char *source = "<invalid>";
	unsigned int mode;

	print_event_time(libinput_event_tablet_pad_get_time(p));

	switch (libinput_event_tablet_pad_get_ring_source(p)) {
	case LIBINPUT_TABLET_PAD_RING_SOURCE_FINGER:
		source = "finger";
		break;
	case LIBINPUT_TABLET_PAD_RING_SOURCE_UNKNOWN:
		source = "unknown";
		break;
	}

	mode = libinput_event_tablet_pad_get_mode(p);
	printf("ring %d position %.2f (source %s) (mode %d)\n",
	       libinput_event_tablet_pad_get_ring_number(p),
	       libinput_event_tablet_pad_get_ring_position(p),
	       source,
	       mode);
}

static void
print_tablet_pad_strip_event(struct libinput_event *ev)
{
	struct libinput_event_tablet_pad *p = libinput_event_get_tablet_pad_event(ev);
	const char *source = "<invalid>";
	unsigned int mode;

	print_event_time(libinput_event_tablet_pad_get_time(p));

	switch (libinput_event_tablet_pad_get_strip_source(p)) {
	case LIBINPUT_TABLET_PAD_STRIP_SOURCE_FINGER:
		source = "finger";
		break;
	case LIBINPUT_TABLET_PAD_STRIP_SOURCE_UNKNOWN:
		source = "unknown";
		break;
	}

	mode = libinput_event_tablet_pad_get_mode(p);
	printf("strip %d position %.2f (source %s) (mode %d)\n",
	       libinput_event_tablet_pad_get_strip_number(p),
	       libinput_event_tablet_pad_get_strip_position(p),
	       source,
	       mode);
}

static void
print_switch_event(struct libinput_event *ev)
{
	struct libinput_event_switch *sw = libinput_event_get_switch_event(ev);
	enum libinput_switch_state state;
	const char *which;

	print_event_time(libinput_event_switch_get_time(sw));

	switch (libinput_event_switch_get_switch(sw)) {
	case LIBINPUT_SWITCH_LID:
		which = "lid";
		break;
	default:
		abort();
	}

	state = libinput_event_switch_get_switch_state(sw);

	printf("switch %s state %d\n", which, state);
}

static int
handle_and_print_events(struct libinput *li)
{
	int rc = -1;
	struct libinput_event *ev;

	libinput_dispatch(li);
	while ((ev = libinput_get_event(li))) {
		print_event_header(ev);

		switch (libinput_event_get_type(ev)) {
		case LIBINPUT_EVENT_NONE:
			abort();
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			print_device_notify(ev);
			tools_device_apply_config(libinput_event_get_device(ev),
						  &context.options);
			break;
		case LIBINPUT_EVENT_KEYBOARD_KEY:
			print_key_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_MOTION:
			print_motion_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
			print_absmotion_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_BUTTON:
			print_pointer_button_event(ev);
			break;
		case LIBINPUT_EVENT_POINTER_AXIS:
			print_pointer_axis_event(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_DOWN:
			print_touch_event_with_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_MOTION:
			print_touch_event_with_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_UP:
			print_touch_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_CANCEL:
			print_touch_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_TOUCH_FRAME:
			print_touch_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
			print_gesture_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
			print_gesture_event_with_coords(ev);
			break;
		case LIBINPUT_EVENT_GESTURE_SWIPE_END:
			print_gesture_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
			print_gesture_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
			print_gesture_event_with_coords(ev);
			break;
		case LIBINPUT_EVENT_GESTURE_PINCH_END:
			print_gesture_event_without_coords(ev);
			break;
		case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
			print_tablet_axis_event(ev);
			break;
		case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
			print_proximity_event(ev);
			break;
		case LIBINPUT_EVENT_TABLET_TOOL_TIP:
			print_tablet_tip_event(ev);
			break;
		case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
			print_tablet_button_event(ev);
			break;
		case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
			print_tablet_pad_button_event(ev);
			break;
		case LIBINPUT_EVENT_TABLET_PAD_RING:
			print_tablet_pad_ring_event(ev);
			break;
		case LIBINPUT_EVENT_TABLET_PAD_STRIP:
			print_tablet_pad_strip_event(ev);
			break;
		case LIBINPUT_EVENT_SWITCH_TOGGLE:
			print_switch_event(ev);
			break;
		}

		libinput_event_destroy(ev);
		libinput_dispatch(li);
		rc = 0;
	}
	return rc;
}

static void
sighandler(int signal, siginfo_t *siginfo, void *userdata)
{
	stop = 1;
}

static void
mainloop(struct libinput *li)
{
	struct pollfd fds;
	struct sigaction act;

	fds.fd = libinput_get_fd(li);
	fds.events = POLLIN;
	fds.revents = 0;

	memset(&act, 0, sizeof(act));
	act.sa_sigaction = sighandler;
	act.sa_flags = SA_SIGINFO;

	if (sigaction(SIGINT, &act, NULL) == -1) {
		fprintf(stderr, "Failed to set up signal handling (%s)\n",
				strerror(errno));
		return;
	}

	/* Handle already-pending device added events */
	if (handle_and_print_events(li))
		fprintf(stderr, "Expected device added events on startup but got none. "
				"Maybe you don't have the right permissions?\n");

	while (!stop && poll(&fds, 1, -1) > -1)
		handle_and_print_events(li);
}

int
main(int argc, char **argv)
{
	struct libinput *li;
	struct timespec tp;

	clock_gettime(CLOCK_MONOTONIC, &tp);
	start_time = tp.tv_sec * 1000 + tp.tv_nsec / 1000000;

	tools_init_context(&context);

	if (tools_parse_args(argc, argv, &context))
		return 1;

	li = tools_open_backend(&context);
	if (!li)
		return 1;

	mainloop(li);

	libinput_unref(li);

	return 0;
}
