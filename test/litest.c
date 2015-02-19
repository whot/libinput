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

#include <assert.h>
#include <check.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "linux/input.h"
#include <sys/ptrace.h>
#include <sys/timerfd.h>
#include <sys/wait.h>

#include "litest.h"
#include "litest-int.h"
#include "libinput-util.h"

#define UDEV_RULES_D "/run/udev/rules.d"
#define UDEV_RULE_PREFIX "99-litest-"

static int in_debugger = -1;
static int verbose = 0;

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

void litest_generic_device_teardown(void)
{
	litest_delete_device(current_device);
	current_device = NULL;
}

extern struct litest_test_device litest_keyboard_device;
extern struct litest_test_device litest_synaptics_clickpad_device;
extern struct litest_test_device litest_synaptics_touchpad_device;
extern struct litest_test_device litest_synaptics_t440_device;
extern struct litest_test_device litest_trackpoint_device;
extern struct litest_test_device litest_bcm5974_device;
extern struct litest_test_device litest_mouse_device;
extern struct litest_test_device litest_wacom_touch_device;
extern struct litest_test_device litest_wacom_bamboo_tablet_device;
extern struct litest_test_device litest_wacom_cintiq_tablet_device;
extern struct litest_test_device litest_wacom_intuos_tablet_device;
extern struct litest_test_device litest_wacom_isdv4_tablet_device;
extern struct litest_test_device litest_alps_device;
extern struct litest_test_device litest_generic_singletouch_device;
extern struct litest_test_device litest_qemu_tablet_device;
extern struct litest_test_device litest_xen_virtual_pointer_device;
extern struct litest_test_device litest_vmware_virtmouse_device;
extern struct litest_test_device litest_synaptics_hover_device;
extern struct litest_test_device litest_synaptics_carbon3rd_device;

struct litest_test_device* devices[] = {
	&litest_synaptics_clickpad_device,
	&litest_synaptics_touchpad_device,
	&litest_synaptics_t440_device,
	&litest_keyboard_device,
	&litest_trackpoint_device,
	&litest_bcm5974_device,
	&litest_mouse_device,
	&litest_wacom_touch_device,
	&litest_wacom_bamboo_tablet_device,
	&litest_wacom_cintiq_tablet_device,
	&litest_wacom_intuos_tablet_device,
	&litest_wacom_isdv4_tablet_device,
	&litest_alps_device,
	&litest_generic_singletouch_device,
	&litest_qemu_tablet_device,
	&litest_xen_virtual_pointer_device,
	&litest_vmware_virtmouse_device,
	&litest_synaptics_hover_device,
	&litest_synaptics_carbon3rd_device,
	NULL,
};

static struct list all_tests;

static void
litest_reload_udev_rules(void)
{
	system("udevadm control --reload-rules");
}

static int
litest_udev_rule_filter(const struct dirent *entry)
{
	return strncmp(entry->d_name,
		       UDEV_RULE_PREFIX,
		       strlen(UDEV_RULE_PREFIX)) == 0;
}

static void
litest_drop_udev_rules(void)
{
	int n;
	int rc;
	struct dirent **entries;
	char path[PATH_MAX];

	n = scandir(UDEV_RULES_D,
		    &entries,
		    litest_udev_rule_filter,
		    alphasort);
	if (n < 0)
		return;

	while (n--) {
		rc = snprintf(path, sizeof(path),
			      "%s/%s",
			      UDEV_RULES_D,
			      entries[n]->d_name);
		if (rc > 0 &&
		    (size_t)rc == strlen(UDEV_RULES_D) +
			    strlen(entries[n]->d_name) + 1)
			unlink(path);
		else
			fprintf(stderr,
				"Failed to delete %s. Remaining tests are unreliable\n",
				entries[n]->d_name);
		free(entries[n]);
	}
	free(entries);

	litest_reload_udev_rules();
}

static void
litest_add_tcase_for_device(struct suite *suite,
			    void *func,
			    const struct litest_test_device *dev)
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
	assert(t != NULL);
	t->name = strdup(test_name);
	t->tc = tcase_create(test_name);
	list_insert(&suite->tests, &t->node);
	/* we can't guarantee that we clean up properly if a test fails, the
	   udev rules used for a previous test may still be in place. Add an
	   unchecked fixture to always clean up all rules before/after a
	   test case completes */
	tcase_add_unchecked_fixture(t->tc,
				    litest_drop_udev_rules,
				    litest_drop_udev_rules);
	tcase_add_checked_fixture(t->tc, dev->setup,
				  dev->teardown ? dev->teardown : litest_generic_device_teardown);
	tcase_add_test(t->tc, func);
	suite_add_tcase(suite->suite, t->tc);
}

static void
litest_add_tcase_no_device(struct suite *suite, void *func)
{
	struct test *t;
	const char *test_name = "no device";

	list_for_each(t, &suite->tests, node) {
		if (strcmp(t->name, test_name) != 0)
			continue;

		tcase_add_test(t->tc, func);
		return;
	}

	t = zalloc(sizeof(*t));
	assert(t != NULL);
	t->name = strdup(test_name);
	t->tc = tcase_create(test_name);
	list_insert(&suite->tests, &t->node);
	tcase_add_test(t->tc, func);
	suite_add_tcase(suite->suite, t->tc);
}

