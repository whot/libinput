/*
 * Copyright © 2010 Intel Corporation
 * Copyright © 2013 Jonas Ådahl
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "linux/input.h"
#include <unistd.h>
#include <fcntl.h>
#include <mtdev-plumbing.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include "libinput.h"
#include "evdev.h"
#include "filter.h"
#include "libinput-private.h"

#define DEFAULT_AXIS_STEP_DISTANCE 10

void
evdev_device_led_update(struct evdev_device *device, enum libinput_led leds)
{
	static const struct {
		enum libinput_led weston;
		int evdev;
	} map[] = {
		{ LIBINPUT_LED_NUM_LOCK, LED_NUML },
		{ LIBINPUT_LED_CAPS_LOCK, LED_CAPSL },
		{ LIBINPUT_LED_SCROLL_LOCK, LED_SCROLLL },
	};
	struct input_event ev[ARRAY_LENGTH(map) + 1];
	unsigned int i;

	if (!(device->seat_caps & EVDEV_DEVICE_KEYBOARD))
		return;

	memset(ev, 0, sizeof(ev));
	for (i = 0; i < ARRAY_LENGTH(map); i++) {
		ev[i].type = EV_LED;
		ev[i].code = map[i].evdev;
		ev[i].value = !!(leds & map[i].weston);
	}
	ev[i].type = EV_SYN;
	ev[i].code = SYN_REPORT;

	i = write(device->fd, ev, sizeof ev);
	(void)i; /* no, we really don't care about the return value */
}

static void
transform_absolute(struct evdev_device *device, int32_t *x, int32_t *y)
{
	if (!device->abs.apply_calibration) {
		*x = device->abs.x;
		*y = device->abs.y;
		return;
	} else {
		*x = device->abs.x * device->abs.calibration[0] +
			device->abs.y * device->abs.calibration[1] +
			device->abs.calibration[2];

		*y = device->abs.x * device->abs.calibration[3] +
			device->abs.y * device->abs.calibration[4] +
			device->abs.calibration[5];
	}
}

static inline double
scale_axis(const struct input_absinfo *absinfo, double val, double to_range)
{
	return (val - absinfo->minimum) * to_range /
		(absinfo->maximum - absinfo->minimum + 1);
}

double
evdev_device_transform_x(struct evdev_device *device,
			 double x,
			 uint32_t width)
{
	return scale_axis(device->abs.absinfo_x, x, width);
}

double
evdev_device_transform_y(struct evdev_device *device,
			 double y,
			 uint32_t height)
{
	return scale_axis(device->abs.absinfo_y, y, height);
}

