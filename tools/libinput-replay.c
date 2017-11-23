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

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <fcntl.h>
#include <getopt.h>
#include <glib.h>
#include <poll.h>
#include <signal.h>
#include <json-glib/json-glib.h>
#include <sys/wait.h>

#include "libinput-util.h"

static bool stop = false;

static void sighandler(int signal)
{
	stop = true;
}

struct replay_context {
	struct device {
		const char *device;
		char *name;
		struct libevdev_uinput *uinput;
		JsonParser *parser;
		int dest; /* fd to the target device or the uinput device */
	} devices[10];
	size_t ndevices;

	uint64_t us;

	bool interactive;
	bool verbose;

	int my_fd;
};

static void
parse_prop_array(JsonArray *array,
		 guint index,
		 JsonNode *node,
		 gpointer user_data)
{
	struct libevdev *dev = user_data;
	libevdev_enable_property(dev, (int)json_node_get_int(node));
}

struct enable_type_data {
	unsigned int type;
	struct libevdev *dev;
};

static void
parse_type_array(JsonArray *array,
		 guint index,
		 JsonNode *node,
		 gpointer user_data)
{
	struct enable_type_data *td = user_data;

	libevdev_enable_event_code(td->dev,
				   td->type,
				   (int)json_node_get_int(node),
				   NULL);
}

static void
parse_absinfo_array(JsonArray *array,
		    guint index,
		    JsonNode *node,
		    gpointer user_data)
{
	struct libevdev *dev = user_data;
	JsonArray *a;
	unsigned int code;
	struct input_absinfo abs = {0};

	a = json_node_get_array(node);
	if (json_array_get_length(a) != 6) {
		fprintf(stderr, "Invalid absinfo array\n");
		return;
	}

	code = (int)json_array_get_int_element(a, 0);
	abs.minimum = (int)json_array_get_int_element(a, 1);
	abs.maximum = (int)json_array_get_int_element(a, 2);
	abs.fuzz = (int)json_array_get_int_element(a, 3);
	abs.flat = (int)json_array_get_int_element(a, 4);
	abs.resolution = (int)json_array_get_int_element(a, 5);
	libevdev_enable_event_code(dev, EV_ABS, code, &abs);
}

static int
create_device(struct replay_context *ctx, int idx)
{
	JsonParser *parser = ctx->devices[idx].parser;
	JsonNode *root, *node;
	JsonObject *o;
	JsonArray *a;
	gint64 version;
	const char *str;
	struct libevdev *dev = NULL;
	int rc = 1;
	struct enable_type_data td;

	dev = libevdev_new();

	root = json_parser_get_root(parser);
	o = json_node_get_object(root);

	version = json_object_get_int_member(o, "version");
	if (version != 1) {
		fprintf(stderr, "Parser error: invalid version\n");
		goto out;
	}

	node = json_object_get_member(o, "evdev");
	if (!node) {
		fprintf(stderr, "Parser error: missing \"evdev\" entry\n");
		goto out;
	}
	o = json_node_get_object(node);

	str = json_object_get_string_member(o, "name");
	if (!str) {
		fprintf(stderr, "Parser error: device name missing\n");
		goto out;
	}
	libevdev_set_name(dev, str);
	ctx->devices[idx].name = strdup(str);

	a = json_object_get_array_member(o, "id");
	if (!a || json_array_get_length(a) != 4) {
		fprintf(stderr, "Parser error: invalid id\n");
		goto out;
	}

	libevdev_set_id_bustype(dev, (int)json_array_get_int_element(a, 0));
	libevdev_set_id_vendor(dev, (int)json_array_get_int_element(a, 1));
	libevdev_set_id_product(dev, (int)json_array_get_int_element(a, 2));
	libevdev_set_id_version(dev, (int)json_array_get_int_element(a, 3));

	a = json_object_get_array_member(o, "properties");
	if (!a) {
		fprintf(stderr,
			"Parser error: missing \"properties\" entry\n");
		goto out;
	}
	json_array_foreach_element(a, parse_prop_array, dev);

	/* parsing absinfo first means we can ignore the abs list later */
	a = json_object_get_array_member(o, "absinfo");
	if (a)
		json_array_foreach_element(a, parse_absinfo_array, dev);

	/* we don't care about syn, it's always enabled */
	for (unsigned int type = 0; type < EV_CNT; type++) {
		const char *key = NULL;

		if (type == EV_SYN || type == EV_ABS)
			continue;

		switch (type) {
		case EV_SYN: key = "syn"; break;
		case EV_KEY: key = "key"; break;
		case EV_REL: key = "rel"; break;
		case EV_ABS: key = "abs"; break;
		case EV_MSC: key = "msc"; break;
		case EV_SW:  key = "sw"; break;
		case EV_LED: key = "led"; break;
		case EV_SND: key = "snd"; break;
		case EV_REP: key = "rep"; break;
		case EV_FF:  key = "ff"; break;
		case EV_PWR: key = "pwr"; break;
		case EV_FF_STATUS: key = "ff_status"; break;
		default:
			break;
		}

		if (key == NULL)
			continue;

		td.dev = dev;
		td.type = type;
		if (!json_object_has_member(o, key))
			continue;

		a = json_object_get_array_member(o, key);
		if (!a) {
			fprintf(stderr,
				"Parser error: entry \"%s\" is invalid",
				key);
			goto out;
		}
		json_array_foreach_element(a, parse_type_array, &td);
	}

	rc = libevdev_uinput_create_from_device(dev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&ctx->devices[idx].uinput);
	if (rc != 0) {
		fprintf(stderr,
			"Failed to create uinput device (%s)\n",
			strerror(-rc));
		goto out;

	}
	ctx->devices[idx].dest = libevdev_uinput_get_fd(ctx->devices[idx].uinput);
	rc = 0;
out:
	if (dev)
		libevdev_free(dev);
	if (rc != 0)
		free(ctx->devices[idx].name);
	return rc;
}

