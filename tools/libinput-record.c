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
#include <linux/input.h>
#include <libevdev/libevdev.h>
#include <sys/signalfd.h>
#include <sys/utsname.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>

#include "libinput-util.h"

struct record_device {
	struct list link;
	char *outfile;		/* file name to record to (cmdline arg +
				   suffix) */

	char *devnode;		/* device node of the source device */
	int fd;			/* fd to the libevdev device */
	struct libevdev *device;

	char *output_file;	/* actual output file name */
	int out_fd;		/* fd to the output file */
};

struct record_context {
	int timeout;
	bool show_keycodes;

	uint64_t offset;

	struct list devices;
	int ndevices;
};

static inline bool
obfuscate_keycode(struct input_event *ev)
{
	switch (ev->type) {
	case EV_KEY:
		if (ev->code >= KEY_ESC && ev->code < KEY_ZENKAKUHANKAKU) {
			ev->code = KEY_A;
			return true;
		}
		break;
	case EV_MSC:
		if (ev->code == MSC_SCAN) {
			ev->value = 30; /* KEY_A scancode */
			return true;
		}
		break;
	}

	return false;
}

static inline void
dprint_event(int fd, struct record_context *ctx, struct input_event *ev)
{
	const char *cname;
	bool was_modified = false;
	char desc[1024];
	bool need_comma = true; /* No comma after SYN_REPORT */

	if (ctx->offset == 0)
		ctx->offset = tv2us(&ev->time);
	ev->time = us2tv(tv2us(&ev->time) - ctx->offset);

	/* Don't leak passwords unless the user wants to */
	if (!ctx->show_keycodes)
		was_modified = obfuscate_keycode(ev);

	cname = libevdev_event_code_get_name(ev->type, ev->code);

	if (ev->type == EV_SYN && ev->code == SYN_MT_REPORT) {
		snprintf(desc,
			 sizeof(desc),
			"++++++++++++ %s (%d) ++++++++++",
			cname,
			ev->value);
	} else if (ev->type == EV_SYN) {
		static unsigned long last_ms = 0;
		unsigned long time, dt;


		time = us2ms(tv2us(&ev->time));
		if (last_ms == 0)
			last_ms = time;
		dt = time - last_ms;
		last_ms = time;

		snprintf(desc,
			 sizeof(desc),
			"------------ %s (%d) ---------- %+ldms",
			cname,
			ev->value,
			dt);
		need_comma = false;
	} else {
		const char *tname = libevdev_event_type_get_name(ev->type);

		snprintf(desc,
			 sizeof(desc),
			"%s / %-20s %4d%s",
			tname,
			cname,
			ev->value,
			was_modified ? " (obfuscated)" : "");
	}

	dprintf(fd,
		"    {\"data\": [%3lu, %6u, %3d, %3d, %5d], \"desc\": \"%s\"}%s\n",
		ev->time.tv_sec,
		(unsigned int)ev->time.tv_usec,
		ev->type,
		ev->code,
		ev->value,
		desc,
		need_comma ? "," : ""
		);
}

static inline void
handle_events(struct record_context *ctx, struct record_device *d)
{
	struct libevdev *dev = d->device;
	struct input_event e;
	static bool new_frame = true;

	while (libevdev_next_event(dev,
				   LIBEVDEV_READ_FLAG_NORMAL,
				   &e) == LIBEVDEV_READ_STATUS_SUCCESS) {
		if (new_frame)
			dprintf(d->out_fd, "  { \"evdev\" : [\n");

		dprint_event(d->out_fd, ctx, &e);
		new_frame = e.type == EV_SYN && e.code != SYN_MT_REPORT;
		if (new_frame)
			dprintf(d->out_fd, "  ] },\n");
	}
}

static inline void
dprint_header(int fd)
{
	struct utsname u;
	const char *kernel = "unknown";
	FILE *dmi;
	char modalias[2048] = "unknown";

	if (uname(&u) != -1)
		kernel = u.release;

	dmi = fopen("/sys/class/dmi/id/modalias", "r");
	if (dmi) {
		if (fgets(modalias, sizeof(modalias), dmi)) {
			modalias[strlen(modalias) - 1] = '\0'; /* linebreak */
		} else {
			sprintf(modalias, "unknown");
		}
		fclose(dmi);
	}

	dprintf(fd, "{ \"version\": 1,\n"
		    "  \"system\": {\n"
		    "    \"kernel\": \"%s\",\n"
		    "    \"dmi\": \"%s\"\n"
		    "  },\n",
		    kernel,
		    modalias);
}

