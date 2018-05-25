/*
 * Copyright © 2018 Red Hat, Inc.
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

#include <config.h>

#include <check.h>
#include <libinput.h>

#include "libinput-util.h"
#include "litest.h"
#include "quirks.h"

static void
log_handler(struct libinput *this_is_null,
	    enum libinput_log_priority priority,
	    const char *format,
	    va_list args)
{
#if 0
	vprintf(format, args);
#endif
}

struct data_dir {
	char *dirname;
	char *filename;
};

static struct data_dir
make_data_dir(const char *file_content)
{
	struct data_dir dir = {0};
	char dirname[PATH_MAX] = "/run/litest-quirk-test-XXXXXX";
	char *filename;
	FILE *fp;
	int rc;

	litest_assert_notnull(mkdtemp(dirname));
	dir.dirname = safe_strdup(dirname);

	if (file_content) {
		rc = xasprintf(&filename, "%s/testfile.quirks", dirname);
		litest_assert_int_eq(rc, (int)(strlen(dirname) + 16));

		fp = fopen(filename, "w+");
		rc = fputs(file_content, fp);
		fclose(fp);
		litest_assert_int_ge(rc, 0);
		dir.filename = filename;
	}

	return dir;
}

static void
cleanup_data_dir(struct data_dir dd)
{
	if (dd.filename) {
		unlink(dd.filename);
		free(dd.filename);
	}
	if (dd.dirname) {
		rmdir(dd.dirname);
		free(dd.dirname);
	}
}

START_TEST(quirks_invalid_dir)
{
	struct quirks_context *ctx;

	ctx = quirks_init_subsystem("/does-not-exist",
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_LIBINPUT_LOGGING);
	ck_assert(ctx == NULL);
}
END_TEST

START_TEST(quirks_empty_dir)
{
	struct quirks_context *ctx;
	struct data_dir dd = make_data_dir(NULL);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_LIBINPUT_LOGGING);
	ck_assert(ctx == NULL);

	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_section_empty)
{
	struct quirks_context *ctx;
	const char quirks_file[] = "[Empty Section]";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_section_double)
{
	struct quirks_context *ctx;
	const char quirks_file[] = "[Section name]";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_section_missing_match)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"AttrSizeHint=10x10\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_section_missing_attr)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"MatchUdevType=mouse\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_section_match_after_attr)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"MatchUdevType=mouse\n"
	"AttrSizeHint=10x10\n"
	"MatchName=mouse\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_section_duplicate_match)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"MatchUdevType=mouse\n"
	"MatchUdevType=mouse\n"
	"AttrSizeHint=10x10\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_section_duplicate_attr)
{
	/* This shouldn't be allowed but the current parser
	   is happy with it */
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"MatchUdevType=mouse\n"
	"AttrSizeHint=10x10\n"
	"AttrSizeHint=10x10\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert_notnull(ctx);
	quirks_context_unref(ctx);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_parse_error_section)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section Missing Bracket\n"
	"MatchUdevType=mouse\n"
	"AttrSizeHint=10x10\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_parse_error_trailing_whitespace)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"MatchUdevType=mouse    \n"
	"AttrSizeHint=10x10\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_parse_error_unknown_match)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"Matchblahblah=mouse\n"
	"AttrSizeHint=10x10\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_parse_error_unknown_attr)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"MatchUdevType=mouse\n"
	"Attrblahblah=10x10\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_parse_error_unknown_model)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"MatchUdevType=mouse\n"
	"Modelblahblah=1\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_parse_error_model_not_one)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"MatchUdevType=mouse\n"
	"ModelAppleTouchpad=true\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert(ctx == NULL);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_parse_comment_inline)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name] # some inline comment\n"
	"MatchUdevType=mouse\t   # another inline comment\n"
	"ModelAppleTouchpad=1#\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert_notnull(ctx);
	quirks_context_unref(ctx);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_parse_comment_empty)
{
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"#\n"
	"   #\n"
	"MatchUdevType=mouse\n"
	"ModelAppleTouchpad=1\n";
	struct data_dir dd = make_data_dir(quirks_file);

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert_notnull(ctx);
	quirks_context_unref(ctx);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_model_one)
{
	struct litest_device *dev = litest_current_device();
	struct udev_device *ud = libinput_device_get_udev_device(dev->libinput_device);
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"MatchUdevType=mouse\n"
	"ModelAppleTouchpad=1\n";
	struct data_dir dd = make_data_dir(quirks_file);
	struct quirks *q;
	bool isset;

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert_notnull(ctx);

	q = quirks_fetch_for_device(ctx, ud);
	ck_assert_notnull(q);

	ck_assert(quirks_get_bool(q, QUIRK_MODEL_APPLE_TOUCHPAD, &isset));
	ck_assert(isset == true);

	quirks_unref(q);
	quirks_context_unref(ctx);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_model_zero)
{
	struct litest_device *dev = litest_current_device();
	struct udev_device *ud = libinput_device_get_udev_device(dev->libinput_device);
	struct quirks_context *ctx;
	const char quirks_file[] =
	"[Section name]\n"
	"MatchUdevType=mouse\n"
	"ModelAppleTouchpad=0\n";
	struct data_dir dd = make_data_dir(quirks_file);
	struct quirks *q;
	bool isset;

	ctx = quirks_init_subsystem(dd.dirname,
				    NULL,
				    log_handler,
				    NULL,
				    QLOG_CUSTOM_LOG_PRIORITIES);
	ck_assert_notnull(ctx);

	q = quirks_fetch_for_device(ctx, ud);
	ck_assert_notnull(q);

	ck_assert(quirks_get_bool(q, QUIRK_MODEL_APPLE_TOUCHPAD, &isset));
	ck_assert(isset == false);

	quirks_unref(q);
	quirks_context_unref(ctx);
	cleanup_data_dir(dd);
}
END_TEST

