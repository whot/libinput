/*
 * Copyright © 2015 Red Hat, Inc.
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

static void
litest_wacom_intuos3_pad_setup(void)
{
	struct litest_device *d = litest_create_device(LITEST_WACOM_INTUOS3_PAD);
	litest_set_current_device(d);
}

static struct input_event down[] = {
	{ .type = -1, .code = -1 },
};

static struct input_event move[] = {
	{ .type = -1, .code = -1 },
};

static struct input_event strip_start[] = {
	{ .type = EV_ABS, .code = ABS_RX, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MISC, .value = 15 },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
} ;

static struct input_event strip_change[] = {
	{ .type = EV_ABS, .code = ABS_RX, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
} ;

static struct input_event strip_end[] = {
	{ .type = EV_ABS, .code = ABS_RX, .value = 0 },
	{ .type = EV_ABS, .code = ABS_MISC, .value = 0 },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
} ;

static struct litest_device_interface interface = {
	.touch_down_events = down,
	.touch_move_events = move,
	.buttonset_strip_start_events = strip_start,
	.buttonset_strip_change_events = strip_change,
	.buttonset_strip_end_events = strip_end,
};

static struct input_absinfo absinfo[] = {
	{ ABS_X, 0, 1, 0, 0, 0 },
	{ ABS_Y, 0, 1, 0, 0, 0 },
	{ ABS_RX, 0, 4096, 0, 0, 0 },
	{ ABS_MISC, 0, 0, 0, 0, 0 },
	{ .value = -1 },
};

static struct input_id input_id = {
	.bustype = 0x3,
	.vendor = 0x56a,
	.product = 0xb7,
};

static int events[] = {
	EV_KEY, BTN_0,
	EV_KEY, BTN_1,
	EV_KEY, BTN_2,
	EV_KEY, BTN_3,
	EV_KEY, BTN_STYLUS,
	-1, -1,
};

struct litest_test_device litest_wacom_intuos3_pad_device = {
	.type = LITEST_WACOM_INTUOS3_PAD,
	.features = LITEST_BUTTONSET,
	.shortname = "wacom-intuos3-pad",
	.setup = litest_wacom_intuos3_pad_setup,
	.interface = &interface,

	.name = "Wacom Intuos3 4x6 Pad",
	.id = &input_id,
	.events = events,
	.absinfo = absinfo,
};
