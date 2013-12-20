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
#include <linux/input.h>
#include <unistd.h>
#include <fcntl.h>
#include <mtdev.h>
#include <assert.h>

#include "libinput.h"
#include "evdev.h"
#include "libinput-private.h"

#define DEFAULT_AXIS_STEP_DISTANCE li_fixed_from_int(10)

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

	if (!device->caps & EVDEV_KEYBOARD)
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
transform_absolute_mt(struct evdev_device *device, int slot, li_fixed_t *x, li_fixed_t *y)
{
	if (!device->abs.apply_calibration) {
		*x = device->mt.slots[slot].x;
		*y = device->mt.slots[slot].y;
		return;
	} else {
		double cx = li_fixed_to_double(device->mt.slots[slot].x) * device->abs.calibration[0] +
			    device->abs.y * device->abs.calibration[1] +
			    device->abs.calibration[2];
		double cy = li_fixed_to_double(device->mt.slots[slot].y) * device->abs.calibration[0] +
			    device->abs.y * device->abs.calibration[1] +
			    device->abs.calibration[2];
		*x = li_fixed_from_double(cx);
		*y = li_fixed_from_double(cy);
	}
}

static void
transform_absolute(struct evdev_device *device, li_fixed_t *x, li_fixed_t *y)
{
	if (!device->abs.apply_calibration) {
		*x = device->abs.x;
		*y = device->abs.y;
		return;
	} else {
		double cx = li_fixed_to_double(device->abs.x) * device->abs.calibration[0] +
			    device->abs.y * device->abs.calibration[1] +
			    device->abs.calibration[2];
		double cy = li_fixed_to_double(device->abs.y) * device->abs.calibration[0] +
			    device->abs.y * device->abs.calibration[1] +
			    device->abs.calibration[2];
		*x = li_fixed_from_double(cx);
		*y = li_fixed_from_double(cy);
	}
}

static void
evdev_flush_pending_event(struct evdev_device *device, uint32_t time)
{
	li_fixed_t cx, cy;
	int slot;
	struct libinput_device *base = &device->base;

	slot = device->mt.slot;

	switch (device->pending_event) {
	case EVDEV_NONE:
		return;
	case EVDEV_RELATIVE_MOTION:
		pointer_notify_motion(base,
				      time,
				      device->rel.dx,
				      device->rel.dy);
		device->rel.dx = 0;
		device->rel.dy = 0;
		goto handled;
	case EVDEV_ABSOLUTE_MT_DOWN:
		transform_absolute_mt(device, slot, &cx, &cy);
		touch_notify_touch(base,
				   time,
				   slot,
				   cx,
				   cy,
				   LIBINPUT_TOUCH_TYPE_DOWN);
		goto handled;
	case EVDEV_ABSOLUTE_MT_MOTION:
		transform_absolute_mt(device, slot, &cx, &cy);
		touch_notify_touch(base,
				   time,
				   slot,
				   cx,
				   cy,
				   LIBINPUT_TOUCH_TYPE_MOTION);
		goto handled;
	case EVDEV_ABSOLUTE_MT_UP:
		touch_notify_touch(base,
				   time,
				   slot,
				   0, 0,
				   LIBINPUT_TOUCH_TYPE_UP);
		goto handled;
	case EVDEV_ABSOLUTE_TOUCH_DOWN:
		transform_absolute(device, &cx, &cy);
		touch_notify_touch(base,
				   time,
				   slot,
				   cx,
				   cy,
				   LIBINPUT_TOUCH_TYPE_DOWN);
		goto handled;
	case EVDEV_ABSOLUTE_MOTION:
		transform_absolute(device, &cx, &cy);
		if (device->caps & EVDEV_TOUCH) {
			touch_notify_touch(base,
					   time,
					   slot,
					   cx,
					   cy,
					   LIBINPUT_TOUCH_TYPE_DOWN);
		} else {
			pointer_notify_motion_absolute(base,
						       time,
						       cx,
						       cy);
		}
		goto handled;
	case EVDEV_ABSOLUTE_TOUCH_UP:
		touch_notify_touch(base,
				   time,
				   0, 0, 0, LIBINPUT_TOUCH_TYPE_UP);
		goto handled;
	}

	assert(0 && "Unknown pending event type");

handled:
	device->pending_event = EVDEV_NONE;
}

static void
evdev_process_touch_button(struct evdev_device *device, int time, int value)
{
	if (device->pending_event != EVDEV_NONE &&
	    device->pending_event != EVDEV_ABSOLUTE_MOTION)
		evdev_flush_pending_event(device, time);

	device->pending_event = (value ?
				 EVDEV_ABSOLUTE_TOUCH_DOWN :
				 EVDEV_ABSOLUTE_TOUCH_UP);
}

