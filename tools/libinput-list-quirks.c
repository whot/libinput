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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>

#include "libinput-util.h"
#include "quirks.h"

static bool verbose = false;

static void
log_handler(struct libinput *this_is_null,
	    enum libinput_log_priority priority,
	    const char *format,
	    va_list args)
{
	FILE *out = stdout;
	enum quirks_log_priorities p = priority;
	char buf[256] = {0};
	const char *prefix = "";

	switch (p) {
	case QLOG_NOISE:
	case QLOG_DEBUG:
		if (!verbose)
			return;
		prefix = "quirks debug";
		break;
	case QLOG_INFO:
		prefix = "quirks info";
		break;
	case QLOG_ERROR:
		out = stderr;
		prefix = "quirks error";
		break;
	case QLOG_PARSER_ERROR:
		out = stderr;
		prefix = "quirks parser error";
		break;
	}

	snprintf(buf, sizeof(buf), "%s: %s", prefix, format);
	vfprintf(out, buf, args);
}

static void
list_device_quirks(struct quirks_context *ctx, struct udev_device *device)
{
	struct quirks *quirks;

	quirks = quirks_fetch_for_device(ctx, device);
	if (!quirks) {
		printf("Device has no quirks defined\n");
	} else {
		enum quirk qlist[] = {
			QUIRK_MODEL_ALPS_TOUCHPAD,
			QUIRK_MODEL_APPLE_TOUCHPAD,
			QUIRK_MODEL_APPLE_MAGICMOUSE,
			QUIRK_MODEL_TABLET_NO_TILT,
			QUIRK_MODEL_APPLE_TOUCHPAD_ONEBUTTON,
			QUIRK_MODEL_TOUCHPAD_VISIBLE_MARKER,
			QUIRK_MODEL_CYBORG_RAT,
			QUIRK_MODEL_CHROMEBOOK,
			QUIRK_MODEL_HP6910_TOUCHPAD,
			QUIRK_MODEL_HP8510_TOUCHPAD,
			QUIRK_MODEL_HP_PAVILION_DM4_TOUCHPAD,
			QUIRK_MODEL_HP_STREAM11_TOUCHPAD,
			QUIRK_MODEL_HP_ZBOOK_STUDIO_G3,
			QUIRK_MODEL_TABLET_NO_PROXIMITY_OUT,
			QUIRK_MODEL_LENOVO_SCROLLPOINT,
			QUIRK_MODEL_LENOVO_X230,
			QUIRK_MODEL_LENOVO_T450_TOUCHPAD,
			QUIRK_MODEL_TABLET_MODE_NO_SUSPEND,
			QUIRK_MODEL_LENOVO_CARBON_X1_6TH,
			QUIRK_MODEL_TRACKBALL,
			QUIRK_MODEL_LOGITECH_MARBLE_MOUSE,
			QUIRK_MODEL_BOUNCING_KEYS,
			QUIRK_MODEL_SYNAPTICS_SERIAL_TOUCHPAD,
			QUIRK_MODEL_SYSTEM76_BONOBO,
			QUIRK_MODEL_CLEVO_W740SU,
			QUIRK_MODEL_SYSTEM76_GALAGO,
			QUIRK_MODEL_SYSTEM76_KUDU,
			QUIRK_MODEL_WACOM_TOUCHPAD,


			QUIRK_ATTR_SIZE_HINT,
			QUIRK_ATTR_TOUCH_SIZE_RANGE,
			QUIRK_ATTR_PALM_SIZE_THRESHOLD,
			QUIRK_ATTR_LID_SWITCH_RELIABILITY,
			QUIRK_ATTR_KEYBOARD_INTEGRATION,
			QUIRK_ATTR_TPKBCOMBO_LAYOUT,
			QUIRK_ATTR_PRESSURE_RANGE,
			QUIRK_ATTR_PALM_PRESSURE_THRESHOLD,
			QUIRK_ATTR_RESOLUTION_HINT,
			QUIRK_ATTR_TRACKPOINT_RANGE,
		};
		enum quirk *q;

		ARRAY_FOR_EACH(qlist, q) {
			if (!quirks_has_quirk(quirks, *q))
				continue;

			printf("%s\n", quirk_get_name(*q));
		}
	}

	quirks_unref(quirks);
}

static void
usage(void)
{
	printf("Usage:\n"
	       "  %s [--data-dir /path/to/data/dir] /dev/input/event0\n"
	       "	Print the quirks for the given device\n"
	       "\n",
	       program_invocation_short_name);
	printf("  %s [--data-dir /path/to/data/dir] --validate-only\n"
	       "	Validate the database\n",
	       program_invocation_short_name);
}

int
main(int argc, char **argv)
{
	struct udev *udev = NULL;
	struct udev_device *device = NULL;
	const char *path;
	const char *data_path = NULL,
	           *override_file = NULL;
	int rc = 1;
	struct quirks_context *quirks;
	bool validate = false;

	while (1) {
		int c;
		int option_index = 0;
		enum {
			OPT_VERBOSE,
			OPT_DATADIR,
			OPT_VALIDATE,
		};
		static struct option opts[] = {
			{ "help",     no_argument,       0, 'h' },
			{ "verbose",  no_argument,       0, OPT_VERBOSE },
			{ "data-dir", required_argument, 0, OPT_DATADIR },
			{ "validate-only", no_argument,  0, OPT_VALIDATE },
			{ 0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "h", opts, &option_index);
		if (c == -1)
			break;

		switch(c) {
		case '?':
			exit(1);
			break;
		case 'h':
			usage();
			exit(0);
			break;
		case OPT_VERBOSE:
			verbose = true;
			break;
		case OPT_DATADIR:
			data_path = optarg;
			break;
		case OPT_VALIDATE:
			validate = true;
			break;
		default:
			usage();
			return 1;
		}
	}

	if (optind >= argc && !validate) {
		usage();
		return 1;
	}

	/* Overriding the data dir means no custom override file */
	if (!data_path) {
		data_path = LIBINPUT_DATA_DIR;
		override_file = LIBINPUT_DATA_OVERRIDE_FILE;
	}

	quirks = quirks_init_subsystem(data_path,
				      override_file,
				      log_handler,
				      NULL,
				      QLOG_CUSTOM_LOG_PRIORITIES);
	if (!quirks) {
		fprintf(stderr,
			"Failed to initialize the device quirks. "
			"Please see the above errors "
			"and/or re-run with --verbose for more details\n");
		return 1;
	}

	if (validate) {
		rc = 0;
		goto out;
	}

	udev = udev_new();
	path = argv[optind];
	if (strneq(path, "/sys/", 5)) {
		device = udev_device_new_from_syspath(udev, path);
	} else {
		struct stat st;
		if (stat(path, &st) < 0) {
			fprintf(stderr, "Error: %s: %m\n", path);
			goto out;
		}

		device = udev_device_new_from_devnum(udev, 'c', st.st_rdev);
	}
	if (device) {
		list_device_quirks(quirks, device);
		rc = 0;
	} else {
		usage();
		rc = 1;
	}

	udev_device_unref(device);
out:
	udev_unref(udev);

	quirks_context_unref(quirks);

	return rc;
}
