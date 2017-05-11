/*
 * Copyright © 2017 Red Hat, Inc.
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

#include <sys/types.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <getopt.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>

#include <libudev.h>

#include <libevdev/libevdev.h>

#include <libinput.h>
#include <libinput-util.h>
#include <libinput-version.h>

#include "shared.h"

#define MINIMUM_EVENT_COUNT 1000

static bool use_color = true;
static bool print_dat_file;
/* interactive goes to stdout unless we're redirected, then it goes to
 * stderr */
static FILE *pdest;

#define error(...) fprintf(stderr, __VA_ARGS__)
#define msg(...) fprintf(pdest, __VA_ARGS__)

struct trackpoint_data {
	int xmin, xmax, ymin, ymax;
	unsigned int xs[256], /* count of each x value, offset by 128 */
		     ys[256]; /* count of each y value, offset by 128 */
	size_t xcount, ycount;
};

static struct trackpoint_data *
trackpoint_data_new(void)
{
	struct trackpoint_data *trackpoint_data;

	trackpoint_data = zalloc(sizeof(struct trackpoint_data));
	assert(trackpoint_data);

	return trackpoint_data;
}

static inline void
trackpoint_data_free(struct trackpoint_data **trackpoint_data)
{
	free((*trackpoint_data));
	*trackpoint_data = NULL;
}

static const char *
get_attr(struct udev_device *udev_device, const char *attr)
{
	struct udev_device *parent = udev_device;

	while (parent) {
		const char *val;
		val = udev_device_get_sysattr_value(parent, attr);

		if (val) {
			return val;
		}

		parent = udev_device_get_parent(parent);
	}

	return NULL;
}

static void
check_attrs(const char *devnode)
{
	struct udev *udev;
	struct udev_device *udev_device;
	struct stat st;
	const char *attr;

	if (stat(devnode, &st) < 0) {
		perror("Error: failed to check udev device");
		return;
	}

	udev = udev_new();
	udev_device = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

	printf("# Trackpoint attributes:\n");

	/* 0x80 is kernel default for sensitivity */
	attr = get_attr(udev_device, "sensitivity");
	printf("#   sensitivity (kernel default %3d): %s\n", 0x80, attr);

	/* 0x61 is kernel default for speed */
	attr = get_attr(udev_device, "speed");
	printf("#   speed       (kernel default %3d): %s\n", 0x61, attr);

	/* 0x5 is kernel default for drift_time */
	attr = get_attr(udev_device, "drift_time");
	printf("#   drift_time  (kernel default %3d): %s\n", 0x5, attr);

	udev_device_unref(udev_device);
	udev_unref(udev);
}

static void
print_current_values(const struct trackpoint_data *d)
{
	static int progress;
	char status = 0;

	switch (progress) {
		case 0: status = '|'; break;
		case 1: status = '/'; break;
		case 2: status = '-'; break;
		case 3: status = '\\'; break;
	}

	progress = (progress + 1) % 4;

	msg("\rTrackpoint sends:	x [%3d..%3d], y [%3d..%3d] count [%zd, %zd]%c",
	    d->xmin, d->xmax,
	    d->ymin, d->ymax,
	    d->xcount, d->ycount,
	    status);
}

static bool
handle_event(struct libevdev *evdev,
	     const struct input_event *ev,
	     void *userdata)
{
	struct trackpoint_data *d = userdata;

	if (ev->type == EV_SYN) {
		print_current_values(d);
		return true;
	} else if (ev->type != EV_REL)
		return true;

	switch(ev->code) {
		case REL_X:
			d->xmin = min(d->xmin, ev->value);
			d->xmax = max(d->xmax, ev->value);
			assert((size_t)(ev->value + 128) < ARRAY_LENGTH(d->xs));
			d->xs[ev->value + 128] += 1;
			d->xcount++;
			break;
		case REL_Y:
			d->ymin = min(d->ymin, ev->value);
			d->ymax = max(d->ymax, ev->value);
			assert((size_t)(ev->value + 128) < ARRAY_LENGTH(d->ys));
			d->ys[ev->value + 128] += 1;
			d->ycount++;
			break;
	}

	return true;
}

