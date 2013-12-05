/*
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

#ifndef LIBINPUT_H
#define LIBINPUT_H

#include <stdlib.h>
#include <stdint.h>
#include <libudev.h>

typedef int32_t li_fixed_t;

enum libinput_device_capability {
	LIBINPUT_DEVICE_CAP_KEYBOARD = 0,
	LIBINPUT_DEVICE_CAP_POINTER = 1,
	LIBINPUT_DEVICE_CAP_TOUCH = 2,
};

enum libinput_keyboard_key_state {
	LIBINPUT_KEYBOARD_KEY_STATE_RELEASED = 0,
	LIBINPUT_KEYBOARD_KEY_STATE_PRESSED = 1,
};

enum libinput_led {
	LIBINPUT_LED_NUM_LOCK = (1 << 0),
	LIBINPUT_LED_CAPS_LOCK = (1 << 1),
	LIBINPUT_LED_SCROLL_LOCK = (1 << 2),
};

enum libinput_pointer_button_state {
	LIBINPUT_POINTER_BUTTON_STATE_RELEASED = 0,
	LIBINPUT_POINTER_BUTTON_STATE_PRESSED = 1,
};

enum libinput_pointer_axis {
	LIBINPUT_POINTER_AXIS_VERTICAL_SCROLL = 0,
	LIBINPUT_POINTER_AXIS_HORIZONTAL_SCROLL = 1,
};

enum libinput_touch_type {
	LIBINPUT_TOUCH_TYPE_DOWN = 0,
	LIBINPUT_TOUCH_TYPE_UP = 1,
	LIBINPUT_TOUCH_TYPE_MOTION = 2,
	LIBINPUT_TOUCH_TYPE_FRAME = 3,
	LIBINPUT_TOUCH_TYPE_CANCEL = 4,
};

enum libinput_event_type {
	LIBINPUT_EVENT_ADDED_SEAT = 0,
	LIBINPUT_EVENT_REMOVED_SEAT,
	LIBINPUT_EVENT_ADDED_DEVICE,
	LIBINPUT_EVENT_REMOVED_DEVICE,

	LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY = 200,
	LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY,

	LIBINPUT_EVENT_KEYBOARD_KEY = 300,

	LIBINPUT_EVENT_POINTER_MOTION = 400,
	LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE,
	LIBINPUT_EVENT_POINTER_BUTTON,
	LIBINPUT_EVENT_POINTER_AXIS,

	LIBINPUT_EVENT_TOUCH_TOUCH = 500,
};

struct libinput;
struct libinput_device;
struct libinput_seat;

union libinput_event_target {
	struct libinput *libinput;
	struct libinput_seat *seat;
	struct libinput_device *device;
};

struct libinput_event {
	enum libinput_event_type type;
	union libinput_event_target target;
};

struct libinput_event_added_seat {
	struct libinput_event base;
	struct libinput_seat *seat;
};

struct libinput_event_removed_seat {
	struct libinput_event base;
	struct libinput_seat *seat;
};

struct libinput_event_added_device {
	struct libinput_event base;
	struct libinput_device *device;
};

struct libinput_event_removed_device {
	struct libinput_event base;
	struct libinput_device *device;
};

struct libinput_event_device_register_capability {
	struct libinput_event base;
	enum libinput_device_capability capability;
};

struct libinput_event_device_unregister_capability {
	struct libinput_event base;
	enum libinput_device_capability capability;
};

struct libinput_event_keyboard_key {
	struct libinput_event base;
	uint32_t time;
	uint32_t key;
	enum libinput_keyboard_key_state state;
};

struct libinput_event_pointer_motion {
	struct libinput_event base;
	uint32_t time;
	li_fixed_t dx;
	li_fixed_t dy;
};

struct libinput_event_pointer_motion_absolute {
	struct libinput_event base;
	uint32_t time;
	li_fixed_t x;
	li_fixed_t y;
};

struct libinput_event_pointer_button {
	struct libinput_event base;
	uint32_t time;
	uint32_t button;
	enum libinput_pointer_button_state state;
};

struct libinput_event_pointer_axis {
	struct libinput_event base;
	uint32_t time;
	enum libinput_pointer_axis axis;
	li_fixed_t value;
};

struct libinput_event_touch_touch {
	struct libinput_event base;
	uint32_t time;
	uint32_t slot;
	li_fixed_t x;
	li_fixed_t y;
	enum libinput_touch_type touch_type;
};

struct libinput_interface {
	int (*open_restricted)(const char *path, int flags, void *user_data);
	void (*close_restricted)(int fd, void *user_data);

	void (*get_current_screen_dimensions)(struct libinput_device *device,
					      int *width,
					      int *height,
					      void *user_data);
};

/*
 * Base
 */

struct libinput *
libinput_create_from_udev(const struct libinput_interface *interface,
			  void *user_data,
			  struct udev *udev,
			  const char *seat_id);

int
libinput_get_fd(struct libinput *libinput);

int
libinput_dispatch(struct libinput *libinput);

struct libinput_event *
libinput_get_event(struct libinput *libinput);

void *
libinput_get_user_data(struct libinput *libinput);

int
libinput_resume(struct libinput *libinput);

void
libinput_suspend(struct libinput *libinput);

void
libinput_destroy(struct libinput *libinput);

/*
 * Seat
 */

void
libinput_seat_ref(struct libinput_seat *seat);

void
libinput_seat_unref(struct libinput_seat *seat);

void
libinput_seat_set_user_data(struct libinput_seat *seat, void *user_data);

void *
libinput_seat_get_user_data(struct libinput_seat *seat);

const char *
libinput_seat_get_name(struct libinput_seat *seat);

/*
 * Device
 */

void
libinput_device_ref(struct libinput_device *device);

void
libinput_device_unref(struct libinput_device *device);

void
libinput_device_set_user_data(struct libinput_device *device, void *user_data);

void *
libinput_device_get_user_data(struct libinput_device *device);

const char *
libinput_device_get_output_name(struct libinput_device *device);

struct libinput_seat *
libinput_device_get_seat(struct libinput_device *device);

void
libinput_device_led_update(struct libinput_device *device,
			   enum libinput_led leds);

int
libinput_device_get_keys(struct libinput_device *device,
			 char *keys, size_t size);

void
libinput_device_calibrate(struct libinput_device *device,
			  float calibration[6]);

#endif /* LIBINPUT_H */