static void
litest_add_tcase(struct suite *suite, void *func,
		 enum litest_device_feature required,
		 enum litest_device_feature excluded)
{
	struct litest_test_device **dev = devices;

	assert(required >= LITEST_DISABLE_DEVICE);
	assert(excluded >= LITEST_DISABLE_DEVICE);

	if (required == LITEST_DISABLE_DEVICE &&
	    excluded == LITEST_DISABLE_DEVICE) {
		litest_add_tcase_no_device(suite, func);
	} else if (required != LITEST_ANY || excluded != LITEST_ANY) {
		while (*dev) {
			if (((*dev)->features & required) == required &&
			    ((*dev)->features & excluded) == 0)
				litest_add_tcase_for_device(suite, func, *dev);
			dev++;
		}
	} else {
		while (*dev) {
			litest_add_tcase_for_device(suite, func, *dev);
			dev++;
		}
	}
}

void
litest_add_no_device(const char *name, void *func)
{
	litest_add(name, func, LITEST_DISABLE_DEVICE, LITEST_DISABLE_DEVICE);
}

static struct suite *
get_suite(const char *name)
{
	struct suite *s;

	if (all_tests.next == NULL && all_tests.prev == NULL)
		list_init(&all_tests);

	list_for_each(s, &all_tests, node) {
		if (strcmp(s->name, name) == 0)
			return s;
	}

	s = zalloc(sizeof(*s));
	assert(s != NULL);
	s->name = strdup(name);
	s->suite = suite_create(s->name);

	list_init(&s->tests);
	list_insert(&all_tests, &s->node);

	return s;
}

void
litest_add(const char *name,
	   void *func,
	   enum litest_device_feature required,
	   enum litest_device_feature excluded)
{
	litest_add_tcase(get_suite(name), func, required, excluded);
}

void
litest_add_for_device(const char *name,
		      void *func,
		      enum litest_device_type type)
{
	struct suite *s;
	struct litest_test_device **dev = devices;

	assert(type < LITEST_NO_DEVICE);

	s = get_suite(name);
	while (*dev) {
		if ((*dev)->type == type) {
			litest_add_tcase_for_device(s, func, *dev);
			return;
		}
		dev++;
	}

	ck_abort_msg("Invalid test device type");
}

static int
is_debugger_attached(void)
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
		} else {
			rc = 1;
		}
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

static void
litest_log_handler(struct libinput *libinput,
		   enum libinput_log_priority pri,
		   const char *format,
		   va_list args)
{
	const char *priority = NULL;

	switch(pri) {
	case LIBINPUT_LOG_PRIORITY_INFO: priority = "info"; break;
	case LIBINPUT_LOG_PRIORITY_ERROR: priority = "error"; break;
	case LIBINPUT_LOG_PRIORITY_DEBUG: priority = "debug"; break;
	}

	fprintf(stderr, "litest %s: ", priority);
	vfprintf(stderr, format, args);
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

struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

static const struct option opts[] = {
	{ "list", 0, 0, 'l' },
	{ "verbose", 0, 0, 'v' },
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
			case 'v':
				verbose = 1;
				break;
			default:
				fprintf(stderr, "usage: %s [--list]\n", argv[0]);
				return 1;

		}
	}

	srunner_run_all(sr, CK_ENV);
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

static struct input_absinfo *
merge_absinfo(const struct input_absinfo *orig,
	      const struct input_absinfo *override)
{
	struct input_absinfo *abs;
	unsigned int nelem, i;
	size_t sz = ABS_MAX + 1;

	if (!orig)
		return NULL;

	abs = calloc(sz, sizeof(*abs));
	ck_assert(abs != NULL);

	nelem = 0;
	while (orig[nelem].value != -1) {
		abs[nelem] = orig[nelem];
		nelem++;
		ck_assert_int_lt(nelem, sz);
	}

	/* just append, if the same axis is present twice, libevdev will
	   only use the last value anyway */
	i = 0;
	while (override && override[i].value != -1) {
		abs[nelem++] = override[i++];
		ck_assert_int_lt(nelem, sz);
	}

	ck_assert_int_lt(nelem, sz);
	abs[nelem].value = -1;

	return abs;
}

static int*
merge_events(const int *orig, const int *override)
{
	int *events;
	unsigned int nelem, i;
	size_t sz = KEY_MAX * 3;

	if (!orig)
		return NULL;

	events = calloc(sz, sizeof(int));
	ck_assert(events != NULL);

	nelem = 0;
	while (orig[nelem] != -1) {
		events[nelem] = orig[nelem];
		nelem++;
		ck_assert_int_lt(nelem, sz);
	}

	/* just append, if the same axis is present twice, libevdev will
	 * ignore the double definition anyway */
	i = 0;
	while (override && override[i] != -1) {
		events[nelem++] = override[i++];
		ck_assert_int_le(nelem, sz);
	}

	ck_assert_int_lt(nelem, sz);
	events[nelem] = -1;

	return events;
}