static inline void
evdev_process_key(struct evdev_device *device, struct input_event *e, int time)
{
	/* ignore kernel key repeat */
	if (e->value == 2)
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
			e->value ? LIBINPUT_POINTER_BUTTON_STATE_PRESSED :
				   LIBINPUT_POINTER_BUTTON_STATE_RELEASED);
		break;

	default:
		keyboard_notify_key(
			&device->base,
			time,
			e->code,
			e->value ? LIBINPUT_KEYBOARD_KEY_STATE_PRESSED :
				   LIBINPUT_KEYBOARD_KEY_STATE_RELEASED);
		break;
	}
}

static inline li_fixed_t
evdev_scale_axis(int value, int from_min, int from_max, int to_min, int to_max)
{
	return li_fixed_from_double(1.0 * (value - from_min) *
				    (to_max - to_min)/(from_max - from_min) + to_min);
}


static void
evdev_process_touch(struct evdev_device *device,
		    struct input_event *e,
		    uint32_t time)
{
	struct libinput *libinput = device->base.seat->libinput;
	int screen_width;
	int screen_height;

	libinput->interface->get_current_screen_dimensions(
		&device->base,
		&screen_width,
		&screen_height,
		libinput->user_data);

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
		device->mt.slots[device->mt.slot].x =
			evdev_scale_axis(e->value,
					 device->abs.min_x,
					 device->abs.max_x,
					 0, screen_width);
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MT_MOTION;
		break;
	case ABS_MT_POSITION_Y:
		device->mt.slots[device->mt.slot].y =
			evdev_scale_axis(e->value,
					 device->abs.min_y,
					 device->abs.max_y,
					 0, screen_height);
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MT_MOTION;
		break;
	}
}

static inline void
evdev_process_absolute_motion(struct evdev_device *device,
			      struct input_event *e)
{
	struct libinput *libinput = device->base.seat->libinput;
	int screen_width;
	int screen_height;

	libinput->interface->get_current_screen_dimensions(
		&device->base,
		&screen_width,
		&screen_height,
		libinput->user_data);

	switch (e->code) {
	case ABS_X:
		device->abs.x =
			evdev_scale_axis(e->value,
					 device->abs.min_x,
					 device->abs.max_x,
					 0, screen_width);
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MOTION;
		break;
	case ABS_Y:
		device->abs.y =
			evdev_scale_axis(e->value,
					 device->abs.min_y,
					 device->abs.max_y,
					 0, screen_height);
		if (device->pending_event == EVDEV_NONE)
			device->pending_event = EVDEV_ABSOLUTE_MOTION;
		break;
	}
}

static inline void
evdev_process_relative(struct evdev_device *device,
		       struct input_event *e, uint32_t time)
{
	struct libinput_device *base = &device->base;

