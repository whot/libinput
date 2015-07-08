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

#include <libinput.h>

enum tools_backend {
	BACKEND_DEVICE,
	BACKEND_UDEV
};

struct tools_options {
	enum tools_backend backend;
	const char *device; /* if backend is BACKEND_DEVICE */
	const char *seat; /* if backend is BACKEND_UDEV */
	int grab; /* EVIOCGRAB */

	int verbose;
	int tapping;
	int drag_lock;
	int natural_scroll;
	int left_handed;
	int middlebutton;
	enum libinput_config_click_method click_method;
	enum libinput_config_scroll_method scroll_method;
	int scroll_button;
	double speed;
	int dwt;
};

struct tools_context {
	struct tools_options options;
	void *user_data;
};

void tools_init_context(struct tools_context *context);
int tools_parse_args(int argc, char **argv, struct tools_context *context);
struct libinput* tools_open_backend(struct tools_context *context);
void tools_device_apply_config(struct libinput_device *device,
			       struct tools_options *options);
void tools_usage();

#endif