static char *
litest_init_udev_rules(struct litest_test_device *dev)
{
	int rc;
	FILE *f;
	char *path;

	if (!dev->udev_rule)
		return NULL;

	rc = mkdir(UDEV_RULES_D, 0755);
	if (rc == -1 && errno != EEXIST)
		ck_abort_msg("Failed to create udev rules directory (%s)\n",
			     strerror(errno));

	rc = asprintf(&path,
		      "%s/%s%s.rules",
		      UDEV_RULES_D,
		      UDEV_RULE_PREFIX,
		      dev->shortname);
	ck_assert_int_eq(rc,
			 strlen(UDEV_RULES_D) +
			 strlen(UDEV_RULE_PREFIX) +
			 strlen(dev->shortname) + 7);
	f = fopen(path, "w");
	ck_assert_notnull(f);
	ck_assert_int_ge(fputs(dev->udev_rule, f), 0);
	fclose(f);

	litest_reload_udev_rules();

	return path;
}

static struct litest_device *
litest_create(enum litest_device_type which,
	      const char *name_override,
	      struct input_id *id_override,
	      const struct input_absinfo *abs_override,
	      const int *events_override)
{
	struct litest_device *d = NULL;
	struct litest_test_device **dev;
	const char *name;
	const struct input_id *id;
	struct input_absinfo *abs;
	int *events;
	char *udev_file;

	dev = devices;
	while (*dev) {
		if ((*dev)->type == which)
			break;
		dev++;
	}

	if (!*dev)
		ck_abort_msg("Invalid device type %d\n", which);

	d = zalloc(sizeof(*d));
	ck_assert(d != NULL);

	udev_file = litest_init_udev_rules(*dev);

	/* device has custom create method */
	if ((*dev)->create) {
		(*dev)->create(d);
		if (abs_override || events_override) {
			if (udev_file)
				unlink(udev_file);
			ck_abort_msg("Custom create cannot"
				     "be overridden");
		}

		return d;
	}

	abs = merge_absinfo((*dev)->absinfo, abs_override);
	events = merge_events((*dev)->events, events_override);
	name = name_override ? name_override : (*dev)->name;
	id = id_override ? id_override : (*dev)->id;

	d->uinput = litest_create_uinput_device_from_description(name,
								 id,
								 abs,
								 events);
	d->interface = (*dev)->interface;
	d->udev_rule_file = udev_file;
	free(abs);
	free(events);

	return d;

}

struct libinput *
litest_create_context(void)
{
	struct libinput *libinput =
		libinput_path_create_context(&interface, NULL);
	ck_assert_notnull(libinput);

	libinput_log_set_handler(libinput, litest_log_handler);
	if (verbose)
		libinput_log_set_priority(libinput, LIBINPUT_LOG_PRIORITY_DEBUG);

	return libinput;
}

struct litest_device *
litest_add_device_with_overrides(struct libinput *libinput,
				 enum litest_device_type which,
				 const char *name_override,
				 struct input_id *id_override,
				 const struct input_absinfo *abs_override,
				 const int *events_override)
{
	struct litest_device *d;
	int fd;
	int rc;
	const char *path;

	d = litest_create(which,
			  name_override,
			  id_override,
			  abs_override,
			  events_override);

	path = libevdev_uinput_get_devnode(d->uinput);
	ck_assert(path != NULL);
	fd = open(path, O_RDWR|O_NONBLOCK);
	ck_assert_int_ne(fd, -1);

	rc = libevdev_new_from_fd(fd, &d->evdev);
	ck_assert_int_eq(rc, 0);

	d->libinput = libinput;
	d->libinput_device = libinput_path_add_device(d->libinput, path);
	ck_assert(d->libinput_device != NULL);
	libinput_device_ref(d->libinput_device);

	if (d->interface) {
		d->interface->min[ABS_X] = libevdev_get_abs_minimum(d->evdev, ABS_X);
		d->interface->max[ABS_X] = libevdev_get_abs_maximum(d->evdev, ABS_X);
		d->interface->min[ABS_Y] = libevdev_get_abs_minimum(d->evdev, ABS_Y);
		d->interface->max[ABS_Y] = libevdev_get_abs_maximum(d->evdev, ABS_Y);
	}
	return d;
}

struct litest_device *
litest_add_device(struct libinput *libinput,
		  enum litest_device_type which)
{
	return litest_add_device_with_overrides(libinput,
						which,
						NULL,
						NULL,
						NULL,
						NULL);
}

struct litest_device *
litest_create_device_with_overrides(enum litest_device_type which,
				    const char *name_override,
				    struct input_id *id_override,
				    const struct input_absinfo *abs_override,
				    const int *events_override)
{
	struct litest_device *dev =
		litest_add_device_with_overrides(litest_create_context(),
						 which,
						 name_override,
						 id_override,
						 abs_override,
						 events_override);
	dev->owns_context = true;
	return dev;
}

