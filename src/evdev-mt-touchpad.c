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

#include "config.h"

#include "evdev.h"

struct touchpad_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;
};

static void
touchpad_process(struct evdev_dispatch *dispatch,
		 struct evdev_device *device,
		 struct input_event *e,
		 uint32_t time)
{
}

static void
touchpad_destroy(struct evdev_dispatch *dispatch)
{
	free(dispatch);
}

static struct evdev_dispatch_interface touchpad_interface = {
	touchpad_process,
	touchpad_destroy
};

static int
touchpad_init(struct touchpad_dispatch *touchpad,
	      struct evdev_device *device)
{
	touchpad->base.interface = &touchpad_interface;
	touchpad->device = device;

	return 0;
}

struct evdev_dispatch *
evdev_mt_touchpad_create(struct evdev_device *device)
{
	struct touchpad_dispatch *touchpad;

	touchpad = zalloc(sizeof *touchpad);
	if (!touchpad)
		return NULL;

	if (touchpad_init(touchpad, device) != 0) {
		free(touchpad);
		return NULL;
	}

	return  &touchpad->base;
}
