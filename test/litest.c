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

#include "config.h"

#include <check.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "linux/input.h"
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/sendfile.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libudev.h>

#include "litest.h"
#include "litest-int.h"
#include "libinput-util.h"

#define UDEV_RULES_D "/run/udev/rules.d"
#define UDEV_RULE_PREFIX "99-litest-"
#define UDEV_HWDB_D "/etc/udev/hwdb.d"
#define UDEV_MODEL_QUIRKS_RULE_FILE UDEV_RULES_D \
	"/91-litest-model-quirks-REMOVEME-XXXXXX.rules"
#define UDEV_MODEL_QUIRKS_HWDB_FILE UDEV_HWDB_D \
	"/91-litest-model-quirks-REMOVEME-XXXXXX.hwdb"
#define UDEV_TEST_DEVICE_RULE_FILE UDEV_RULES_D \
	"/91-litest-test-device-REMOVEME-XXXXXXX.rules"
#define UDEV_DEVICE_GROUPS_FILE UDEV_RULES_D \
	"/80-libinput-device-groups-litest-XXXXXX.rules"

static int jobs = 8;
static int in_debugger = -1;
static int verbose = 0;
const char *filter_test = NULL;
const char *filter_device = NULL;
const char *filter_group = NULL;

struct created_file {
	struct list link;
	char *path;
};

struct list created_files_list; /* list of all files to remove at the end of
				   the test run */

static void litest_init_udev_rules(struct list *created_files_list);
static void litest_remove_udev_rules(struct list *created_files_list);

/* defined for the litest selftest */
#ifndef LITEST_DISABLE_BACKTRACE_LOGGING
#define litest_log(...) fprintf(stderr, __VA_ARGS__)
#define litest_vlog(format_, args_) vfprintf(stderr, format_, args_)
#else
#define litest_log(...) { /* __VA_ARGS__ */ }
#define litest_vlog(...) { /* __VA_ARGS__ */ }
#endif

#if HAVE_LIBUNWIND
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
		const char *filename = "?";
		int i = 0;

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

LIBINPUT_ATTRIBUTE_PRINTF(5, 6)
__attribute__((noreturn))
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

__attribute__((noreturn))
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

__attribute__((noreturn))
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
	char *devname;
	void *func;
	void *setup;
	void *teardown;

	struct range range;
};

struct suite {
	struct list node;
	struct list tests;
	char *name;
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
extern struct litest_test_device litest_mouse_gladius_device;
extern struct litest_test_device litest_mouse_wheel_click_angle_device;
extern struct litest_test_device litest_apple_keyboard_device;
extern struct litest_test_device litest_anker_mouse_kbd_device;
extern struct litest_test_device litest_waltop_tablet_device;
extern struct litest_test_device litest_huion_tablet_device;
extern struct litest_test_device litest_cyborg_rat_device;
extern struct litest_test_device litest_yubikey_device;
extern struct litest_test_device litest_synaptics_i2c_device;
extern struct litest_test_device litest_wacom_cintiq_24hd_device;
extern struct litest_test_device litest_multitouch_fuzz_screen_device;
extern struct litest_test_device litest_wacom_intuos3_pad_device;
extern struct litest_test_device litest_wacom_intuos5_pad_device;
extern struct litest_test_device litest_keyboard_all_codes_device;
extern struct litest_test_device litest_magicmouse_device;
extern struct litest_test_device litest_wacom_ekr_device;
extern struct litest_test_device litest_wacom_cintiq_24hdt_pad_device;
extern struct litest_test_device litest_wacom_cintiq_13hdt_finger_device;
extern struct litest_test_device litest_wacom_cintiq_13hdt_pen_device;
extern struct litest_test_device litest_wacom_cintiq_13hdt_pad_device;
extern struct litest_test_device litest_wacom_hid4800_tablet_device;
extern struct litest_test_device litest_mouse_wheel_click_count_device;
extern struct litest_test_device litest_calibrated_touchscreen_device;
extern struct litest_test_device litest_acer_hawaii_keyboard_device;
extern struct litest_test_device litest_acer_hawaii_touchpad_device;
extern struct litest_test_device litest_synaptics_rmi4_device;
extern struct litest_test_device litest_mouse_wheel_tilt_device;
extern struct litest_test_device litest_lid_switch_device;
extern struct litest_test_device litest_lid_switch_surface3_device;
extern struct litest_test_device litest_appletouch_device;
extern struct litest_test_device litest_gpio_keys_device;
extern struct litest_test_device litest_ignored_mouse_device;
extern struct litest_test_device litest_wacom_mobilestudio_13hdt_pad_device;
extern struct litest_test_device litest_thinkpad_extrabuttons_device;

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
	&litest_mouse_gladius_device,
	&litest_mouse_wheel_click_angle_device,
	&litest_apple_keyboard_device,
	&litest_anker_mouse_kbd_device,
	&litest_waltop_tablet_device,
	&litest_huion_tablet_device,
	&litest_cyborg_rat_device,
	&litest_yubikey_device,
	&litest_synaptics_i2c_device,
	&litest_wacom_cintiq_24hd_device,
	&litest_multitouch_fuzz_screen_device,
	&litest_wacom_intuos3_pad_device,
	&litest_wacom_intuos5_pad_device,
	&litest_keyboard_all_codes_device,
	&litest_magicmouse_device,
	&litest_wacom_ekr_device,
	&litest_wacom_cintiq_24hdt_pad_device,
	&litest_wacom_cintiq_13hdt_finger_device,
	&litest_wacom_cintiq_13hdt_pen_device,
	&litest_wacom_cintiq_13hdt_pad_device,
	&litest_wacom_hid4800_tablet_device,
	&litest_mouse_wheel_click_count_device,
	&litest_calibrated_touchscreen_device,
	&litest_acer_hawaii_keyboard_device,
	&litest_acer_hawaii_touchpad_device,
	&litest_synaptics_rmi4_device,
	&litest_mouse_wheel_tilt_device,
	&litest_lid_switch_device,
	&litest_lid_switch_surface3_device,
	&litest_appletouch_device,
	&litest_gpio_keys_device,
	&litest_ignored_mouse_device,
	&litest_wacom_mobilestudio_13hdt_pad_device,
	&litest_thinkpad_extrabuttons_device,
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

static void
litest_add_tcase_for_device(struct suite *suite,
			    const char *funcname,
			    void *func,
			    const struct litest_test_device *dev,
			    const struct range *range)
{
	struct test *t;

	t = zalloc(sizeof(*t));
	t->name = safe_strdup(funcname);
	t->devname = safe_strdup(dev->shortname);
	t->func = func;
	t->setup = dev->setup;
	t->teardown = dev->teardown ?
			dev->teardown : litest_generic_device_teardown;
	if (range)
		t->range = *range;

	list_insert(&suite->tests, &t->node);
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

	t = zalloc(sizeof(*t));
	t->name = safe_strdup(test_name);
	t->devname = safe_strdup("no device");
	t->func = func;
	if (range)
		t->range = *range;
	t->setup = NULL;
	t->teardown = NULL;

	list_insert(&suite->tests, &t->node);
}

static struct suite *
get_suite(const char *name)
{
	struct suite *s;

	list_for_each(s, &all_tests, node) {
		if (streq(s->name, name))
			return s;
	}

	s = zalloc(sizeof(*s));
	s->name = safe_strdup(name);

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
	bool added = false;

	litest_assert(required >= LITEST_DISABLE_DEVICE);
	litest_assert(excluded >= LITEST_DISABLE_DEVICE);

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
		added = true;
	} else if (required != LITEST_ANY || excluded != LITEST_ANY) {
		for (; *dev; dev++) {
			if ((*dev)->features & LITEST_IGNORED)
				continue;

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
			added = true;
		}
	} else {
		for (; *dev; dev++) {
			if ((*dev)->features & LITEST_IGNORED)
				continue;

			if (filter_device &&
			    fnmatch(filter_device, (*dev)->shortname, 0) != 0)
				continue;

			litest_add_tcase_for_device(suite,
						    funcname,
						    func,
						    *dev,
						    range);
			added = true;
		}
	}

