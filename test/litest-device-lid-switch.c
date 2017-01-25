/*
 * Copyright © 2017 James Ye <jye836@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include "litest.h"
#include "litest-int.h"

static void
litest_lid_switch_setup(void)
{
	struct litest_device *d = litest_create_device(LITEST_LID_SWITCH);
	litest_set_current_device(d);
}

static struct input_id input_id = {
	.bustype = 0x19,
	.vendor = 0x0,
	.product = 0x5,
};

static int events[] = {
	EV_SW, SW_LID,
	-1, -1,
};

static const char udev_rule[] =
"ACTION==\"remove\", GOTO=\"switch_end\"\n"
"KERNEL!=\"event*\", GOTO=\"switch_end\"\n"
"\n"
"ATTRS{name}==\"litest Lid Switch*\",\\\n"
"    ENV{ID_INPUT_SWITCH}=\"1\",\\\n"
"    ENV{LIBINPUT_ATTR_LID_SWITCH_RELIABILITY}=\"reliable\"\n"
"\n"
"LABEL=\"switch_end\"";

struct litest_test_device litest_lid_switch_device = {
	.type = LITEST_LID_SWITCH,
	.features = LITEST_SWITCH,
	.shortname = "lid switch",
	.setup = litest_lid_switch_setup,
	.interface = NULL,

	.name = "Lid Switch",
	.id = &input_id,
	.events = events,
	.absinfo = NULL,

	.udev_rule = udev_rule,
};
