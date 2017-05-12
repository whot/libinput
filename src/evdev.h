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
	EVDEV_DEVICE_TABLET = (1 << 3),
	EVDEV_DEVICE_TABLET_PAD = (1 << 4),
	EVDEV_DEVICE_GESTURE = (1 << 5),
	EVDEV_DEVICE_SWITCH = (1 << 6),
};

enum evdev_device_tags {
	EVDEV_TAG_EXTERNAL_MOUSE = (1 << 0),
	EVDEV_TAG_INTERNAL_TOUCHPAD = (1 << 1),
	EVDEV_TAG_EXTERNAL_TOUCHPAD = (1 << 2),
	EVDEV_TAG_TRACKPOINT = (1 << 3),
	EVDEV_TAG_KEYBOARD = (1 << 4),
	EVDEV_TAG_LID_SWITCH = (1 << 5),
	EVDEV_TAG_INTERNAL_KEYBOARD = (1 << 6),
	EVDEV_TAG_EXTERNAL_KEYBOARD = (1 << 7),
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
	EVDEV_MODEL_LENOVO_X220_TOUCHPAD_FW81 = (1 << 12),
	EVDEV_MODEL_CYBORG_RAT = (1 << 14),
	EVDEV_MODEL_HP_STREAM11_TOUCHPAD = (1 << 16),
	EVDEV_MODEL_LENOVO_T450_TOUCHPAD= (1 << 17),
	EVDEV_MODEL_TOUCHPAD_VISIBLE_MARKER = (1 << 18),
	EVDEV_MODEL_TRACKBALL = (1 << 19),
	EVDEV_MODEL_APPLE_MAGICMOUSE = (1 << 20),
	EVDEV_MODEL_HP8510_TOUCHPAD = (1 << 21),
	EVDEV_MODEL_HP6910_TOUCHPAD = (1 << 22),
	EVDEV_MODEL_HP_ZBOOK_STUDIO_G3 = (1 << 23),
	EVDEV_MODEL_HP_PAVILION_DM4_TOUCHPAD = (1 << 24),
	EVDEV_MODEL_APPLE_TOUCHPAD_ONEBUTTON = (1 << 25),
	EVDEV_MODEL_LOGITECH_MARBLE_MOUSE = (1 << 26),
};

enum evdev_button_scroll_state {
	BUTTONSCROLL_IDLE,
	BUTTONSCROLL_BUTTON_DOWN,	/* button is down */
	BUTTONSCROLL_READY,		/* ready for scroll events */
	BUTTONSCROLL_SCROLLING,		/* have sent scroll events */
};

struct mt_slot {
	int32_t seat_slot;
	struct device_coords point;
	struct device_coords hysteresis_center;
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
	enum evdev_device_seat_capability seat_caps;
	enum evdev_device_tags tags;
	bool is_mt;
	bool is_suspended;
	int dpi; /* HW resolution */
	int trackpoint_range; /* trackpoint max delta */
	struct ratelimit syn_drop_limit; /* ratelimit for SYN_DROPPED logging */
	struct ratelimit nonpointer_rel_limit; /* ratelimit for REL_* events from non-pointer devices */
	uint32_t model_flags;
	struct mtdev *mtdev;

	struct {
		const struct input_absinfo *absinfo_x, *absinfo_y;
		bool is_fake_resolution;

		int apply_calibration;
		struct matrix calibration;
		struct matrix default_calibration; /* from LIBINPUT_CALIBRATION_MATRIX */
		struct matrix usermatrix; /* as supplied by the caller */

		struct device_coords dimensions;

		struct {
			struct device_coords min, max;
			struct ratelimit range_warn_limit;
		} warning_range;
	} abs;

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
		enum evdev_button_scroll_state button_scroll_state;
		double threshold;
		double direction_lock_threshold;
		uint32_t direction;
		struct normalized_coords buildup;

		struct libinput_device_config_natural_scroll config_natural;
		/* set during device init if we want natural scrolling,
		 * used at runtime to enable/disable the feature */
		bool natural_scrolling_enabled;

		/* angle per REL_WHEEL click in degrees */
		struct wheel_angle wheel_click_angle;

