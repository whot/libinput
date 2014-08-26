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

#include "linux/input.h"
#include <libevdev/libevdev.h>

#include "libinput-private.h"

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

enum evdev_device_seat_capability {
	EVDEV_DEVICE_POINTER = (1 << 0),
	EVDEV_DEVICE_KEYBOARD = (1 << 1),
	EVDEV_DEVICE_TOUCH = (1 << 2)
};

struct mt_slot {
	int32_t seat_slot;
	int32_t x, y;
};

struct evdev_device {
	struct libinput_device base;

	struct libinput_source *source;

	struct evdev_dispatch *dispatch;
	struct libevdev *evdev;
	char *output_name;
	char *devnode;
	char *sysname;
	const char *devname;
	int fd;
	struct {
		const struct input_absinfo *absinfo_x, *absinfo_y;
		int fake_resolution;

		int32_t x, y;
		int32_t seat_slot;

		int apply_calibration;
		struct matrix calibration;
		struct matrix default_calibration; /* from LIBINPUT_CALIBRATION_MATRIX */
		struct matrix usermatrix; /* as supplied by the caller */
	} abs;

	struct {
		int slot;
		struct mt_slot *slots;
		size_t slots_len;
	} mt;
	struct mtdev *mtdev;

	struct {
		int dx, dy;
	} rel;

	enum evdev_event_type pending_event;
	enum evdev_device_seat_capability seat_caps;

	int is_mt;

	struct {
		struct motion_filter *filter;
	} pointer;

	/* Bitmask of pressed keys used to ignore initial release events from
	 * the kernel. */
	unsigned long key_mask[NLONGS(KEY_CNT)];
	/* Key counter used for multiplexing button events internally in
	 * libinput. */
	uint8_t key_count[KEY_CNT];
};

#define EVDEV_UNHANDLED_DEVICE ((struct evdev_device *) 1)

struct evdev_dispatch;

struct evdev_dispatch_interface {
	/* Process an evdev input event. */
	void (*process)(struct evdev_dispatch *dispatch,
			struct evdev_device *device,
			struct input_event *event,
			uint64_t time);

	/* Destroy an event dispatch handler and free all its resources. */
	void (*destroy)(struct evdev_dispatch *dispatch);
};

struct evdev_dispatch {
	struct evdev_dispatch_interface *interface;
	struct libinput_device_config_calibration calibration;
};

struct evdev_device *
evdev_device_create(struct libinput_seat *seat,
		    const char *devnode,
		    const char *sysname);

struct evdev_dispatch *
evdev_touchpad_create(struct evdev_device *device);

struct evdev_dispatch *
evdev_mt_touchpad_create(struct evdev_device *device);

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

const char *
evdev_device_get_name(struct evdev_device *device);

unsigned int
evdev_device_get_id_product(struct evdev_device *device);

unsigned int
evdev_device_get_id_vendor(struct evdev_device *device);

void
evdev_device_set_default_calibration(struct evdev_device *device,
				     const float calibration[6]);
void
evdev_device_calibrate(struct evdev_device *device,
		       const float calibration[6]);

int
evdev_device_has_capability(struct evdev_device *device,
			    enum libinput_device_capability capability);

int
evdev_device_get_size(struct evdev_device *device,
		      double *w,
		      double *h);

double
evdev_device_transform_x(struct evdev_device *device,
			 double x,
			 uint32_t width);

double
evdev_device_transform_y(struct evdev_device *device,
			 double y,
			 uint32_t height);

void
evdev_keyboard_notify_key(struct evdev_device *device,
			  uint32_t time,
			  int key,
			  enum libinput_key_state state);

void
evdev_pointer_notify_button(struct evdev_device *device,
			    uint32_t time,
			    int button,
			    enum libinput_button_state state);

void
evdev_device_remove(struct evdev_device *device);

void
evdev_device_destroy(struct evdev_device *device);

static inline double
evdev_convert_to_mm(const struct input_absinfo *absinfo, double v)
{
	double value = v - absinfo->minimum;
	return value/absinfo->resolution;
}

#endif /* EVDEV_H */
