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

#include "litest.h"
#include "litest-int.h"
#include "libinput-util.h"

void litest_keyboard_setup(void)
{
	struct litest_device *d = litest_create_device(LITEST_KEYBOARD);
	litest_set_current_device(d);
}

static struct litest_device_interface interface = {
};

void
litest_create_keyboard(struct litest_device *d)
{
	struct libevdev *dev;
	int rc;
	const int keys[] = {
		KEY_MENU,
		KEY_CALC,
		KEY_SETUP,
		KEY_SLEEP,
		KEY_WAKEUP,
		KEY_SCREENLOCK,
		KEY_DIRECTION,
		KEY_CYCLEWINDOWS,
		KEY_MAIL,
		KEY_BOOKMARKS,
		KEY_COMPUTER,
		KEY_BACK,
		KEY_FORWARD,
		KEY_NEXTSONG,
		KEY_PLAYPAUSE,
		KEY_PREVIOUSSONG,
		KEY_STOPCD,
		KEY_HOMEPAGE,
		KEY_REFRESH,
		KEY_F14,
		KEY_F15,
		KEY_SEARCH,
		KEY_MEDIA,
		KEY_FN,
	};
	int k;
	const int *key;
	int delay = 500, period = 30;

	d->interface = &interface;

	dev = libevdev_new();
	ck_assert(dev != NULL);

	libevdev_set_name(dev, "AT Translated Set 2 keyboard");
	libevdev_set_id_bustype(dev, 0x11);
	libevdev_set_id_vendor(dev, 0x1);
	libevdev_set_id_product(dev, 0x1);
	for (k = KEY_ESC; k <= KEY_STOP; k++) {
		if (k == KEY_SCALE)
			continue;
		libevdev_enable_event_code(dev, EV_KEY, k, NULL);
	}

	ARRAY_FOR_EACH(keys, key)
		libevdev_enable_event_code(dev, EV_KEY, *key, NULL);

	libevdev_enable_event_code(dev, EV_LED, LED_NUML, NULL);
	libevdev_enable_event_code(dev, EV_LED, LED_CAPSL, NULL);
	libevdev_enable_event_code(dev, EV_LED, LED_SCROLLL, NULL);
	libevdev_enable_event_code(dev, EV_MSC, MSC_SCAN, NULL);
	libevdev_enable_event_code(dev, EV_REP, REP_PERIOD, &period);
	libevdev_enable_event_code(dev, EV_REP, REP_DELAY, &delay);

	rc = libevdev_uinput_create_from_device(dev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&d->uinput);
	ck_assert_int_eq(rc, 0);
	libevdev_free(dev);
}