static void
print_histogram(const struct trackpoint_data *trackpoint_data,
		const char *path)
{
	size_t i;
	bool more_left;
	size_t start = 0, end = 0;
	const unsigned int *data;
	const size_t sz = ARRAY_LENGTH(trackpoint_data->xs);

	if (trackpoint_data->xcount < MINIMUM_EVENT_COUNT ||
	    trackpoint_data->ycount < MINIMUM_EVENT_COUNT) {
		error("WARNING: insufficient events for analysis."
		      "Skipping histogram\n");
		return;
	}

	if (trackpoint_data->xmin >= trackpoint_data->xmax  ||
	    trackpoint_data->ymin >= trackpoint_data->ymax) {
		error("WARNING: invalid data ranges. Aborting.\n");
		return;
	}

	check_attrs(path);

	/* Find min(xs, ys) and max(xs, ys) */
	for (i = 0; i < sz; i++) {
		if (trackpoint_data->xs[i] > 0 || trackpoint_data->ys[i] > 0) {
			start = i;
			break;
		}
	}

	for (i = sz - 1; i > 0; i--) {
		if (trackpoint_data->xs[i] > 0 || trackpoint_data->ys[i] > 0) {
			end = i;
			break;
		}
	}

	/* we want the next smaller/bigger multiple of 10 for the header */
	start = 128 - (abs((int)start - 128) + 9)/10 * 10;
	end = 128 + (end - 128 + 9)/10 * 10;

	printf("Histogram for x/y in counts of 5:\n");
	data = trackpoint_data->xs;

	for (int axis = 0; axis < 2; axis++) {
		unsigned int count = 0;

		/* Header bar */
		for (i = start; i <= end; i += 10) {
			int val = i - 128;
			if (abs(val) % 10 == 0)
				printf("%-10d", val);
		}
		printf("\n");

		do {
			more_left = false;
			for (i = start; i <= end; i++) {
				int val = i - 128;
				if (val == 0) {
					printf("|");
					continue;
				}
				printf("%s", (data[i] > count) ? "+" : " ");
				if (data[i] > count)
					more_left = true;
			}
			printf("\n");
			count += 5;
		} while (more_left);

		data = trackpoint_data->ys;
	}
}

static inline void
print_dat(struct trackpoint_data *trackpoint_data, const char *path)
{
	int max = ARRAY_LENGTH(trackpoint_data->xs);

	printf("# libinput-measure-trackpoint-range (v%s)\n", LIBINPUT_VERSION);
	check_attrs(path);
	printf("# File contents:\n");
	printf("# Columns:\n"
	       "#   1: delta value\n"
	       "#   2: count of REL_X events for value in $1\n"
	       "#   3: count of REL_Y events for value in $1\n");

	for (int i = 0; i < max; i++) {
		int val = i - 128;

		printf("%d\t%d\t%d\n",
		       val,
		       trackpoint_data->xs[i],
		       trackpoint_data->ys[i]);
	}
}

static void
usage(void)
{
	printf("Usage: measure trackpoint-range [--help] [/dev/input/event0]\n"
	       "\n"
	       "This tool prints various debugging information about the trackpoint\n"
	       "in this system.\n"
	       "If a path to the device is provided, that device is used. Otherwise, this tool\n"
	       "will pick the first suitable trackpoint device.\n"
	       "\n"
	       "Options\n"
	       "--help ..................... Print this help\n"
	       "\n"
	       "See the man page for more information\n"
	       "\n"
	       "This tool requires access to the /dev/input/eventX nodes.\n");
}

int
main(int argc, char **argv)
{
	struct trackpoint_data *trackpoint_data;
	char path[PATH_MAX];
	int option_index = 0;
	const char *format = "summary";
	int rc;

	if (!isatty(STDOUT_FILENO))
		use_color = false;

	while(1) {
		enum opts {
			OPT_HELP,
			OPT_FORMAT,
		};

		static struct option opts[] = {
			{ "help",	no_argument,       0, OPT_HELP },
			{ "format",	required_argument, 0, OPT_FORMAT },
			{ 0, 0, 0, 0}
		};
		int c;

		c = getopt_long(argc, argv, "h", opts, &option_index);
		if (c == -1)
			break;

		switch(c) {
		case OPT_HELP:
			usage();
			return EXIT_SUCCESS;;
		case OPT_FORMAT:
			format = optarg;
			break;
		default:
			usage();
			return EXIT_FAILURE;
		}
	}

	if (streq(format, "summary")) {
		print_dat_file = false;
	} else if (streq(format, "dat")) {
		print_dat_file = true;
	} else {
		error("Unknown print format '%s'\n", format);
		return EXIT_FAILURE;
	}

	if (optind == argc) {
		if (!find_trackpoint_device(path, sizeof(path))) {
			error("Failed to find a trackpoint device.\n");
			return EXIT_FAILURE;
		}
	} else {
		snprintf(path, sizeof(path), "%s", argv[optind]);
		if (!is_trackpoint_device(path)) {
			error("Device is not a trackpoint.\n");
			return EXIT_FAILURE;
		}
	}

	if (!isatty(STDOUT_FILENO)) {
		pdest = stderr;
	} else {
		pdest = stdout;
		setbuf(stdout, NULL);
	}

	msg("Push the trackpoint:\n"
	    "- Four times around the screen edges\n"
	    "- From the top left to the bottom right and back, twice\n"
	    "- From the top right to the bottom left and back, twice\n"
	    "Movements should emulate the fastest reasonable pointer movement on the screen.\n"
	    "A minimum of %d events is required\n"
	    "\n",
	    MINIMUM_EVENT_COUNT);

	trackpoint_data = trackpoint_data_new();
	rc = tools_generic_event_loop(path,
				      handle_event,
				      trackpoint_data);
	if (rc != EXIT_SUCCESS)
		goto out;

	printf("\n");

	if (print_dat_file)
		print_dat(trackpoint_data, path);
	else
		print_histogram(trackpoint_data, path);

	trackpoint_data_free(&trackpoint_data);
out:
	return rc;
}