		struct wheel_tilt_flags is_tilt;
	} scroll;

	struct {
		struct libinput_device_config_accel config;
		struct motion_filter *filter;
	} pointer;

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
};

static inline struct evdev_device *
evdev_device(struct libinput_device *device)
{
	return container_of(device, struct evdev_device, base);
}

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

	/* Called immediately after the LIBINPUT_EVENT_DEVICE_ADDED event
	 * was sent */
	void (*post_added)(struct evdev_device *device,
			   struct evdev_dispatch *dispatch);

	void (*toggle_touch)(struct evdev_dispatch *dispatch,
			     struct evdev_device *device,
			     bool enable);
};

enum evdev_dispatch_type {
	DISPATCH_FALLBACK,
	DISPATCH_TOUCHPAD,
	DISPATCH_TABLET,
	DISPATCH_TABLET_PAD,
	DISPATCH_LID_SWITCH,
};

struct evdev_dispatch {
	enum evdev_dispatch_type dispatch_type;
	struct evdev_dispatch_interface *interface;

	struct {
		struct libinput_device_config_send_events config;
		enum libinput_config_send_events_mode current_mode;
	} sendevents;
};

static inline void
evdev_verify_dispatch_type(struct evdev_dispatch *dispatch,
			   enum evdev_dispatch_type type)
{
	if (dispatch->dispatch_type != type)
		abort();
}

struct fallback_dispatch {
	struct evdev_dispatch base;

	struct libinput_device_config_calibration calibration;

	struct {
		bool is_enabled;
		int angle;
		struct matrix matrix;
		struct libinput_device_config_rotation config;
	} rotation;

	struct {
		struct device_coords point;
		int32_t seat_slot;

		struct {
			struct device_coords min, max;
			struct ratelimit range_warn_limit;
		} warning_range;
	} abs;

	struct {
		int slot;
		struct mt_slot *slots;
		size_t slots_len;
		bool want_hysteresis;
		struct device_coords hysteresis_margin;
	} mt;

	struct device_coords rel;

	/* Bitmask of pressed keys used to ignore initial release events from
	 * the kernel. */
	unsigned long hw_key_mask[NLONGS(KEY_CNT)];

	enum evdev_event_type pending_event;

	/* true if we're reading events (i.e. not suspended) but we're
	   ignoring them */
	bool ignore_events;
};

static inline struct fallback_dispatch*
fallback_dispatch(struct evdev_dispatch *dispatch)
{
	evdev_verify_dispatch_type(dispatch, DISPATCH_FALLBACK);

	return container_of(dispatch, struct fallback_dispatch, base);
}

struct evdev_device *
evdev_device_create(struct libinput_seat *seat,
		    struct udev_device *device);

void
evdev_transform_absolute(struct evdev_device *device,
			 struct device_coords *point);

void
evdev_transform_relative(struct evdev_device *device,
			 struct device_coords *point);

void
evdev_init_calibration(struct evdev_device *device,
		        struct libinput_device_config_calibration *calibration);

void
evdev_read_calibration_prop(struct evdev_device *device);

void
evdev_init_sendevents(struct evdev_device *device,
		      struct evdev_dispatch *dispatch);

void
evdev_device_init_pointer_acceleration(struct evdev_device *device,
				       struct motion_filter *filter);

struct evdev_dispatch *
evdev_touchpad_create(struct evdev_device *device);

struct evdev_dispatch *
evdev_mt_touchpad_create(struct evdev_device *device);

struct evdev_dispatch *
evdev_tablet_create(struct evdev_device *device);

struct evdev_dispatch *
evdev_tablet_pad_create(struct evdev_device *device);

struct evdev_dispatch *
evdev_lid_switch_dispatch_create(struct evdev_device *device);

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

bool
evdev_device_has_capability(struct evdev_device *device,
			    enum libinput_device_capability capability);

int
evdev_device_get_size(const struct evdev_device *device,
		      double *w,
		      double *h);

int
evdev_device_has_button(struct evdev_device *device, uint32_t code);

int
evdev_device_has_key(struct evdev_device *device, uint32_t code);

int
evdev_device_tablet_pad_get_num_buttons(struct evdev_device *device);