static void
evdev_flush_pending_event(struct evdev_device *device, uint64_t time)
{
	struct libinput *libinput = device->base.seat->libinput;
	struct motion_params motion;
	int32_t cx, cy;
	double x, y;
	int slot;
	int seat_slot;
	struct libinput_device *base = &device->base;
	struct libinput_seat *seat = base->seat;

	slot = device->mt.slot;

	switch (device->pending_event) {
	case EVDEV_NONE:
		return;
	case EVDEV_RELATIVE_MOTION:
		motion.dx = device->rel.dx;
		motion.dy = device->rel.dy;
		device->rel.dx = 0;
		device->rel.dy = 0;

		/* Apply pointer acceleration. */
		filter_dispatch(device->pointer.filter, &motion, device, time);

		if (motion.dx == 0.0 && motion.dy == 0.0)
			break;

		pointer_notify_motion(base, time, motion.dx, motion.dy);
		break;
	case EVDEV_ABSOLUTE_MT_DOWN:
		if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
			break;

		if (device->mt.slots[slot].seat_slot != -1) {
			log_bug_kernel(libinput,
				       "%s: Driver sent multiple touch down for the "
				       "same slot", device->devnode);
			break;
		}

		seat_slot = ffs(~seat->slot_map) - 1;
		device->mt.slots[slot].seat_slot = seat_slot;

		if (seat_slot == -1)
			break;

		seat->slot_map |= 1 << seat_slot;
		x = device->mt.slots[slot].x;
		y = device->mt.slots[slot].y;

		touch_notify_touch_down(base, time, slot, seat_slot, x, y);
		break;
	case EVDEV_ABSOLUTE_MT_MOTION:
		if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
			break;

		seat_slot = device->mt.slots[slot].seat_slot;
		x = device->mt.slots[slot].x;
		y = device->mt.slots[slot].y;

		if (seat_slot == -1)
			break;

		touch_notify_touch_motion(base, time, slot, seat_slot, x, y);
		break;
	case EVDEV_ABSOLUTE_MT_UP:
		if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
			break;

		seat_slot = device->mt.slots[slot].seat_slot;
		device->mt.slots[slot].seat_slot = -1;

		if (seat_slot == -1)
			break;

		seat->slot_map &= ~(1 << seat_slot);

		touch_notify_touch_up(base, time, slot, seat_slot);
		break;
	case EVDEV_ABSOLUTE_TOUCH_DOWN:
		if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
			break;

		if (device->abs.seat_slot != -1) {
			log_bug_kernel(libinput,
				       "%s: Driver sent multiple touch down for the "
				       "same slot", device->devnode);
			break;
		}

		seat_slot = ffs(~seat->slot_map) - 1;
		device->abs.seat_slot = seat_slot;

		if (seat_slot == -1)
			break;

		seat->slot_map |= 1 << seat_slot;

		transform_absolute(device, &cx, &cy);

		touch_notify_touch_down(base, time, -1, seat_slot, cx, cy);
		break;
	case EVDEV_ABSOLUTE_MOTION:
		transform_absolute(device, &cx, &cy);
		x = cx;
		y = cy;

		if (device->seat_caps & EVDEV_DEVICE_TOUCH) {
			seat_slot = device->abs.seat_slot;

			if (seat_slot == -1)
				break;

			touch_notify_touch_motion(base, time, -1, seat_slot, x, y);
		} else if (device->seat_caps & EVDEV_DEVICE_POINTER) {
			pointer_notify_motion_absolute(base, time, x, y);
		}
		break;
	case EVDEV_ABSOLUTE_TOUCH_UP:
		if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
			break;

		seat_slot = device->abs.seat_slot;
		device->abs.seat_slot = -1;

		if (seat_slot == -1)
			break;

		seat->slot_map &= ~(1 << seat_slot);

		touch_notify_touch_up(base, time, -1, seat_slot);
		break;
	default:
		assert(0 && "Unknown pending event type");
		break;
	}

	device->pending_event = EVDEV_NONE;
}

static void
evdev_process_touch_button(struct evdev_device *device,
			   uint64_t time, int value)
{
	if (device->pending_event != EVDEV_NONE &&
	    device->pending_event != EVDEV_ABSOLUTE_MOTION)
		evdev_flush_pending_event(device, time);

	device->pending_event = (value ?
				 EVDEV_ABSOLUTE_TOUCH_DOWN :
				 EVDEV_ABSOLUTE_TOUCH_UP);
}

static inline void
evdev_process_key(struct evdev_device *device,
		  struct input_event *e, uint64_t time)
{
	/* ignore kernel key repeat */
	if (e->value == 2)
		return;

	if (e->code > KEY_MAX)
		return;

	if (e->code == BTN_TOUCH) {
		if (!device->is_mt)
			evdev_process_touch_button(device, time, e->value);
		return;
	}

	evdev_flush_pending_event(device, time);

	switch (e->code) {
	case BTN_LEFT:
	case BTN_RIGHT:
	case BTN_MIDDLE:
	case BTN_SIDE:
	case BTN_EXTRA:
	case BTN_FORWARD:
	case BTN_BACK:
	case BTN_TASK:
		pointer_notify_button(
			&device->base,
			time,
			e->code,
			e->value ? LIBINPUT_BUTTON_STATE_PRESSED :
				   LIBINPUT_BUTTON_STATE_RELEASED);
		break;

	default:
		/* Only let KEY_* codes pass through. */
		if (!(e->code <= KEY_MICMUTE ||
		      (e->code >= KEY_OK && e->code <= KEY_LIGHTS_TOGGLE)))
			break;

		keyboard_notify_key(
			&device->base,
			time,
			e->code,
			e->value ? LIBINPUT_KEY_STATE_PRESSED :
				   LIBINPUT_KEY_STATE_RELEASED);
		break;
	}
}

static void
evdev_process_touch(struct evdev_device *device,
		    struct input_event *e,
		    uint64_t time)
{
	switch (e->code) {
	case ABS_MT_SLOT:
		evdev_flush_pending_event(device, time);
		device->mt.slot = e->value;
		break;
	case ABS_MT_TRACKING_ID:
		if (device->pending_event != EVDEV_NONE &&
		    device->pending_event != EVDEV_ABSOLUTE_MT_MOTION)
			evdev_flush_pending_event(device, time);
		if (e->value >= 0)
			device->pending_event = EVDEV_ABSOLUTE_MT_DOWN;
		else
			device->pending_event = EVDEV_ABSOLUTE_MT_UP;
		break;
	case ABS_MT_POSITION_X:
		device->mt.slots[device->mt.slot].x = e->value;
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MT_MOTION;
		break;
	case ABS_MT_POSITION_Y:
		device->mt.slots[device->mt.slot].y = e->value;
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MT_MOTION;
		break;
	}
}

