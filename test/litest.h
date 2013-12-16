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
	LITEST_NO_DEVICE = 0,
	LITEST_SYNAPTICS_CLICKPAD = (1 << 0),
	LITEST_KEYBOARD = (1 << 1),
	LITEST_TRACKPOINT = (1 << 2),

	LITEST_ALL_TOUCHPADS = LITEST_SYNAPTICS_CLICKPAD,
	LITEST_ALL_KEYBOARDS = LITEST_KEYBOARD,
	LITEST_ALL_POINTERS = LITEST_TRACKPOINT,
	LITEST_ALL_DEVICES = LITEST_ALL_TOUCHPADS|LITEST_ALL_KEYBOARDS|LITEST_ALL_POINTERS,
};

struct litest_device {
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	struct libinput *libinput;
	struct litest_device_interface *interface;
};

void litest_add(const char *name, void *func, enum litest_device_type devices);
int litest_run(int argc, char **argv);
struct litest_device * litest_create_device(enum litest_device_type which);
struct litest_device *litest_current_device(void);
void litest_delete_device(struct litest_device *d);
int litest_handle_events(struct litest_device *d);

void litest_event(struct litest_device *t, unsigned int type, unsigned int code, int value);
void litest_touch_up(struct litest_device *d, unsigned int slot);
void litest_touch_move(struct litest_device *d, unsigned int slot, int x, int y);
void litest_touch_down(struct litest_device *d, unsigned int slot, int x, int y);
void litest_touch_move_to(struct litest_device *d, unsigned int slot, int x_from, int y_from, int x_to, int y_to, int steps);
void litest_button_click(struct litest_device *d, unsigned int button, bool is_press);

#endif /* LITEST_H */
