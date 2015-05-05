/*
 * Copyright © 2013 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef LITEST_H
#define LITEST_H

#include <stdbool.h>
#include <check.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <libinput.h>

enum litest_device_type {
	LITEST_NO_DEVICE = -1,
	LITEST_SYNAPTICS_CLICKPAD = -2,
	LITEST_SYNAPTICS_TOUCHPAD = -3,
	LITEST_SYNAPTICS_TOPBUTTONPAD = -4,
	LITEST_BCM5974 = -5,
	LITEST_KEYBOARD = -6,
	LITEST_TRACKPOINT = -7,
	LITEST_MOUSE = -8,
	LITEST_WACOM_TOUCH = -9,
	LITEST_ALPS_SEMI_MT = -10,
	LITEST_GENERIC_SINGLETOUCH = -11,
	LITEST_MS_SURFACE_COVER = -12,
	LITEST_QEMU_TABLET = -13,
	LITEST_XEN_VIRTUAL_POINTER = -14,
	LITEST_VMWARE_VIRTMOUSE = -15,
	LITEST_SYNAPTICS_HOVER_SEMI_MT = -16,
	LITEST_SYNAPTICS_TRACKPOINT_BUTTONS = -17,
	LITEST_PROTOCOL_A_SCREEN = -18,
	LITEST_WACOM_FINGER = -19,
	LITEST_KEYBOARD_BLACKWIDOW = -20,
	LITEST_WHEEL_ONLY = -21,
	LITEST_MOUSE_ROCCAT = -22,
	LITEST_LOGITECH_TRACKBALL = -23,
	LITEST_ATMEL_HOVER = -24,
};

enum litest_device_feature {
	LITEST_DISABLE_DEVICE = -1,
	LITEST_ANY = 0,
	LITEST_TOUCHPAD = 1 << 0,
	LITEST_CLICKPAD = 1 << 1,
	LITEST_BUTTON = 1 << 2,
	LITEST_KEYS = 1 << 3,
	LITEST_RELATIVE = 1 << 4,
	LITEST_WHEEL = 1 << 5,
	LITEST_TOUCH = 1 << 6,
	LITEST_SINGLE_TOUCH = 1 << 7,
	LITEST_APPLE_CLICKPAD = 1 << 8,
	LITEST_TOPBUTTONPAD = 1 << 9,
	LITEST_SEMI_MT = 1 << 10,
	LITEST_POINTINGSTICK = 1 << 11,
	LITEST_FAKE_MT = 1 << 12,
	LITEST_ABSOLUTE = 1 << 13,
	LITEST_PROTOCOL_A = 1 << 14,
	LITEST_HOVER = 1 << 15,
};

struct litest_device {
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	struct libinput *libinput;
	bool owns_context;
	struct libinput_device *libinput_device;
	struct litest_device_interface *interface;

	int ntouches_down;
	bool skip_ev_syn;

	void *private; /* device-specific data */

	char *udev_rule_file;
};

/* A loop range, resolves to:
   for (i = lower; i < upper; i++)
 */
struct range {
	int lower; /* inclusive */
	int upper; /* exclusive */
};

struct libinput *litest_create_context(void);
void litest_disable_log_handler(struct libinput *libinput);
void litest_restore_log_handler(struct libinput *libinput);

void litest_add(const char *name, void *func,
		enum litest_device_feature required_feature,
		enum litest_device_feature excluded_feature);
void litest_add_ranged(const char *name,
		       void *func,
		       enum litest_device_feature required,
		       enum litest_device_feature excluded,
		       const struct range *range);
void
litest_add_for_device(const char *name,
		      void *func,
		      enum litest_device_type type);
void litest_add_ranged_for_device(const char *name,
				  void *func,
				  enum litest_device_type type,
				  const struct range *range);
void litest_add_no_device(const char *name, void *func);
void litest_add_ranged_no_device(const char *name,
				 void *func,
				 const struct range *range);

int litest_run(int argc, char **argv);
struct litest_device * litest_create_device(enum litest_device_type which);
struct litest_device * litest_add_device(struct libinput *libinput,
					 enum litest_device_type which);
struct libevdev_uinput *
litest_create_uinput_device_from_description(const char *name,
					     const struct input_id *id,
					     const struct input_absinfo *abs,
					     const int *events);
struct litest_device *
litest_create_device_with_overrides(enum litest_device_type which,
				    const char *name_override,
				    struct input_id *id_override,
				    const struct input_absinfo *abs_override,
				    const int *events_override);
struct litest_device *
litest_add_device_with_overrides(struct libinput *libinput,
				 enum litest_device_type which,
				 const char *name_override,
				 struct input_id *id_override,
				 const struct input_absinfo *abs_override,
				 const int *events_override);

struct litest_device *litest_current_device(void);
void litest_delete_device(struct litest_device *d);
int litest_handle_events(struct litest_device *d);

