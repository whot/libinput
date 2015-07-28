/*
 * Copyright © 2011, 2012 Intel Corporation
 * Copyright © 2013 Jonas Ådahl
 * Copyright © 2013-2015 Red Hat, Inc.
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

#ifndef EVDEV_H
#define EVDEV_H

#include "config.h"

#include <stdbool.h>
#include "linux/input.h"
#include <libevdev/libevdev.h>

#include "libinput-private.h"
#include "timer.h"
#include "filter.h"

/*
 * The constant (linear) acceleration factor we use to normalize trackpoint
 * deltas before calculating pointer acceleration.
 */
#define DEFAULT_TRACKPOINT_ACCEL 1.0

/* The fake resolution value for abs devices without resolution */
#define EVDEV_FAKE_RESOLUTION 1

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
	EVDEV_DEVICE_TOUCH = (1 << 2),
	EVDEV_DEVICE_GESTURE = (1 << 5),
};

enum evdev_device_tags {
	EVDEV_TAG_EXTERNAL_MOUSE = (1 << 0),
	EVDEV_TAG_INTERNAL_TOUCHPAD = (1 << 1),
	EVDEV_TAG_TRACKPOINT = (1 << 2),
	EVDEV_TAG_KEYBOARD = (1 << 3),
};

enum evdev_middlebutton_state {
	MIDDLEBUTTON_IDLE,
	MIDDLEBUTTON_LEFT_DOWN,
	MIDDLEBUTTON_RIGHT_DOWN,
	MIDDLEBUTTON_MIDDLE,
	MIDDLEBUTTON_LEFT_UP_PENDING,
	MIDDLEBUTTON_RIGHT_UP_PENDING,
	MIDDLEBUTTON_IGNORE_LR,
	MIDDLEBUTTON_IGNORE_L,
	MIDDLEBUTTON_IGNORE_R,
	MIDDLEBUTTON_PASSTHROUGH,
};

enum evdev_middlebutton_event {
	MIDDLEBUTTON_EVENT_L_DOWN,
	MIDDLEBUTTON_EVENT_R_DOWN,
	MIDDLEBUTTON_EVENT_OTHER,
	MIDDLEBUTTON_EVENT_L_UP,
	MIDDLEBUTTON_EVENT_R_UP,
	MIDDLEBUTTON_EVENT_TIMEOUT,
	MIDDLEBUTTON_EVENT_ALL_UP,
};

enum evdev_device_model {
	EVDEV_MODEL_DEFAULT = 0,
	EVDEV_MODEL_LENOVO_X230 = (1 << 0),
	EVDEV_MODEL_CHROMEBOOK = (1 << 1),
	EVDEV_MODEL_SYSTEM76_BONOBO = (1 << 2),
	EVDEV_MODEL_SYSTEM76_GALAGO = (1 << 3),
	EVDEV_MODEL_SYSTEM76_KUDU = (1 << 4),
	EVDEV_MODEL_CLEVO_W740SU = (1 << 5),
	EVDEV_MODEL_APPLE_TOUCHPAD = (1 << 6),
	EVDEV_MODEL_WACOM_TOUCHPAD = (1 << 7),
	EVDEV_MODEL_ALPS_TOUCHPAD = (1 << 8),
	EVDEV_MODEL_SYNAPTICS_SERIAL_TOUCHPAD = (1 << 9),
	EVDEV_MODEL_JUMPING_SEMI_MT = (1 << 10),
	EVDEV_MODEL_ELANTECH_TOUCHPAD = (1 << 11),
};

struct mt_slot {
	int32_t seat_slot;
	struct device_coords point;
};

struct evdev_device {
	struct libinput_device base;

	struct libinput_source *source;

	struct evdev_dispatch *dispatch;
	struct libevdev *evdev;
	struct udev_device *udev_device;
	char *output_name;
	const char *devname;
	bool was_removed;
	int fd;
	struct {
		const struct input_absinfo *absinfo_x, *absinfo_y;
		int fake_resolution;

		struct device_coords point;
		int32_t seat_slot;

		int apply_calibration;
		struct matrix calibration;
		struct matrix default_calibration; /* from LIBINPUT_CALIBRATION_MATRIX */
		struct matrix usermatrix; /* as supplied by the caller */

		struct device_coords dimensions;
	} abs;

	struct {
		int slot;
		struct mt_slot *slots;
		size_t slots_len;
	} mt;
	struct mtdev *mtdev;

	struct device_coords rel;

	struct {
		struct libinput_timer timer;
		struct libinput_device_config_scroll_method config;
		/* Currently enabled method, button */
		enum libinput_config_scroll_method method;
		uint32_t button;
		uint64_t button_down_time;

		/* set during device init, used at runtime to delay changes
		 * until all buttons are up */
		enum libinput_config_scroll_method want_method;
		uint32_t want_button;
		/* Checks if buttons are down and commits the setting */
		void (*change_scroll_method)(struct evdev_device *device);
		bool button_scroll_active;
		double threshold;
		uint32_t direction;
		struct normalized_coords buildup;

		struct libinput_device_config_natural_scroll config_natural;
		/* set during device init if we want natural scrolling,
		 * used at runtime to enable/disable the feature */
		bool natural_scrolling_enabled;

		/* angle per REL_WHEEL click in degrees */
		int wheel_click_angle;
	} scroll;

	enum evdev_event_type pending_event;
	enum evdev_device_seat_capability seat_caps;
	enum evdev_device_tags tags;

	int is_mt;
	int suspended;

	struct {
		struct libinput_device_config_accel config;
		struct motion_filter *filter;
	} pointer;