static inline void
evdev_process_absolute_motion(struct evdev_device *device,
			      struct input_event *e)
{
	switch (e->code) {
	case ABS_X:
		device->abs.x = e->value;
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MOTION;
		break;
	case ABS_Y:
		device->abs.y = e->value;
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MOTION;
		break;
	}
}

static inline void
evdev_process_relative(struct evdev_device *device,
		       struct input_event *e, uint64_t time)
{
	struct libinput_device *base = &device->base;

	switch (e->code) {
	case REL_X:
		if (device->pending_event != EVDEV_RELATIVE_MOTION)
			evdev_flush_pending_event(device, time);
		device->rel.dx += e->value;
		device->pending_event = EVDEV_RELATIVE_MOTION;
		break;
	case REL_Y:
		if (device->pending_event != EVDEV_RELATIVE_MOTION)
			evdev_flush_pending_event(device, time);
		device->rel.dy += e->value;
		device->pending_event = EVDEV_RELATIVE_MOTION;
		break;
	case REL_WHEEL:
		evdev_flush_pending_event(device, time);
		pointer_notify_axis(
			base,
			time,
			LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
			-1 * e->value * DEFAULT_AXIS_STEP_DISTANCE);
		break;
	case REL_HWHEEL:
		evdev_flush_pending_event(device, time);
		switch (e->value) {
		case -1:
			/* Scroll left */
		case 1:
			/* Scroll right */
			pointer_notify_axis(
				base,
				time,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL,
				e->value * DEFAULT_AXIS_STEP_DISTANCE);
			break;
		default:
			break;

		}
	}
}

static inline void
evdev_process_absolute(struct evdev_device *device,
		       struct input_event *e,
		       uint64_t time)
{
	if (device->is_mt) {
		evdev_process_touch(device, e, time);
	} else {
		evdev_process_absolute_motion(device, e);
	}
}

static inline int
evdev_need_touch_frame(struct evdev_device *device)
{
	if (!(device->seat_caps & EVDEV_DEVICE_TOUCH))
		return 0;

	switch (device->pending_event) {
	case EVDEV_NONE:
	case EVDEV_RELATIVE_MOTION:
		break;
	case EVDEV_ABSOLUTE_MT_DOWN:
	case EVDEV_ABSOLUTE_MT_MOTION:
	case EVDEV_ABSOLUTE_MT_UP:
	case EVDEV_ABSOLUTE_TOUCH_DOWN:
	case EVDEV_ABSOLUTE_TOUCH_UP:
	case EVDEV_ABSOLUTE_MOTION:
		return 1;
	}

	return 0;
}

static void
fallback_process(struct evdev_dispatch *dispatch,
		 struct evdev_device *device,
		 struct input_event *event,
		 uint64_t time)
{
	int need_frame = 0;

	switch (event->type) {
	case EV_REL:
		evdev_process_relative(device, event, time);
		break;
	case EV_ABS:
		evdev_process_absolute(device, event, time);
		break;
	case EV_KEY:
		evdev_process_key(device, event, time);
		break;
	case EV_SYN:
		need_frame = evdev_need_touch_frame(device);
		evdev_flush_pending_event(device, time);
		if (need_frame)
			touch_notify_frame(&device->base, time);
		break;
	}
}

static void
fallback_destroy(struct evdev_dispatch *dispatch)
{
	free(dispatch);
}

struct evdev_dispatch_interface fallback_interface = {
	fallback_process,
	fallback_destroy
};

static struct evdev_dispatch *
fallback_dispatch_create(void)
{
	struct evdev_dispatch *dispatch = malloc(sizeof *dispatch);
	if (dispatch == NULL)
		return NULL;

	dispatch->interface = &fallback_interface;

	return dispatch;
}

static inline void
evdev_process_event(struct evdev_device *device, struct input_event *e)
{
	struct evdev_dispatch *dispatch = device->dispatch;
	uint64_t time = e->time.tv_sec * 1000ULL + e->time.tv_usec / 1000;

	dispatch->interface->process(dispatch, device, e, time);
}

