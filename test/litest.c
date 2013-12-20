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

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ptrace.h>
#include <sys/timerfd.h>
#include <sys/wait.h>

#include "litest.h"
#include "litest-int.h"
#include "litest-synaptics.h"
#include "litest-bcm5974.h"
#include "litest-keyboard.h"
#include "litest-trackpoint.h"
#include "libinput-util.h"

static int in_debugger = -1;

struct test {
	struct list node;
	char *name;
	TCase *tc;
	enum litest_device_type devices;
};

struct suite {
	struct list node;
	struct list tests;
	char *name;
	Suite *suite;
};

static struct litest_device *current_device;

struct litest_device *litest_current_device(void) {
	return current_device;
}

void litest_set_current_device(struct litest_device *device) {
	current_device = device;
}

static void generic_device_teardown(void)
{
	litest_delete_device(current_device);
	current_device = NULL;
}

struct device {
       enum litest_device_type type;
       enum litest_device_feature features;
       const char *shortname;
       void (*setup)(void); /* test fixture, used by check */
       void (*teardown)(void); /* test fixture, used by check */

       void (*create)(struct litest_device *d);
} devices[] = {
	{
		.type = LITEST_SYNAPTICS_CLICKPAD,
		.features = LITEST_TOUCHPAD | LITEST_CLICKPAD | LITEST_BUTTON,
		.shortname = "synaptics",
		.setup = litest_synaptics_clickpad_setup,
		.teardown = generic_device_teardown,
		.create = litest_create_synaptics_clickpad,
	},
	{
		.type = LITEST_BCM5974,
		.features = LITEST_TOUCHPAD | LITEST_CLICKPAD | LITEST_BUTTON,
		.shortname = "bcm5974",
		.setup = litest_bcm5974_setup,
		.teardown = generic_device_teardown,
		.create = litest_create_bcm5974,
	},
	{
		.type = LITEST_KEYBOARD,
		.features = LITEST_KEYBOARD,
		.shortname = "default keyboard",
		.setup = litest_keyboard_setup,
		.teardown = generic_device_teardown,
		.create = litest_create_keyboard,
	},
	{
		.type = LITEST_TRACKPOINT,
		.features = LITEST_POINTER | LITEST_BUTTON,
		.shortname = "trackpoint",
		.setup = litest_trackpoint_setup,
		.teardown = generic_device_teardown,
		.create = litest_create_trackpoint,
	},
	{ LITEST_NO_DEVICE, LITEST_ANY, "no device", NULL, NULL },
};


static struct list all_tests;

static void
litest_add_tcase_for_device(struct suite *suite, void *func, const struct device *dev)
{
	struct test *t;
	const char *test_name = dev->shortname;

	list_for_each(t, &suite->tests, node) {
		if (strcmp(t->name, test_name) != 0)
			continue;

		tcase_add_test(t->tc, func);
		return;
	}

	t = zalloc(sizeof(*t));
	t->name = strdup(test_name);
	t->tc = tcase_create(test_name);
	list_insert(&suite->tests, &t->node);
	tcase_add_checked_fixture(t->tc, dev->setup, dev->teardown);
	tcase_add_test(t->tc, func);
	suite_add_tcase(suite->suite, t->tc);
}

static void
litest_add_tcase(struct suite *suite, void *func,
		 enum litest_device_feature required,
		 enum litest_device_feature excluded)
{
	const struct device *dev = devices;
	if (required != LITEST_ANY || excluded != LITEST_ANY) {
		while (dev->features != 0) {
			if ((dev->features & required) == required &&
			    (dev->features & excluded) == 0)
				litest_add_tcase_for_device(suite, func, dev);
			dev++;
		}
	} else {
		while (dev->type != LITEST_NO_DEVICE) {
			litest_add_tcase_for_device(suite, func, dev);
			dev++;
		}
	}
}

void
litest_add(const char *name, void *func,
	   enum litest_device_feature required,
	   enum litest_device_feature excluded)
{
	struct suite *s;

	if (all_tests.next == NULL && all_tests.prev == NULL)
		list_init(&all_tests);

	list_for_each(s, &all_tests, node) {
		if (strcmp(s->name, name) == 0) {
			litest_add_tcase(s, func, required, excluded);
			return;
		}
	}

	s = zalloc(sizeof(*s));
	s->name = strdup(name);
	s->suite = suite_create(s->name);

	list_init(&s->tests);
	list_insert(&all_tests, &s->node);
	litest_add_tcase(s, func, required, excluded);
}

int is_debugger_attached()
{
	int status;
	int rc;
	int pid = fork();

	if (pid == -1)
		return 0;

	if (pid == 0) {
		int ppid = getppid();
		if (ptrace(PTRACE_ATTACH, ppid, NULL, NULL) == 0) {
			waitpid(ppid, NULL, 0);
			ptrace(PTRACE_CONT, NULL, NULL);
			ptrace(PTRACE_DETACH, ppid, NULL, NULL);
			rc = 0;
		} else
			rc = 1;
		_exit(rc);
	} else {
		waitpid(pid, &status, 0);
		rc = WEXITSTATUS(status);
	}

	return rc;
}


static void
litest_list_tests(struct list *tests)
{
	struct suite *s;

	list_for_each(s, tests, node) {
		struct test *t;
		printf("%s:\n", s->name);
		list_for_each(t, &s->tests, node) {
			printf("	%s\n", t->name);
		}
	}
}

