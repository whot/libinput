/*
 * Copyright © 2016 Red Hat, Inc.
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
#include <stdbool.h>

#include "libinput-util.h"
#include "litest.h"

START_TEST(pad_cap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;

	ck_assert(libinput_device_has_capability(device,
						 LIBINPUT_DEVICE_CAP_TABLET_PAD));

}
END_TEST

START_TEST(pad_no_cap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;

	ck_assert(!libinput_device_has_capability(device,
						  LIBINPUT_DEVICE_CAP_TABLET_PAD));
}
END_TEST

START_TEST(pad_has_button)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	unsigned int code;
	bool available;

	for (code = BTN_LEFT; code < KEY_MAX; code++) {
		available = libevdev_has_event_code(dev->evdev,
						    EV_KEY,
						    code);
		ck_assert_int_eq(libinput_device_tablet_pad_has_button(device,
								       code),
				 available);
	}

}
END_TEST

START_TEST(pad_button)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	unsigned int code;

	litest_drain_events(li);

	for (code = BTN_LEFT; code < KEY_MAX; code++) {
		if (!libevdev_has_event_code(dev->evdev,
					     EV_KEY,
					     code))
			continue;

		litest_button_click(dev, code, 1);
		libinput_dispatch(li);

		litest_assert_tablet_pad_button_event(li,
						      code,
						      LIBINPUT_BUTTON_STATE_PRESSED);

		litest_button_click(dev, code, 0);
		libinput_dispatch(li);

		litest_assert_tablet_pad_button_event(li,
						      code,
						      LIBINPUT_BUTTON_STATE_RELEASED);

	}

	litest_assert_empty_queue(li);

}
END_TEST

START_TEST(pad_has_ring)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	int nrings;

	nrings = libinput_device_tablet_pad_get_num_rings(device);
	ck_assert_int_ge(nrings, 1);
}
END_TEST

START_TEST(pad_has_strip)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	int nstrips;

	nstrips = libinput_device_tablet_pad_get_num_strips(device);
	ck_assert_int_ge(nstrips, 1);
}
END_TEST

void
litest_setup_tests(void)
{
	litest_add("pad:cap", pad_cap, LITEST_TABLET_PAD, LITEST_ANY);
	litest_add("pad:cap", pad_no_cap, LITEST_ANY, LITEST_TABLET_PAD);

	litest_add("pad:button", pad_has_button, LITEST_TABLET_PAD, LITEST_ANY);
	litest_add("pad:button", pad_button, LITEST_TABLET_PAD, LITEST_ANY);

	litest_add("pad:ring", pad_has_ring, LITEST_RING, LITEST_ANY);

	litest_add("pad:strip", pad_has_strip, LITEST_STRIP, LITEST_ANY);

}
