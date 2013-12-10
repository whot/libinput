/*
 * Copyright © 2013 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"

#include <fcntl.h>
#include <string.h>
#include <libudev.h>

#include "path.h"
#include "evdev.h"

int path_input_process_event(struct libinput_event);

static void
path_input_disable(struct libinput *libinput)
{
	struct path_input *input = (struct path_input*)libinput;
	struct evdev_device *device = input->device;

	close_restricted(libinput, device->fd);
	evdev_device_remove(device);
}

static void
path_seat_destroy(struct libinput_seat *seat)
{
	struct path_seat *pseat = (struct path_seat*)seat;
	notify_removed_seat(seat);
	libinput_seat_unref(seat);
	free(pseat);
}

static struct path_seat*
path_seat_create(struct path_input *input)
{
	struct path_seat *seat;

	seat = zalloc(sizeof(*seat));
	if (!seat)
		return NULL;

	seat->name = "default";

	libinput_seat_init(&seat->base, &input->base, seat->name, path_seat_destroy);
	list_insert(&input->base.seat_list, &seat->base.link);
	notify_added_seat(&seat->base);

	return seat;
}

static char *
path_get_sysname(const char *path)
{
	struct udev *udev = NULL;
	struct udev_device *device = NULL;
	struct stat st;
	char *syspath = NULL;

	udev = udev_new();
	if (!udev)
		goto out;

	if (stat(path, &st) < 0)
		goto out;

	device = udev_device_new_from_devnum(udev, 'c', st.st_rdev);
	if (!device)
		goto out;

	syspath = strdup(udev_device_get_syspath(device));
out:
	if (device)
		udev_device_unref(device);
	if (udev)
		udev_unref(udev);
	return syspath;
}

static int
path_input_enable(struct libinput *libinput)
{
	struct path_input *input = (struct path_input*)libinput;
	struct path_seat *seat;
	struct evdev_device *device;
	const char *devnode = input->path;
	char *syspath;
	int fd;

	fd = open_restricted(libinput, input->path, O_RDWR|O_NONBLOCK);
	if (fd < 0) {
		log_info("opening input device '%s' failed.\n", devnode);
		return -1;
	}

	syspath = path_get_sysname(devnode);
	if (!syspath) {
		close_restricted(libinput, fd);
		log_info("failed to obtain syspath for device '%s'.\n", devnode);
		return -1;
	}

	seat = path_seat_create(input);

	device = evdev_device_create(&seat->base, devnode, syspath, fd);
	if (device == EVDEV_UNHANDLED_DEVICE) {
		close_restricted(libinput, fd);
		log_info("not using input device '%s'.\n", devnode);
		return -1;
	} else if (device == NULL) {
		close_restricted(libinput, fd);
		log_info("failed to create input device '%s'.\n", devnode);
		return -1;
	}

	input->device = device;

	return 0;
}

void
path_input_destroy(struct path_input *input)
{
	libinput_destroy(&input->base);
	free(input->path);
}

static const struct libinput_interface_private interface_private = {
	.resume = path_input_enable,
	.suspend = path_input_disable,
};

LIBINPUT_EXPORT struct libinput *
libinput_create_from_path(const struct libinput_interface *interface,
			  void *user_data,
			  const char *path)
{
	struct path_input *input;

	if (!interface || !path)
		return NULL;

	input = zalloc(sizeof *input);
	if (!input)
		return NULL;

	if (libinput_init(&input->base, interface,
			  &interface_private, user_data) != 0) {
		free(input);
		return NULL;
	}

	input->path = strdup(path);

	if (path_input_enable(&input->base) < 0) {
		path_input_destroy(input);
		return NULL;
	}

	return &input->base;
}