struct litest_device *
litest_create_device(enum litest_device_type which)
{
	return litest_create_device_with_overrides(which, NULL, NULL, NULL, NULL);
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

	if (d->udev_rule_file) {
		unlink(d->udev_rule_file);
		free(d->udev_rule_file);
		d->udev_rule_file = NULL;
	}

	libinput_device_unref(d->libinput_device);
	libinput_path_remove_device(d->libinput_device);
	if (d->owns_context)
		libinput_unref(d->libinput);
	libevdev_free(d->evdev);
	libevdev_uinput_destroy(d->uinput);
	free(d->private);
	memset(d,0, sizeof(*d));
	free(d);
}

void
litest_event(struct litest_device *d, unsigned int type,
	     unsigned int code, int value)
{
	int ret;

	if (d->skip_ev_syn && type == EV_SYN && code == SYN_REPORT)
		return;

	ret = libevdev_uinput_write_event(d->uinput, type, code, value);
	ck_assert_int_eq(ret, 0);
}

int
litest_auto_assign_value(struct litest_device *d,
			 const struct input_event *ev,
			 int slot, double x, double y)
{
	static int tracking_id;
	int value = ev->value;

	if (value != LITEST_AUTO_ASSIGN || ev->type != EV_ABS)
		return value;

	switch (ev->code) {
	case ABS_X:
	case ABS_MT_POSITION_X:
		value = litest_scale(d, ABS_X, x);
		break;
	case ABS_Y:
	case ABS_MT_POSITION_Y:
		value = litest_scale(d, ABS_Y, y);
		break;
	case ABS_MT_TRACKING_ID:
		value = ++tracking_id;
		break;
	case ABS_MT_SLOT:
		value = slot;
		break;
	}

	return value;
}

static void
send_btntool(struct litest_device *d)
{
	litest_event(d, EV_KEY, BTN_TOUCH, d->ntouches_down != 0);
	litest_event(d, EV_KEY, BTN_TOOL_FINGER, d->ntouches_down == 1);
	litest_event(d, EV_KEY, BTN_TOOL_DOUBLETAP, d->ntouches_down == 2);
	litest_event(d, EV_KEY, BTN_TOOL_TRIPLETAP, d->ntouches_down == 3);
	litest_event(d, EV_KEY, BTN_TOOL_QUADTAP, d->ntouches_down == 4);
	litest_event(d, EV_KEY, BTN_TOOL_QUINTTAP, d->ntouches_down == 5);
}

