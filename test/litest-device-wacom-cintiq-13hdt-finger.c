/*
 * Copyright © 2016 Red Hat, Inc.
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

static void litest_wacom_cintiq_tablet_setup(void)
{
	struct litest_device *d = litest_create_device(LITEST_WACOM_CINTIQ_13HDT_FINGER);
	litest_set_current_device(d);
}

static struct input_event down[] = {
	{ .type = EV_ABS, .code = ABS_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_SLOT, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_ORIENTATION, .value = 0 },
	{ .type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_POSITION_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
};

static struct input_event move[] = {
	{ .type = EV_ABS, .code = ABS_MT_SLOT, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_POSITION_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
};

static struct litest_device_interface interface = {
	.touch_down_events = down,
	.touch_move_events = move,
};

static struct input_absinfo absinfo[] = {
	{ ABS_X, 0, 2937, 0, 0, 10 },
	{ ABS_Y, 0, 1652, 0, 0, 10 },
	{ ABS_MT_SLOT, 0, 9, 0, 0, 0 },
	{ ABS_MT_TOUCH_MAJOR, 0, 2937, 0, 0, 0 },
	{ ABS_MT_WIDTH_MAJOR, 0, 2937, 0, 0, 0 },
	{ ABS_MT_WIDTH_MINOR, 0, 1652, 0, 0, 0 },
	{ ABS_MT_ORIENTATION, 0, 1, 0, 0, 0 },
	{ ABS_MT_POSITION_X, 0, 2937, 0, 0, 10 },
	{ ABS_MT_POSITION_Y, 0, 1652, 0, 0, 10 },
	{ ABS_MT_TRACKING_ID, 0, 65535, 0, 0, 0 },
	{ ABS_MISC, 0, 0, 0, 0, 0 },
	{ .value = -1 },
};

static struct input_id input_id = {
	.bustype = 0x3,
	.vendor = 0x56a,
	.product = 0x335,
	.version = 0x110,
};

static int events[] = {
	EV_KEY, BTN_TOUCH,
	INPUT_PROP_MAX, INPUT_PROP_DIRECT,
	-1, -1,
};

static const char udev_rule[] =
"ACTION==\"remove\", GOTO=\"rule_end\"\n"
"KERNEL!=\"event*\", GOTO=\"rule_end\"\n"
"\n"
"ATTRS{name}==\"litest Wacom Cintiq 13 HD touch Finger*\",\\\n"
"    ENV{LIBINPUT_DEVICE_GROUP}=\"wacom-13hdt-group\"\n"
"\n"
"LABEL=\"rule_end\"";

struct litest_test_device litest_wacom_cintiq_13hdt_finger_device = {
	.type = LITEST_WACOM_CINTIQ_13HDT_FINGER,
	.features = LITEST_TOUCH,
	.shortname = "wacom-cintiq-13hdt-finger",
	.setup = litest_wacom_cintiq_tablet_setup,
	.interface = &interface,

	.name = "Wacom Cintiq 13 HD touch Finger",
	.id = &input_id,
	.events = events,
	.absinfo = absinfo,
	.udev_rule = udev_rule,
};
