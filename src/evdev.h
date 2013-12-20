/*
 * Copyright © 2011, 2012 Intel Corporation
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

#ifndef EVDEV_H
#define EVDEV_H

#include "config.h"

#include <linux/input.h>

#include "libinput-private.h"

#define MAX_SLOTS 16

enum evdev_event_type {
	EVDEV_NONE,
	EVDEV_ABSOLUTE_TOUCH_DOWN,
	EVDEV_ABSOLUTE_MOTION,
	EVDEV_ABSOLUTE_TOUCH_UP,
	EVDEV_ABSOLUTE_MT_DOWN,
	EVDEV_ABSOLUTE_MT_MOTION,
	EVDEV_ABSOLUTE_MT_UP,
	EVDEV_RELATIVE_MOTION,
};

enum evdev_device_capability {
	EVDEV_KEYBOARD = (1 << 0),
	EVDEV_BUTTON = (1 << 1),
	EVDEV_MOTION_ABS = (1 << 2),
	EVDEV_MOTION_REL = (1 << 3),
	EVDEV_TOUCH = (1 << 4),
};

enum evdev_device_seat_capability {
	EVDEV_DEVICE_POINTER = (1 << 0),
	EVDEV_DEVICE_KEYBOARD = (1 << 1),
	EVDEV_DEVICE_TOUCH = (1 << 2)
};

struct evdev_device {
	struct libinput_device base;

	struct libinput_source *source;

	struct evdev_dispatch *dispatch;
	char *output_name;
	char *devnode;
	char *sysname;
	char *devname;
	int fd;
	struct {
		int min_x, max_x, min_y, max_y;
		li_fixed_t x, y;

		int apply_calibration;
		float calibration[6];
	} abs;

	struct {
		int slot;
		struct {
			li_fixed_t x, y;
		} slots[MAX_SLOTS];
	} mt;
	struct mtdev *mtdev;

	struct {
		li_fixed_t dx, dy;
	} rel;

	enum evdev_event_type pending_event;
	enum evdev_device_capability caps;
	enum evdev_device_seat_capability seat_caps;

	int is_mt;
};

/* copied from udev/extras/input_id/input_id.c */
/* we must use this kernel-compatible implementation */
#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define TEST_BIT(array, bit)    ((array[LONG(bit)] >> OFF(bit)) & 1)
/* end copied */

#define EVDEV_UNHANDLED_DEVICE ((struct evdev_device *) 1)

struct evdev_dispatch;

struct evdev_dispatch_interface {
	/* Process an evdev input event. */
	void (*process)(struct evdev_dispatch *dispatch,
			struct evdev_device *device,
			struct input_event *event,
			uint32_t time);

	/* Destroy an event dispatch handler and free all its resources. */
	void (*destroy)(struct evdev_dispatch *dispatch);
};

struct evdev_dispatch {
	struct evdev_dispatch_interface *interface;
};

struct evdev_device *
evdev_device_create(struct libinput_seat *seat,
		    const char *devnode,
		    const char *sysname,
		    int fd);

struct evdev_dispatch *
evdev_touchpad_create(struct evdev_device *device);

void
evdev_device_proces_event(struct libinput_event *event);

void
evdev_device_led_update(struct evdev_device *device, enum libinput_led leds);

int
evdev_device_get_keys(struct evdev_device *device, char *keys, size_t size);

const char *
evdev_device_get_output(struct evdev_device *device);

const char *
evdev_device_get_sysname(struct evdev_device *device);

void
evdev_device_calibrate(struct evdev_device *device, float calibration[6]);

int
evdev_device_has_capability(struct evdev_device *device,
			    enum libinput_device_capability capability);

void
evdev_device_remove(struct evdev_device *device);

void
evdev_device_destroy(struct evdev_device *device);

#endif /* EVDEV_H */