static inline void
evdev_device_dispatch_one(struct evdev_device *device,
			  struct input_event *ev)
{
	if (!device->mtdev) {
		evdev_process_event(device, ev);
	} else {
		mtdev_put_event(device->mtdev, ev);
		if (libevdev_event_is_code(ev, EV_SYN, SYN_REPORT)) {
			while (!mtdev_empty(device->mtdev)) {
				struct input_event e;
				mtdev_get_event(device->mtdev, &e);
				evdev_process_event(device, &e);
			}
		}
	}
}

static int
evdev_sync_device(struct evdev_device *device)
{
	struct input_event ev;
	int rc;

	do {
		rc = libevdev_next_event(device->evdev,
					 LIBEVDEV_READ_FLAG_SYNC, &ev);
		if (rc < 0)
			break;
		evdev_device_dispatch_one(device, &ev);
	} while (rc == LIBEVDEV_READ_STATUS_SYNC);

	return rc == -EAGAIN ? 0 : rc;
}

static void
evdev_device_dispatch(void *data)
{
	struct evdev_device *device = data;
	struct libinput *libinput = device->base.seat->libinput;
	struct input_event ev;
	int rc;

	/* If the compositor is repainting, this function is called only once
	 * per frame and we have to process all the events available on the
	 * fd, otherwise there will be input lag. */
	do {
		rc = libevdev_next_event(device->evdev,
					 LIBEVDEV_READ_FLAG_NORMAL, &ev);
		if (rc == LIBEVDEV_READ_STATUS_SYNC) {
			/* send one more sync event so we handle all
			   currently pending events before we sync up
			   to the current state */
			ev.code = SYN_REPORT;
			evdev_device_dispatch_one(device, &ev);

			rc = evdev_sync_device(device);
			if (rc == 0)
				rc = LIBEVDEV_READ_STATUS_SUCCESS;
		} else if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
			evdev_device_dispatch_one(device, &ev);
		}
	} while (rc == LIBEVDEV_READ_STATUS_SUCCESS);

	if (rc != -EAGAIN && rc != -EINTR) {
		libinput_remove_source(libinput, device->source);
		device->source = NULL;
	}
}

static int
configure_pointer_acceleration(struct evdev_device *device)
{
	device->pointer.filter =
		create_pointer_accelator_filter(
			pointer_accel_profile_smooth_simple);
	if (!device->pointer.filter)
		return -1;

	return 0;
}

