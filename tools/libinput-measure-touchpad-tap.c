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

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/signalfd.h>

#include <libudev.h>
#include <libevdev/libevdev.h>

#include <libinput-util.h>
#include <libinput-version.h>

#include "shared.h"

static bool print_dat_file;
/* interactive goes to stdout unless we're redirected, then it goes to
 * stderr */
static FILE *pdest;

#define error(...) fprintf(stderr, __VA_ARGS__)
#define msg(...) fprintf(pdest, __VA_ARGS__)

struct touch {
	uint32_t tdown, tup; /* in ms */
};

struct tap_data {
	struct touch *touches;
	size_t touches_sz;
	unsigned int count;

	uint32_t toffset; /* in ms */
};

static inline uint32_t
touch_tdelta_ms(const struct touch *t)
{
	return t->tup - t->tdown;
}

static inline struct tap_data *
tap_data_new(void)
{
	struct tap_data *tap_data;

	tap_data = zalloc(sizeof(struct tap_data));

	return tap_data;
}

static inline unsigned int
tap_data_ntouches(struct tap_data *tap_data)
{
	return tap_data->count;
}

static inline void
tap_data_free(struct tap_data **tap_data)
{
	free((*tap_data)->touches);
	free((*tap_data));
	*tap_data = NULL;
}

static inline struct touch *
tap_data_get_current_touch(struct tap_data *tap_data)
{
	assert(tap_data->count > 0);

	return &tap_data->touches[tap_data->count - 1];
}

static inline struct touch *
tap_data_get_touch(struct tap_data *tap_data, unsigned int idx)
{
	assert(idx < tap_data->count);

	return &tap_data->touches[idx];
}

#define tap_data_for_each(tapdata_, t_) \
	for (unsigned i_ = 0; i_ < (tapdata_)->count && (t_ = &(tapdata_)->touches[i_]); i_++)

static inline struct tap_data *
tap_data_duplicate_sorted(const struct tap_data *src,
			  int (*cmp)(const void *a, const void *b))
{
	struct tap_data *dest = tap_data_new();

	assert(src->count > 0);

	dest->count = src->count;
	dest->toffset = src->toffset;
	dest->touches_sz = dest->count;
	dest->touches = zalloc(dest->count * sizeof(*dest->touches));

	memcpy(dest->touches,
	       src->touches,
	       dest->count * sizeof(*dest->touches));

	if (cmp)
		qsort(dest->touches,
		      dest->count,
		      sizeof(*dest->touches),
		      cmp);

	return dest;
}

static inline struct touch*
tap_data_new_touch(struct tap_data *tap_data)
{
	tap_data->count++;
	if (tap_data->touches_sz < tap_data->count) {
		tap_data->touches_sz += 50;
		tap_data->touches = realloc(tap_data->touches,
			     tap_data->touches_sz * sizeof(*tap_data->touches));
		if (tap_data->touches == NULL) {
			error("Allocation error. Oops\n");
			abort();
		}
		memset(&tap_data->touches[tap_data->count - 1],
		       0,
		       sizeof(*tap_data->touches));
	}

	return &tap_data->touches[tap_data->count - 1];
}

static int
sort_by_time_delta(const void *ap, const void *bp)
{
	const struct touch *a = ap;
	const struct touch *b = bp;
	uint32_t da, db;

	da = touch_tdelta_ms(a);
	db = touch_tdelta_ms(b);

	return da == db ? 0 : da > db ? 1 : -1;
}