void litest_event(struct litest_device *t,
		  unsigned int type,
		  unsigned int code,
		  int value);
int litest_auto_assign_value(struct litest_device *d,
			     const struct input_event *ev,
			     int slot, double x, double y,
			     bool touching);
void litest_touch_up(struct litest_device *d, unsigned int slot);
void litest_touch_move(struct litest_device *d,
		       unsigned int slot,
		       double x,
		       double y);
void litest_touch_down(struct litest_device *d,
		       unsigned int slot,
		       double x,
		       double y);
void litest_touch_move_to(struct litest_device *d,
			  unsigned int slot,
			  double x_from, double y_from,
			  double x_to, double y_to,
			  int steps, int sleep_ms);
void litest_touch_move_two_touches(struct litest_device *d,
				   double x0, double y0,
				   double x1, double y1,
				   double dx, double dy,
				   int steps, int sleep_ms);
void litest_hover_start(struct litest_device *d,
			unsigned int slot,
			double x,
			double y);
void litest_hover_end(struct litest_device *d, unsigned int slot);
void litest_hover_move(struct litest_device *d,
		       unsigned int slot,
		       double x,
		       double y);
void litest_hover_move_to(struct litest_device *d,
			  unsigned int slot,
			  double x_from, double y_from,
			  double x_to, double y_to,
			  int steps, int sleep_ms);
void litest_hover_move_two_touches(struct litest_device *d,
				   double x0, double y0,
				   double x1, double y1,
				   double dx, double dy,
				   int steps, int sleep_ms);
void litest_button_click(struct litest_device *d,
			 unsigned int button,
			 bool is_press);
void litest_button_scroll(struct litest_device *d,
			 unsigned int button,
			 double dx, double dy);
void litest_keyboard_key(struct litest_device *d,
			 unsigned int key,
			 bool is_press);
void litest_wait_for_event(struct libinput *li);
void litest_wait_for_event_of_type(struct libinput *li, ...);
void litest_drain_events(struct libinput *li);
void litest_assert_empty_queue(struct libinput *li);
struct libinput_event_pointer * litest_is_button_event(
		       struct libinput_event *event,
		       unsigned int button,
		       enum libinput_button_state state);
struct libinput_event_pointer * litest_is_axis_event(
		       struct libinput_event *event,
		       enum libinput_pointer_axis axis,
		       enum libinput_pointer_axis_source source);
struct libinput_event_pointer * litest_is_motion_event(
		       struct libinput_event *event);
struct libinput_event_touch * litest_is_touch_event(
		       struct libinput_event *event,
		       enum libinput_event_type type);
struct libinput_event_keyboard * litest_is_keyboard_event(
		       struct libinput_event *event,
		       unsigned int key,
		       enum libinput_key_state state);
void litest_assert_button_event(struct libinput *li,
				unsigned int button,
				enum libinput_button_state state);
void litest_assert_scroll(struct libinput *li,
			  enum libinput_pointer_axis axis,
			  int minimum_movement);
void litest_assert_only_typed_events(struct libinput *li,
				     enum libinput_event_type type);

struct libevdev_uinput * litest_create_uinput_device(const char *name,
						     struct input_id *id,
						     ...);
struct libevdev_uinput * litest_create_uinput_abs_device(const char *name,
							 struct input_id *id,
							 const struct input_absinfo *abs,
							 ...);

void litest_timeout_tap(void);
void litest_timeout_tapndrag(void);
void litest_timeout_softbuttons(void);
void litest_timeout_buttonscroll(void);
void litest_timeout_edgescroll(void);
void litest_timeout_finger_switch(void);
void litest_timeout_middlebutton(void);

void litest_push_event_frame(struct litest_device *dev);
void litest_pop_event_frame(struct litest_device *dev);

/* this is a semi-mt device, so we keep track of the touches that the tests
 * send and modify them so that the first touch is always slot 0 and sends
 * the top-left of the bounding box, the second is always slot 1 and sends
 * the bottom-right of the bounding box.
 * Lifting any of two fingers terminates slot 1
 */
struct litest_semi_mt {
	int tracking_id;
	/* The actual touches requested by the test for the two slots
	 * in the 0..100 range used by litest */
	struct {
		double x, y;
	} touches[2];
};

void litest_semi_mt_touch_down(struct litest_device *d,
			       struct litest_semi_mt *semi_mt,
			       unsigned int slot,
			       double x, double y);
void litest_semi_mt_touch_move(struct litest_device *d,
			       struct litest_semi_mt *semi_mt,
			       unsigned int slot,
			       double x, double y);
void litest_semi_mt_touch_up(struct litest_device *d,
			     struct litest_semi_mt *semi_mt,
			     unsigned int slot);

#ifndef ck_assert_notnull
#define ck_assert_notnull(ptr) ck_assert_ptr_ne(ptr, NULL)
#endif

#endif /* LITEST_H */