START_TEST(quirks_model_alps)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct quirks *q;
	bool exists, value;

	q = dev->quirks;
	exists = quirks_get_bool(q, QUIRK_MODEL_ALPS_TOUCHPAD, &value);

	if (strstr(libinput_device_get_name(device), "ALPS")) {
		ck_assert(exists);
		ck_assert(value);
	} else {
		ck_assert(!exists);
		ck_assert(!value);
	}
}
END_TEST

START_TEST(quirks_model_wacom)
{
	struct litest_device *dev = litest_current_device();
	struct quirks *q;
	bool exists, value;

	q = dev->quirks;
	exists = quirks_get_bool(q, QUIRK_MODEL_WACOM_TOUCHPAD, &value);

	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_WACOM) {
		ck_assert(exists);
		ck_assert(value);
	} else {
		ck_assert(!exists);
		ck_assert(!value);
	}
}
END_TEST

START_TEST(quirks_model_apple)
{
	struct litest_device *dev = litest_current_device();
	struct quirks *q;
	bool exists, value;

	q = dev->quirks;
	exists = quirks_get_bool(q, QUIRK_MODEL_APPLE_TOUCHPAD, &value);

	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_APPLE) {
		ck_assert(exists);
		ck_assert(value);
	} else {
		ck_assert(!exists);
		ck_assert(!value);
	}
}
END_TEST

START_TEST(quirks_model_synaptics_serial)
{
	struct litest_device *dev = litest_current_device();
	struct quirks *q;
	bool exists, value;

	q = dev->quirks;
	exists = quirks_get_bool(q, QUIRK_MODEL_SYNAPTICS_SERIAL_TOUCHPAD, &value);

	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_SYNAPTICS_SERIAL &&
	    libevdev_get_id_product(dev->evdev) == PRODUCT_ID_SYNAPTICS_SERIAL) {
		ck_assert(exists);
		ck_assert(value);
	} else {
		ck_assert(!exists);
		ck_assert(!value);
	}
}
END_TEST

TEST_COLLECTION(quirks)
{
	litest_add_for_device("quirks:datadir", quirks_invalid_dir, LITEST_MOUSE);
	litest_add_for_device("quirks:datadir", quirks_empty_dir, LITEST_MOUSE);

	litest_add_for_device("quirks:structure", quirks_section_empty, LITEST_MOUSE);
	litest_add_for_device("quirks:structure", quirks_section_double, LITEST_MOUSE);
	litest_add_for_device("quirks:structure", quirks_section_missing_match, LITEST_MOUSE);
	litest_add_for_device("quirks:structure", quirks_section_missing_attr, LITEST_MOUSE);
	litest_add_for_device("quirks:structure", quirks_section_match_after_attr, LITEST_MOUSE);
	litest_add_for_device("quirks:structure", quirks_section_duplicate_match, LITEST_MOUSE);
	litest_add_for_device("quirks:structure", quirks_section_duplicate_attr, LITEST_MOUSE);

	litest_add_for_device("quirks:parsing", quirks_parse_error_section, LITEST_MOUSE);
	litest_add_for_device("quirks:parsing", quirks_parse_error_trailing_whitespace, LITEST_MOUSE);
	litest_add_for_device("quirks:parsing", quirks_parse_error_unknown_match, LITEST_MOUSE);
	litest_add_for_device("quirks:parsing", quirks_parse_error_unknown_attr, LITEST_MOUSE);
	litest_add_for_device("quirks:parsing", quirks_parse_error_unknown_model, LITEST_MOUSE);
	litest_add_for_device("quirks:parsing", quirks_parse_error_model_not_one, LITEST_MOUSE);
	litest_add_for_device("quirks:parsing", quirks_parse_comment_inline, LITEST_MOUSE);
	litest_add_for_device("quirks:parsing", quirks_parse_comment_empty, LITEST_MOUSE);

	litest_add_for_device("quirks:model", quirks_model_one, LITEST_MOUSE);
	litest_add_for_device("quirks:model", quirks_model_zero, LITEST_MOUSE);

	litest_add("quirks:devices", quirks_model_alps, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("quirks:devices", quirks_model_wacom, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("quirks:devices", quirks_model_apple, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("quirks:devices", quirks_model_synaptics_serial, LITEST_TOUCHPAD, LITEST_ANY);
}