static inline void
print_statistics(struct tap_data *tap_data)
{
	uint64_t delta_sum = 0;
	uint32_t average;
	uint32_t max = 0,
		 min = UINT_MAX;
	struct tap_data *data_sorted_tdelta;
	struct touch *t,
		     *median,
		     *pc90,
		     *pc95;

	if (tap_data->count == 0) {
		error("No tap data available.\n");
		return;
	}

	tap_data_for_each(tap_data, t) {
		uint32_t delta = touch_tdelta_ms(t);

		delta_sum += delta;
		max = max(delta, max);
		min = min(delta, min);
	}

	average = delta_sum/tap_data_ntouches(tap_data);

	printf("Time:\n");
	printf("  Max delta: %dms\n", max);
	printf("  Min delta: %dms\n", min);
	printf("  Average delta: %dms\n", average);

	/* Median, 90th, 95th percentile, requires sorting by time delta */
	data_sorted_tdelta = tap_data_duplicate_sorted(tap_data,
						       sort_by_time_delta);
	median = tap_data_get_touch(tap_data,
				    tap_data_ntouches(tap_data)/2);
	pc90= tap_data_get_touch(tap_data,
				 tap_data_ntouches(tap_data) * 0.9);
	pc95 = tap_data_get_touch(tap_data,
				  tap_data_ntouches(tap_data) * 0.95);
	printf("  Median delta: %dms\n", touch_tdelta_ms(median));
	printf("  90th percentile: %dms\n", touch_tdelta_ms(pc90));
	printf("  95th percentile: %dms\n", touch_tdelta_ms(pc95));

	tap_data_free(&data_sorted_tdelta);
}

static inline void
print_dat(struct tap_data *tap_data)
{
	unsigned int i;
	struct touch *t;
	struct tap_data *sorted;

	printf("# libinput-measure-touchpad-tap (v%s)\n", LIBINPUT_VERSION);
	printf("# File contents:\n"
	       "#    This file contains multiple prints of the data in different\n"
	       "#    sort order. Row number is index of touch point within each group.\n"
	       "#    Comparing data across groups will result in invalid analysis.\n"
	       "# Columns (1-indexed):\n");
	printf("# Group 1, sorted by time of occurence\n"
	       "#  1: touch down time in ms, offset by first event\n"
	       "#  2: touch up time in ms, offset by first event\n"
	       "#  3: time delta in ms\n");
	printf("# Group 2, sorted by touch down-up delta time (ascending)\n"
	       "#  4: touch down time in ms, offset by first event\n"
	       "#  5: touch up time in ms, offset by first event\n"
	       "#  6: time delta in ms\n");

	sorted = tap_data_duplicate_sorted(tap_data, sort_by_time_delta);

	for (i = 0; i < tap_data_ntouches(tap_data); i++) {
		t = tap_data_get_touch(tap_data, i);
		printf("%d %d %d ",
		       t->tdown,
		       t->tup,
		       touch_tdelta_ms(t));

		t = tap_data_get_touch(sorted, i);
		printf("%d %d %d ",
		       t->tdown,
		       t->tup,
		       touch_tdelta_ms(t));

		printf("\n");
	}

	tap_data_free(&sorted);
}

static inline void
handle_btn_touch(struct tap_data *tap_data,
		 struct libevdev *evdev,
		 const struct input_event *ev)
{

	if (ev->value) {
		struct touch *new_touch = tap_data_new_touch(tap_data);

		new_touch->tdown = us2ms(tv2us(&ev->time)) - tap_data->toffset;
	} else {
		struct touch *current = tap_data_get_current_touch(tap_data);

		msg("\rTouch sequences detected: %d", tap_data->count);

		current->tup = us2ms(tv2us(&ev->time)) - tap_data->toffset;
	}
}

static inline bool
handle_key(struct tap_data *tap_data,
	   struct libevdev *evdev,
	   const struct input_event *ev)
{
	switch (ev->code) {
	case BTN_TOOL_DOUBLETAP:
	case BTN_TOOL_TRIPLETAP:
	case BTN_TOOL_QUADTAP:
	case BTN_TOOL_QUINTTAP:
		error("This tool only supports single-finger taps. Aborting.\n");
		return false;
	case BTN_TOUCH:
		handle_btn_touch(tap_data, evdev, ev);
		break;
	default:
		break;
	}

	return true;
}

static inline bool
handle_abs(struct tap_data *tap_data,
	   struct libevdev *evdev,
	   const struct input_event *ev)
{
	return true;
}

