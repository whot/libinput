/*
 * Copyright © 2016 Red Hat, Inc.
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

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdio.h>

#include "litest.h"

static inline void
usage(void)
{
	printf("%s [install-rules|remove-rules]\n", program_invocation_short_name);
}

int
main(int argc, char **argv)
{
	struct list udev_rules;

	list_init(&udev_rules);

	if (argc == 1) {
		if (strstr(argv[0], "litest-setup.test") != NULL) {
			litest_init_udev_rules(&udev_rules);
		} else if (strstr(argv[0], "litest-teardown.test") != NULL) {
			litest_init_udev_rules(&udev_rules);
			litest_remove_udev_rules(&udev_rules);
		} else {
			usage();
			return 1;
		}
	} else if (argc == 1) {
		if (streq(argv[1], "install-rules")) {
			litest_init_udev_rules(&udev_rules);
		} else if (streq(argv[1], "remove-rules")) {
			/* we instal them, then remove them, otherwise we don't have
			 * a correct list. Oh well */
			litest_init_udev_rules(&udev_rules);
			litest_remove_udev_rules(&udev_rules);
		} else {
			usage();
			return 1;
		}
	} else {
		usage();
		return 1;
	}

	return 0;
}