int
evdev_device_tablet_pad_get_num_rings(struct evdev_device *device);

int
evdev_device_tablet_pad_get_num_strips(struct evdev_device *device);

int
evdev_device_tablet_pad_get_num_mode_groups(struct evdev_device *device);

struct libinput_tablet_pad_mode_group *
evdev_device_tablet_pad_get_mode_group(struct evdev_device *device,
				       unsigned int index);

unsigned int
evdev_device_tablet_pad_mode_group_get_button_target(
				     struct libinput_tablet_pad_mode_group *g,
				     unsigned int button_index);

struct libinput_tablet_pad_led *
evdev_device_tablet_pad_get_led(struct evdev_device *device,
				unsigned int led);

double
evdev_device_transform_x(struct evdev_device *device,
			 double x,
			 uint32_t width);

double
evdev_device_transform_y(struct evdev_device *device,
			 double y,
			 uint32_t height);
void
evdev_device_suspend(struct evdev_device *device);

int
evdev_device_resume(struct evdev_device *device);

void
evdev_notify_suspended_device(struct evdev_device *device);

void
evdev_notify_resumed_device(struct evdev_device *device);

void
evdev_pointer_notify_button(struct evdev_device *device,
			    uint64_t time,
			    unsigned int button,
			    enum libinput_button_state state);
void
evdev_pointer_notify_physical_button(struct evdev_device *device,
				     uint64_t time,
				     int button,
				     enum libinput_button_state state);

void
evdev_init_natural_scroll(struct evdev_device *device);

void
evdev_notify_axis(struct evdev_device *device,
		  uint64_t time,
		  uint32_t axes,
		  enum libinput_pointer_axis_source source,
		  const struct normalized_coords *delta_in,
		  const struct discrete_coords *discrete_in);
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

enum libinput_config_middle_emulation_state
evdev_middlebutton_get(struct libinput_device *device);

int
evdev_middlebutton_is_available(struct libinput_device *device);

enum libinput_config_middle_emulation_state
evdev_middlebutton_get_default(struct libinput_device *device);

static inline double
evdev_convert_to_mm(const struct input_absinfo *absinfo, double v)
{
	double value = v - absinfo->minimum;
	return value/absinfo->resolution;
}

void
evdev_init_left_handed(struct evdev_device *device,
		       void (*change_to_left_handed)(struct evdev_device *));

bool
evdev_tablet_has_left_handed(struct evdev_device *device);

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

/**
 * Apply a hysteresis filtering to the coordinate in, based on the current
 * hysteresis center and the margin. If 'in' is within 'margin' of center,
 * return the center (and thus filter the motion). If 'in' is outside,
 * return a point on the edge of the new margin. So for a point x in the
 * space outside c + margin we return r:
 * +---+       +---+
 * | c |  x →  | r x
 * +---+       +---+
 *
 * The effect of this is that initial small motions are filtered. Once we
 * move into one direction we lag the real coordinates by 'margin' but any
 * movement that continues into that direction will always be just outside
 * margin - we get responsive movement. Once we move back into the other
 * direction, the first movements are filtered again.
 *
 * Returning the edge rather than the point avoids cursor jumps, as the
 * first reachable coordinate is the point next to the center (center + 1).
 * Otherwise, the center has a dead zone of size margin around it and the
 * first reachable point is the margin edge.
 *
 * Hysteresis is handled separately per axis (and the window is thus
 * rectangular, not circular). It is unkown if that's an issue, but the
 * calculation to do circular hysteresis are nontrivial, especially since
 * many touchpads have uneven x/y resolutions.
 *
 * @param in The input coordinate
 * @param center Current center of the hysteresis
 * @param margin Hysteresis width (on each side)
 *
 * @return The new center of the hysteresis
 */
static inline int
evdev_hysteresis(int in, int center, int margin)
{
	int diff = in - center;
	if (abs(diff) <= margin)
		return center;

	if (diff > 0)
		return in - margin;
	else
		return in + margin;
}

static inline struct libinput *
evdev_libinput_context(const struct evdev_device *device)
{
	return device->base.seat->libinput;
}