static const struct option opts[] = {
	{ "list", 0, 0, 'l' },
	{ 0, 0, 0, 0}
};

int
litest_run(int argc, char **argv) {
	struct suite *s, *snext;
	int failed;
	SRunner *sr = NULL;

	if (in_debugger == -1) {
		in_debugger = is_debugger_attached();
		if (in_debugger)
			setenv("CK_FORK", "no", 0);
	}

	list_for_each(s, &all_tests, node) {
		if (!sr)
			sr = srunner_create(s->suite);
		else
			srunner_add_suite(sr, s->suite);
	}

	while(1) {
		int c;
		int option_index = 0;

		c = getopt_long(argc, argv, "", opts, &option_index);
		if (c == -1)
			break;
		switch(c) {
			case 'l':
				litest_list_tests(&all_tests);
				return 0;
		}
	}

	srunner_run_all(sr, CK_NORMAL);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	list_for_each_safe(s, snext, &all_tests, node) {
		struct test *t, *tnext;

		list_for_each_safe(t, tnext, &s->tests, node) {
			free(t->name);
			list_remove(&t->node);
			free(t);
		}

		list_remove(&s->node);
		free(s->name);
		free(s);
	}

	return failed;
}

static int
open_restricted(const char *path, int flags, void *userdata)
{
	return open(path, flags);
}

static void
close_restricted(int fd, void *userdata)
{
	close(fd);
}

const struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

struct litest_device *
litest_create_device(enum litest_device_type which)
{
	struct litest_device *d = zalloc(sizeof(*d));
	int fd;
	int rc;
	const char *path;
	struct device *dev;

	ck_assert(d != NULL);

	dev = devices;
	while (dev->type != LITEST_NO_DEVICE) {
		if (dev->type == which) {
			dev->create(d);
			break;
		}
		dev++;
	}

	if (dev->type == LITEST_NO_DEVICE) {
		ck_abort_msg("Invalid device type %d\n", which);
		return NULL;
	}

	path = libevdev_uinput_get_devnode(d->uinput);
	ck_assert(path != NULL);
	fd = open(path, O_RDWR|O_NONBLOCK);
	ck_assert_int_ne(fd, -1);

	rc = libevdev_new_from_fd(fd, &d->evdev);
	ck_assert_int_eq(rc, 0);

	d->libinput = libinput_create_from_path(&interface, NULL, path);
	ck_assert(d->libinput != NULL);

	d->interface->min[ABS_X] = libevdev_get_abs_minimum(d->evdev, ABS_X);
	d->interface->max[ABS_X] = libevdev_get_abs_maximum(d->evdev, ABS_X);
	d->interface->min[ABS_Y] = libevdev_get_abs_minimum(d->evdev, ABS_Y);
	d->interface->max[ABS_Y] = libevdev_get_abs_maximum(d->evdev, ABS_Y);
	return d;
}

int
litest_handle_events(struct litest_device *d)
{
	struct pollfd fd;

	fd.fd = libinput_get_fd(d->libinput);
	fd.events = POLLIN;

	while (poll(&fd, 1, 1))
		libinput_dispatch(d->libinput);

	return 0;
}

void
litest_delete_device(struct litest_device *d)
{
	if (!d)
		return;

	libinput_destroy(d->libinput);
	libevdev_free(d->evdev);
	libevdev_uinput_destroy(d->uinput);
	memset(d,0, sizeof(*d));
	free(d);
}

void
litest_event(struct litest_device *d, unsigned int type, unsigned int code, int value)
{
	libevdev_uinput_write_event(d->uinput, type, code, value);
}

void
litest_touch_down(struct litest_device *d, unsigned int slot, int x, int y)
{
	d->interface->touch_down(d, slot, x, y);
}

void
litest_touch_up(struct litest_device *d, unsigned int slot)
{
	struct input_event *ev;
	struct input_event up[] = {
		{ .type = EV_ABS, .code = ABS_MT_SLOT, .value = slot },
		{ .type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1 },
		{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	};

	ARRAY_FOR_EACH(up, ev)
		litest_event(d, ev->type, ev->code, ev->value);
}

void
litest_touch_move(struct litest_device *d, unsigned int slot, int x, int y)
{
	d->interface->touch_move(d, slot, x, y);
}

void
litest_touch_move_to(struct litest_device *d, unsigned int slot, int x_from, int y_from, int x_to, int y_to, int steps)
{
	for (int i = 0; i < steps - 1; i++)
		litest_touch_move(d, slot, x_from + (x_to - x_from)/steps * i, y_from + (y_to - y_from)/steps * i);
	litest_touch_move(d, slot, x_to, y_to);
}

void
litest_click(struct litest_device *d, unsigned int button, bool is_press)
{

	struct input_event *ev;
	struct input_event click[] = {
		{ .type = EV_KEY, .code = button, .value = is_press ? 1 : 0 },
		{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	};

	ARRAY_FOR_EACH(click, ev)
		litest_event(d, ev->type, ev->code, ev->value);
}

int litest_scale(const struct litest_device *d, unsigned int axis, int val)
{
	ck_assert_int_ge(val, 0);
	ck_assert_int_le(val, 100);
	ck_assert_int_le(axis, ABS_Y);

	return (d->interface->max[axis] - d->interface->min[axis]) * val/100.0 + d->interface->min[axis];
}