static inline bool
handle_event(struct tap_data *tap_data,
	     struct libevdev *evdev,
	     const struct input_event *ev)
{
	bool rc = true;

	if (tap_data->toffset == 0)
		tap_data->toffset = us2ms(tv2us(&ev->time));

	switch (ev->type) {
	default:
		error("Unexpected event %s %s (%d, %d). Aborting.\n",
		      libevdev_event_type_get_name(ev->type),
		      libevdev_event_code_get_name(ev->type, ev->code),
		      ev->type,
		      ev->code);
		break;
	case EV_KEY:
		rc = handle_key(tap_data, evdev, ev);
		break;
	case EV_ABS:
		rc = handle_abs(tap_data, evdev, ev);
		break;
	case EV_SYN:
		rc = true;
		break;
	}

	return rc;
}

static int
loop(struct tap_data *data, const char *path)
{
	struct libevdev *evdev;
	struct pollfd fds[2];
	sigset_t mask;
	int fd;
	int rc;

	fd = open(path, O_RDONLY|O_NONBLOCK);
	if (fd < 0) {
		error("Failed to open device: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	rc = libevdev_new_from_fd(fd, &evdev);
	if (rc < 0) {
		error("Failed to init device: %s\n", strerror(-rc));
		close(fd);
		return EXIT_FAILURE;
	}
	libevdev_set_clock_id(evdev, CLOCK_MONOTONIC);

	fds[0].fd = fd;
	fds[0].events = POLLIN;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	fds[1].fd = signalfd(-1, &mask, SFD_NONBLOCK);
	fds[1].events = POLLIN;

	sigprocmask(SIG_BLOCK, &mask, NULL);

	rc = EXIT_FAILURE;

	error("Ready for recording data.\n"
	      "Tap the touchpad multiple times with a single finger only.\n"
	      "For useful data we recommend at least 20 taps.\n"
	      "Ctrl+C to exit\n");

	while (poll(fds, 2, -1)) {
		struct input_event ev;
		int rc;

		if (fds[1].revents)
			break;

		do {
			rc = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
			if (rc == LIBEVDEV_READ_STATUS_SYNC) {
				error("Error: cannot keep up\n");
				goto out;
			} else if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
				if (!handle_event(data, evdev, &ev))
					goto out;
			} else if (rc != -EAGAIN && rc < 0) {
				error("Error: %s\n", strerror(-rc));
				goto out;
			}
		} while (rc != -EAGAIN);
	}

	rc = EXIT_SUCCESS;
out:
	close(fd);
	if (evdev)
		libevdev_free(evdev);

	printf("\n");

	return rc;
}

static inline void
usage(void)
{
	printf("Usage: libinput measure touchpad-tap [--help] [/dev/input/event0]\n");
}

int
main(int argc, char **argv)
{
	struct tap_data *tap_data;
	char path[PATH_MAX];
	int option_index = 0;
	const char *format = "summary";
	int rc;

	while (1) {
		enum opts {
			OPT_HELP,
			OPT_FORMAT,
		};
		static struct option opts[] = {
			{ "help",	      no_argument, 0, OPT_HELP },
			{ "format",	required_argument, 0, OPT_FORMAT},
			{ 0, 0, 0, 0 },
		};
		int c;

		c = getopt_long(argc, argv, "", opts, &option_index);
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
		if (!find_touchpad_device(path, sizeof(path))) {
			error("Failed to find a touchpad device.\n");
			return EXIT_FAILURE;
		}
	} else {
		snprintf(path, sizeof(path), "%s", argv[optind]);
		if (!is_touchpad_device(path)) {
			error("Device is not a touchpad.\n");
			return EXIT_FAILURE;
		}
	}

	if (!isatty(STDOUT_FILENO)) {
		pdest = stderr;
	} else {
		pdest = stdout;
		setbuf(stdout, NULL);
	}

	tap_data = tap_data_new();
	rc = loop(tap_data, path);

	if (rc != EXIT_SUCCESS)
		goto out;

	if (print_dat_file)
		print_dat(tap_data);
	else
		print_statistics(tap_data);

out:
	tap_data_free(&tap_data);

	return rc;
}
