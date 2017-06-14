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

#ifndef _SHARED_H_
#define _SHARED_H_

#include <stdbool.h>

#include <libinput.h>

enum configuration_options {
	OPT_TAP_ENABLE = 256,
	OPT_TAP_DISABLE,
	OPT_TAP_MAP,
	OPT_DRAG_ENABLE,
	OPT_DRAG_DISABLE,
	OPT_DRAG_LOCK_ENABLE,
	OPT_DRAG_LOCK_DISABLE,
	OPT_NATURAL_SCROLL_ENABLE,
	OPT_NATURAL_SCROLL_DISABLE,
	OPT_LEFT_HANDED_ENABLE,
	OPT_LEFT_HANDED_DISABLE,
	OPT_MIDDLEBUTTON_ENABLE,
	OPT_MIDDLEBUTTON_DISABLE,
	OPT_DWT_ENABLE,
	OPT_DWT_DISABLE,
	OPT_CLICK_METHOD,
	OPT_SCROLL_METHOD,
	OPT_SCROLL_BUTTON,
	OPT_SPEED,
	OPT_PROFILE,
	OPT_DISABLE_SENDEVENTS,
	OPT_CURVE_POINTS,
};

#define CONFIGURATION_OPTIONS \
	{ "disable-sendevents",        required_argument, 0, OPT_DISABLE_SENDEVENTS }, \
	{ "enable-tap",                no_argument,       0, OPT_TAP_ENABLE }, \
	{ "disable-tap",               no_argument,       0, OPT_TAP_DISABLE }, \
	{ "enable-drag",               no_argument,       0, OPT_DRAG_ENABLE }, \
	{ "disable-drag",              no_argument,       0, OPT_DRAG_DISABLE }, \
	{ "enable-drag-lock",          no_argument,       0, OPT_DRAG_LOCK_ENABLE }, \
	{ "disable-drag-lock",         no_argument,       0, OPT_DRAG_LOCK_DISABLE }, \
	{ "enable-natural-scrolling",  no_argument,       0, OPT_NATURAL_SCROLL_ENABLE }, \
	{ "disable-natural-scrolling", no_argument,       0, OPT_NATURAL_SCROLL_DISABLE }, \
	{ "enable-left-handed",        no_argument,       0, OPT_LEFT_HANDED_ENABLE }, \
	{ "disable-left-handed",       no_argument,       0, OPT_LEFT_HANDED_DISABLE }, \
	{ "enable-middlebutton",       no_argument,       0, OPT_MIDDLEBUTTON_ENABLE }, \
	{ "disable-middlebutton",      no_argument,       0, OPT_MIDDLEBUTTON_DISABLE }, \
	{ "enable-dwt",                no_argument,       0, OPT_DWT_ENABLE }, \
	{ "disable-dwt",               no_argument,       0, OPT_DWT_DISABLE }, \
	{ "set-click-method",          required_argument, 0, OPT_CLICK_METHOD }, \
	{ "set-scroll-method",         required_argument, 0, OPT_SCROLL_METHOD }, \
	{ "set-scroll-button",         required_argument, 0, OPT_SCROLL_BUTTON }, \
	{ "set-profile",               required_argument, 0, OPT_PROFILE }, \
	{ "set-tap-map",               required_argument, 0, OPT_TAP_MAP }, \
	{ "set-speed",                 required_argument, 0, OPT_SPEED }, \
	{ "set-accel-curve-points",    required_argument, 0, OPT_CURVE_POINTS }

enum tools_backend {
	BACKEND_DEVICE,
	BACKEND_UDEV
};

struct tools_options {
	int tapping;
	int drag;
	int drag_lock;
	int natural_scroll;
	int left_handed;
	int middlebutton;
	enum libinput_config_click_method click_method;
	enum libinput_config_scroll_method scroll_method;
	enum libinput_config_tap_button_map tap_map;
	int scroll_button;
	double speed;
	int dwt;
	enum libinput_config_accel_profile profile;
	char disable_pattern[64];

	struct key_value_double *curve_points;
	ssize_t ncurve_points;
};

void tools_init_options(struct tools_options *options);
int tools_parse_option(int option,
		       const char *optarg,
		       struct tools_options *options);
struct libinput* tools_open_backend(enum tools_backend which,
				    const char *seat_or_device,
				    bool verbose,
				    bool grab);
void tools_device_apply_config(struct libinput_device *device,
			       struct tools_options *options);
int tools_exec_command(const char *prefix, int argc, char **argv);

bool find_touchpad_device(char *path, size_t path_len);
bool is_touchpad_device(const char *devnode);

#endif
