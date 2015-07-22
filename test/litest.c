/*
 * Copyright © 2013 Red Hat, Inc.
 * Copyright © 2013 Marcin Slusarz <marcin.slusarz@gmail.com>
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

#include <assert.h>
#include <check.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "linux/input.h"
#include <sys/ptrace.h>
#include <sys/sendfile.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <libudev.h>

#include "litest.h"
#include "litest-int.h"
#include "libinput-util.h"

#define UDEV_RULES_D "/run/udev/rules.d"
#define UDEV_RULE_PREFIX "99-litest-"
#define UDEV_HWDB_D "/etc/udev/hwdb.d"
#define UDEV_COMMON_RULE_FILE UDEV_RULES_D "/91-litest-model-quirks-REMOVEME.rules"
#define UDEV_COMMON_HWDB_FILE UDEV_HWDB_D "/91-litest-model-quirks-REMOVEME.hwdb"

static int in_debugger = -1;
static int verbose = 0;
const char *filter_test = NULL;
const char *filter_device = NULL;
const char *filter_group = NULL;

static inline void litest_remove_model_quirks(void);

/* defined for the litest selftest */
#ifndef LITEST_DISABLE_BACKTRACE_LOGGING
#define litest_log(...) fprintf(stderr, __VA_ARGS__)
#define litest_vlog(format_, args_) vfprintf(stderr, format_, args_)
#else
#define litest_log(...) { /* __VA_ARGS__ */ }
#define litest_vlog(...) { /* __VA_ARGS__ */ }
#endif

#ifdef HAVE_LIBUNWIND
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <dlfcn.h>

static char cwd[PATH_MAX];

static bool
litest_backtrace_get_lineno(const char *executable,
			    unw_word_t addr,
			    char *file_return,
			    int *line_return)
{
#if HAVE_ADDR2LINE
	FILE* f;
	char buffer[PATH_MAX];
	char *s;
	unsigned int i;

	if (!cwd[0]) {
		if (getcwd(cwd, sizeof(cwd)) == NULL)
			cwd[0] = 0; /* contents otherwise undefined. */
	}

	sprintf (buffer,
		 ADDR2LINE " -C -e %s -i %lx",
		 executable,
		 (unsigned long) addr);

	f = popen(buffer, "r");
	if (f == NULL) {
		litest_log("Failed to execute: %s\n", buffer);
		return false;
	}

	buffer[0] = '?';
	if (fgets(buffer, sizeof(buffer), f) == NULL) {
		pclose(f);
		return false;
	}
	pclose(f);

	if (buffer[0] == '?')
		return false;

	s = strrchr(buffer, ':');
	if (!s)
		return false;

	*s = '\0';
	s++;
	sscanf(s, "%d", line_return);

	/* now strip cwd from buffer */
	s = buffer;
	i = 0;
	while(i < strlen(cwd) && *s != '\0' && cwd[i] == *s) {
		*s = '\0';
		s++;
		i++;
	}

	if (i > 0)
		*(--s) = '.';
	strcpy(file_return, s);

	return true;
#else /* HAVE_ADDR2LINE */
	return false;
#endif
}

static void
litest_backtrace(void)
{
	unw_cursor_t cursor;
	unw_context_t context;
	unw_word_t off;
	unw_proc_info_t pip;
	int ret;
	char procname[256];
	Dl_info dlinfo;
	/* filename and i are unused ifdef LITEST_SHUTUP */
	const char *filename __attribute__((unused));
	int i __attribute__((unused)) = 0;

	pip.unwind_info = NULL;
	ret = unw_getcontext(&context);
	if (ret) {
		litest_log("unw_getcontext failed: %s [%d]\n",
			   unw_strerror(ret),
			   ret);
		return;
	}

	ret = unw_init_local(&cursor, &context);
	if (ret) {
		litest_log("unw_init_local failed: %s [%d]\n",
			   unw_strerror(ret),
			   ret);
		return;
	}

	litest_log("\nBacktrace:\n");
	ret = unw_step(&cursor);
	while (ret > 0) {
		char file[PATH_MAX];
		int line;
		bool have_lineno = false;

		ret = unw_get_proc_info(&cursor, &pip);
		if (ret) {
			litest_log("unw_get_proc_info failed: %s [%d]\n",
				   unw_strerror(ret),
				   ret);
			break;
		}

		ret = unw_get_proc_name(&cursor, procname, 256, &off);
		if (ret && ret != -UNW_ENOMEM) {
			if (ret != -UNW_EUNSPEC)
				litest_log("unw_get_proc_name failed: %s [%d]\n",
					   unw_strerror(ret),
					   ret);
			procname[0] = '?';
			procname[1] = 0;
		}

		if (dladdr((void *)(pip.start_ip + off), &dlinfo) &&
		    dlinfo.dli_fname &&
		    *dlinfo.dli_fname) {
			filename = dlinfo.dli_fname;
			have_lineno = litest_backtrace_get_lineno(filename,
								  (pip.start_ip + off),
								  file,
								  &line);
		} else {
			filename = "?";
		}

		if (have_lineno) {
			litest_log("%d: %s() (%s:%d)\n",
				   i,
				   procname,
				   file,
				   line);
		} else  {
			litest_log("%d: %s (%s%s+%#x) [%p]\n",
				   i,
				   filename,
				   procname,
				   ret == -UNW_ENOMEM ? "..." : "",
				   (int)off,
				   (void *)(pip.start_ip + off));
		}

		i++;
		ret = unw_step(&cursor);
		if (ret < 0)
			litest_log("unw_step failed: %s [%d]\n",
				   unw_strerror(ret),
				   ret);
	}
	litest_log("\n");
}
#else /* HAVE_LIBUNWIND */
static inline void
litest_backtrace(void)
{
	/* thou shall install libunwind */
}
#endif