/* Fixed-width print to make the human bits a bit nicer to look at */
static inline void
dprint_fw(int fd, const char *msg, ...)
{
	char buf[1024];
	va_list args;

	va_start(args, msg);
	vsnprintf(buf, sizeof(buf), msg, args);
	va_end(args);

	dprintf(fd, "    \" %-70s\",\n", buf);
}


static inline void
dprint_description_abs(int fd, struct libevdev *dev, unsigned int code)
{
	const struct input_absinfo *abs;

	abs = libevdev_get_abs_info(dev, code);
	assert(abs);

	dprint_fw(fd, "       Value      %6d", abs->value);
	dprint_fw(fd, "       Min        %6d", abs->minimum);
	dprint_fw(fd, "       Max        %6d", abs->maximum);
	dprint_fw(fd, "       Fuzz       %6d", abs->fuzz);
	dprint_fw(fd, "       Flat       %6d", abs->flat);
	dprint_fw(fd, "       Resolution %6d", abs->resolution);
}

static inline void
dprint_description_state(int fd,
			 struct libevdev *dev,
			 unsigned int type,
			 unsigned int code)
{
	int state = libevdev_get_event_value(dev, type, code);
	dprint_fw(fd, "       State %d\n", state);
}

static inline void
dprint_description_codes(int fd, struct libevdev *dev, unsigned int type)
{
	int max;

	max = libevdev_event_type_get_max(type);
	if (max == -1)
		return;

	dprint_fw(fd,
		  "Event type %d (%s)",
		  type,
		  libevdev_event_type_get_name(type));

	if (type == EV_SYN)
		return;

	for (unsigned int code = 0; code <= (unsigned int)max; code++) {
		if (!libevdev_has_event_code(dev, type, code))
			continue;

		dprint_fw(fd,
			  "  Event code %d (%s)",
			  code,
			  libevdev_event_code_get_name(type,
						       code));

		switch (type) {
		case EV_ABS:
			dprint_description_abs(fd, dev, code);
			break;
		case EV_LED:
		case EV_SW:
			dprint_description_state(fd, dev, type, code);
			break;
		}
	}
}


static inline void
dprint_description(int fd, struct libevdev *dev)
{
	const struct input_absinfo *x, *y;

	dprint_fw(fd, "Name: %s", libevdev_get_name(dev));
	dprint_fw(fd,
		  "ID: bus %#02x vendor %#02x product %#02x version %#02x",
		  libevdev_get_id_bustype(dev),
		  libevdev_get_id_vendor(dev),
		  libevdev_get_id_product(dev),
		  libevdev_get_id_version(dev));

	x = libevdev_get_abs_info(dev, ABS_X);
	y = libevdev_get_abs_info(dev, ABS_Y);
	if (x && y) {
		if (x->resolution || y->resolution) {
			int w, h;

			w = (x->maximum - x->minimum)/x->resolution;
			h = (y->maximum - y->minimum)/y->resolution;
			dprint_fw(fd, "Size in mm: %dx%d", w, h);
		} else {
			dprintf(fd,
				"Size in mm: unknown due to missing resolution");
		}
	}

	dprint_fw(fd, "Supported Events:");

	for (unsigned int type = 0; type < EV_CNT; type++) {
		if (!libevdev_has_event_type(dev, type))
			continue;

		dprint_description_codes(fd, dev, type);
	}

	dprint_fw(fd, "Properties:");

	for (unsigned int prop = 0; prop < INPUT_PROP_CNT; prop++) {
		if (libevdev_has_property(dev, prop)) {
			dprint_fw(fd,
				"   Property %d (%s)",
				prop,
				libevdev_property_get_name(prop));
		}
	}
}

static inline void
dprint_bits_info(int fd, struct libevdev *dev)
{
	dprintf(fd, "    \"name\": \"%s\",\n", libevdev_get_name(dev));
	dprintf(fd, "    \"id\": [%d, %d, %d, %d],\n",
		libevdev_get_id_bustype(dev),
		libevdev_get_id_vendor(dev),
		libevdev_get_id_product(dev),
		libevdev_get_id_version(dev));
}