void
litest_touch_down(struct litest_device *d, unsigned int slot,
		  double x, double y)
{
	struct input_event *ev;

	assert(d->ntouches_down >= 0);
	d->ntouches_down++;

	send_btntool(d);

	if (d->interface->touch_down) {
		d->interface->touch_down(d, slot, x, y);
		return;
	}

	ev = d->interface->touch_down_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		int value = litest_auto_assign_value(d, ev, slot, x, y);
		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_touch_up(struct litest_device *d, unsigned int slot)
{
	struct input_event *ev;
	struct input_event up[] = {
		{ .type = EV_ABS, .code = ABS_MT_SLOT, .value = LITEST_AUTO_ASSIGN },
		{ .type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1 },
		{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
		{ .type = -1, .code = -1 }
	};

	assert(d->ntouches_down > 0);
	d->ntouches_down--;

	send_btntool(d);

	if (d->interface->touch_up) {
		d->interface->touch_up(d, slot);
		return;
	} else if (d->interface->touch_up_events) {
		ev = d->interface->touch_up_events;
	} else
		ev = up;

	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		int value = litest_auto_assign_value(d, ev, slot, 0, 0);
		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_touch_move(struct litest_device *d, unsigned int slot,
		  double x, double y)
{
	struct input_event *ev;

	if (d->interface->touch_move) {
		d->interface->touch_move(d, slot, x, y);
		return;
	}

	ev = d->interface->touch_move_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		int value = litest_auto_assign_value(d, ev, slot, x, y);
		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_touch_move_to(struct litest_device *d,
		     unsigned int slot,
		     double x_from, double y_from,
		     double x_to, double y_to,
		     int steps, int sleep_ms)
{
	for (int i = 0; i < steps - 1; i++) {
		litest_touch_move(d, slot,
				  x_from + (x_to - x_from)/steps * i,
				  y_from + (y_to - y_from)/steps * i);
		if (sleep_ms) {
			libinput_dispatch(d->libinput);
			msleep(sleep_ms);
			libinput_dispatch(d->libinput);
		}
	}
	litest_touch_move(d, slot, x_to, y_to);
}

static int32_t
axis_replacement_value(struct axis_replacement *axes,
		       int32_t evcode)
{
	struct axis_replacement *axis = axes;

	while (axis->evcode != -1) {
		if (axis->evcode == evcode)
			return axis->value;
		axis++;
	}

	return -1;
}

static int
auto_assign_tablet_value(struct litest_device *d,
			 const struct input_event *ev,
			 int x, int y,
			 struct axis_replacement *axes)
{
	int value = ev->value;

	if (value != LITEST_AUTO_ASSIGN || ev->type != EV_ABS)
		return value;

	switch (ev->code) {
	case ABS_X:
		value = litest_scale(d, ABS_X, x);
		break;
	case ABS_Y:
		value = litest_scale(d, ABS_Y, y);
		break;
	default:
		value = axis_replacement_value(axes, ev->code);
		break;
	}

	return value;
}

static int
tablet_ignore_event(const struct input_event *ev, int value)
{
	return value == -1 && (ev->code == ABS_PRESSURE || ev->code == ABS_DISTANCE);
}

void
litest_tablet_proximity_in(struct litest_device *d, int x, int y, struct axis_replacement *axes)
{
	struct input_event *ev;

	ev = d->interface->tablet_proximity_in_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		int value = auto_assign_tablet_value(d, ev, x, y, axes);
		if (!tablet_ignore_event(ev, value))
			litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_tablet_proximity_out(struct litest_device *d)
{
	struct input_event *ev;

	ev = d->interface->tablet_proximity_out_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		int value = auto_assign_tablet_value(d, ev, -1, -1, NULL);
		if (!tablet_ignore_event(ev, value))
			litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_tablet_motion(struct litest_device *d, int x, int y, struct axis_replacement *axes)
{
	struct input_event *ev;

	ev = d->interface->tablet_motion_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		int value = auto_assign_tablet_value(d, ev, x, y, axes);
		if (!tablet_ignore_event(ev, value))
			litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_button_click(struct litest_device *d, unsigned int button, bool is_press)
{

	struct input_event *ev;
	struct input_event click[] = {
		{ .type = EV_KEY, .code = button, .value = is_press ? 1 : 0 },
		{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	};

	ARRAY_FOR_EACH(click, ev)
		litest_event(d, ev->type, ev->code, ev->value);
}

void
litest_button_scroll(struct litest_device *dev,
		     unsigned int button,
		     double dx, double dy)
{
	struct libinput *li = dev->libinput;

	litest_button_click(dev, button, 1);

	libinput_dispatch(li);
	litest_timeout_buttonscroll();
	libinput_dispatch(li);

	litest_event(dev, EV_REL, REL_X, dx);
	litest_event(dev, EV_REL, REL_Y, dy);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	litest_button_click(dev, button, 0);

	libinput_dispatch(li);
}

void
litest_keyboard_key(struct litest_device *d, unsigned int key, bool is_press)
{
	litest_button_click(d, key, is_press);
}

int
litest_scale(const struct litest_device *d, unsigned int axis, double val)
{
	int min, max;
	ck_assert_int_ge(val, 0);
	ck_assert_int_le(val, 100);
	ck_assert_int_le(axis, ABS_Y);

	min = d->interface->min[axis];
	max = d->interface->max[axis];
	return (max - min) * val/100.0 + min;
}

void
litest_wait_for_event(struct libinput *li)
{
	return litest_wait_for_event_of_type(li, -1);
}

void
litest_wait_for_event_of_type(struct libinput *li, ...)
{
	va_list args;
	enum libinput_event_type types[32] = {LIBINPUT_EVENT_NONE};
	size_t ntypes = 0;
	enum libinput_event_type type;

	va_start(args, li);
	type = va_arg(args, int);
	while ((int)type != -1) {
		assert(type > 0);
		assert(ntypes < ARRAY_LENGTH(types));
		types[ntypes++] = type;
		type = va_arg(args, int);
	}
	va_end(args);

	while (1) {
		size_t i;
		struct libinput_event *event;

		while ((type = libinput_next_event_type(li)) == LIBINPUT_EVENT_NONE) {
			msleep(10);
			libinput_dispatch(li);
		}

		/* no event mask means wait for any event */
		if (ntypes == 0)
			return;

		for (i = 0; i < ntypes; i++) {
			if (type == types[i])
				return;
		}

		event = libinput_get_event(li);
		libinput_event_destroy(event);
	}
}

void
litest_drain_events(struct libinput *li)
{
	struct libinput_event *event;

	libinput_dispatch(li);
	while ((event = libinput_get_event(li))) {
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}
}

static const char *
litest_event_type_str(struct libinput_event *event)
{
	const char *str = NULL;

	switch (libinput_event_get_type(event)) {
	case LIBINPUT_EVENT_NONE:
		abort();
	case LIBINPUT_EVENT_DEVICE_ADDED:
		str = "ADDED";
		break;
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		str = "REMOVED";
		break;
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		str = "KEY";
		break;
	case LIBINPUT_EVENT_POINTER_MOTION:
		str = "MOTION";
		break;
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		str = "ABSOLUTE";
		break;
	case LIBINPUT_EVENT_POINTER_BUTTON:
		str = "BUTTON";
		break;
	case LIBINPUT_EVENT_POINTER_AXIS:
		str = "AXIS";
		break;
	case LIBINPUT_EVENT_TOUCH_DOWN:
		str = "TOUCH DOWN";
		break;
	case LIBINPUT_EVENT_TOUCH_UP:
		str = "TOUCH UP";
		break;
	case LIBINPUT_EVENT_TOUCH_MOTION:
		str = "TOUCH MOTION";
		break;
	case LIBINPUT_EVENT_TOUCH_CANCEL:
		str = "TOUCH CANCEL";
		break;
	case LIBINPUT_EVENT_TOUCH_FRAME:
		str = "TOUCH FRAME";
		break;
	case LIBINPUT_EVENT_TABLET_AXIS:
		str = "TABLET AXIS";
		break;
	case LIBINPUT_EVENT_TABLET_PROXIMITY:
		str = "TABLET PROX";
		break;
	case LIBINPUT_EVENT_TABLET_BUTTON:
		str = "TABLET BUTTON";
		break;
	}
	return str;
}

static void
litest_print_event(struct libinput_event *event)
{
	struct libinput_event_pointer *p;
	struct libinput_device *dev;
	enum libinput_event_type type;
	double x, y;

	dev = libinput_event_get_device(event);
	type = libinput_event_get_type(event);

	fprintf(stderr,
		"device %s type %s ",
		libinput_device_get_sysname(dev),
		litest_event_type_str(event));
	switch (type) {
	case LIBINPUT_EVENT_POINTER_MOTION:
		p = libinput_event_get_pointer_event(event);
		x = libinput_event_pointer_get_dx(p);
		y = libinput_event_pointer_get_dy(p);
		fprintf(stderr, "%.2f/%.2f", x, y);
		break;
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		p = libinput_event_get_pointer_event(event);
		x = libinput_event_pointer_get_absolute_x(p);
		y = libinput_event_pointer_get_absolute_y(p);
		fprintf(stderr, "%.2f/%.2f", x, y);
		break;
	case LIBINPUT_EVENT_POINTER_BUTTON:
		p = libinput_event_get_pointer_event(event);
		fprintf(stderr,
			"button %d state %d",
			libinput_event_pointer_get_button(p),
			libinput_event_pointer_get_button_state(p));
		break;
	case LIBINPUT_EVENT_POINTER_AXIS:
		p = libinput_event_get_pointer_event(event);
		fprintf(stderr,
			"vert %.f horiz %.2f",
			libinput_event_pointer_get_axis_value(p,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL),
			libinput_event_pointer_get_axis_value(p,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL));
		break;
	default:
		break;
	}

	fprintf(stderr, "\n");
}

void
litest_assert_empty_queue(struct libinput *li)
{
	bool empty_queue = true;
	struct libinput_event *event;

	libinput_dispatch(li);
	while ((event = libinput_get_event(li))) {
		empty_queue = false;
		fprintf(stderr,
			"Unexpected event: ");
		litest_print_event(event);
		libinput_event_destroy(event);
		libinput_dispatch(li);
	}

	ck_assert(empty_queue);
}

struct libevdev_uinput *
litest_create_uinput_device_from_description(const char *name,
					     const struct input_id *id,
					     const struct input_absinfo *abs_info,
					     const int *events)
{
	struct libevdev_uinput *uinput;
	struct libevdev *dev;
	int type, code;
	int rc, fd;
	const struct input_absinfo *abs;
	const struct input_absinfo default_abs = {
		.value = 0,
		.minimum = 0,
		.maximum = 0xffff,
		.fuzz = 0,
		.flat = 0,
		.resolution = 100
	};
	char buf[512];
	const char *devnode;

	dev = libevdev_new();
	ck_assert(dev != NULL);

	snprintf(buf, sizeof(buf), "litest %s", name);
	libevdev_set_name(dev, buf);
	if (id) {
		libevdev_set_id_bustype(dev, id->bustype);
		libevdev_set_id_vendor(dev, id->vendor);
		libevdev_set_id_product(dev, id->product);
		libevdev_set_id_version(dev, id->version);
	}

	abs = abs_info;
	while (abs && abs->value != -1) {
		rc = libevdev_enable_event_code(dev, EV_ABS,
						abs->value, abs);
		ck_assert_int_eq(rc, 0);
		abs++;
	}

	while (events &&
	       (type = *events++) != -1 &&
	       (code = *events++) != -1) {
		if (type == INPUT_PROP_MAX) {
			rc = libevdev_enable_property(dev, code);
		} else {
			rc = libevdev_enable_event_code(dev, type, code,
							type == EV_ABS ? &default_abs : NULL);
		}
		ck_assert_int_eq(rc, 0);
	}

	rc = libevdev_uinput_create_from_device(dev,
					        LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput);
	/* workaround for a bug in libevdev pre-1.3
	   http://cgit.freedesktop.org/libevdev/commit/?id=debe9b030c8069cdf78307888ef3b65830b25122 */
	if (rc == -EBADF)
		rc = -EACCES;
	ck_assert_msg(rc == 0, "Failed to create uinput device: %s", strerror(-rc));

	libevdev_free(dev);

	/* uinput does not yet support setting the resolution, so we set it
	 * afterwards. This is of course racy as hell but the way we
	 * _generally_ use this function by the time libinput uses the
	 * device, we're finished here */

	devnode = libevdev_uinput_get_devnode(uinput);
	ck_assert_notnull(devnode);
	fd = open(devnode, O_RDONLY);
	ck_assert_int_gt(fd, -1);
	rc = libevdev_new_from_fd(fd, &dev);
	ck_assert_int_eq(rc, 0);

	abs = abs_info;
	while (abs && abs->value != -1) {
		if (abs->resolution != 0) {
			rc = libevdev_kernel_set_abs_info(dev,
							  abs->value,
							  abs);
			ck_assert_int_eq(rc, 0);
		}
		abs++;
	}
	close(fd);
	libevdev_free(dev);

	return uinput;
}

static struct libevdev_uinput *
litest_create_uinput_abs_device_v(const char *name,
				  struct input_id *id,
				  const struct input_absinfo *abs,
				  va_list args)
{
	int events[KEY_MAX * 2 + 2]; /* increase this if not sufficient */
	int *event = events;
	int type, code;

	while ((type = va_arg(args, int)) != -1 &&
	       (code = va_arg(args, int)) != -1) {
		*event++ = type;
		*event++ = code;
		ck_assert(event < &events[ARRAY_LENGTH(events) - 2]);
	}

	*event++ = -1;
	*event++ = -1;

	return litest_create_uinput_device_from_description(name, id,
							    abs, events);
}

struct libevdev_uinput *
litest_create_uinput_abs_device(const char *name,
				struct input_id *id,
				const struct input_absinfo *abs,
				...)
{
	struct libevdev_uinput *uinput;
	va_list args;

	va_start(args, abs);
	uinput = litest_create_uinput_abs_device_v(name, id, abs, args);
	va_end(args);

	return uinput;
}

struct libevdev_uinput *
litest_create_uinput_device(const char *name, struct input_id *id, ...)
{
	struct libevdev_uinput *uinput;
	va_list args;

	va_start(args, id);
	uinput = litest_create_uinput_abs_device_v(name, id, NULL, args);
	va_end(args);

	return uinput;
}

void
litest_assert_button_event(struct libinput *li, unsigned int button,
			   enum libinput_button_state state)
{
	struct libinput_event *event;
	struct libinput_event_pointer *ptrev;

	litest_wait_for_event(li);
	event = libinput_get_event(li);

	ck_assert(event != NULL);
	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_POINTER_BUTTON);
	ptrev = libinput_event_get_pointer_event(event);
	ck_assert_int_eq(libinput_event_pointer_get_button(ptrev),
			 button);
	ck_assert_int_eq(libinput_event_pointer_get_button_state(ptrev),
			 state);
	libinput_event_destroy(event);
}

void
litest_assert_tablet_button_event(struct libinput *li, unsigned int button,
				  enum libinput_button_state state)
{
	struct libinput_event *event;
	struct libinput_event_tablet *tev;

	litest_wait_for_event(li);
	event = libinput_get_event(li);

	ck_assert(event != NULL);
	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_TABLET_BUTTON);
	tev = libinput_event_get_tablet_event(event);
	ck_assert_int_eq(libinput_event_tablet_get_button(tev),
			 button);
	ck_assert_int_eq(libinput_event_tablet_get_button_state(tev),
			 state);
	libinput_event_destroy(event);
}

void
litest_assert_scroll(struct libinput *li,
		     enum libinput_pointer_axis axis,
		     int minimum_movement)
{
	struct libinput_event *event, *next_event;
	struct libinput_event_pointer *ptrev;

	event = libinput_get_event(li);
	next_event = libinput_get_event(li);
	ck_assert(next_event != NULL); /* At least 1 scroll + stop scroll */

	while (event) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_POINTER_AXIS);
		ptrev = libinput_event_get_pointer_event(event);
		ck_assert(ptrev != NULL);

		if (next_event) {
			/* Normal scroll event, check dir */
			if (minimum_movement > 0) {
				ck_assert_int_ge(
					libinput_event_pointer_get_axis_value(ptrev,
									      axis),
					minimum_movement);
			} else {
				ck_assert_int_le(
					libinput_event_pointer_get_axis_value(ptrev,
									      axis),
					minimum_movement);
			}
		} else {
			/* Last scroll event, must be 0 */
			ck_assert_int_eq(
				libinput_event_pointer_get_axis_value(ptrev, axis),
				0);
		}
		libinput_event_destroy(event);
		event = next_event;
		next_event = libinput_get_event(li);
	}
}

void
litest_assert_only_typed_events(struct libinput *li,
				enum libinput_event_type type)
{
	struct libinput_event *event;

	assert(type != LIBINPUT_EVENT_NONE);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert_notnull(event);

	while (event) {
		ck_assert_int_eq(libinput_event_get_type(event),
				 type);
		libinput_event_destroy(event);
		libinput_dispatch(li);
		event = libinput_get_event(li);
	}
}

void
litest_timeout_tap(void)
{
	msleep(200);
}

void
litest_timeout_softbuttons(void)
{
	msleep(300);
}

void
litest_timeout_buttonscroll(void)
{
	msleep(300);
}

void
litest_push_event_frame(struct litest_device *dev)
{
	assert(!dev->skip_ev_syn);
	dev->skip_ev_syn = true;
}

void
litest_pop_event_frame(struct litest_device *dev)
{
	assert(dev->skip_ev_syn);
	dev->skip_ev_syn = false;
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
}

static void
send_abs_xy(struct litest_device *d, double x, double y)
{
	struct input_event e;
	int val;

	e.type = EV_ABS;
	e.code = ABS_X;
	e.value = LITEST_AUTO_ASSIGN;
	val = litest_auto_assign_value(d, &e, 0, x, y);
	litest_event(d, EV_ABS, ABS_X, val);

	e.code = ABS_Y;
	val = litest_auto_assign_value(d, &e, 0, x, y);
	litest_event(d, EV_ABS, ABS_Y, val);
}

static void
send_abs_mt_xy(struct litest_device *d, double x, double y)
{
	struct input_event e;
	int val;

	e.type = EV_ABS;
	e.code = ABS_MT_POSITION_X;
	e.value = LITEST_AUTO_ASSIGN;
	val = litest_auto_assign_value(d, &e, 0, x, y);
	litest_event(d, EV_ABS, ABS_MT_POSITION_X, val);

	e.code = ABS_MT_POSITION_Y;
	e.value = LITEST_AUTO_ASSIGN;
	val = litest_auto_assign_value(d, &e, 0, x, y);
	litest_event(d, EV_ABS, ABS_MT_POSITION_Y, val);
}

void
litest_semi_mt_touch_down(struct litest_device *d,
			  struct litest_semi_mt *semi_mt,
			  unsigned int slot,
			  double x, double y)
{
	double t, l, r = 0, b = 0; /* top, left, right, bottom */

	if (d->ntouches_down > 2 || slot > 1)
		return;

	if (d->ntouches_down == 1) {
		l = x;
		t = y;
	} else {
		int other = (slot + 1) % 2;
		l = min(x, semi_mt->touches[other].x);
		t = min(y, semi_mt->touches[other].y);
		r = max(x, semi_mt->touches[other].x);
		b = max(y, semi_mt->touches[other].y);
	}

	send_abs_xy(d, l, t);

	litest_event(d, EV_ABS, ABS_MT_SLOT, 0);

	if (d->ntouches_down == 1)
		litest_event(d, EV_ABS, ABS_MT_TRACKING_ID, ++semi_mt->tracking_id);

	send_abs_mt_xy(d, l, t);

	if (d->ntouches_down == 2) {
		litest_event(d, EV_ABS, ABS_MT_SLOT, 1);
		litest_event(d, EV_ABS, ABS_MT_TRACKING_ID, ++semi_mt->tracking_id);

		send_abs_mt_xy(d, r, b);
	}

	litest_event(d, EV_SYN, SYN_REPORT, 0);

	semi_mt->touches[slot].x = x;
	semi_mt->touches[slot].y = y;
}

void
litest_semi_mt_touch_move(struct litest_device *d,
			  struct litest_semi_mt *semi_mt,
			  unsigned int slot,
			  double x, double y)
{
	double t, l, r = 0, b = 0; /* top, left, right, bottom */

	if (d->ntouches_down > 2 || slot > 1)
		return;

	if (d->ntouches_down == 1) {
		l = x;
		t = y;
	} else {
		int other = (slot + 1) % 2;
		l = min(x, semi_mt->touches[other].x);
		t = min(y, semi_mt->touches[other].y);
		r = max(x, semi_mt->touches[other].x);
		b = max(y, semi_mt->touches[other].y);
	}

	send_abs_xy(d, l, t);

	litest_event(d, EV_ABS, ABS_MT_SLOT, 0);
	send_abs_mt_xy(d, l, t);

	if (d->ntouches_down == 2) {
		litest_event(d, EV_ABS, ABS_MT_SLOT, 1);
		send_abs_mt_xy(d, r, b);
	}

	litest_event(d, EV_SYN, SYN_REPORT, 0);

	semi_mt->touches[slot].x = x;
	semi_mt->touches[slot].y = y;
}

void
litest_semi_mt_touch_up(struct litest_device *d,
			struct litest_semi_mt *semi_mt,
			unsigned int slot)
{
	/* note: ntouches_down is decreased before we get here */
	if (d->ntouches_down >= 2 || slot > 1)
		return;

	litest_event(d, EV_ABS, ABS_MT_SLOT, d->ntouches_down);
	litest_event(d, EV_ABS, ABS_MT_TRACKING_ID, -1);

	/* if we have one finger left, send x/y coords for that finger left.
	   this is likely to happen with a real touchpad */
	if (d->ntouches_down == 1) {
		int other = (slot + 1) % 2;
		send_abs_xy(d, semi_mt->touches[other].x, semi_mt->touches[other].y);
		litest_event(d, EV_ABS, ABS_MT_SLOT, 0);
		send_abs_mt_xy(d, semi_mt->touches[other].x, semi_mt->touches[other].y);
	}

	litest_event(d, EV_SYN, SYN_REPORT, 0);
}
