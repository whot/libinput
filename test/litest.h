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
	LITEST_SYNAPTICS_CLICKPAD,
	LITEST_SYNAPTICS_TOUCHPAD,
	LITEST_SYNAPTICS_TOPBUTTONPAD,
	LITEST_BCM5974,
	LITEST_KEYBOARD,
	LITEST_TRACKPOINT,
	LITEST_MOUSE,
	LITEST_WACOM_TOUCH,
	LITEST_ALPS_SEMI_MT,
	LITEST_GENERIC_SINGLETOUCH,
};

enum litest_device_feature {
	LITEST_DISABLE_DEVICE = -1,
	LITEST_ANY = 0,
	LITEST_TOUCHPAD = 1 << 0,
	LITEST_CLICKPAD = 1 << 1,
	LITEST_BUTTON = 1 << 2,
	LITEST_KEYS = 1 << 3,
	LITEST_POINTER = 1 << 4,
	LITEST_WHEEL = 1 << 5,
	LITEST_TOUCH = 1 << 6,
	LITEST_SINGLE_TOUCH = 1 << 7,
	LITEST_APPLE_CLICKPAD = 1 << 8,
	LITEST_TOPBUTTONPAD = 1 << 9,
	LITEST_SEMI_MT = 1 << 10,
};

struct litest_device {
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	struct libinput *libinput;
	bool owns_context;
	struct libinput_device *libinput_device;
	struct litest_device_interface *interface;

	int ntouches_down;
	void *private; /* device-specific data */
};

struct libinput *litest_create_context(void);

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
			  int steps);
void litest_button_click(struct litest_device *d,
			 unsigned int button,
			 bool is_press);
void litest_keyboard_key(struct litest_device *d,
			 unsigned int key,
			 bool is_press);
void litest_wait_for_event(struct libinput *li);
void litest_wait_for_event_of_type(struct libinput *li, ...);
void litest_drain_events(struct libinput *li);
void litest_assert_empty_queue(struct libinput *li);

struct libevdev_uinput * litest_create_uinput_device(const char *name,
						     struct input_id *id,
						     ...);
struct libevdev_uinput * litest_create_uinput_abs_device(const char *name,
							 struct input_id *id,
							 const struct input_absinfo *abs,
							 ...);

#ifndef ck_assert_notnull
#define ck_assert_notnull(ptr) ck_assert_ptr_ne(ptr, NULL)
#endif

#endif /* LITEST_H */