static void
play(JsonArray *array, guint index, JsonNode *node, gpointer user_data)
{
	struct replay_context *ctx = user_data;
	JsonObject *o;
	JsonArray *a;
	struct input_event e;
	uint64_t etime;
	unsigned int tdelta;
	const int ERROR_MARGIN = 150; /* us */
	int nevents;

	if (stop)
		return;

	o = json_node_get_object(node);

	if (!json_object_has_member(o, "evdev"))
		return;

	a = json_object_get_array_member(o, "evdev");
	nevents = json_array_get_length(a);
	assert(nevents > 0);

	for (int i = 0; i < nevents; i++) {
		JsonArray *data;

		o = json_array_get_object_element(a, i);
		data = json_object_get_array_member(o, "data");
		if (!data) {
			fprintf(stderr, "Parser error: missing event data\n");
			return;
		}

		e.time.tv_sec = (int)json_array_get_int_element(data, 0);
		e.time.tv_usec = (int)json_array_get_int_element(data, 1);
		e.type = (int)json_array_get_int_element(data, 2);
		e.code = (int)json_array_get_int_element(data, 3);
		e.value = (int)json_array_get_int_element(data, 4);

		etime = tv2us(&e.time);
		tdelta = etime - ctx->us;
		if (tdelta > 0)
			usleep(tdelta - ERROR_MARGIN);
		ctx->us = etime;

		write(ctx->my_fd, &e, sizeof(e));

		if (ctx->verbose ) {
			if (e.type == EV_SYN && e.type != SYN_MT_REPORT) {
				printf("%03ld.%06u ------------ %s (%d) ----------\n",
				       e.time.tv_sec,
				       (unsigned int)e.time.tv_usec,
				       libevdev_event_code_get_name(e.type, e.code),
				       e.code);
		       } else {
				printf("%03ld.%06u %s / %-20s %4d\n",
				       e.time.tv_sec,
				       (unsigned int)e.time.tv_usec,
				       libevdev_event_type_get_name(e.type),
				       libevdev_event_code_get_name(e.type, e.code),
				       e.value);
		       }
		}
	}
}

