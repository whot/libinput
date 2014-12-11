/*
 * Copyright © 2014 Red Hat, Inc.
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

#include <assert.h>

#include "libinput-util.h"

#include "litest.h"
#include "litest-int.h"

static void
synaptics_hover_create(struct litest_device *d);

static void
litest_synaptics_hover_setup(void)
{
	struct litest_device *d = litest_create_device(LITEST_SYNAPTICS_HOVER_SEMI_MT);
	litest_set_current_device(d);
}

static void
synaptics_hover_touch_down(struct litest_device *d, unsigned int slot, double x, double y)
{
	struct litest_semi_mt *semi_mt = d->private;

	litest_semi_mt_touch_down(d, semi_mt, slot, x, y);
}

static void
synaptics_hover_touch_move(struct litest_device *d, unsigned int slot, double x, double y)
{
	struct litest_semi_mt *semi_mt = d->private;

	litest_semi_mt_touch_move(d, semi_mt, slot, x, y);
}

static void
synaptics_hover_touch_up(struct litest_device *d, unsigned int slot)
{
	struct litest_semi_mt *semi_mt = d->private;

	litest_semi_mt_touch_up(d, semi_mt, slot);
}

static struct litest_device_interface interface = {
	.touch_down = synaptics_hover_touch_down,
	.touch_move = synaptics_hover_touch_move,
	.touch_up = synaptics_hover_touch_up,
};

static struct input_id input_id = {
	.bustype = 0x11,
	.vendor = 0x2,
	.product = 0x7,
};

static int events[] = {
	EV_KEY, BTN_LEFT,
	EV_KEY, BTN_RIGHT,
	EV_KEY, BTN_TOOL_FINGER,
	EV_KEY, BTN_TOUCH,
	EV_KEY, BTN_TOOL_DOUBLETAP,
	EV_KEY, BTN_TOOL_TRIPLETAP,
	INPUT_PROP_MAX, INPUT_PROP_POINTER,
	INPUT_PROP_MAX, INPUT_PROP_SEMI_MT,
	-1, -1,
};

static struct input_absinfo absinfo[] = {
	{ ABS_X, 1472, 5472, 0, 0, 60 },
	{ ABS_Y, 1408, 4498, 0, 0, 85 },
	{ ABS_PRESSURE, 0, 255, 0, 0, 0 },
	{ ABS_TOOL_WIDTH, 0, 15, 0, 0, 0 },
	{ ABS_MT_SLOT, 0, 1, 0, 0, 0 },
	{ ABS_MT_POSITION_X, 1472, 5472, 0, 0, 60 },
	{ ABS_MT_POSITION_Y, 1408, 4498, 0, 0, 85 },
	{ ABS_MT_TRACKING_ID, 0, 65535, 0, 0, 0 },
	{ .value = -1 }
};

struct litest_test_device litest_synaptics_hover_device = {
	.type = LITEST_SYNAPTICS_HOVER_SEMI_MT,
	.features = LITEST_TOUCHPAD | LITEST_SEMI_MT | LITEST_BUTTON,
	.shortname = "synaptics hover",
	.setup = litest_synaptics_hover_setup,
	.interface = &interface,
	.create = synaptics_hover_create,

	.name = "SynPS/2 Synaptics TouchPad",
	.id = &input_id,
	.events = events,
	.absinfo = absinfo,
};

static void
synaptics_hover_create(struct litest_device *d)
{
	struct litest_semi_mt *semi_mt = zalloc(sizeof(*semi_mt));
	assert(semi_mt);

	d->private = semi_mt;

	d->uinput = litest_create_uinput_device_from_description(
			litest_synaptics_hover_device.name,
			litest_synaptics_hover_device.id,
			absinfo,
			events);
	d->interface = &interface;
}
