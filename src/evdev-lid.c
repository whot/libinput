/*
 * Copyright © 2017 James Ye <jye836@gmail.com>
 * Copyright © 2017 Red Hat, Inc.
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

#include "config.h"

#include "libinput.h"
#include "evdev.h"
#include "libinput-private.h"

struct lid_switch_dispatch {
	struct evdev_dispatch base;

	bool lid_is_closed;
};

static void
lid_switch_process_switch(struct lid_switch_dispatch *dispatch,
			  struct evdev_device *device,
			  struct input_event *e,
			  uint64_t time)
{
	bool is_closed;

	switch (e->code) {
	case SW_LID:
		is_closed = !!e->value;

		if (dispatch->lid_is_closed == is_closed)
			return;

		dispatch->lid_is_closed = is_closed;
		switch_notify_toggle(&device->base,
				     time,
				     LIBINPUT_SWITCH_LID,
				     dispatch->lid_is_closed);
		break;
	}
}

static void
lid_switch_process(struct evdev_dispatch *evdev_dispatch,
		   struct evdev_device *device,
		   struct input_event *event,
		   uint64_t time)
{
	struct lid_switch_dispatch *dispatch =
		(struct lid_switch_dispatch*)evdev_dispatch;

	switch (event->type) {
	case EV_SW:
		lid_switch_process_switch(dispatch, device, event, time);
		break;
	case EV_SYN:
		break;
	default:
		assert(0 && "Unknown event type");
		break;
	}
}

static inline enum switch_reliability
evdev_read_switch_reliability_prop(struct evdev_device *device)
{
	const char *prop;
	enum switch_reliability r;

	prop = udev_device_get_property_value(device->udev_device,
					      "LIBINPUT_ATTR_LID_SWITCH_RELIABILITY");
	if (!parse_switch_reliability_property(prop, &r)) {
		log_error(evdev_libinput_context(device),
			  "%s: switch reliability set to unknown value '%s'\n",
			  device->devname,
			  prop);
		r =  RELIABILITY_UNKNOWN;
	}

	return r;
}

static void
lid_switch_destroy(struct evdev_dispatch *evdev_dispatch)
{
	struct lid_switch_dispatch *dispatch =
		(struct lid_switch_dispatch*)evdev_dispatch;

	free(dispatch);
}

static void
lid_switch_sync_initial_state(struct evdev_device *device,
			      struct evdev_dispatch *evdev_dispatch)
{
	struct lid_switch_dispatch *dispatch =
		(struct lid_switch_dispatch*)evdev_dispatch;
	struct libevdev *evdev = device->evdev;
	bool is_closed = false;

	/* For the initial state sync, we depend on whether the lid switch
	 * is reliable. If we know it's reliable, we sync as expected.
	 * If we're not sure, we ignore the initial state and only sync on
	 * the first future lid close event. Laptops with a broken switch
	 * that always have the switch in 'on' state thus don't mess up our
	 * touchpad.
	 */
	switch(evdev_read_switch_reliability_prop(device)) {
	case RELIABILITY_UNKNOWN:
		is_closed = false;
		break;
	case RELIABILITY_RELIABLE:
		is_closed = libevdev_get_event_value(evdev, EV_SW, SW_LID);
		break;
	}

	dispatch->lid_is_closed = is_closed;
	if (dispatch->lid_is_closed) {
		uint64_t time;
		time = libinput_now(evdev_libinput_context(device));
		switch_notify_toggle(&device->base,
				     time,
				     LIBINPUT_SWITCH_LID,
				     LIBINPUT_SWITCH_STATE_ON);
	}
}

struct evdev_dispatch_interface lid_switch_interface = {
	lid_switch_process,
	NULL, /* suspend */
	NULL, /* remove */
	lid_switch_destroy,
	NULL, /* device_added */
	NULL, /* device_removed */
	NULL, /* device_suspended */
	NULL, /* device_resumed */
	lid_switch_sync_initial_state,
	NULL, /* toggle_touch */
};

struct evdev_dispatch *
evdev_lid_switch_dispatch_create(struct evdev_device *lid_device)
{
	struct lid_switch_dispatch *dispatch = zalloc(sizeof *dispatch);

	if (dispatch == NULL)
		return NULL;

	dispatch->base.interface = &lid_switch_interface;

	evdev_init_sendevents(lid_device, &dispatch->base);

	dispatch->lid_is_closed = false;

	return &dispatch->base;
}