static inline void
dprint_bits_absinfo(int fd, struct libevdev *dev)
{
	const struct input_absinfo *abs;
	bool first = true;

	dprintf(fd, "    \"absinfo\": [\n");

	for (unsigned int code = 0; code < ABS_CNT; code++) {
		abs = libevdev_get_abs_info(dev, code);
		if (!abs)
			continue;

		dprintf(fd,
			"%s        [%d, %d, %d, %d, %d, %d]",
			first ? "" : ",\n",
			code,
			abs->minimum,
			abs->maximum,
			abs->fuzz,
			abs->flat,
			abs->resolution);
		first = false;
	}
	dprintf(fd, "\n    ],\n");
}


static inline void
dprint_bits_codes(int fd, struct libevdev *dev, unsigned int type)
{
	int max;
	const char *prefix;
	bool first = true;

	max = libevdev_event_type_get_max(type);
	if (max == -1)
		return;

	switch (type) {
	case EV_SYN: prefix = "syn"; break;
	case EV_KEY: prefix = "key"; break;
	case EV_REL: prefix = "rel"; break;
	case EV_ABS: prefix = "abs"; break;
	case EV_MSC: prefix = "msc"; break;
	case EV_SW:  prefix = "sw"; break;
	case EV_LED: prefix = "led"; break;
	case EV_SND: prefix = "snd"; break;
	case EV_REP: prefix = "rep"; break;
	case EV_FF:  prefix = "ff"; break;
	case EV_PWR: prefix = "pwr"; break;
	case EV_FF_STATUS: prefix = "ff_status"; break;
	default:
		   abort();
		   break;
	}

	dprintf(fd, "    \"%s\": [", prefix);

	for (unsigned int code = 0; code <= (unsigned int)max; code++) {
		if (!libevdev_has_event_code(dev, type, code))
			continue;

		dprintf(fd, "%s%d", first ? "" : ", ", code);
		first = false;
	}
	dprintf(fd, "],\n");
}

static inline void
dprint_bits_types(int fd, struct libevdev *dev)
{
	for (unsigned int type = 0; type < EV_CNT; type++) {
		if (!libevdev_has_event_type(dev, type))
			continue;
		dprint_bits_codes(fd, dev, type);
	}
}

static inline void
dprint_bits_props(int fd, struct libevdev *dev)
{
	bool first = true;

	dprintf(fd, "    \"properties\": [");
	for (unsigned int prop = 0; prop < INPUT_PROP_CNT; prop++) {
		if (libevdev_has_property(dev, prop)) {
			dprintf(fd, "%s%d", first ? "" : ", ", prop);
			first = false;
		}
	}
	dprintf(fd, "]\n"); /* last entry, no comma */
}

static inline void
print_device_description(int fd, struct libevdev *dev)
{
	dprint_header(fd);

	dprintf(fd, "  \"evdev\": {\n");
	dprintf(fd, "    \"desc\" : [\n");
	dprint_description(fd, dev);
	dprintf(fd, "    \"\"],\n"); /* close description */

	dprint_bits_info(fd, dev);
	dprint_bits_types(fd, dev);
	dprint_bits_absinfo(fd, dev);
	dprint_bits_props(fd, dev);
	dprintf(fd, "  },\n"); /* close evdev */
}

static int is_event_node(const struct dirent *dir) {
	return strneq(dir->d_name, "event", 5);
}