LIBINPUT_ATTRIBUTE_PRINTF(3, 0)
static inline void
evdev_log_msg_va(struct evdev_device *device,
		 enum libinput_log_priority priority,
		 const char *format,
		 va_list args)
{
	log_msg(evdev_libinput_context(device),
		priority,
		"%-7s - ",
		evdev_device_get_sysname(device));

	/* Anything info and above is user-visible, use the device name */
	if (priority > LIBINPUT_LOG_PRIORITY_DEBUG)
		log_msg(evdev_libinput_context(device),
			priority,
			"%s: ",
			device->devname);

	log_msg_va(evdev_libinput_context(device),
		   priority,
		   format,
		   args);
}

LIBINPUT_ATTRIBUTE_PRINTF(3, 4)
static inline void
evdev_log_msg(struct evdev_device *device,
	      enum libinput_log_priority priority,
	      const char *format,
	      ...)
{
	va_list args;

	va_start(args, format);
	evdev_log_msg_va(device, priority, format, args);
	va_end(args);

}

LIBINPUT_ATTRIBUTE_PRINTF(4, 5)
static inline void
evdev_log_msg_ratelimit(struct evdev_device *device,
			struct ratelimit *ratelimit,
			enum libinput_log_priority priority,
			const char *format,
			...)
{
	va_list args;
	enum ratelimit_state state;

	state = ratelimit_test(ratelimit);
	if (state == RATELIMIT_EXCEEDED)
		return;

	va_start(args, format);
	evdev_log_msg_va(device, priority, format, args);
	va_end(args);

	if (state == RATELIMIT_THRESHOLD)
		evdev_log_msg(device,
			      priority,
			      "WARNING: log rate limit exceeded (%d msgs per %dms). Discarding future messages.\n",
			      ratelimit->burst,
			      us2ms(ratelimit->interval));
}

#define evdev_log_debug(d_, ...) evdev_log_msg((d_), LIBINPUT_LOG_PRIORITY_DEBUG, __VA_ARGS__)
#define evdev_log_info(d_, ...) evdev_log_msg((d_), LIBINPUT_LOG_PRIORITY_INFO, __VA_ARGS__)
#define evdev_log_error(d_, ...) evdev_log_msg((d_), LIBINPUT_LOG_PRIORITY_ERROR, __VA_ARGS__)
#define evdev_log_bug_kernel(d_, ...) evdev_log_msg((d_), LIBINPUT_LOG_PRIORITY_ERROR, "kernel bug: " __VA_ARGS__)
#define evdev_log_bug_libinput(d_, ...) evdev_log_msg((d_), LIBINPUT_LOG_PRIORITY_ERROR, "libinput bug: " __VA_ARGS__)
#define evdev_log_bug_client(d_, ...) evdev_log_msg((d_), LIBINPUT_LOG_PRIORITY_ERROR, "client bug: " __VA_ARGS__)

#define evdev_log_debug_ratelimit(d_, r_, ...) \
	evdev_log_msg_ratelimit((d_), (r_), LIBINPUT_LOG_PRIORITY_DEBUG, __VA_ARGS__)
#define evdev_log_info_ratelimit(d_, r_, ...) \
	evdev_log_msg_ratelimit((d_), (r_), LIBINPUT_LOG_PRIORITY_INFO, __VA_ARGS__)
#define evdev_log_error_ratelimit(d_, r_, ...) \
	evdev_log_msg_ratelimit((d_), (r_), LIBINPUT_LOG_PRIORITY_ERROR, __VA_ARGS__)
#define evdev_log_bug_kernel_ratelimit(d_, r_, ...) \
	evdev_log_msg_ratelimit((d_), (r_), LIBINPUT_LOG_PRIORITY_ERROR, "kernel bug: " __VA_ARGS__)
#define evdev_log_bug_libinput_ratelimit(d_, r_, ...) \
	evdev_log_msg_ratelimit((d_), (r_), LIBINPUT_LOG_PRIORITY_ERROR, "libinput bug: " __VA_ARGS__)
#define evdev_log_bug_client_ratelimit(d_, r_, ...) \
	evdev_log_msg_ratelimit((d_), (r_), LIBINPUT_LOG_PRIORITY_ERROR, "client bug: " __VA_ARGS__)