static void
play_events(struct replay_context *ctx)
{
	JsonArray *a[ctx->ndevices];
	struct sigaction act;

	for (size_t i = 0; i < ctx->ndevices; i++) {
		JsonParser *parser;
		JsonNode *root;
		JsonObject *o;
		parser = ctx->devices[i].parser;
		root = json_parser_get_root(parser);
		o = json_node_get_object(root);
		a[i] = json_object_get_array_member(o, "events");
	}

	act.sa_handler = sighandler;
	act.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGINT, &act, NULL);

	do {
		int status;

		if (ctx->interactive) {
			char line[32];
			printf("Hit enter to start replaying");
			fflush(stdout);
			fgets(line, sizeof(line), stdin);
		}

		ctx->us = 0;

		for (size_t i = 0; i < ctx->ndevices; i++) {
			if (fork() == 0) {
				close(STDIN_FILENO);
				ctx->my_fd = ctx->devices[i].dest;
				json_array_foreach_element(a[i], play, ctx);
				exit(0);
			}
		}

		while (wait(&status) != -1) {
			/* humm dee dumm */
		}
		if (errno == ECHILD)
			break;
		else
			fprintf(stderr, "oops. %m\n");
	} while (ctx->interactive && !stop);

}

static inline void
usage(void)
{
	printf("Usage: %s [--help] recordings-file\n"
	       "For more information, see the %s(1) man page\n",
	       program_invocation_short_name,
	       program_invocation_short_name);
}

enum options {
	OPT_DEVICE,
	OPT_HELP,
	OPT_INTERACTIVE,
	OPT_VERBOSE,
};

int main(int argc, char **argv)
{
	struct replay_context ctx = {0};
	struct option opts[] = {
		{ "replay-on", required_argument, 0, OPT_DEVICE },
		{ "interactive", no_argument, 0, OPT_INTERACTIVE },
		{ "help", no_argument, 0, OPT_HELP },
		{ "verbose", no_argument, 0, OPT_VERBOSE },
		{ 0, 0, 0, 0 },
	};
	int rc = 1;
	const char *device_arg = NULL;

	for (size_t i = 0; i < ARRAY_LENGTH(ctx.devices); i++)
		ctx.devices[i].dest = -1;

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
		case OPT_VERBOSE:
			ctx.verbose = true;
			break;
		case OPT_DEVICE:
			device_arg = optarg;
			break;
		case OPT_INTERACTIVE:
			ctx.interactive = true;
			break;
		}
	}

	if (optind >= argc) {
		usage();
		goto out;
	}

	ctx.ndevices = argc - optind;
	if (ctx.ndevices >= ARRAY_LENGTH(ctx.devices)) {
		fprintf(stderr,
			"Number of files must not exceed %zd\n",
			ARRAY_LENGTH(ctx.devices));
		goto out;
	}

	if (ctx.ndevices > 1 && device_arg) {
		fprintf(stderr,
			"Option --replay-on can only work with one file\n");
		goto out;
	}

	for (size_t i = 0; i < ctx.ndevices; i++) {
		const char *recording = NULL;
		JsonParser *parser;

		recording = argv[optind++];

		parser = json_parser_new();
		if (!parser) {
			fprintf(stderr, "Failed to create parser\n");
			goto out;
		}

		if (!json_parser_load_from_file(parser, recording, NULL)) {
			g_object_unref(parser);
			fprintf(stderr, "Failed to parse %s. Oops\n", recording);
			goto out;
		}
		ctx.devices[i].parser = parser;
	}

	if (device_arg == NULL || ctx.ndevices > 1)
		ctx.interactive = true;

	if (!device_arg) {
		for (size_t i = 0; i < ctx.ndevices; i++) {
			if (create_device(&ctx, i) != 0)
				goto out;
			printf("%s: %s\n",
			       ctx.devices[i].name,
			       libevdev_uinput_get_devnode(ctx.devices[i].uinput));
		}
	} else {
		ctx.devices[0].dest = open(device_arg, O_RDWR);
		if (ctx.devices[0].dest == -1) {
			fprintf(stderr, "Failed to open %s (%m)\n", device_arg);
		}
	}

	/* device is created now */
	play_events(&ctx);

	rc = 0;

out:
	for (size_t i = 0; i < ctx.ndevices; i++) {
		if (ctx.devices[i].parser)
			g_object_unref(ctx.devices[i].parser);
		if (ctx.devices[i].uinput)
			libevdev_uinput_destroy(ctx.devices[i].uinput);
		free(ctx.devices[i].name);
		close(ctx.devices[i].dest);
	}

	return rc;
}