static inline char *
select_device(void)
{
	struct dirent **namelist;
	int ndev, selected_device;
	int rc;
	char *device_path;

	ndev = scandir("/dev/input", &namelist, is_event_node, versionsort);
	if (ndev <= 0)
		return NULL;

	fprintf(stderr, "Available devices:\n");
	for (int i = 0; i < ndev; i++) {
		struct libevdev *device;
		char path[PATH_MAX];
		int fd = -1;

		snprintf(path,
			 sizeof(path),
			 "/dev/input/%s",
			 namelist[i]->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;

		rc = libevdev_new_from_fd(fd, &device);
		close(fd);
		if (rc != 0)
			continue;

		fprintf(stderr, "%s:	%s\n", path, libevdev_get_name(device));
		libevdev_free(device);
	}

	for (int i = 0; i < ndev; i++)
		free(namelist[i]);
	free(namelist);

	fprintf(stderr, "Select the device event number: ");
	rc = scanf("%d", &selected_device);

	if (rc != 1 || selected_device < 0)
		return NULL;

	rc = xasprintf(&device_path, "/dev/input/event%d", selected_device);
	if (rc == -1)
		return NULL;

	return device_path;
}

static char *
init_output_file(const char *file, bool is_prefix)
{
	char name[PATH_MAX];

	assert(file != NULL);

	if (is_prefix) {
		struct tm *tm;
		time_t t;
		char suffix[64];

		t = time(NULL);
		tm = localtime(&t);
		strftime(suffix, sizeof(suffix), "%F-%T", tm);
		snprintf(name,
			 sizeof(name),
			 "%s.%s",
			 file,
			 suffix);
	} else {
		snprintf(name, sizeof(name), "%s", file);
	}

	return strdup(name);
}

static int
open_output_file(struct record_device *d, bool is_prefix)
{
	char *fname = NULL;
	int out_fd;

	if (d->outfile) {
		fname = init_output_file(d->outfile, is_prefix);
		d->output_file = fname;
		out_fd = open(fname,
			      O_WRONLY|O_CREAT|O_TRUNC,
			      0666);
		if (out_fd < 0)
			return false;
	} else {
		out_fd = STDOUT_FILENO;
	}

	d->out_fd = out_fd;

	return true;
}

static int
mainloop(struct record_context *ctx)
{
	bool autorestart = (ctx->timeout > 0);
	struct pollfd fds[ctx->ndevices + 1];
	struct record_device *d;
	struct timespec ts;
	sigset_t mask;
	int idx;


	assert(ctx->timeout != 0);

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	fds[0].fd = signalfd(-1, &mask, SFD_NONBLOCK);
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	idx = 1;
	list_for_each(d, &ctx->devices, link) {
		fds[idx].fd = libevdev_get_fd(d->device);
		fds[idx].events = POLLIN;
		fds[idx].revents = 0;
		idx++;
	}

	if (ctx->ndevices > 1) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		ctx->offset = s2us(ts.tv_sec) + ns2us(ts.tv_nsec);
	}

	do {
		int rc;
		bool had_events = false; /* we delete files without events */

		list_for_each(d, &ctx->devices, link) {

			if (!open_output_file(d, autorestart)) {
				fprintf(stderr,
					"Failed to open output file %s (%m)\n",
					d->output_file);
				return 1;
			}

			fprintf(stderr,
				"%s recording to %s\n",
				d->devnode,
				d->output_file);

			print_device_description(d->out_fd, d->device);

			if (autorestart)
				dprintf(d->out_fd,
					"  \"desc\" : \"Autorestart timeout: %d\",\n",
					ctx->timeout);

			/* Add an extra 2 spaces so we can lseek back even when we
			 * don't have events, see below after the loop */
			dprintf(d->out_fd, "  \"events\": [  \n");
		}

		while (true) {
			rc = poll(fds, ARRAY_LENGTH(fds), ctx->timeout);
			if (rc == -1) { /* error */
				fprintf(stderr, "Error: %m\n");
				autorestart = false;
				break;
			} else if (rc == 0) {
				fprintf(stderr,
					" ... timeout%s\n",
					had_events ? "" : " (file is empty)");
				break;
			} else if (fds[0].revents != 0) { /* signal */
				autorestart = false;
				break;
			} else { /* events */
				had_events = true;
				list_for_each(d, &ctx->devices, link) {
					handle_events(ctx, d);
				}
			}
		}

		list_for_each(d, &ctx->devices, link) {
			int rc;

			/* Remove ,\n, replace with just \n
			   on stdout just print an eof marker */
			rc = lseek(d->out_fd, -2, SEEK_CUR);
			if (rc == -1)
				dprintf(d->out_fd, "\"eof\" : []\n");
			dprintf(d->out_fd, "\n");
			dprintf(d->out_fd, "  ]");
			if (autorestart)
				dprintf(d->out_fd,
					",\n  \"desc\": \"Closing after %ds inactivity\"",
					ctx->timeout/1000);
			dprintf(d->out_fd, "\n}\n");
			fsync(d->out_fd);
			close(d->out_fd);
			d->out_fd = -1;

			/* If we didn't have events, delete the file. */
			if (!had_events && d->output_file)
				unlink(d->output_file);
			free(d->output_file);
			d->output_file = NULL;
		}

	} while (autorestart);

	close(fds[0].fd);

	return 0;
}

