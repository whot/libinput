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
	LITEST_WACOM_BAMBOO = -18,
	LITEST_WACOM_CINTIQ = -19,
	LITEST_WACOM_INTUOS = -20,
	LITEST_WACOM_ISDV4 = -21,
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
	LITEST_TABLET = 1 << 14,
	LITEST_DISTANCE = 1 << 15,
	LITEST_TOOL_SERIAL = 1 << 16,
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

struct libinput *litest_create_context(void);
struct axis_replacement {
	int32_t evcode;
	int32_t value;
};

void litest_add(const char *name, void *func,
		enum litest_device_feature required_feature,
		enum litest_device_feature excluded_feature);
void
litest_add_for_device(const char *name,
		      void *func,
		      enum litest_device_type type);
void litest_add_no_device(const char *name, void *func);

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
			     int slot, double x, double y);
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
void litest_tablet_proximity_in(struct litest_device *d,
				int x, int y,
				struct axis_replacement *axes);
void litest_tablet_proximity_out(struct litest_device *d);
void litest_tablet_motion(struct litest_device *d,
			  int x, int y,
			  struct axis_replacement *axes);
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
void litest_assert_button_event(struct libinput *li,
				unsigned int button,
				enum libinput_button_state state);
void litest_assert_scroll(struct libinput *li,
			  enum libinput_pointer_axis axis,
			  int minimum_movement);
void litest_assert_only_typed_events(struct libinput *li,
				     enum libinput_event_type type);
void litest_assert_tablet_button_event(struct libinput *li,
				       unsigned int button,
				       enum libinput_button_state state);

struct libevdev_uinput * litest_create_uinput_device(const char *name,
						     struct input_id *id,
						     ...);
struct libevdev_uinput * litest_create_uinput_abs_device(const char *name,
							 struct input_id *id,
							 const struct input_absinfo *abs,
							 ...);
#define litest_assert_double_eq(a_, b_)\
	ck_assert_int_eq((int)((a_) * 256), (int)((b_) * 256))

#define litest_assert_double_ne(a_, b_)\
	ck_assert_int_ne((int)((a_) * 256), (int)((b_) * 256))

#define litest_assert_double_lt(a_, b_)\
	ck_assert_int_lt((int)((a_) * 256), (int)((b_) * 256))

#define litest_assert_double_le(a_, b_)\
	ck_assert_int_le((int)((a_) * 256), (int)((b_) * 256))

#define litest_assert_double_gt(a_, b_)\
	ck_assert_int_gt((int)((a_) * 256), (int)((b_) * 256))

#define litest_assert_double_ge(a_, b_)\
	ck_assert_int_ge((int)((a_) * 256), (int)((b_) * 256))

void litest_timeout_tap(void);
void litest_timeout_softbuttons(void);
void litest_timeout_buttonscroll(void);

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