	/* Bitmask of pressed keys used to ignore initial release events from
	 * the kernel. */
	unsigned long hw_key_mask[NLONGS(KEY_CNT)];
	/* Key counter used for multiplexing button events internally in
	 * libinput. */
	uint8_t key_count[KEY_CNT];

	struct {
		struct libinput_device_config_left_handed config;
		/* left-handed currently enabled */
		bool enabled;
		/* set during device init if we want left_handed config,
		 * used at runtime to delay the effect until buttons are up */
		bool want_enabled;
		/* Checks if buttons are down and commits the setting */
		void (*change_to_enabled)(struct evdev_device *device);
	} left_handed;

	struct {
		struct libinput_device_config_middle_emulation config;
		/* middle-button emulation enabled */
		bool enabled;
		bool enabled_default;
		bool want_enabled;
		enum evdev_middlebutton_state state;
		struct libinput_timer timer;
		uint32_t button_mask;
		uint64_t first_event_time;
	} middlebutton;

	int dpi; /* HW resolution */
	struct ratelimit syn_drop_limit; /* ratelimit for SYN_DROPPED logging */

	uint32_t model_flags;
};

#define EVDEV_UNHANDLED_DEVICE ((struct evdev_device *) 1)

struct evdev_dispatch;

struct evdev_dispatch_interface {
	/* Process an evdev input event. */
	void (*process)(struct evdev_dispatch *dispatch,
			struct evdev_device *device,
			struct input_event *event,
			uint64_t time);

	/* Device is being suspended */
	void (*suspend)(struct evdev_dispatch *dispatch,
			struct evdev_device *device);

	/* Device is being removed (may be NULL) */
	void (*remove)(struct evdev_dispatch *dispatch);

	/* Destroy an event dispatch handler and free all its resources. */
	void (*destroy)(struct evdev_dispatch *dispatch);

	/* A new device was added */
	void (*device_added)(struct evdev_device *device,
			     struct evdev_device *added_device);

	/* A device was removed */
	void (*device_removed)(struct evdev_device *device,
			       struct evdev_device *removed_device);

	/* A device was suspended */
	void (*device_suspended)(struct evdev_device *device,
				 struct evdev_device *suspended_device);

	/* A device was resumed */
	void (*device_resumed)(struct evdev_device *device,
			       struct evdev_device *resumed_device);
};

struct evdev_dispatch {
	struct evdev_dispatch_interface *interface;
	struct libinput_device_config_calibration calibration;

	struct {
		struct libinput_device_config_send_events config;
		enum libinput_config_send_events_mode current_mode;
	} sendevents;
};

struct evdev_device *
evdev_device_create(struct libinput_seat *seat,
		    struct udev_device *device);

int
evdev_device_init_pointer_acceleration(struct evdev_device *device,
				       struct motion_filter *filter);

struct evdev_dispatch *
evdev_touchpad_create(struct evdev_device *device);

struct evdev_dispatch *
evdev_mt_touchpad_create(struct evdev_device *device);

void
evdev_tag_touchpad(struct evdev_device *device,
		   struct udev_device *udev_device);

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

struct udev_device *
evdev_device_get_udev_device(struct evdev_device *device);

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

int
evdev_device_has_button(struct evdev_device *device, uint32_t code);

int
evdev_device_has_key(struct evdev_device *device, uint32_t code);

double
evdev_device_transform_x(struct evdev_device *device,
			 double x,
			 uint32_t width);

double
evdev_device_transform_y(struct evdev_device *device,
			 double y,
			 uint32_t height);
int
evdev_device_suspend(struct evdev_device *device);

int
evdev_device_resume(struct evdev_device *device);

void
evdev_notify_suspended_device(struct evdev_device *device);

void
evdev_notify_resumed_device(struct evdev_device *device);

void
evdev_keyboard_notify_key(struct evdev_device *device,
			  uint64_t time,
			  int key,
			  enum libinput_key_state state);

void
evdev_pointer_notify_button(struct evdev_device *device,
			    uint64_t time,
			    int button,
			    enum libinput_button_state state);
void
evdev_pointer_notify_physical_button(struct evdev_device *device,
				     uint64_t time,
				     int button,
				     enum libinput_button_state state);

void
evdev_init_natural_scroll(struct evdev_device *device);

void
evdev_post_scroll(struct evdev_device *device,
		  uint64_t time,
		  enum libinput_pointer_axis_source source,
		  const struct normalized_coords *delta);

void
evdev_stop_scroll(struct evdev_device *device,
		  uint64_t time,
		  enum libinput_pointer_axis_source source);

void
evdev_device_remove(struct evdev_device *device);

void
evdev_device_destroy(struct evdev_device *device);

bool
evdev_middlebutton_filter_button(struct evdev_device *device,
				 uint64_t time,
				 int button,
				 enum libinput_button_state state);

void
evdev_init_middlebutton(struct evdev_device *device,
			bool enabled,
			bool want_config);

static inline double
evdev_convert_to_mm(const struct input_absinfo *absinfo, double v)
{
	double value = v - absinfo->minimum;
	return value/absinfo->resolution;
}

int
evdev_init_left_handed(struct evdev_device *device,
		       void (*change_to_left_handed)(struct evdev_device *));

static inline uint32_t
evdev_to_left_handed(struct evdev_device *device,
		     uint32_t button)
{
	if (device->left_handed.enabled) {
		if (button == BTN_LEFT)
			return BTN_RIGHT;
		else if (button == BTN_RIGHT)
			return BTN_LEFT;
	}
	return button;
}

#endif /* EVDEV_H */