	switch (e->code) {
	case REL_X:
		if (device->pending_event != EVDEV_RELATIVE_MOTION)
			evdev_flush_pending_event(device, time);
		device->rel.dx += li_fixed_from_int(e->value);
		device->pending_event = EVDEV_RELATIVE_MOTION;
		break;
	case REL_Y:
		if (device->pending_event != EVDEV_RELATIVE_MOTION)
			evdev_flush_pending_event(device, time);
		device->rel.dy += li_fixed_from_int(e->value);
		device->pending_event = EVDEV_RELATIVE_MOTION;
		break;
	case REL_WHEEL:
		evdev_flush_pending_event(device, time);
		switch (e->value) {
		case -1:
			/* Scroll down */
		case 1:
			/* Scroll up */
			pointer_notify_axis(
				base,
				time,
				LIBINPUT_POINTER_AXIS_VERTICAL_SCROLL,
				-1 * e->value * DEFAULT_AXIS_STEP_DISTANCE);
			break;
		default:
			break;
		}
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
				LIBINPUT_POINTER_AXIS_HORIZONTAL_SCROLL,
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
		       uint32_t time)
{
	if (device->is_mt) {
		evdev_process_touch(device, e, time);
	} else {
		evdev_process_absolute_motion(device, e);
	}
}

static void
fallback_process(struct evdev_dispatch *dispatch,
		 struct evdev_device *device,
		 struct input_event *event,
		 uint32_t time)
{
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
		evdev_flush_pending_event(device, time);
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

static void
evdev_process_events(struct evdev_device *device,
		     struct input_event *ev, int count)
{
	struct evdev_dispatch *dispatch = device->dispatch;
	struct input_event *e, *end;
	uint32_t time = 0;

	e = ev;
	end = e + count;
	for (e = ev; e < end; e++) {
		time = e->time.tv_sec * 1000 + e->time.tv_usec / 1000;

		dispatch->interface->process(dispatch, device, e, time);
	}
}

static void
evdev_device_dispatch(void *data)
{
	struct evdev_device *device = data;
	struct libinput *libinput = device->base.seat->libinput;
	int fd = device->fd;
	struct input_event ev[32];
	int len;

	/* If the compositor is repainting, this function is called only once
	 * per frame and we have to process all the events available on the
	 * fd, otherwise there will be input lag. */
	do {
		if (device->mtdev)
			len = mtdev_get(device->mtdev, fd, ev,
					ARRAY_LENGTH(ev)) *
				sizeof (struct input_event);
		else
			len = read(fd, &ev, sizeof ev);

		if (len < 0 || len % sizeof ev[0] != 0) {
			if (len < 0 && errno != EAGAIN && errno != EINTR) {
				libinput_remove_source(libinput,
						       device->source);
				device->source = NULL;
			}

			return;
		}

		evdev_process_events(device, ev, len / sizeof ev[0]);

	} while (len > 0);
}

static int
evdev_handle_device(struct evdev_device *device)
{
	struct input_absinfo absinfo;
	unsigned long ev_bits[NBITS(EV_MAX)];
	unsigned long abs_bits[NBITS(ABS_MAX)];
	unsigned long rel_bits[NBITS(REL_MAX)];
	unsigned long key_bits[NBITS(KEY_MAX)];
	int has_key, has_abs;
	unsigned int i;

	has_key = 0;
	has_abs = 0;
	device->caps = 0;

	ioctl(device->fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits);
	if (TEST_BIT(ev_bits, EV_ABS)) {
		has_abs = 1;

		ioctl(device->fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)),
		      abs_bits);

		if (TEST_BIT(abs_bits, ABS_WHEEL) ||
		    TEST_BIT(abs_bits, ABS_GAS) ||
		    TEST_BIT(abs_bits, ABS_BRAKE) ||
		    TEST_BIT(abs_bits, ABS_HAT0X)) {
			/* Device %s is a joystick, ignoring. */
			return 0;
		}

		if (TEST_BIT(abs_bits, ABS_X)) {
			ioctl(device->fd, EVIOCGABS(ABS_X), &absinfo);
			device->abs.min_x = absinfo.minimum;
			device->abs.max_x = absinfo.maximum;
			device->caps |= EVDEV_MOTION_ABS;
		}
		if (TEST_BIT(abs_bits, ABS_Y)) {
			ioctl(device->fd, EVIOCGABS(ABS_Y), &absinfo);
			device->abs.min_y = absinfo.minimum;
			device->abs.max_y = absinfo.maximum;
			device->caps |= EVDEV_MOTION_ABS;
		}
                /* We only handle the slotted Protocol B in weston.
                   Devices with ABS_MT_POSITION_* but not ABS_MT_SLOT
                   require mtdev for conversion. */
		if (TEST_BIT(abs_bits, ABS_MT_POSITION_X) &&
		    TEST_BIT(abs_bits, ABS_MT_POSITION_Y)) {
			ioctl(device->fd, EVIOCGABS(ABS_MT_POSITION_X),
			      &absinfo);
			device->abs.min_x = absinfo.minimum;
			device->abs.max_x = absinfo.maximum;
			ioctl(device->fd, EVIOCGABS(ABS_MT_POSITION_Y),
			      &absinfo);
			device->abs.min_y = absinfo.minimum;
			device->abs.max_y = absinfo.maximum;
			device->is_mt = 1;
			device->caps |= EVDEV_TOUCH;

			if (!TEST_BIT(abs_bits, ABS_MT_SLOT)) {
				device->mtdev = mtdev_new_open(device->fd);
				if (!device->mtdev) {
					/* mtdev required but failed to open. */
					return 0;
				}
				device->mt.slot = device->mtdev->caps.slot.value;
			} else {
				ioctl(device->fd, EVIOCGABS(ABS_MT_SLOT),
				      &absinfo);
				device->mt.slot = absinfo.value;
			}
		}
	}
	if (TEST_BIT(ev_bits, EV_REL)) {
		ioctl(device->fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)),
		      rel_bits);
		if (TEST_BIT(rel_bits, REL_X) || TEST_BIT(rel_bits, REL_Y))
			device->caps |= EVDEV_MOTION_REL;
	}
	if (TEST_BIT(ev_bits, EV_KEY)) {
		has_key = 1;
		ioctl(device->fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)),
		      key_bits);
		if (TEST_BIT(key_bits, BTN_TOOL_FINGER) &&
		    !TEST_BIT(key_bits, BTN_TOOL_PEN) &&
		    has_abs) {
			device->dispatch = evdev_touchpad_create(device);
		}
		for (i = KEY_ESC; i < KEY_MAX; i++) {
			if (i >= BTN_MISC && i < KEY_OK)
				continue;
			if (TEST_BIT(key_bits, i)) {
				device->caps |= EVDEV_KEYBOARD;
				break;
			}
		}
		if (TEST_BIT(key_bits, BTN_TOUCH)) {
			device->caps |= EVDEV_TOUCH;
		}
		for (i = BTN_MISC; i < BTN_JOYSTICK; i++) {
			if (TEST_BIT(key_bits, i)) {
				device->caps |= EVDEV_BUTTON;
				device->caps &= ~EVDEV_TOUCH;
				break;
			}
		}
	}
	if (TEST_BIT(ev_bits, EV_LED)) {
		device->caps |= EVDEV_KEYBOARD;
	}

	/* This rule tries to catch accelerometer devices and opt out. We may
	 * want to adjust the protocol later adding a proper event for dealing
	 * with accelerometers and implement here accordingly */
	if (has_abs && !has_key && !device->is_mt) {
		return 0;
	}

	return 1;
}