static inline void
usage(void)
{
	printf("Usage: %s [--help] [/dev/input/event0]\n"
	       "For more information, see the %s(1) man page\n",
	       program_invocation_short_name,
	       program_invocation_short_name);
}

enum options {
	OPT_AUTORESTART,
	OPT_HELP,
	OPT_OUTFILE,
	OPT_KEYCODES,
	OPT_MULTIPLE,
};

int
main(int argc, char **argv)
{
	struct record_context ctx = {
		.timeout = -1,
		.show_keycodes = false,
	};
	struct option opts[] = {
		{ "autorestart", required_argument, 0, OPT_AUTORESTART },
		{ "output-file", required_argument, 0, OPT_OUTFILE },
		{ "show-keycodes", no_argument, 0, OPT_KEYCODES },
		{ "multiple", no_argument, 0, OPT_MULTIPLE },
		{ "help", no_argument, 0, OPT_HELP },
		{ 0, 0, 0, 0 },
	};
	struct record_device *d, *tmp;
	const char *output_arg = NULL;
	bool multiple = false;
	int ndevices;
	int rc = 1;

	list_init(&ctx.devices);

	while (1) {
		int c;
		int option_index = 0;

		c = getopt_long(argc, argv, "ho:", opts, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
		case OPT_HELP:
			usage();
			rc = 0;
			goto out;
		case OPT_AUTORESTART:
			if (!safe_atoi(optarg, &ctx.timeout) ||
			    ctx.timeout <= 0) {
				usage();
				goto out;
			}
			ctx.timeout = ctx.timeout * 1000;
			break;
		case 'o':
		case OPT_OUTFILE:
			output_arg = optarg;
			break;
		case OPT_KEYCODES:
			ctx.show_keycodes = true;
			break;
		case OPT_MULTIPLE:
			multiple = true;
			break;
		}
	}

	if (ctx.timeout > 0 && output_arg == NULL) {
		fprintf(stderr,
			"Option --autorestart requires that an output file is specified\n");
		goto out;
	}

	ndevices = argc - optind;

	if (multiple) {
		if (output_arg == NULL) {
			fprintf(stderr,
				"Option --multiple requires that an output file is specified\n");
			goto out;
		}

		if (ndevices <= 0) {
			fprintf(stderr,
				"Option --multiple requires all device node be provided on the commandline\n");
			goto out;
		}


		if (optind + 5 < argc) {
			fprintf(stderr,
				"Too many devices, maximum allowed is 5\n");
			goto out;
		}
	}

	if (!multiple) {
		struct record_device *d;
		char *path;

		path = ndevices <= 0 ? select_device() : safe_strdup(argv[optind++]);
		if (path == NULL) {
			fprintf(stderr, "Invalid device path\n");
			goto out;
		}

		d = zalloc(sizeof(*d));
		d->devnode = path;
		d->outfile = safe_strdup(output_arg);
		list_insert(&ctx.devices, &d->link);
	} else {
		for (int i = 0; i < ndevices; i++) {
			struct record_device *d;
			const char *prefix;
			char *bname;

			d = zalloc(sizeof(*d));

			d->devnode = safe_strdup(argv[optind++]);
			bname = basename(d->devnode);
			prefix = output_arg;
			xasprintf(&d->outfile, "%s.%s", prefix, bname);

			list_insert(&ctx.devices, &d->link);
		}

	}

	list_for_each(d, &ctx.devices, link) {
		int fd;

		fd = open(d->devnode, O_RDONLY|O_NONBLOCK);
		if (fd < 0) {
			fprintf(stderr,
				"Failed to open device %s (%m)\n",
				d->devnode);
			goto out;
		}

		rc = libevdev_new_from_fd(fd, &d->device);
		if (rc != 0) {
			fprintf(stderr,
				"Failed to create context for %s (%s)\n",
				d->devnode,
				strerror(-rc));
			close(fd);
			goto out;
		}

		libevdev_set_clock_id(d->device, CLOCK_MONOTONIC);
		ctx.ndevices++;
	}

	rc = mainloop(&ctx);
out:
	list_for_each_safe(d, tmp, &ctx.devices, link) {
		free(d->devnode);
		free(d->outfile);
		free(d->output_file);
		close(d->fd);
		close(d->out_fd);
		libevdev_free(d->device);
	}

	return rc;
}