static int
evdev_configure_device(struct evdev_device *device)
{
	struct libinput *libinput = device->base.seat->libinput;
	struct libevdev *evdev = device->evdev;
	const struct input_absinfo *absinfo;
	struct input_absinfo fixed;
	int has_abs, has_rel, has_mt;
	int has_button, has_keyboard, has_touch;
	struct mt_slot *slots;
	int num_slots;
	int active_slot;
	int slot;
	unsigned int i;

	has_rel = 0;
	has_abs = 0;
	has_mt = 0;
	has_button = 0;
	has_keyboard = 0;
	has_touch = 0;

	if (libevdev_has_event_type(evdev, EV_ABS)) {

		if ((absinfo = libevdev_get_abs_info(evdev, ABS_X))) {
			if (absinfo->resolution == 0) {
				fixed = *absinfo;
				fixed.resolution = 1;
				libevdev_set_abs_info(evdev, ABS_X, &fixed);
			}
			device->abs.absinfo_x = absinfo;
			has_abs = 1;
		}
		if ((absinfo = libevdev_get_abs_info(evdev, ABS_Y))) {
			if (absinfo->resolution == 0) {
				fixed = *absinfo;
				fixed.resolution = 1;
				libevdev_set_abs_info(evdev, ABS_Y, &fixed);
			}
			device->abs.absinfo_y = absinfo;
			has_abs = 1;
		}
                /* We only handle the slotted Protocol B in weston.
                   Devices with ABS_MT_POSITION_* but not ABS_MT_SLOT
                   require mtdev for conversion. */
		if (libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) &&
		    libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y)) {
			absinfo = libevdev_get_abs_info(evdev, ABS_MT_POSITION_X);
			if (absinfo->resolution == 0) {
				fixed = *absinfo;
				fixed.resolution = 1;
				libevdev_set_abs_info(evdev,
						      ABS_MT_POSITION_X,
						      &fixed);
			}
			device->abs.absinfo_x = absinfo;
			absinfo = libevdev_get_abs_info(evdev, ABS_MT_POSITION_Y);
			if (absinfo->resolution == 0) {
				fixed = *absinfo;
				fixed.resolution = 1;
				libevdev_set_abs_info(evdev,
						      ABS_MT_POSITION_Y,
						      &fixed);
			}
			device->abs.absinfo_y = absinfo;
			device->is_mt = 1;
			has_touch = 1;
			has_mt = 1;

			if (!libevdev_has_event_code(evdev,
						     EV_ABS, ABS_MT_SLOT)) {
				device->mtdev = mtdev_new_open(device->fd);
				if (!device->mtdev)
					return -1;

				num_slots = device->mtdev->caps.slot.maximum;
				if (device->mtdev->caps.slot.minimum < 0 ||
				    num_slots <= 0)
					return -1;
				active_slot = device->mtdev->caps.slot.value;
			} else {
				num_slots = libevdev_get_num_slots(device->evdev);
				active_slot = libevdev_get_current_slot(evdev);
			}

			slots = calloc(num_slots, sizeof(struct mt_slot));
			if (!slots)
				return -1;

			for (slot = 0; slot < num_slots; ++slot) {
				slots[slot].seat_slot = -1;
				slots[slot].x = 0;
				slots[slot].y = 0;
			}
			device->mt.slots = slots;
			device->mt.slots_len = num_slots;
			device->mt.slot = active_slot;
		}
	}
	if (libevdev_has_event_code(evdev, EV_REL, REL_X) ||
	    libevdev_has_event_code(evdev, EV_REL, REL_Y))
		has_rel = 1;

	if (libevdev_has_event_type(evdev, EV_KEY)) {
		if (!libevdev_has_property(evdev, INPUT_PROP_DIRECT) &&
		    libevdev_has_event_code(evdev, EV_KEY, BTN_TOOL_FINGER) &&
		    !libevdev_has_event_code(evdev, EV_KEY, BTN_TOOL_PEN) &&
		    (has_abs || has_mt)) {
			device->dispatch = evdev_mt_touchpad_create(device);
			log_info(libinput,
				 "input device '%s', %s is a touchpad\n",
				 device->devname, device->devnode);
		}
		for (i = KEY_ESC; i < KEY_MAX; i++) {
			if (i >= BTN_MISC && i < KEY_OK)
				continue;
			if (libevdev_has_event_code(evdev, EV_KEY, i)) {
				has_keyboard = 1;
				break;
			}
		}
		if (libevdev_has_event_code(evdev, EV_KEY, BTN_TOUCH))
			has_touch = 1;
		for (i = BTN_MISC; i < BTN_JOYSTICK; i++) {
			if (libevdev_has_event_code(evdev, EV_KEY, i)) {
				has_button = 1;
				break;
			}
		}
	}
	if (libevdev_has_event_type(evdev, EV_LED))
		has_keyboard = 1;

	if ((has_abs || has_rel) && has_button) {
		if (configure_pointer_acceleration(device) == -1)
			return -1;

		device->seat_caps |= EVDEV_DEVICE_POINTER;

		log_info(libinput,
			 "input device '%s', %s is a pointer caps =%s%s%s\n",
			 device->devname, device->devnode,
			 has_abs ? " absolute-motion" : "",
			 has_rel ? " relative-motion": "",
			 has_button ? " button" : "");
	}
	if (has_keyboard) {
		device->seat_caps |= EVDEV_DEVICE_KEYBOARD;
		log_info(libinput,
			 "input device '%s', %s is a keyboard\n",
			 device->devname, device->devnode);
	}
	if (has_touch && !has_button) {
		device->seat_caps |= EVDEV_DEVICE_TOUCH;
		log_info(libinput,
			 "input device '%s', %s is a touch device\n",
			 device->devname, device->devnode);
	}

	return 0;
}