void
litest_fail_condition(const char *file,
		      int line,
		      const char *func,
		      const char *condition,
		      const char *message,
		      ...)
{
	litest_log("FAILED: %s\n", condition);

	if (message) {
		va_list args;
		va_start(args, message);
		litest_vlog(message, args);
		va_end(args);
	}

	litest_log("in %s() (%s:%d)\n", func, file, line);
	litest_backtrace();
	abort();
}

void
litest_fail_comparison_int(const char *file,
			   int line,
			   const char *func,
			   const char *operator,
			   int a,
			   int b,
			   const char *astr,
			   const char *bstr)
{
	litest_log("FAILED COMPARISON: %s %s %s\n", astr, operator, bstr);
	litest_log("Resolved to: %d %s %d\n", a, operator, b);
	litest_log("in %s() (%s:%d)\n", func, file, line);
	litest_backtrace();
	abort();
}

void
litest_fail_comparison_ptr(const char *file,
			   int line,
			   const char *func,
			   const char *comparison)
{
	litest_log("FAILED COMPARISON: %s\n", comparison);
	litest_log("in %s() (%s:%d)\n", func, file, line);
	litest_backtrace();
	abort();
}

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

struct litest_device *litest_current_device(void)
{
	return current_device;
}

void litest_set_current_device(struct litest_device *device)
{
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
extern struct litest_test_device litest_alps_device;
extern struct litest_test_device litest_generic_singletouch_device;
extern struct litest_test_device litest_qemu_tablet_device;
extern struct litest_test_device litest_xen_virtual_pointer_device;
extern struct litest_test_device litest_vmware_virtmouse_device;
extern struct litest_test_device litest_synaptics_hover_device;
extern struct litest_test_device litest_synaptics_carbon3rd_device;
extern struct litest_test_device litest_protocol_a_screen;
extern struct litest_test_device litest_wacom_finger_device;
extern struct litest_test_device litest_keyboard_blackwidow_device;
extern struct litest_test_device litest_wheel_only_device;
extern struct litest_test_device litest_mouse_roccat_device;
extern struct litest_test_device litest_ms_surface_cover_device;
extern struct litest_test_device litest_logitech_trackball_device;
extern struct litest_test_device litest_atmel_hover_device;
extern struct litest_test_device litest_alps_dualpoint_device;
extern struct litest_test_device litest_mouse_low_dpi_device;
extern struct litest_test_device litest_generic_multitouch_screen_device;
extern struct litest_test_device litest_nexus4_device;
extern struct litest_test_device litest_magicpad_device;
extern struct litest_test_device litest_elantech_touchpad_device;

struct litest_test_device* devices[] = {
	&litest_synaptics_clickpad_device,
	&litest_synaptics_touchpad_device,
	&litest_synaptics_t440_device,
	&litest_keyboard_device,
	&litest_trackpoint_device,
	&litest_bcm5974_device,
	&litest_mouse_device,
	&litest_wacom_touch_device,
	&litest_alps_device,
	&litest_generic_singletouch_device,
	&litest_qemu_tablet_device,
	&litest_xen_virtual_pointer_device,
	&litest_vmware_virtmouse_device,
	&litest_synaptics_hover_device,
	&litest_synaptics_carbon3rd_device,
	&litest_protocol_a_screen,
	&litest_wacom_finger_device,
	&litest_keyboard_blackwidow_device,
	&litest_wheel_only_device,
	&litest_mouse_roccat_device,
	&litest_ms_surface_cover_device,
	&litest_logitech_trackball_device,
	&litest_atmel_hover_device,
	&litest_alps_dualpoint_device,
	&litest_mouse_low_dpi_device,
	&litest_generic_multitouch_screen_device,
	&litest_nexus4_device,
	&litest_magicpad_device,
	&litest_elantech_touchpad_device,
	NULL,
};

static struct list all_tests;

static inline void
litest_system(const char *command)
{
	int ret;

	ret = system(command);

	if (ret == -1) {
		litest_abort_msg("Failed to execute: %s", command);
	} else if (WIFEXITED(ret)) {
		if (WEXITSTATUS(ret))
			litest_abort_msg("'%s' failed with %d",
					 command,
					 WEXITSTATUS(ret));
	} else if (WIFSIGNALED(ret)) {
		litest_abort_msg("'%s' terminated with signal %d",
				 command,
				 WTERMSIG(ret));
	}
}

static void
litest_reload_udev_rules(void)
{
	litest_system("udevadm control --reload-rules");
	litest_system("udevadm hwdb --update");
}

static int
litest_udev_rule_filter(const struct dirent *entry)
{
	return strneq(entry->d_name,
		      UDEV_RULE_PREFIX,
		      strlen(UDEV_RULE_PREFIX));
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

	litest_remove_model_quirks();
	litest_reload_udev_rules();
}

static void
litest_add_tcase_for_device(struct suite *suite,
			    const char *funcname,
			    void *func,
			    const struct litest_test_device *dev,
			    const struct range *range)
{
	struct test *t;
	const char *test_name = dev->shortname;

	list_for_each(t, &suite->tests, node) {
		if (!streq(t->name, test_name))
			continue;

		if (range)
			tcase_add_loop_test(t->tc,
					    func,
					    range->lower,
					    range->upper);
		else
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
	if (range)
		tcase_add_loop_test(t->tc,
				    func,
				    range->lower,
				    range->upper);
	else
		tcase_add_test(t->tc, func);
	suite_add_tcase(suite->suite, t->tc);
}

static void
litest_add_tcase_no_device(struct suite *suite,
			   void *func,
			   const struct range *range)
{
	struct test *t;
	const char *test_name = "no device";

	if (filter_device &&
	    fnmatch(filter_device, test_name, 0) != 0)
		return;

	list_for_each(t, &suite->tests, node) {
		if (!streq(t->name, test_name))
			continue;

		if (range)
			tcase_add_loop_test(t->tc, func, range->lower, range->upper);
		else
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

static struct suite *
get_suite(const char *name)
{
	struct suite *s;

	if (all_tests.next == NULL && all_tests.prev == NULL)
		list_init(&all_tests);

	list_for_each(s, &all_tests, node) {
		if (streq(s->name, name))
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

static void
litest_add_tcase(const char *suite_name,
		 const char *funcname,
		 void *func,
		 enum litest_device_feature required,
		 enum litest_device_feature excluded,
		 const struct range *range)
{
	struct litest_test_device **dev = devices;
	struct suite *suite;

	assert(required >= LITEST_DISABLE_DEVICE);
	assert(excluded >= LITEST_DISABLE_DEVICE);

	if (filter_test &&
	    fnmatch(filter_test, funcname, 0) != 0)
		return;

	if (filter_group &&
	    fnmatch(filter_group, suite_name, 0) != 0)
		return;

	suite = get_suite(suite_name);

	if (required == LITEST_DISABLE_DEVICE &&
	    excluded == LITEST_DISABLE_DEVICE) {
		litest_add_tcase_no_device(suite, func, range);
	} else if (required != LITEST_ANY || excluded != LITEST_ANY) {
		for (; *dev; dev++) {
			if (filter_device &&
			    fnmatch(filter_device, (*dev)->shortname, 0) != 0)
				continue;
			if (((*dev)->features & required) != required ||
			    ((*dev)->features & excluded) != 0)
				continue;

			litest_add_tcase_for_device(suite,
						    funcname,
						    func,
						    *dev,
						    range);
		}
	} else {
		for (; *dev; dev++) {
			if (filter_device &&
			    fnmatch(filter_device, (*dev)->shortname, 0) != 0)
				continue;

			litest_add_tcase_for_device(suite,
						    funcname,
						    func,
						    *dev,
						    range);
		}
	}
}

void
_litest_add_no_device(const char *name, const char *funcname, void *func)
{
	_litest_add(name, funcname, func, LITEST_DISABLE_DEVICE, LITEST_DISABLE_DEVICE);
}

void
_litest_add_ranged_no_device(const char *name,
			     const char *funcname,
			     void *func,
			     const struct range *range)
{
	_litest_add_ranged(name,
			   funcname,
			   func,
			   LITEST_DISABLE_DEVICE,
			   LITEST_DISABLE_DEVICE,
			   range);
}

void
_litest_add(const char *name,
	    const char *funcname,
	    void *func,
	    enum litest_device_feature required,
	    enum litest_device_feature excluded)
{
	_litest_add_ranged(name,
			   funcname,
			   func,
			   required,
			   excluded,
			   NULL);
}

void
_litest_add_ranged(const char *name,
		   const char *funcname,
		   void *func,
		   enum litest_device_feature required,
		   enum litest_device_feature excluded,
		   const struct range *range)
{
	litest_add_tcase(name, funcname, func, required, excluded, range);
}

void
_litest_add_for_device(const char *name,
		       const char *funcname,
		       void *func,
		       enum litest_device_type type)
{
	_litest_add_ranged_for_device(name, funcname, func, type, NULL);
}

void
_litest_add_ranged_for_device(const char *name,
			      const char *funcname,
			      void *func,
			      enum litest_device_type type,
			      const struct range *range)
{
	struct suite *s;
	struct litest_test_device **dev = devices;
	bool device_filtered = false;

	assert(type < LITEST_NO_DEVICE);

	if (filter_test &&
	    fnmatch(filter_test, funcname, 0) != 0)
		return;

	if (filter_group &&
	    fnmatch(filter_group, name, 0) != 0)
		return;

	s = get_suite(name);
	for (; *dev; dev++) {
		if (filter_device &&
		    fnmatch(filter_device, (*dev)->shortname, 0) != 0) {
			device_filtered = true;
			continue;
		}

		if ((*dev)->type == type) {
			litest_add_tcase_for_device(s,
						    funcname,
						    func,
						    *dev,
						    range);
			return;
		}
	}

	/* only abort if no filter was set, that's a bug */
	if (!device_filtered)
		litest_abort_msg("Invalid test device type");
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
	default:
		  abort();
	}

	fprintf(stderr, "litest %s: ", priority);
	vfprintf(stderr, format, args);
}

static int
open_restricted(const char *path, int flags, void *userdata)
{
	int fd = open(path, flags);
	return fd < 0 ? -errno : fd;
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

static inline int
litest_run(int argc, char **argv)
{
	struct suite *s, *snext;
	int failed;
	SRunner *sr = NULL;

	if (list_empty(&all_tests)) {
		fprintf(stderr,
			"Error: filters are too strict, no tests to run.\n");
		return 1;
	}

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

	if (getenv("LITEST_VERBOSE"))
		verbose = 1;

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
	litest_assert(abs != NULL);

	nelem = 0;
	while (orig[nelem].value != -1) {
		abs[nelem] = orig[nelem];
		nelem++;
		litest_assert_int_lt(nelem, sz);
	}

	/* just append, if the same axis is present twice, libevdev will
	   only use the last value anyway */
	i = 0;
	while (override && override[i].value != -1) {
		abs[nelem++] = override[i++];
		litest_assert_int_lt(nelem, sz);
	}

	litest_assert_int_lt(nelem, sz);
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
	litest_assert(events != NULL);

	nelem = 0;
	while (orig[nelem] != -1) {
		events[nelem] = orig[nelem];
		nelem++;
		litest_assert_int_lt(nelem, sz);
	}

	/* just append, if the same axis is present twice, libevdev will
	 * ignore the double definition anyway */
	i = 0;
	while (override && override[i] != -1) {
		events[nelem++] = override[i++];
		litest_assert_int_le(nelem, sz);
	}

	litest_assert_int_lt(nelem, sz);
	events[nelem] = -1;

	return events;
}

static inline void
litest_copy_file(const char *dest, const char *src, const char *header)
{
	int in, out;

	out = open(dest, O_CREAT|O_WRONLY, 0644);
	litest_assert_int_gt(out, -1);

	if (header)
		write(out, header, strlen(header));

	in = open(src, O_RDONLY);
	litest_assert_int_gt(in, -1);
	/* lazy, just check for error and empty file copy */
	litest_assert_int_gt(sendfile(out, in, NULL, 4096), 0);
	close(out);
	close(in);
}

static inline void
litest_install_model_quirks(void)
{
	const char *warning =
			 "#################################################################\n"
			 "# WARNING: REMOVE THIS FILE\n"
			 "# This is a run-time file for the libinput test suite and\n"
			 "# should be removed on exit. If the test-suite is not currently \n"
			 "# running, remove this file and update your hwdb: \n"
			 "#       sudo udevadm hwdb --update\n"
			 "#################################################################\n\n";
	litest_copy_file(UDEV_COMMON_RULE_FILE,
			 LIBINPUT_UDEV_RULES_FILE,
			 warning);
	litest_copy_file(UDEV_COMMON_HWDB_FILE,
			 LIBINPUT_UDEV_HWDB_FILE,
			 warning);
}

static inline void
litest_remove_model_quirks(void)
{
	unlink(UDEV_COMMON_RULE_FILE);
	unlink(UDEV_COMMON_HWDB_FILE);
}

static char *
litest_init_udev_rules(struct litest_test_device *dev)
{
	int rc;
	FILE *f;
	char *path = NULL;

	rc = mkdir(UDEV_RULES_D, 0755);
	if (rc == -1 && errno != EEXIST)
		ck_abort_msg("Failed to create udev rules directory (%s)\n",
			     strerror(errno));

	rc = mkdir(UDEV_HWDB_D, 0755);
	if (rc == -1 && errno != EEXIST)
		ck_abort_msg("Failed to create udev hwdb directory (%s)\n",
			     strerror(errno));

	litest_install_model_quirks();

	/* device-specific udev rules */
	if (!dev->udev_rule)
		goto out;

	rc = xasprintf(&path,
		      "%s/%s%s.rules",
		      UDEV_RULES_D,
		      UDEV_RULE_PREFIX,
		      dev->shortname);
	litest_assert_int_eq(rc,
			     (int)(
				   strlen(UDEV_RULES_D) +
				   strlen(UDEV_RULE_PREFIX) +
				   strlen(dev->shortname) + 7));
	f = fopen(path, "w");
	litest_assert_notnull(f);
	litest_assert_int_ge(fputs(dev->udev_rule, f), 0);
	fclose(f);

out:
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
	litest_assert(d != NULL);

	udev_file = litest_init_udev_rules(*dev);

	/* device has custom create method */
	if ((*dev)->create) {
		(*dev)->create(d);
		if (abs_override || events_override) {
			litest_remove_model_quirks();
			if (udev_file)
				unlink(udev_file);
			litest_abort_msg("Custom create cannot be overridden");
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
	litest_assert_notnull(libinput);

	libinput_log_set_handler(libinput, litest_log_handler);
	if (verbose)
		libinput_log_set_priority(libinput, LIBINPUT_LOG_PRIORITY_DEBUG);

	return libinput;
}

void
litest_disable_log_handler(struct libinput *libinput)
{
	libinput_log_set_handler(libinput, NULL);
}

void
litest_restore_log_handler(struct libinput *libinput)
{
	libinput_log_set_handler(libinput, litest_log_handler);
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
	litest_assert(path != NULL);
	fd = open(path, O_RDWR|O_NONBLOCK);
	litest_assert_int_ne(fd, -1);

	rc = libevdev_new_from_fd(fd, &d->evdev);
	litest_assert_int_eq(rc, 0);

	d->libinput = libinput;
	d->libinput_device = libinput_path_add_device(d->libinput, path);
	litest_assert(d->libinput_device != NULL);
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
		litest_remove_model_quirks();
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
	litest_assert_int_eq(ret, 0);
}

static bool
axis_replacement_value(struct axis_replacement *axes,
		       int32_t evcode,
		       int32_t *value)
{
	struct axis_replacement *axis = axes;

	if (!axes)
		return false;

	while (axis->evcode != -1) {
		if (axis->evcode == evcode) {
			*value = axis->value;
			return true;
		}
		axis++;
	}

	return false;
}

int
litest_auto_assign_value(struct litest_device *d,
			 const struct input_event *ev,
			 int slot, double x, double y,
			 struct axis_replacement *axes,
			 bool touching)
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
	case ABS_MT_DISTANCE:
		value = touching ? 0 : 1;
		break;
	default:
		value = -1;
		if (!axis_replacement_value(axes, ev->code, &value) &&
		    d->interface->get_axis_default)
			d->interface->get_axis_default(d, ev->code, &value);
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

static void
litest_slot_start(struct litest_device *d,
		  unsigned int slot,
		  double x,
		  double y,
		  struct axis_replacement *axes,
		  bool touching)
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
		int value = litest_auto_assign_value(d,
						     ev,
						     slot,
						     x,
						     y,
						     axes,
						     touching);

		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_touch_down(struct litest_device *d,
		  unsigned int slot,
		  double x,
		  double y)
{
	litest_slot_start(d, slot, x, y, NULL, true);
}

void
litest_touch_down_extended(struct litest_device *d,
			   unsigned int slot,
			   double x,
			   double y,
			   struct axis_replacement *axes)
{
	litest_slot_start(d, slot, x, y, axes, true);
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

	litest_assert_int_gt(d->ntouches_down, 0);
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
		int value = litest_auto_assign_value(d,
						     ev,
						     slot,
						     0,
						     0,
						     NULL,
						     false);
		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

static void
litest_slot_move(struct litest_device *d,
		 unsigned int slot,
		 double x,
		 double y,
		 struct axis_replacement *axes,
		 bool touching)
{
	struct input_event *ev;

	if (d->interface->touch_move) {
		d->interface->touch_move(d, slot, x, y);
		return;
	}

	ev = d->interface->touch_move_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		int value = litest_auto_assign_value(d,
						     ev,
						     slot,
						     x,
						     y,
						     axes,
						     touching);
		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_touch_move(struct litest_device *d,
		  unsigned int slot,
		  double x,
		  double y)
{
	litest_slot_move(d, slot, x, y, NULL, true);
}

void
litest_touch_move_extended(struct litest_device *d,
			   unsigned int slot,
			   double x,
			   double y,
			   struct axis_replacement *axes)
{
	litest_slot_move(d, slot, x, y, axes, true);
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

void
litest_touch_move_two_touches(struct litest_device *d,
			      double x0, double y0,
			      double x1, double y1,
			      double dx, double dy,
			      int steps, int sleep_ms)
{
	for (int i = 0; i < steps - 1; i++) {
		litest_touch_move(d, 0, x0 + dx / steps * i,
					y0 + dy / steps * i);
		litest_touch_move(d, 1, x1 + dx / steps * i,
					y1 + dy / steps * i);
		if (sleep_ms) {
			libinput_dispatch(d->libinput);
			msleep(sleep_ms);
			libinput_dispatch(d->libinput);
		}
	}
	litest_touch_move(d, 0, x0 + dx, y0 + dy);
	litest_touch_move(d, 1, x1 + dx, y1 + dy);
}

void
litest_touch_move_three_touches(struct litest_device *d,
				double x0, double y0,
				double x1, double y1,
				double x2, double y2,
				double dx, double dy,
				int steps, int sleep_ms)
{
	for (int i = 0; i < steps - 1; i++) {
		litest_touch_move(d, 0, x0 + dx / steps * i,
					y0 + dy / steps * i);
		litest_touch_move(d, 1, x1 + dx / steps * i,
					y1 + dy / steps * i);
		litest_touch_move(d, 2, x2 + dx / steps * i,
					y2 + dy / steps * i);
		if (sleep_ms) {
			libinput_dispatch(d->libinput);
			msleep(sleep_ms);
			libinput_dispatch(d->libinput);
		}
	}
	litest_touch_move(d, 0, x0 + dx, y0 + dy);
	litest_touch_move(d, 1, x1 + dx, y1 + dy);
	litest_touch_move(d, 2, x2 + dx, y2 + dy);
}

void
litest_hover_start(struct litest_device *d,
		   unsigned int slot,
		   double x,
		   double y)
{
	litest_slot_start(d, slot, x, y, NULL, 0);
}

void
litest_hover_end(struct litest_device *d, unsigned int slot)
{
	struct input_event *ev;
	struct input_event up[] = {
		{ .type = EV_ABS, .code = ABS_MT_SLOT, .value = LITEST_AUTO_ASSIGN },
		{ .type = EV_ABS, .code = ABS_MT_DISTANCE, .value = 1 },
		{ .type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1 },
		{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
		{ .type = -1, .code = -1 }
	};

	litest_assert_int_gt(d->ntouches_down, 0);
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
		int value = litest_auto_assign_value(d, ev, slot, 0, 0, NULL, false);
		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_hover_move(struct litest_device *d, unsigned int slot,
		  double x, double y)
{
	litest_slot_move(d, slot, x, y, NULL, false);
}

void
litest_hover_move_to(struct litest_device *d,
		     unsigned int slot,
		     double x_from, double y_from,
		     double x_to, double y_to,
		     int steps, int sleep_ms)
{
	for (int i = 0; i < steps - 1; i++) {
		litest_hover_move(d, slot,
				  x_from + (x_to - x_from)/steps * i,
				  y_from + (y_to - y_from)/steps * i);
		if (sleep_ms) {
			libinput_dispatch(d->libinput);
			msleep(sleep_ms);
			libinput_dispatch(d->libinput);
		}
	}
	litest_hover_move(d, slot, x_to, y_to);
}

void
litest_hover_move_two_touches(struct litest_device *d,
			      double x0, double y0,
			      double x1, double y1,
			      double dx, double dy,
			      int steps, int sleep_ms)
{
	for (int i = 0; i < steps - 1; i++) {
		litest_push_event_frame(d);
		litest_hover_move(d, 0, x0 + dx / steps * i,
					y0 + dy / steps * i);
		litest_hover_move(d, 1, x1 + dx / steps * i,
					y1 + dy / steps * i);
		litest_pop_event_frame(d);
		if (sleep_ms) {
			libinput_dispatch(d->libinput);
			msleep(sleep_ms);
			libinput_dispatch(d->libinput);
		}
	}
	litest_push_event_frame(d);
	litest_hover_move(d, 0, x0 + dx, y0 + dy);
	litest_hover_move(d, 1, x1 + dx, y1 + dy);
	litest_pop_event_frame(d);
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
	litest_assert_int_ge((int)val, 0);
	litest_assert_int_le((int)val, 100);
	litest_assert_int_le(axis, (unsigned int)ABS_Y);

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
	struct pollfd fds;

	va_start(args, li);
	type = va_arg(args, int);
	while ((int)type != -1) {
		assert(type > 0);
		assert(ntypes < ARRAY_LENGTH(types));
		types[ntypes++] = type;
		type = va_arg(args, int);
	}
	va_end(args);

	fds.fd = libinput_get_fd(li);
	fds.events = POLLIN;
	fds.revents = 0;

	while (1) {
		size_t i;
		struct libinput_event *event;

		while ((type = libinput_next_event_type(li)) == LIBINPUT_EVENT_NONE) {
			poll(&fds, 1, -1);
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
	case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
		str = "GESTURE SWIPE START";
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
		str = "GESTURE SWIPE UPDATE";
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_END:
		str = "GESTURE SWIPE END";
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
		str = "GESTURE PINCH START";
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
		str = "GESTURE PINCH UPDATE";
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_END:
		str = "GESTURE PINCH END";
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

	litest_assert(empty_queue);
}

static struct libevdev_uinput *
litest_create_uinput(const char *name,
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
	litest_assert(dev != NULL);

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
		litest_assert_int_eq(rc, 0);
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
		litest_assert_int_eq(rc, 0);
	}

	rc = libevdev_uinput_create_from_device(dev,
					        LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput);
	/* workaround for a bug in libevdev pre-1.3
	   http://cgit.freedesktop.org/libevdev/commit/?id=debe9b030c8069cdf78307888ef3b65830b25122 */
	if (rc == -EBADF)
		rc = -EACCES;
	litest_assert_msg(rc == 0, "Failed to create uinput device: %s", strerror(-rc));

	libevdev_free(dev);

	devnode = libevdev_uinput_get_devnode(uinput);
	litest_assert_notnull(devnode);
	fd = open(devnode, O_RDONLY);
	litest_assert_int_gt(fd, -1);
	rc = libevdev_new_from_fd(fd, &dev);
	litest_assert_int_eq(rc, 0);

	/* uinput does not yet support setting the resolution, so we set it
	 * afterwards. This is of course racy as hell but the way we
	 * _generally_ use this function by the time libinput uses the
	 * device, we're finished here */
	abs = abs_info;
	while (abs && abs->value != -1) {
		if (abs->resolution != 0) {
			rc = libevdev_kernel_set_abs_info(dev,
							  abs->value,
							  abs);
			litest_assert_int_eq(rc, 0);
		}
		abs++;
	}
	close(fd);
	libevdev_free(dev);

	return uinput;
}

struct libevdev_uinput *
litest_create_uinput_device_from_description(const char *name,
					     const struct input_id *id,
					     const struct input_absinfo *abs_info,
					     const int *events)
{
	struct libevdev_uinput *uinput;
	const char *syspath;

	struct udev *udev;
	struct udev_monitor *udev_monitor;
	struct udev_device *udev_device;
	const char *udev_action;
	const char *udev_syspath = NULL;
	int rc;

	udev = udev_new();
	litest_assert_notnull(udev);
	udev_monitor = udev_monitor_new_from_netlink(udev, "udev");
	litest_assert_notnull(udev_monitor);
	udev_monitor_filter_add_match_subsystem_devtype(udev_monitor, "input",
							NULL);
	/* remove O_NONBLOCK */
	rc = fcntl(udev_monitor_get_fd(udev_monitor), F_SETFL, 0);
	litest_assert_int_ne(rc, -1);
	litest_assert_int_eq(udev_monitor_enable_receiving(udev_monitor),
			     0);

	uinput = litest_create_uinput(name, id, abs_info, events);

	syspath = libevdev_uinput_get_syspath(uinput);

	/* blocking, we don't want to continue until udev is ready */
	do {
		udev_device = udev_monitor_receive_device(udev_monitor);
		litest_assert_notnull(udev_device);
		udev_action = udev_device_get_action(udev_device);
		if (strcmp(udev_action, "add") != 0) {
			udev_device_unref(udev_device);
			continue;
		}

		udev_syspath = udev_device_get_syspath(udev_device);
	} while (!udev_syspath || strcmp(udev_syspath, syspath) != 0);

	litest_assert(udev_device_get_property_value(udev_device, "ID_INPUT"));

	udev_device_unref(udev_device);
	udev_monitor_unref(udev_monitor);
	udev_unref(udev);

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
		litest_assert(event < &events[ARRAY_LENGTH(events) - 2]);
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

struct libinput_event_pointer*
litest_is_button_event(struct libinput_event *event,
		       unsigned int button,
		       enum libinput_button_state state)
{
	struct libinput_event_pointer *ptrev;
	enum libinput_event_type type = LIBINPUT_EVENT_POINTER_BUTTON;

	litest_assert(event != NULL);
	litest_assert_int_eq(libinput_event_get_type(event), type);
	ptrev = libinput_event_get_pointer_event(event);
	litest_assert_int_eq(libinput_event_pointer_get_button(ptrev),
			     button);
	litest_assert_int_eq(libinput_event_pointer_get_button_state(ptrev),
			     state);

	return ptrev;
}

struct libinput_event_pointer *
litest_is_axis_event(struct libinput_event *event,
		     enum libinput_pointer_axis axis,
		     enum libinput_pointer_axis_source source)
{
	struct libinput_event_pointer *ptrev;
	enum libinput_event_type type = LIBINPUT_EVENT_POINTER_AXIS;

	litest_assert(event != NULL);
	litest_assert_int_eq(libinput_event_get_type(event), type);
	ptrev = libinput_event_get_pointer_event(event);
	litest_assert(libinput_event_pointer_has_axis(ptrev, axis));

	if (source != 0)
		litest_assert_int_eq(libinput_event_pointer_get_axis_source(ptrev),
				     source);

	return ptrev;
}

struct libinput_event_pointer *
litest_is_motion_event(struct libinput_event *event)
{
	struct libinput_event_pointer *ptrev;
	enum libinput_event_type type = LIBINPUT_EVENT_POINTER_MOTION;
	double x, y, ux, uy;

	litest_assert(event != NULL);
	litest_assert_int_eq(libinput_event_get_type(event), type);
	ptrev = libinput_event_get_pointer_event(event);

	x = libinput_event_pointer_get_dx(ptrev);
	y = libinput_event_pointer_get_dy(ptrev);
	ux = libinput_event_pointer_get_dx_unaccelerated(ptrev);
	uy = libinput_event_pointer_get_dy_unaccelerated(ptrev);

	/* No 0 delta motion events */
	litest_assert(x != 0.0 || y != 0.0 ||
		      ux != 0.0 || uy != 0.0);

	return ptrev;
}

void
litest_assert_button_event(struct libinput *li, unsigned int button,
			   enum libinput_button_state state)
{
	struct libinput_event *event;

	litest_wait_for_event(li);
	event = libinput_get_event(li);

	litest_is_button_event(event, button, state);

	libinput_event_destroy(event);
}

struct libinput_event_touch *
litest_is_touch_event(struct libinput_event *event,
		      enum libinput_event_type type)
{
	struct libinput_event_touch *touch;

	litest_assert(event != NULL);

	if (type == 0)
		type = libinput_event_get_type(event);

	switch (type) {
	case LIBINPUT_EVENT_TOUCH_DOWN:
	case LIBINPUT_EVENT_TOUCH_UP:
	case LIBINPUT_EVENT_TOUCH_MOTION:
	case LIBINPUT_EVENT_TOUCH_FRAME:
		litest_assert_int_eq(libinput_event_get_type(event), type);
		break;
	default:
		ck_abort_msg("%s: invalid touch type %d\n", __func__, type);
	}

	touch = libinput_event_get_touch_event(event);

	return touch;
}

struct libinput_event_keyboard *
litest_is_keyboard_event(struct libinput_event *event,
			 unsigned int key,
			 enum libinput_key_state state)
{
	struct libinput_event_keyboard *kevent;
	enum libinput_event_type type = LIBINPUT_EVENT_KEYBOARD_KEY;

	litest_assert(event != NULL);
	litest_assert_int_eq(libinput_event_get_type(event), type);

	kevent = libinput_event_get_keyboard_event(event);
	litest_assert(kevent != NULL);

	litest_assert_int_eq(libinput_event_keyboard_get_key(kevent), key);
	litest_assert_int_eq(libinput_event_keyboard_get_key_state(kevent),
			     state);
	return kevent;
}

struct libinput_event_gesture *
litest_is_gesture_event(struct libinput_event *event,
			enum libinput_event_type type,
			int nfingers)
{
	struct libinput_event_gesture *gevent;

	litest_assert(event != NULL);
	litest_assert_int_eq(libinput_event_get_type(event), type);

	gevent = libinput_event_get_gesture_event(event);
	litest_assert(gevent != NULL);

	if (nfingers != -1)
		litest_assert_int_eq(libinput_event_gesture_get_finger_count(gevent),
				     nfingers);
	return gevent;
}

void
litest_assert_scroll(struct libinput *li,
		     enum libinput_pointer_axis axis,
		     int minimum_movement)
{
	struct libinput_event *event, *next_event;
	struct libinput_event_pointer *ptrev;
	int value;

	event = libinput_get_event(li);
	next_event = libinput_get_event(li);
	litest_assert(next_event != NULL); /* At least 1 scroll + stop scroll */

	while (event) {
		ptrev = litest_is_axis_event(event, axis, 0);

		if (next_event) {
			value = libinput_event_pointer_get_axis_value(ptrev,
								      axis);
			/* Normal scroll event, check dir */
			if (minimum_movement > 0) {
				litest_assert_int_ge(value, minimum_movement);
			} else {
				litest_assert_int_le(value, minimum_movement);
			}
		} else {
			/* Last scroll event, must be 0 */
			litest_assert_int_eq(
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
	litest_assert_notnull(event);

	while (event) {
		litest_assert_int_eq(libinput_event_get_type(event),
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
litest_timeout_tapndrag(void)
{
	msleep(520);
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
litest_timeout_finger_switch(void)
{
	msleep(120);
}

void
litest_timeout_edgescroll(void)
{
	msleep(300);
}

void
litest_timeout_middlebutton(void)
{
	msleep(70);
}

void
litest_timeout_dwt_short(void)
{
	msleep(220);
}

void
litest_timeout_dwt_long(void)
{
	msleep(520);
}

void
litest_timeout_gesture(void)
{
	msleep(120);
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
	val = litest_auto_assign_value(d, &e, 0, x, y, NULL, true);
	litest_event(d, EV_ABS, ABS_X, val);

	e.code = ABS_Y;
	val = litest_auto_assign_value(d, &e, 0, x, y, NULL, true);
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
	val = litest_auto_assign_value(d, &e, 0, x, y, NULL, true);
	litest_event(d, EV_ABS, ABS_MT_POSITION_X, val);

	e.code = ABS_MT_POSITION_Y;
	e.value = LITEST_AUTO_ASSIGN;
	val = litest_auto_assign_value(d, &e, 0, x, y, NULL, true);
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

enum litest_mode {
	LITEST_MODE_ERROR,
	LITEST_MODE_TEST,
	LITEST_MODE_LIST,
};

static inline enum litest_mode
litest_parse_argv(int argc, char **argv)
{
	enum {
		OPT_FILTER_TEST,
		OPT_FILTER_DEVICE,
		OPT_FILTER_GROUP,
		OPT_LIST,
		OPT_VERBOSE,
	};
	static const struct option opts[] = {
		{ "filter-test", 1, 0, OPT_FILTER_TEST },
		{ "filter-device", 1, 0, OPT_FILTER_DEVICE },
		{ "filter-group", 1, 0, OPT_FILTER_GROUP },
		{ "list", 0, 0, OPT_LIST },
		{ "verbose", 0, 0, OPT_VERBOSE },
		{ 0, 0, 0, 0}
	};

	while(1) {
		int c;
		int option_index = 0;

		c = getopt_long(argc, argv, "", opts, &option_index);
		if (c == -1)
			break;
		switch(c) {
		case OPT_FILTER_TEST:
			filter_test = optarg;
			break;
		case OPT_FILTER_DEVICE:
			filter_device = optarg;
			break;
		case OPT_FILTER_GROUP:
			filter_group = optarg;
			break;
		case OPT_LIST:
			return LITEST_MODE_LIST;
		case OPT_VERBOSE:
			verbose = 1;
			break;
		default:
			fprintf(stderr, "usage: %s [--list]\n", argv[0]);
			return LITEST_MODE_ERROR;
		}
	}

	return LITEST_MODE_TEST;
}

#ifndef LITEST_NO_MAIN
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

int
main(int argc, char **argv)
{
	enum litest_mode mode;

	list_init(&all_tests);

	setenv("CK_DEFAULT_TIMEOUT", "10", 0);

	mode = litest_parse_argv(argc, argv);
	if (mode == LITEST_MODE_ERROR)
		return EXIT_FAILURE;

	litest_setup_tests();

	if (mode == LITEST_MODE_LIST) {
		litest_list_tests(&all_tests);
		return EXIT_SUCCESS;
	}

	return litest_run(argc, argv);
}
#endif