	if (!added &&
	    filter_test == NULL &&
	    filter_device == NULL &&
	    filter_group == NULL) {
		fprintf(stderr, "Test '%s' does not match any devices. Aborting.\n", funcname);
		abort();
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

	litest_assert(type < LITEST_NO_DEVICE);

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

LIBINPUT_ATTRIBUTE_PRINTF(3, 0)
static void
litest_log_handler(struct libinput *libinput,
		   enum libinput_log_priority pri,
		   const char *format,
		   va_list args)
{
	static int is_tty = -1;
	static bool had_newline = true;
	const char *priority = NULL;
	const char *color;

	if (is_tty == -1)
		is_tty = isatty(STDERR_FILENO);

	switch(pri) {
	case LIBINPUT_LOG_PRIORITY_INFO:
		priority =  "info ";
		color = ANSI_HIGHLIGHT;
		break;
	case LIBINPUT_LOG_PRIORITY_ERROR:
		priority = "error";
		color = ANSI_BRIGHT_RED;
		break;
	case LIBINPUT_LOG_PRIORITY_DEBUG:
		priority = "debug";
		color = ANSI_NORMAL;
		break;
	default:
		  abort();
	}

	if (!is_tty)
		color = "";

	if (had_newline)
		fprintf(stderr, "%slitest %s ", color, priority);

	if (strstr(format, "tap state:"))
		color = ANSI_BLUE;
	else if (strstr(format, "thumb state:"))
		color = ANSI_YELLOW;
	else if (strstr(format, "button state:"))
		color = ANSI_MAGENTA;
	else if (strstr(format, "touch-size:") ||
		 strstr(format, "pressure:"))
		color = ANSI_GREEN;
	else if (strstr(format, "palm:") ||
		 strstr(format, "thumb:"))
		color = ANSI_CYAN;
	else if (strstr(format, "edge state:"))
		color = ANSI_BRIGHT_GREEN;

	if (is_tty)
		fprintf(stderr, "%s ", color);

	vfprintf(stderr, format, args);
	had_newline = strlen(format) >= 1 &&
		      format[strlen(format) - 1] == '\n';
	if (is_tty && had_newline)
		fprintf(stderr, ANSI_NORMAL);

	if (strstr(format, "client bug: ") ||
	    strstr(format, "libinput bug: "))
		litest_abort_msg("libinput bug triggered, aborting.\n");
}

static char *
litest_init_device_udev_rules(struct litest_test_device *dev);

static void
litest_init_all_device_udev_rules(struct list *created_files)
{
	struct litest_test_device **dev = devices;

	while (*dev) {
		char *udev_file;

		udev_file = litest_init_device_udev_rules(*dev);
		if (udev_file) {
			struct created_file *file = zalloc(sizeof(*file));
			file->path = udev_file;
			list_insert(created_files, &file->link);
		}
		dev++;
	}
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

static void
litest_signal(int sig)
{
	struct created_file *f, *tmp;

	list_for_each_safe(f, tmp, &created_files_list, link) {
		list_remove(&f->link);
		unlink(f->path);
		/* in the sighandler, we can't free */
	}

	if (fork() == 0) {
		/* child, we can run system() */
		litest_reload_udev_rules();
		exit(0);
	}

	exit(1);
}

static inline void
litest_setup_sighandler(int sig)
{
	struct sigaction act, oact;
	int rc;

	sigemptyset(&act.sa_mask);
	sigaddset(&act.sa_mask, sig);
	act.sa_flags = 0;
	act.sa_handler = litest_signal;
	rc = sigaction(sig, &act, &oact);
	litest_assert_int_ne(rc, -1);
}

static void
litest_free_test_list(struct list *tests)
{
	struct suite *s, *snext;

	list_for_each_safe(s, snext, tests, node) {
		struct test *t, *tnext;

		list_for_each_safe(t, tnext, &s->tests, node) {
			free(t->name);
			free(t->devname);
			list_remove(&t->node);
			free(t);
		}

		list_remove(&s->node);
		free(s->name);
		free(s);
	}
}

static int
litest_run_suite(struct list *tests, int which, int max)
{
	int failed = 0;
	SRunner *sr = NULL;
	struct suite *s;
	struct test *t;
	int count = -1;
	struct name {
		struct list node;
		char *name;
	};
	struct name *n, *tmp;
	struct list testnames;

	/* Check just takes the suite/test name pointers but doesn't strdup
	 * them - we have to keep them around */
	list_init(&testnames);

	/* For each test, create one test suite with one test case, then
	   add it to the test runner. The only benefit suites give us in
	   check is that we can filter them, but our test runner has a
	   --filter-group anyway. */
	list_for_each(s, tests, node) {
		list_for_each(t, &s->tests, node) {
			Suite *suite;
			TCase *tc;
			char *sname, *tname;

			count = (count + 1) % max;
			if (max != 1 && (count % max) != which)
				continue;

			xasprintf(&sname,
				  "%s:%s:%s",
				  s->name,
				  t->name,
				  t->devname);
			litest_assert(sname != NULL);
			n = zalloc(sizeof(*n));
			n->name = sname;
			list_insert(&testnames, &n->node);

			xasprintf(&tname,
				  "%s:%s",
				  t->name,
				  t->devname);
			litest_assert(tname != NULL);
			n = zalloc(sizeof(*n));
			n->name = tname;
			list_insert(&testnames, &n->node);

			tc = tcase_create(tname);
			tcase_add_checked_fixture(tc,
						  t->setup,
						  t->teardown);
			if (t->range.upper != t->range.lower)
				tcase_add_loop_test(tc,
						    t->func,
						    t->range.lower,
						    t->range.upper);
			else
				tcase_add_test(tc, t->func);

			suite = suite_create(sname);
			suite_add_tcase(suite, tc);

			if (!sr)
				sr = srunner_create(suite);
			else
				srunner_add_suite(sr, suite);
		}
	}

	if (!sr)
		goto out;

	srunner_run_all(sr, CK_ENV);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);
out:
	list_for_each_safe(n, tmp, &testnames, node) {
		free(n->name);
		free(n);
	}

	return failed;
}

static int
litest_fork_subtests(struct list *tests, int max_forks)
{
	int failed = 0;
	int status;
	pid_t pid;
	int f;

	for (f = 0; f < max_forks; f++) {
		pid = fork();
		if (pid == 0) {
			failed = litest_run_suite(tests, f, max_forks);
			litest_free_test_list(&all_tests);
			exit(failed);
			/* child always exits here */
		}
	}

	/* parent process only */
	while (wait(&status) != -1 && errno != ECHILD) {
		if (WEXITSTATUS(status) != 0)
			failed = 1;
	}

	return failed;
}

static inline int
litest_run(int argc, char **argv)
{
	int failed = 0;

	list_init(&created_files_list);

	if (list_empty(&all_tests)) {
		fprintf(stderr,
			"Error: filters are too strict, no tests to run.\n");
		return 1;
	}

	if (getenv("LITEST_VERBOSE"))
		verbose = 1;

	litest_init_udev_rules(&created_files_list);

	litest_setup_sighandler(SIGINT);

	if (jobs == 1)
		failed = litest_run_suite(&all_tests, 1, 1);
	else
		failed = litest_fork_subtests(&all_tests, jobs);

	litest_free_test_list(&all_tests);

	litest_remove_udev_rules(&created_files_list);

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

	abs = zalloc(sz * sizeof(*abs));
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

	events = zalloc(sz * sizeof(int));
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

static inline struct created_file *
litest_copy_file(const char *dest, const char *src, const char *header)
{
	int in, out, length;
	struct created_file *file;
	int suffixlen;

	file = zalloc(sizeof(*file));
	file->path = safe_strdup(dest);

	suffixlen = file->path + strlen(file->path)  - rindex(file->path, '.');
	out = mkstemps(file->path, suffixlen);
	if (out == -1)
		litest_abort_msg("Failed to write to file %s (%s)\n",
				 file->path,
				 strerror(errno));
	litest_assert_int_ne(chmod(file->path, 0644), -1);

	if (header) {
		length = strlen(header);
		litest_assert_int_eq(write(out, header, length), length);
	}

	in = open(src, O_RDONLY);
	if (in == -1)
		litest_abort_msg("Failed to open file %s (%s)\n",
				 src,
				 strerror(errno));
	/* lazy, just check for error and empty file copy */
	litest_assert_int_gt(sendfile(out, in, NULL, 40960), 0);
	close(out);
	close(in);

	return file;
}

static inline void
litest_install_model_quirks(struct list *created_files_list)
{
	const char *warning =
			 "#################################################################\n"
			 "# WARNING: REMOVE THIS FILE\n"
			 "# This is a run-time file for the libinput test suite and\n"
			 "# should be removed on exit. If the test-suite is not currently \n"
			 "# running, remove this file and update your hwdb: \n"
			 "#       sudo udevadm hwdb --update\n"
			 "#################################################################\n\n";
	struct created_file *file;

	file = litest_copy_file(UDEV_MODEL_QUIRKS_RULE_FILE,
				LIBINPUT_MODEL_QUIRKS_UDEV_RULES_FILE,
				warning);
	list_insert(created_files_list, &file->link);

	file = litest_copy_file(UDEV_MODEL_QUIRKS_HWDB_FILE,
				LIBINPUT_MODEL_QUIRKS_UDEV_HWDB_FILE,
				warning);
	list_insert(created_files_list, &file->link);

	file = litest_copy_file(UDEV_TEST_DEVICE_RULE_FILE,
				LIBINPUT_TEST_DEVICE_RULES_FILE,
				warning);
	list_insert(created_files_list, &file->link);

	file = litest_copy_file(UDEV_DEVICE_GROUPS_FILE,
				LIBINPUT_DEVICE_GROUPS_RULES_FILE,
				warning);
	list_insert(created_files_list, &file->link);
}

static void
litest_init_udev_rules(struct list *created_files)
{
	int rc;

	rc = mkdir(UDEV_RULES_D, 0755);
	if (rc == -1 && errno != EEXIST)
		litest_abort_msg("Failed to create udev rules directory (%s)\n",
				 strerror(errno));

	rc = mkdir(UDEV_HWDB_D, 0755);
	if (rc == -1 && errno != EEXIST)
		litest_abort_msg("Failed to create udev hwdb directory (%s)\n",
				 strerror(errno));

	litest_install_model_quirks(created_files);
	litest_init_all_device_udev_rules(created_files);
	litest_reload_udev_rules();
}

static void
litest_remove_udev_rules(struct list *created_files_list)
{
	struct created_file *f, *tmp;

	list_for_each_safe(f, tmp, created_files_list, link) {
		list_remove(&f->link);
		unlink(f->path);
		free(f->path);
		free(f);
	}

	litest_reload_udev_rules();
}

static char *
litest_init_device_udev_rules(struct litest_test_device *dev)
{
	int rc;
	int fd;
	FILE *f;
	char *path = NULL;

	if (!dev->udev_rule)
		return NULL;

	rc = xasprintf(&path,
		      "%s/%s%s-XXXXXX.rules",
		      UDEV_RULES_D,
		      UDEV_RULE_PREFIX,
		      dev->shortname);
	litest_assert_int_eq(rc,
			     (int)(
				   strlen(UDEV_RULES_D) +
				   strlen(UDEV_RULE_PREFIX) +
				   strlen(dev->shortname) + 14));

	fd = mkstemps(path, 6);
	litest_assert_int_ne(fd, -1);
	f = fdopen(fd, "w");
	litest_assert_notnull(f);
	litest_assert_int_ge(fputs(dev->udev_rule, f), 0);
	fclose(f);

	return path;
}

/**
 * Creates a uinput device but does not add it to a libinput context
 */
struct litest_device *
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
	int *events, *e;
	const char *path;
	int fd, rc;

	dev = devices;
	while (*dev) {
		if ((*dev)->type == which)
			break;
		dev++;
	}

	if (!*dev)
		ck_abort_msg("Invalid device type %d\n", which);

	d = zalloc(sizeof(*d));

	/* device has custom create method */
	if ((*dev)->create) {
		(*dev)->create(d);
		if (abs_override || events_override) {
			litest_abort_msg("Custom create cannot be overridden");
		}
	} else {
		abs = merge_absinfo((*dev)->absinfo, abs_override);
		events = merge_events((*dev)->events, events_override);
		name = name_override ? name_override : (*dev)->name;
		id = id_override ? id_override : (*dev)->id;

		d->uinput = litest_create_uinput_device_from_description(name,
									 id,
									 abs,
									 events);
		d->interface = (*dev)->interface;

		for (e = events; *e != -1; e += 2) {
			unsigned int type = *e,
				     code = *(e + 1);

			if (type == INPUT_PROP_MAX &&
			    code == INPUT_PROP_SEMI_MT) {
				d->semi_mt.is_semi_mt = true;
				break;
			}
		}

		free(abs);
		free(events);
	}

	path = libevdev_uinput_get_devnode(d->uinput);
	litest_assert(path != NULL);
	fd = open(path, O_RDWR|O_NONBLOCK);
	litest_assert_int_ne(fd, -1);

	rc = libevdev_new_from_fd(fd, &d->evdev);
	litest_assert_int_eq(rc, 0);

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
	if (verbose)
		libinput_log_set_priority(libinput, LIBINPUT_LOG_PRIORITY_DEBUG);
}

LIBINPUT_ATTRIBUTE_PRINTF(3, 0)
static void
litest_bug_log_handler(struct libinput *libinput,
		       enum libinput_log_priority pri,
		       const char *format,
		       va_list args)
{
	if (strstr(format, "client bug: ") ||
	    strstr(format, "libinput bug: "))
		return;

	litest_abort_msg("Expected bug statement in log msg, aborting.\n");
}

void
litest_set_log_handler_bug(struct libinput *libinput)
{
	libinput_log_set_handler(libinput, litest_bug_log_handler);
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
	const char *path;

	d = litest_create(which,
			  name_override,
			  id_override,
			  abs_override,
			  events_override);

	path = libevdev_uinput_get_devnode(d->uinput);
	litest_assert(path != NULL);

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

void
litest_delete_device(struct litest_device *d)
{
	if (!d)
		return;

	litest_assert_int_eq(d->skip_ev_syn, 0);

	if (d->libinput_device) {
		libinput_device_unref(d->libinput_device);
		libinput_path_remove_device(d->libinput_device);
	}
	if (d->owns_context)
		libinput_unref(d->libinput);
	close(libevdev_get_fd(d->evdev));
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
axis_replacement_value(struct litest_device *d,
		       struct axis_replacement *axes,
		       int32_t evcode,
		       int32_t *value)
{
	struct axis_replacement *axis = axes;

	if (!axes)
		return false;

	while (axis->evcode != -1) {
		if (axis->evcode == evcode) {
			*value = litest_scale(d, evcode, axis->value);
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
		if (!axis_replacement_value(d, axes, ev->code, &value) &&
		    d->interface->get_axis_default)
			d->interface->get_axis_default(d, ev->code, &value);
		break;
	}

	return value;
}

static void
send_btntool(struct litest_device *d, bool hover)
{
	litest_event(d, EV_KEY, BTN_TOUCH, d->ntouches_down != 0 && !hover);
	litest_event(d, EV_KEY, BTN_TOOL_FINGER, d->ntouches_down == 1);
	litest_event(d, EV_KEY, BTN_TOOL_DOUBLETAP, d->ntouches_down == 2);
	litest_event(d, EV_KEY, BTN_TOOL_TRIPLETAP, d->ntouches_down == 3);
	litest_event(d, EV_KEY, BTN_TOOL_QUADTAP, d->ntouches_down == 4);
	litest_event(d, EV_KEY, BTN_TOOL_QUINTTAP, d->ntouches_down == 5);
}

static void
slot_start(struct litest_device *d,
	   unsigned int slot,
	   double x,
	   double y,
	   struct axis_replacement *axes,
	   bool touching,
	   bool filter_abs_xy)
{
	struct input_event *ev;

	litest_assert(d->ntouches_down >= 0);
	d->ntouches_down++;

	send_btntool(d, !touching);

	if (d->interface->touch_down) {
		d->interface->touch_down(d, slot, x, y);
		return;
	}

	for (ev = d->interface->touch_down_events;
	     ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1;
	     ev++) {
		int value = litest_auto_assign_value(d,
						     ev,
						     slot,
						     x,
						     y,
						     axes,
						     touching);
		if (value == LITEST_AUTO_ASSIGN)
			continue;

		if (filter_abs_xy && ev->type == EV_ABS &&
		    (ev->code == ABS_X || ev->code == ABS_Y))
			continue;

		litest_event(d, ev->type, ev->code, value);
	}
}

static void
slot_move(struct litest_device *d,
	  unsigned int slot,
	  double x,
	  double y,
	  struct axis_replacement *axes,
	  bool touching,
	  bool filter_abs_xy)
{
	struct input_event *ev;

	if (d->interface->touch_move) {
		d->interface->touch_move(d, slot, x, y);
		return;
	}

	for (ev = d->interface->touch_move_events;
	     ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1;
	     ev++) {
		int value = litest_auto_assign_value(d,
						     ev,
						     slot,
						     x,
						     y,
						     axes,
						     touching);
		if (value == LITEST_AUTO_ASSIGN)
			continue;

		if (filter_abs_xy && ev->type == EV_ABS &&
		    (ev->code == ABS_X || ev->code == ABS_Y))
			continue;

		litest_event(d, ev->type, ev->code, value);
	}
}

static void
touch_up(struct litest_device *d, unsigned int slot)
{
	struct input_event *ev;
	struct input_event up[] = {
		{ .type = EV_ABS, .code = ABS_MT_SLOT, .value = LITEST_AUTO_ASSIGN },
		{ .type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1 },
		{ .type = EV_ABS, .code = ABS_MT_PRESSURE, .value = 0 },
		{ .type = EV_ABS, .code = ABS_MT_TOUCH_MAJOR, .value = 0 },
		{ .type = EV_ABS, .code = ABS_MT_TOUCH_MINOR, .value = 0 },
		{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
		{ .type = -1, .code = -1 }
	};

	litest_assert_int_gt(d->ntouches_down, 0);
	d->ntouches_down--;

	send_btntool(d, false);

	if (d->interface->touch_up) {
		d->interface->touch_up(d, slot);
		return;
	} else if (d->interface->touch_up_events) {
		ev = d->interface->touch_up_events;
	} else
		ev = up;

	for ( /* */;
	     ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1;
	     ev++) {
		int value = litest_auto_assign_value(d,
						     ev,
						     slot,
						     0,
						     0,
						     NULL,
						     false);
		litest_event(d, ev->type, ev->code, value);
	}
}

static void
litest_slot_start(struct litest_device *d,
		  unsigned int slot,
		  double x,
		  double y,
		  struct axis_replacement *axes,
		  bool touching)
{
	double t, l, r = 0, b = 0; /* top, left, right, bottom */
	bool filter_abs_xy = false;

	if (!d->semi_mt.is_semi_mt) {
		slot_start(d, slot, x, y, axes, touching, filter_abs_xy);
		return;
	}

	if (d->ntouches_down >= 2 || slot > 1)
		return;

	slot = d->ntouches_down;

	if (d->ntouches_down == 0) {
		l = x;
		t = y;
	} else {
		int other = (slot + 1) % 2;
		l = min(x, d->semi_mt.touches[other].x);
		t = min(y, d->semi_mt.touches[other].y);
		r = max(x, d->semi_mt.touches[other].x);
		b = max(y, d->semi_mt.touches[other].y);
	}

	litest_push_event_frame(d);
	if (d->ntouches_down == 0)
		slot_start(d, 0, l, t, axes, touching, filter_abs_xy);
	else
		slot_move(d, 0, l, t, axes, touching, filter_abs_xy);

	if (slot == 1) {
		filter_abs_xy = true;
		slot_start(d, 1, r, b, axes, touching, filter_abs_xy);
	}

	litest_pop_event_frame(d);

	d->semi_mt.touches[slot].x = x;
	d->semi_mt.touches[slot].y = y;
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

static void
litest_slot_move(struct litest_device *d,
		 unsigned int slot,
		 double x,
		 double y,
		 struct axis_replacement *axes,
		 bool touching)
{
	double t, l, r = 0, b = 0; /* top, left, right, bottom */
	bool filter_abs_xy = false;

	if (!d->semi_mt.is_semi_mt) {
		slot_move(d, slot, x, y, axes, touching, filter_abs_xy);
		return;
	}

	if (d->ntouches_down > 2 || slot > 1)
		return;

	if (d->ntouches_down == 1) {
		l = x;
		t = y;
	} else {
		int other = (slot + 1) % 2;
		l = min(x, d->semi_mt.touches[other].x);
		t = min(y, d->semi_mt.touches[other].y);
		r = max(x, d->semi_mt.touches[other].x);
		b = max(y, d->semi_mt.touches[other].y);
	}

	litest_push_event_frame(d);
	slot_move(d, 0, l, t, axes, touching, filter_abs_xy);

	if (d->ntouches_down == 2) {
		filter_abs_xy = true;
		slot_move(d, 1, r, b, axes, touching, filter_abs_xy);
	}

	litest_pop_event_frame(d);

	d->semi_mt.touches[slot].x = x;
	d->semi_mt.touches[slot].y = y;
}

void
litest_touch_up(struct litest_device *d, unsigned int slot)
{
	if (!d->semi_mt.is_semi_mt) {
		touch_up(d, slot);
		return;
	}

	if (d->ntouches_down > 2 || slot > 1)
		return;

	litest_push_event_frame(d);
	touch_up(d, d->ntouches_down - 1);

	/* if we have one finger left, send x/y coords for that finger left.
	   this is likely to happen with a real touchpad */
	if (d->ntouches_down == 1) {
		bool touching = true;
		bool filter_abs_xy = false;

		int other = (slot + 1) % 2;
		slot_move(d,
			  0,
			  d->semi_mt.touches[other].x,
			  d->semi_mt.touches[other].y,
			  NULL,
			  touching,
			  filter_abs_xy);
	}

	litest_pop_event_frame(d);
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
	for (int i = 1; i < steps; i++) {
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
litest_touch_move_to_extended(struct litest_device *d,
			      unsigned int slot,
			      double x_from, double y_from,
			      double x_to, double y_to,
			      struct axis_replacement *axes,
			      int steps, int sleep_ms)
{
	for (int i = 1; i < steps - 1; i++) {
		litest_touch_move_extended(d, slot,
					   x_from + (x_to - x_from)/steps * i,
					   y_from + (y_to - y_from)/steps * i,
					   axes);
		if (sleep_ms) {
			libinput_dispatch(d->libinput);
			msleep(sleep_ms);
			libinput_dispatch(d->libinput);
		}
	}
	litest_touch_move_extended(d, slot, x_to, y_to, axes);
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
		if (!axis_replacement_value(d, axes, ev->code, &value) &&
		    d->interface->get_axis_default)
			d->interface->get_axis_default(d, ev->code, &value);
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
litest_touch_move_two_touches(struct litest_device *d,
			      double x0, double y0,
			      double x1, double y1,
			      double dx, double dy,
			      int steps, int sleep_ms)
{
	for (int i = 1; i < steps; i++) {
		litest_push_event_frame(d);
		litest_touch_move(d, 0, x0 + dx / steps * i,
					y0 + dy / steps * i);
		litest_touch_move(d, 1, x1 + dx / steps * i,
					y1 + dy / steps * i);
		litest_pop_event_frame(d);
		if (sleep_ms) {
			libinput_dispatch(d->libinput);
			msleep(sleep_ms);
		}
		libinput_dispatch(d->libinput);
	}
	litest_push_event_frame(d);
	litest_touch_move(d, 0, x0 + dx, y0 + dy);
	litest_touch_move(d, 1, x1 + dx, y1 + dy);
	litest_pop_event_frame(d);
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
	struct axis_replacement axes[] = {
		{ABS_MT_PRESSURE, 0 },
		{ABS_PRESSURE, 0 },
		{-1, -1 },
	};

	litest_slot_start(d, slot, x, y, axes, 0);
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

	send_btntool(d, true);

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
	struct axis_replacement axes[] = {
		{ABS_MT_PRESSURE, 0 },
		{ABS_PRESSURE, 0 },
		{-1, -1 },
	};

	litest_slot_move(d, slot, x, y, axes, false);
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
	litest_timeout_debounce();
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

void
litest_switch_action(struct litest_device *dev,
		     enum libinput_switch sw,
		     enum libinput_switch_state state)
{
	unsigned int code;

	switch (sw) {
	case LIBINPUT_SWITCH_LID:
		code = SW_LID;
		break;
	case LIBINPUT_SWITCH_TABLET_MODE:
		code = SW_TABLET_MODE;
		break;
	default:
		litest_abort_msg("Invalid switch %d", sw);
		break;
	}
	litest_event(dev, EV_SW, code, state);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
}

static int
litest_scale_axis(const struct litest_device *d,
		  unsigned int axis,
		  double val)
{
	const struct input_absinfo *abs;

	litest_assert_double_ge(val, 0.0);
	/* major/minor must be able to beyond 100% for large fingers */
	if (axis != ABS_MT_TOUCH_MAJOR &&
	    axis != ABS_MT_TOUCH_MINOR) {
		litest_assert_double_le(val, 100.0);
	}

	abs = libevdev_get_abs_info(d->evdev, axis);
	litest_assert_notnull(abs);

	return (abs->maximum - abs->minimum) * val/100.0 + abs->minimum;
}

static inline int
litest_scale_range(int min, int max, double val)
{
	litest_assert_int_ge((int)val, 0);
	litest_assert_int_le((int)val, 100);

	return (max - min) * val/100.0 + min;
}

int
litest_scale(const struct litest_device *d, unsigned int axis, double val)
{
	int min, max;

	litest_assert_double_ge(val, 0.0);
	/* major/minor must be able to beyond 100% for large fingers */
	if (axis != ABS_MT_TOUCH_MAJOR &&
	    axis != ABS_MT_TOUCH_MINOR)
		litest_assert_double_le(val, 100.0);

	if (axis <= ABS_Y) {
		min = d->interface->min[axis];
		max = d->interface->max[axis];

		return litest_scale_range(min, max, val);
	} else {
		return litest_scale_axis(d, axis, val);
	}
}

static inline int
auto_assign_pad_value(struct litest_device *dev,
		      struct input_event *ev,
		      double value)
{
	const struct input_absinfo *abs;

	if (ev->value != LITEST_AUTO_ASSIGN ||
	    ev->type != EV_ABS)
		return value;

	abs = libevdev_get_abs_info(dev->evdev, ev->code);
	litest_assert_notnull(abs);

	if (ev->code == ABS_RX || ev->code == ABS_RY) {
		double min = abs->minimum != 0 ? log2(abs->minimum) : 0,
		       max = abs->maximum != 0 ? log2(abs->maximum) : 0;

		/* Value 0 is reserved for finger up, so a value of 0% is
		 * actually 1 */
		if (value == 0.0) {
			return 1;
		} else {
			value = litest_scale_range(min, max, value);
			return pow(2, value);
		}
	} else {
		return litest_scale_range(abs->minimum, abs->maximum, value);
	}
}

void
litest_pad_ring_start(struct litest_device *d, double value)
{
	struct input_event *ev;

	ev = d->interface->pad_ring_start_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		value = auto_assign_pad_value(d, ev, value);
		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_pad_ring_change(struct litest_device *d, double value)
{
	struct input_event *ev;

	ev = d->interface->pad_ring_change_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		value = auto_assign_pad_value(d, ev, value);
		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_pad_ring_end(struct litest_device *d)
{
	struct input_event *ev;

	ev = d->interface->pad_ring_end_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		litest_event(d, ev->type, ev->code, ev->value);
		ev++;
	}
}

void
litest_pad_strip_start(struct litest_device *d, double value)
{
	struct input_event *ev;

	ev = d->interface->pad_strip_start_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		value = auto_assign_pad_value(d, ev, value);
		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_pad_strip_change(struct litest_device *d, double value)
{
	struct input_event *ev;

	ev = d->interface->pad_strip_change_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		value = auto_assign_pad_value(d, ev, value);
		litest_event(d, ev->type, ev->code, value);
		ev++;
	}
}

void
litest_pad_strip_end(struct litest_device *d)
{
	struct input_event *ev;

	ev = d->interface->pad_strip_end_events;
	while (ev && (int16_t)ev->type != -1 && (int16_t)ev->code != -1) {
		litest_event(d, ev->type, ev->code, ev->value);
		ev++;
	}
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
		litest_assert(type > 0);
		litest_assert(ntypes < ARRAY_LENGTH(types));
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
			int rc = poll(&fds, 1, 2000);
			litest_assert_int_gt(rc, 0);
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
litest_event_type_str(enum libinput_event_type type)
{
	const char *str = NULL;

	switch (type) {
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
	case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
		str = "TABLET TOOL AXIS";
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
		str = "TABLET TOOL PROX";
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_TIP:
		str = "TABLET TOOL TIP";
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
		str = "TABLET TOOL BUTTON";
		break;
	case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
		str = "TABLET PAD BUTTON";
		break;
	case LIBINPUT_EVENT_TABLET_PAD_RING:
		str = "TABLET PAD RING";
		break;
	case LIBINPUT_EVENT_TABLET_PAD_STRIP:
		str = "TABLET PAD STRIP";
		break;
	case LIBINPUT_EVENT_SWITCH_TOGGLE:
		str = "SWITCH TOGGLE";
		break;
	}
	return str;
}

static const char *
litest_event_get_type_str(struct libinput_event *event)
{
	return litest_event_type_str(libinput_event_get_type(event));
}

static void
litest_print_event(struct libinput_event *event)
{
	struct libinput_event_pointer *p;
	struct libinput_event_tablet_tool *t;
	struct libinput_event_tablet_pad *pad;
	struct libinput_device *dev;
	enum libinput_event_type type;
	double x, y;

	dev = libinput_event_get_device(event);
	type = libinput_event_get_type(event);

	fprintf(stderr,
		"device %s type %s ",
		libinput_device_get_sysname(dev),
		litest_event_get_type_str(event));
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
		x = 0.0;
		y = 0.0;
		if (libinput_event_pointer_has_axis(p,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
			y = libinput_event_pointer_get_axis_value(p,
				LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		if (libinput_event_pointer_has_axis(p,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
			x = libinput_event_pointer_get_axis_value(p,
				LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
		fprintf(stderr, "vert %.f horiz %.2f", y, x);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
		t = libinput_event_get_tablet_tool_event(event);
		fprintf(stderr, "proximity %d",
			libinput_event_tablet_tool_get_proximity_state(t));
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_TIP:
		t = libinput_event_get_tablet_tool_event(event);
		fprintf(stderr, "tip %d",
			libinput_event_tablet_tool_get_tip_state(t));
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
		t = libinput_event_get_tablet_tool_event(event);
		fprintf(stderr, "button %d state %d",
			libinput_event_tablet_tool_get_button(t),
			libinput_event_tablet_tool_get_button_state(t));
		break;
	case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
		pad = libinput_event_get_tablet_pad_event(event);
		fprintf(stderr, "button %d state %d",
			libinput_event_tablet_pad_get_button_number(pad),
			libinput_event_tablet_pad_get_button_state(pad));
		break;
	case LIBINPUT_EVENT_TABLET_PAD_RING:
		pad = libinput_event_get_tablet_pad_event(event);
		fprintf(stderr, "ring %d position %.2f source %d",
			libinput_event_tablet_pad_get_ring_number(pad),
			libinput_event_tablet_pad_get_ring_position(pad),
			libinput_event_tablet_pad_get_ring_source(pad));
		break;
	case LIBINPUT_EVENT_TABLET_PAD_STRIP:
		pad = libinput_event_get_tablet_pad_event(event);
		fprintf(stderr, "strip %d position %.2f source %d",
			libinput_event_tablet_pad_get_ring_number(pad),
			libinput_event_tablet_pad_get_ring_position(pad),
			libinput_event_tablet_pad_get_ring_source(pad));
		break;
	default:
		break;
	}

	fprintf(stderr, "\n");
}

void
litest_assert_event_type(struct libinput_event *event,
			 enum libinput_event_type want)
{
	if (libinput_event_get_type(event) == want)
		return;

	fprintf(stderr,
		"FAILED EVENT TYPE: have %s (%d) but want %s (%d)\n",
		litest_event_get_type_str(event),
		libinput_event_get_type(event),
		litest_event_type_str(want),
		want);
	litest_backtrace();
	abort();
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
		.maximum = 100,
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
		struct input_absinfo a = *abs;

		/* abs_info->value is used for the code and may be outside
		   of [min, max] */
		a.value = abs->minimum;
		rc = libevdev_enable_event_code(dev, EV_ABS, abs->value, &a);
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
	litest_assert_msg(rc == 0, "Failed to create uinput device: %s\n", strerror(-rc));

	libevdev_free(dev);

	devnode = libevdev_uinput_get_devnode(uinput);
	litest_assert_notnull(devnode);
	fd = open(devnode, O_RDONLY);
	litest_assert_int_gt(fd, -1);
	rc = libevdev_new_from_fd(fd, &dev);
	litest_assert_int_eq(rc, 0);

	/* uinput before kernel 4.5 + libevdev 1.5.0 does not support
	 * setting the resolution, so we set it afterwards. This is of
	 * course racy as hell but the way we _generally_ use this function
	 * by the time libinput uses the device, we're finished here.
	 *
	 * If you have kernel 4.5 and libevdev 1.5.0 or later, this code
	 * just keeps the room warm.
	 */
	abs = abs_info;
	while (abs && abs->value != -1) {
		if (abs->resolution != 0) {
			if (libevdev_get_abs_resolution(dev, abs->value) ==
			    abs->resolution)
				break;

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
	char path[PATH_MAX];

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
	snprintf(path, sizeof(path), "%s/event", syspath);

	/* blocking, we don't want to continue until udev is ready */
	while (1) {
		udev_device = udev_monitor_receive_device(udev_monitor);
		litest_assert_notnull(udev_device);
		udev_action = udev_device_get_action(udev_device);
		if (strcmp(udev_action, "add") != 0) {
			udev_device_unref(udev_device);
			continue;
		}

		udev_syspath = udev_device_get_syspath(udev_device);
		if (udev_syspath && strneq(udev_syspath, path, strlen(path)))
			break;

		udev_device_unref(udev_device);
	}

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
	litest_assert_event_type(event, type);
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
	litest_assert_event_type(event, type);
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
	litest_assert_event_type(event, type);
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
		litest_assert_event_type(event, type);
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
	litest_assert_event_type(event, type);

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
	litest_assert_event_type(event, type);

	gevent = libinput_event_get_gesture_event(event);
	litest_assert(gevent != NULL);

	if (nfingers != -1)
		litest_assert_int_eq(libinput_event_gesture_get_finger_count(gevent),
				     nfingers);
	return gevent;
}

struct libinput_event_tablet_tool *
litest_is_tablet_event(struct libinput_event *event,
		       enum libinput_event_type type)
{
	struct libinput_event_tablet_tool *tevent;

	litest_assert(event != NULL);
	litest_assert_event_type(event, type);

	tevent = libinput_event_get_tablet_tool_event(event);
	litest_assert(tevent != NULL);

	return tevent;
}

void
litest_assert_tablet_button_event(struct libinput *li, unsigned int button,
				  enum libinput_button_state state)
{
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	enum libinput_event_type type = LIBINPUT_EVENT_TABLET_TOOL_BUTTON;

	litest_wait_for_event(li);
	event = libinput_get_event(li);

	litest_assert_notnull(event);
	litest_assert_event_type(event, type);
	tev = libinput_event_get_tablet_tool_event(event);
	litest_assert_int_eq(libinput_event_tablet_tool_get_button(tev),
			     button);
	litest_assert_int_eq(libinput_event_tablet_tool_get_button_state(tev),
			     state);
	libinput_event_destroy(event);
}

void litest_assert_tablet_proximity_event(struct libinput *li,
					  enum libinput_tablet_tool_proximity_state state)
{
	struct libinput_event *event;
	struct libinput_event_tablet_tool *tev;
	enum libinput_event_type type = LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY;

	litest_wait_for_event(li);
	event = libinput_get_event(li);

	litest_assert_notnull(event);
	litest_assert_event_type(event, type);
	tev = libinput_event_get_tablet_tool_event(event);
	litest_assert_int_eq(libinput_event_tablet_tool_get_proximity_state(tev),
			     state);
	libinput_event_destroy(event);
}

struct libinput_event_tablet_pad *
litest_is_pad_button_event(struct libinput_event *event,
			   unsigned int button,
			   enum libinput_button_state state)
{
	struct libinput_event_tablet_pad *p;
	enum libinput_event_type type = LIBINPUT_EVENT_TABLET_PAD_BUTTON;

	litest_assert(event != NULL);
	litest_assert_event_type(event, type);

	p = libinput_event_get_tablet_pad_event(event);
	litest_assert(p != NULL);

	litest_assert_int_eq(libinput_event_tablet_pad_get_button_number(p),
			     button);
	litest_assert_int_eq(libinput_event_tablet_pad_get_button_state(p),
			     state);

	return p;
}

struct libinput_event_tablet_pad *
litest_is_pad_ring_event(struct libinput_event *event,
			 unsigned int number,
			 enum libinput_tablet_pad_ring_axis_source source)
{
	struct libinput_event_tablet_pad *p;
	enum libinput_event_type type = LIBINPUT_EVENT_TABLET_PAD_RING;

	litest_assert(event != NULL);
	litest_assert_event_type(event, type);
	p = libinput_event_get_tablet_pad_event(event);

	litest_assert_int_eq(libinput_event_tablet_pad_get_ring_number(p),
			     number);
	litest_assert_int_eq(libinput_event_tablet_pad_get_ring_source(p),
			     source);

	return p;
}

struct libinput_event_tablet_pad *
litest_is_pad_strip_event(struct libinput_event *event,
			  unsigned int number,
			  enum libinput_tablet_pad_strip_axis_source source)
{
	struct libinput_event_tablet_pad *p;
	enum libinput_event_type type = LIBINPUT_EVENT_TABLET_PAD_STRIP;

	litest_assert(event != NULL);
	litest_assert_event_type(event, type);
	p = libinput_event_get_tablet_pad_event(event);

	litest_assert_int_eq(libinput_event_tablet_pad_get_strip_number(p),
			     number);
	litest_assert_int_eq(libinput_event_tablet_pad_get_strip_source(p),
			     source);

	return p;
}

struct libinput_event_switch *
litest_is_switch_event(struct libinput_event *event,
		       enum libinput_switch sw,
		       enum libinput_switch_state state)
{
	struct libinput_event_switch *swev;
	enum libinput_event_type type = LIBINPUT_EVENT_SWITCH_TOGGLE;

	litest_assert_notnull(event);
	litest_assert_event_type(event, type);
	swev = libinput_event_get_switch_event(event);

	litest_assert_int_eq(libinput_event_switch_get_switch(swev), sw);
	litest_assert_int_eq(libinput_event_switch_get_switch_state(swev),
			     state);

	return swev;
}

void
litest_assert_pad_button_event(struct libinput *li,
			       unsigned int button,
			       enum libinput_button_state state)
{
	struct libinput_event *event;
	struct libinput_event_tablet_pad *pev;

	litest_wait_for_event(li);
	event = libinput_get_event(li);

	pev = litest_is_pad_button_event(event, button, state);
	libinput_event_destroy(libinput_event_tablet_pad_get_base_event(pev));
}

void
litest_assert_scroll(struct libinput *li,
		     enum libinput_pointer_axis axis,
		     int minimum_movement)
{
	struct libinput_event *event, *next_event;
	struct libinput_event_pointer *ptrev;
	int value;
	int nevents = 0;

	event = libinput_get_event(li);
	next_event = libinput_get_event(li);
	litest_assert(next_event != NULL); /* At least 1 scroll + stop scroll */

	while (event) {
		ptrev = litest_is_axis_event(event, axis, 0);
		nevents++;

		if (next_event) {
			int min = minimum_movement;

			value = libinput_event_pointer_get_axis_value(ptrev,
								      axis);
			/* Due to how the hysteresis works on touchpad
			 * events, the first event is reduced by the
			 * hysteresis margin that can cause the first event
			 * go under the minimum we expect for all other
			 * events */
			if (nevents == 1)
				min = minimum_movement/2;

			/* Normal scroll event, check dir */
			if (minimum_movement > 0)
				litest_assert_int_ge(value, min);
			else
				litest_assert_int_le(value, min);
		} else {
			/* Last scroll event, must be 0 */
			ck_assert_double_eq(
				libinput_event_pointer_get_axis_value(ptrev, axis),
				0.0);
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

	litest_assert(type != LIBINPUT_EVENT_NONE);

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
litest_assert_touch_sequence(struct libinput *li)
{
	struct libinput_event *event;

	event = libinput_get_event(li);
	litest_is_touch_event(event, LIBINPUT_EVENT_TOUCH_DOWN);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	litest_is_touch_event(event, LIBINPUT_EVENT_TOUCH_FRAME);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	do {
		litest_is_touch_event(event, LIBINPUT_EVENT_TOUCH_MOTION);
		libinput_event_destroy(event);

		event = libinput_get_event(li);
		litest_is_touch_event(event, LIBINPUT_EVENT_TOUCH_FRAME);
		libinput_event_destroy(event);

		event = libinput_get_event(li);
		litest_assert_notnull(event);
	} while (libinput_event_get_type(event) != LIBINPUT_EVENT_TOUCH_UP);

	litest_is_touch_event(event, LIBINPUT_EVENT_TOUCH_UP);
	libinput_event_destroy(event);
	event = libinput_get_event(li);
	litest_is_touch_event(event, LIBINPUT_EVENT_TOUCH_FRAME);
	libinput_event_destroy(event);
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
litest_timeout_debounce(void)
{
	msleep(15);
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
litest_timeout_gesture_scroll(void)
{
	msleep(180);
}

void
litest_timeout_trackpoint(void)
{
	msleep(320);
}

void
litest_push_event_frame(struct litest_device *dev)
{
	litest_assert(dev->skip_ev_syn >= 0);
	dev->skip_ev_syn++;
}

void
litest_pop_event_frame(struct litest_device *dev)
{
	litest_assert(dev->skip_ev_syn > 0);
	dev->skip_ev_syn--;
	if (dev->skip_ev_syn == 0)
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
}

void
litest_filter_event(struct litest_device *dev,
		    unsigned int type,
		    unsigned int code)
{
	libevdev_disable_event_code(dev->evdev, type, code);
}

void
litest_unfilter_event(struct litest_device *dev,
		      unsigned int type,
		      unsigned int code)
{
	/* would need an non-NULL argument for re-enabling, so simply abort
	 * until we need to be more sophisticated */
	litest_assert(type != EV_ABS);

	libevdev_enable_event_code(dev->evdev, type, code, NULL);
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
		OPT_JOBS,
		OPT_LIST,
		OPT_VERBOSE,
	};
	static const struct option opts[] = {
		{ "filter-test", 1, 0, OPT_FILTER_TEST },
		{ "filter-device", 1, 0, OPT_FILTER_DEVICE },
		{ "filter-group", 1, 0, OPT_FILTER_GROUP },
		{ "jobs", 1, 0, OPT_JOBS },
		{ "list", 0, 0, OPT_LIST },
		{ "verbose", 0, 0, OPT_VERBOSE },
		{ 0, 0, 0, 0}
	};

	enum {
		JOBS_DEFAULT,
		JOBS_SINGLE,
		JOBS_CUSTOM
	} want_jobs = JOBS_DEFAULT;

	if (in_debugger)
		want_jobs = JOBS_SINGLE;

	while(1) {
		int c;
		int option_index = 0;

		c = getopt_long(argc, argv, "j:", opts, &option_index);
		if (c == -1)
			break;
		switch(c) {
		case OPT_FILTER_TEST:
			filter_test = optarg;
			if (want_jobs == JOBS_DEFAULT)
				want_jobs = JOBS_SINGLE;
			break;
		case OPT_FILTER_DEVICE:
			filter_device = optarg;
			if (want_jobs == JOBS_DEFAULT)
				want_jobs = JOBS_SINGLE;
			break;
		case OPT_FILTER_GROUP:
			filter_group = optarg;
			if (want_jobs == JOBS_DEFAULT)
				want_jobs = JOBS_SINGLE;
			break;
		case 'j':
		case OPT_JOBS:
			jobs = atoi(optarg);
			want_jobs = JOBS_CUSTOM;
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

	if (want_jobs == JOBS_SINGLE)
		jobs = 1;

	return LITEST_MODE_TEST;
}

#ifndef LITEST_NO_MAIN
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

int
main(int argc, char **argv)
{
	const struct rlimit corelimit = { 0, 0 };
	enum litest_mode mode;

	list_init(&all_tests);

	setenv("CK_DEFAULT_TIMEOUT", "30", 0);
	setenv("LIBINPUT_RUNNING_TEST_SUITE", "1", 1);

	in_debugger = is_debugger_attached();
	if (in_debugger)
		setenv("CK_FORK", "no", 0);

	mode = litest_parse_argv(argc, argv);
	if (mode == LITEST_MODE_ERROR)
		return EXIT_FAILURE;

	litest_setup_tests_udev();
	litest_setup_tests_path();
	litest_setup_tests_pointer();
	litest_setup_tests_touch();
	litest_setup_tests_log();
	litest_setup_tests_tablet();
	litest_setup_tests_pad();
	litest_setup_tests_touchpad();
	litest_setup_tests_touchpad_tap();
	litest_setup_tests_touchpad_buttons();
	litest_setup_tests_trackpoint();
	litest_setup_tests_trackball();
	litest_setup_tests_misc();
	litest_setup_tests_keyboard();
	litest_setup_tests_device();
	litest_setup_tests_gestures();
	litest_setup_tests_lid();

	if (mode == LITEST_MODE_LIST) {
		litest_list_tests(&all_tests);
		return EXIT_SUCCESS;
	}

	if (setrlimit(RLIMIT_CORE, &corelimit) != 0)
		perror("WARNING: Core dumps not disabled. Reason");

	return litest_run(argc, argv);
}
#endif