struct evdev_device *
evdev_device_create(struct libinput_seat *seat,
		    const char *devnode,
		    const char *sysname)
{
	struct libinput *libinput = seat->libinput;
	struct evdev_device *device;
	int rc;
	int fd;
	int unhandled_device = 0;

	/* Use non-blocking mode so that we can loop on read on
	 * evdev_device_data() until all events on the fd are
	 * read.  mtdev_get() also expects this. */
	fd = open_restricted(libinput, devnode, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		log_info(libinput,
			 "opening input device '%s' failed (%s).\n",
			 devnode, strerror(-fd));
		return NULL;
	}

	device = zalloc(sizeof *device);
	if (device == NULL)
		return NULL;

	libinput_device_init(&device->base, seat);

	rc = libevdev_new_from_fd(fd, &device->evdev);
	if (rc != 0)
		return NULL;

	libevdev_set_clock_id(device->evdev, CLOCK_MONOTONIC);

	device->seat_caps = 0;
	device->is_mt = 0;
	device->mtdev = NULL;
	device->devnode = strdup(devnode);
	device->sysname = strdup(sysname);
	device->rel.dx = 0;
	device->rel.dy = 0;
	device->abs.seat_slot = -1;
	device->dispatch = NULL;
	device->fd = fd;
	device->pending_event = EVDEV_NONE;
	device->devname = libevdev_get_name(device->evdev);

	libinput_seat_ref(seat);

	if (evdev_configure_device(device) == -1)
		goto err;

	if (device->seat_caps == 0) {
		unhandled_device = 1;
		goto err;
	}

	/* If the dispatch was not set up use the fallback. */
	if (device->dispatch == NULL)
		device->dispatch = fallback_dispatch_create();
	if (device->dispatch == NULL)
		goto err;

	device->source =
		libinput_add_fd(libinput, fd, evdev_device_dispatch, device);
	if (!device->source)
		goto err;

	list_insert(seat->devices_list.prev, &device->base.link);
	notify_added_device(&device->base);

	return device;

err:
	if (fd >= 0)
		close_restricted(libinput, fd);
	evdev_device_destroy(device);

	return unhandled_device ? EVDEV_UNHANDLED_DEVICE :  NULL;
}

int
evdev_device_get_keys(struct evdev_device *device, char *keys, size_t size)
{
	int len;

	memset(keys, 0, size);
	len = ioctl(device->fd, EVIOCGKEY(size), keys);

	return (len == -1) ? -errno : len;
}

const char *
evdev_device_get_output(struct evdev_device *device)
{
	return device->output_name;
}

const char *
evdev_device_get_sysname(struct evdev_device *device)
{
	return device->sysname;
}

const char *
evdev_device_get_name(struct evdev_device *device)
{
	return device->devname;
}

unsigned int
evdev_device_get_id_product(struct evdev_device *device)
{
	return libevdev_get_id_product(device->evdev);
}

unsigned int
evdev_device_get_id_vendor(struct evdev_device *device)
{
	return libevdev_get_id_vendor(device->evdev);
}

void
evdev_device_calibrate(struct evdev_device *device, float calibration[6])
{
	device->abs.apply_calibration = 1;
	memcpy(device->abs.calibration, calibration, sizeof device->abs.calibration);
}

int
evdev_device_has_capability(struct evdev_device *device,
			    enum libinput_device_capability capability)
{
	switch (capability) {
	case LIBINPUT_DEVICE_CAP_POINTER:
		return !!(device->seat_caps & EVDEV_DEVICE_POINTER);
	case LIBINPUT_DEVICE_CAP_KEYBOARD:
		return !!(device->seat_caps & EVDEV_DEVICE_KEYBOARD);
	case LIBINPUT_DEVICE_CAP_TOUCH:
		return !!(device->seat_caps & EVDEV_DEVICE_TOUCH);
	default:
		return 0;
	}
}

int
evdev_device_get_size(struct evdev_device *device,
		      double *width,
		      double *height)
{
	const struct input_absinfo *x, *y;

	x = libevdev_get_abs_info(device->evdev, ABS_X);
	y = libevdev_get_abs_info(device->evdev, ABS_Y);

	if (!x || !y || !x->resolution || !y->resolution)
		return -1;

	*width = evdev_convert_to_mm(x, x->maximum);
	*height = evdev_convert_to_mm(y, y->maximum);

	return 0;
}

void
evdev_device_remove(struct evdev_device *device)
{
	if (device->source)
		libinput_remove_source(device->base.seat->libinput,
				       device->source);

	if (device->mtdev)
		mtdev_close_delete(device->mtdev);
	close_restricted(device->base.seat->libinput, device->fd);
	device->fd = -1;
	list_remove(&device->base.link);

	notify_removed_device(&device->base);
	libinput_device_unref(&device->base);
}

void
evdev_device_destroy(struct evdev_device *device)
{
	struct evdev_dispatch *dispatch;

	dispatch = device->dispatch;
	if (dispatch)
		dispatch->interface->destroy(dispatch);

	filter_destroy(device->pointer.filter);
	libinput_seat_unref(device->base.seat);
	libevdev_free(device->evdev);
	free(device->mt.slots);
	free(device->devnode);
	free(device->sysname);
	free(device);
}