static void
evdev_configure_device(struct evdev_device *device)
{
	if ((device->caps & (EVDEV_MOTION_ABS | EVDEV_MOTION_REL)) &&
	    (device->caps & EVDEV_BUTTON))
		device->seat_caps |= EVDEV_DEVICE_POINTER;
	if ((device->caps & EVDEV_KEYBOARD))
		device->seat_caps |= EVDEV_DEVICE_KEYBOARD;
	if ((device->caps & EVDEV_TOUCH))
		device->seat_caps |= EVDEV_DEVICE_TOUCH;
}

static void
register_device_capabilities(struct evdev_device *device)
{
	if (device->seat_caps & EVDEV_DEVICE_POINTER) {
		device_register_capability(&device->base,
					   LIBINPUT_DEVICE_CAP_POINTER);
	}
	if (device->seat_caps & EVDEV_DEVICE_KEYBOARD) {
		device_register_capability(&device->base,
					   LIBINPUT_DEVICE_CAP_KEYBOARD);
	}
	if (device->seat_caps & EVDEV_DEVICE_TOUCH) {
		device_register_capability(&device->base,
					   LIBINPUT_DEVICE_CAP_TOUCH);
	}
}

struct evdev_device *
evdev_device_create(struct libinput_seat *seat,
		    const char *devnode,
		    const char *sysname,
		    int fd)
{
	struct libinput *libinput = seat->libinput;
	struct evdev_device *device;
	char devname[256] = "unknown";

	device = zalloc(sizeof *device);
	if (device == NULL)
		return NULL;

	libinput_device_init(&device->base, seat);

	device->seat_caps = 0;
	device->is_mt = 0;
	device->mtdev = NULL;
	device->devnode = strdup(devnode);
	device->sysname = strdup(sysname);
	device->mt.slot = -1;
	device->rel.dx = 0;
	device->rel.dy = 0;
	device->dispatch = NULL;
	device->fd = fd;
	device->pending_event = EVDEV_NONE;

	ioctl(device->fd, EVIOCGNAME(sizeof(devname)), devname);
	devname[sizeof(devname) - 1] = '\0';
	device->devname = strdup(devname);

	if (!evdev_handle_device(device)) {
		evdev_device_destroy(device);
		return NULL;
	}

	evdev_configure_device(device);

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
	register_device_capabilities(device);

	return device;

err:
	evdev_device_destroy(device);
	return NULL;
}

int
evdev_device_get_keys(struct evdev_device *device, char *keys, size_t size)
{
	memset(keys, 0, size);
	return ioctl(device->fd, EVIOCGKEY(size), keys);
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

void
evdev_device_remove(struct evdev_device *device)
{
	if (device->seat_caps & EVDEV_DEVICE_POINTER) {
		device_unregister_capability(&device->base,
					     LIBINPUT_DEVICE_CAP_POINTER);
	}
	if (device->seat_caps & EVDEV_DEVICE_KEYBOARD) {
		device_unregister_capability(&device->base,
					     LIBINPUT_DEVICE_CAP_KEYBOARD);
	}
	if (device->seat_caps & EVDEV_DEVICE_TOUCH) {
		device_unregister_capability(&device->base,
					     LIBINPUT_DEVICE_CAP_TOUCH);
	}

	if (device->source)
		libinput_remove_source(device->base.seat->libinput,
				       device->source);

	if (device->mtdev)
		mtdev_close_delete(device->mtdev);
	close(device->fd);
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

	free(device->devname);
	free(device->devnode);
	free(device->sysname);
	free(device);
}