/**
 * Convert the pair of delta coordinates in device space to mm.
 */
static inline struct phys_coords
evdev_device_unit_delta_to_mm(const struct evdev_device* device,
			      const struct device_coords *units)
{
	struct phys_coords mm = { 0,  0 };
	const struct input_absinfo *absx, *absy;

	if (device->abs.absinfo_x == NULL ||
	    device->abs.absinfo_y == NULL) {
		log_bug_libinput(evdev_libinput_context(device),
				 "%s: is not an abs device\n",
				 device->devname);
		return mm;
	}

	absx = device->abs.absinfo_x;
	absy = device->abs.absinfo_y;

	mm.x = 1.0 * units->x/absx->resolution;
	mm.y = 1.0 * units->y/absy->resolution;

	return mm;
}

/**
 * Convert the pair of coordinates in device space to mm. This takes the
 * axis min into account, i.e. a unit of min is equivalent to 0 mm.
 */
static inline struct phys_coords
evdev_device_units_to_mm(const struct evdev_device* device,
			 const struct device_coords *units)
{
	struct phys_coords mm = { 0,  0 };
	const struct input_absinfo *absx, *absy;

	if (device->abs.absinfo_x == NULL ||
	    device->abs.absinfo_y == NULL) {
		log_bug_libinput(evdev_libinput_context(device),
				 "%s: is not an abs device\n",
				 device->devname);
		return mm;
	}

	absx = device->abs.absinfo_x;
	absy = device->abs.absinfo_y;

	mm.x = (units->x - absx->minimum)/absx->resolution;
	mm.y = (units->y - absy->minimum)/absy->resolution;

	return mm;
}

/**
 * Convert the pair of coordinates in mm to device units. This takes the
 * axis min into account, i.e. 0 mm  is equivalent to the min.
 */
static inline struct device_coords
evdev_device_mm_to_units(const struct evdev_device *device,
			 const struct phys_coords *mm)
{
	struct device_coords units = { 0,  0 };
	const struct input_absinfo *absx, *absy;

	if (device->abs.absinfo_x == NULL ||
	    device->abs.absinfo_y == NULL) {
		log_bug_libinput(evdev_libinput_context(device),
				 "%s: is not an abs device\n",
				 device->devname);
		return units;
	}

	absx = device->abs.absinfo_x;
	absy = device->abs.absinfo_y;

	units.x = mm->x * absx->resolution + absx->minimum;
	units.y = mm->y * absy->resolution + absy->minimum;

	return units;
}

static inline void
evdev_device_init_abs_range_warnings(struct evdev_device *device)
{
	const struct input_absinfo *x, *y;
	int width, height;

	x = device->abs.absinfo_x;
	y = device->abs.absinfo_y;
	width = device->abs.dimensions.x;
	height = device->abs.dimensions.y;

	device->abs.warning_range.min.x = x->minimum - 0.05 * width;
	device->abs.warning_range.min.y = y->minimum - 0.05 * height;
	device->abs.warning_range.max.x = x->maximum + 0.05 * width;
	device->abs.warning_range.max.y = y->maximum + 0.05 * height;

	/* One warning every 5 min is enough */
	ratelimit_init(&device->abs.warning_range.range_warn_limit,
		       s2us(3000),
		       1);
}

static inline void
evdev_device_check_abs_axis_range(struct evdev_device *device,
				  unsigned int code,
				  int value)
{
	int min, max;

	switch(code) {
	case ABS_X:
	case ABS_MT_POSITION_X:
		min = device->abs.warning_range.min.x;
		max = device->abs.warning_range.max.x;
		break;
	case ABS_Y:
	case ABS_MT_POSITION_Y:
		min = device->abs.warning_range.min.y;
		max = device->abs.warning_range.max.y;
		break;
	default:
		return;
	}

	if (value < min || value > max) {
		log_info_ratelimit(evdev_libinput_context(device),
				   &device->abs.warning_range.range_warn_limit,
				   "Axis %#x value %d is outside expected range [%d, %d]\n"
				   "See %s/absolute_coordinate_ranges.html for details\n",
				   code, value, min, max,
				   HTTP_DOC_LINK);
	}
}

#endif /* EVDEV_H */
