/*
 * Copyright © 2013 Red Hat, Inc.
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

#include <config.h>

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <unistd.h>

#include "libinput-util.h"
#include "litest.h"

static void drain_events(struct libinput *li)
{
	struct libinput_event *event;

	libinput_dispatch(li);
	while ((event = libinput_get_event(li))) {
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}
}

static void
test_relative_event(struct litest_device *dev, int dx, int dy)
{
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	libevdev_uinput_write_event(dev->uinput, EV_REL, REL_X, dx);
	libevdev_uinput_write_event(dev->uinput, EV_REL, REL_Y, dy);
	libevdev_uinput_write_event(dev->uinput, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert(event != NULL);
	ck_assert_int_eq(libinput_event_get_type(event), LIBINPUT_EVENT_POINTER_MOTION);

	ptrev = libinput_event_get_pointer_event(event);
	ck_assert(ptrev != NULL);
	ck_assert_int_eq(libinput_event_pointer_get_dx(ptrev), li_fixed_from_int(dx));
	ck_assert_int_eq(libinput_event_pointer_get_dy(ptrev), li_fixed_from_int(dy));
}

START_TEST(pointer_motion_relative)
{
	struct litest_device *dev = litest_current_device();

	drain_events(dev->libinput);

	test_relative_event(dev, 1, 0);
	test_relative_event(dev, 1, 1);
	test_relative_event(dev, 1, -1);
	test_relative_event(dev, 0, 1);

	test_relative_event(dev, -1, 0);
	test_relative_event(dev, -1, 1);
	test_relative_event(dev, -1, -1);
	test_relative_event(dev, 0, -1);
}
END_TEST

static void
test_button_event(struct litest_device *dev, int button, int state)
{
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	libevdev_uinput_write_event(dev->uinput, EV_KEY, button, state);
	libevdev_uinput_write_event(dev->uinput, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert(event != NULL);
	ck_assert_int_eq(libinput_event_get_type(event), LIBINPUT_EVENT_POINTER_BUTTON);

	ptrev = libinput_event_get_pointer_event(event);
	ck_assert(ptrev != NULL);
	ck_assert_int_eq(libinput_event_pointer_get_button(ptrev), button);
	ck_assert_int_eq(libinput_event_pointer_get_button_state(ptrev),
			 state ?
				LIBINPUT_POINTER_BUTTON_STATE_PRESSED :
				LIBINPUT_POINTER_BUTTON_STATE_RELEASED);
}


START_TEST(pointer_button)
{
	struct litest_device *dev = litest_current_device();

	drain_events(dev->libinput);

	test_button_event(dev, BTN_LEFT, 1);
	test_button_event(dev, BTN_LEFT, 0);

	/* press it twice for good measure */
	test_button_event(dev, BTN_LEFT, 1);
	test_button_event(dev, BTN_LEFT, 0);

	if (libevdev_has_event_code(dev->evdev, EV_KEY, BTN_RIGHT)) {
		test_button_event(dev, BTN_RIGHT, 1);
		test_button_event(dev, BTN_RIGHT, 0);
	}

	if (libevdev_has_event_code(dev->evdev, EV_KEY, BTN_MIDDLE)) {
		test_button_event(dev, BTN_MIDDLE, 1);
		test_button_event(dev, BTN_MIDDLE, 0);
	}
}
END_TEST

int main (int argc, char **argv) {

	litest_add("pointer:motion", pointer_motion_relative, LITEST_POINTER, LITEST_ANY);
	litest_add("pointer:button", pointer_button, LITEST_BUTTON, LITEST_ANY);

	return litest_run(argc, argv);
}
