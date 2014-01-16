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

static const char default_seat[] = "seat0";
static const char default_seat_name[] = "default";

int path_input_process_event(struct libinput_event);
static void path_seat_destroy(struct libinput_seat *seat);

static void
path_input_disable(struct libinput *libinput)
{
	struct path_input *input = (struct path_input*)libinput;
	struct evdev_device *device = input->device;

	if (device) {
		close_restricted(libinput, device->fd);
		evdev_device_remove(device);
		input->device = NULL;
	}
}

static void
path_seat_destroy(struct libinput_seat *seat)
{
	struct path_seat *pseat = (struct path_seat*)seat;
	free(pseat);
}

static struct path_seat*
path_seat_create(struct path_input *input,
		 const char *seat_name,
		 const char *seat_logical_name)
{
	struct path_seat *seat;

	seat = zalloc(sizeof(*seat));
	if (!seat)
		return NULL;

	libinput_seat_init(&seat->base, &input->base, seat_name,
			   seat_logical_name, path_seat_destroy);
	list_insert(&input->base.seat_list, &seat->base.link);

	return seat;
}

static int
path_get_udev_properties(const char *path,
			 char **syspath,
			 char **seat_name,
			 char **seat_logical_name)
{
	struct udev *udev = NULL;
	struct udev_device *device = NULL;
	struct stat st;
	const char *seat;
	int rc = -1;

	udev = udev_new();
	if (!udev)
		goto out;

	if (stat(path, &st) < 0)
		goto out;

	device = udev_device_new_from_devnum(udev, 'c', st.st_rdev);
	if (!device)
		goto out;

	*syspath = strdup(udev_device_get_syspath(device));

	seat = udev_device_get_property_value(device, "ID_SEAT");
	*seat_name = strdup(seat ? seat : default_seat);

	seat = udev_device_get_property_value(device, "WL_SEAT");
	*seat_logical_name = strdup(seat ? seat : default_seat_name);

	rc = 0;

out:
	if (device)
		udev_device_unref(device);
	if (udev)
		udev_unref(udev);
	return rc;
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
	char *seat_name, *seat_logical_name;

	if (input->device)
		return 0;

	fd = open_restricted(libinput, devnode, O_RDWR|O_NONBLOCK);
	if (fd < 0) {
		log_info("opening input device '%s' failed.\n", devnode);
		return -1;
	}

	if (path_get_udev_properties(devnode, &syspath,
				     &seat_name, &seat_logical_name) == -1) {
		close_restricted(libinput, fd);
		log_info("failed to obtain syspath for device '%s'.\n", devnode);
		return -1;
	}

	seat = path_seat_create(input, seat_name, seat_logical_name);
	free(seat_name);
	free(seat_logical_name);

	device = evdev_device_create(&seat->base, devnode, syspath, fd);
	free(syspath);
	libinput_seat_unref(&seat->base);

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

static void
path_input_destroy(struct libinput *input)
{
	struct path_input *path_input = (struct path_input*)input;
	free(path_input->path);
}

static const struct libinput_interface_backend interface_backend = {
	.resume = path_input_enable,
	.suspend = path_input_disable,
	.destroy = path_input_destroy,
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
			  &interface_backend, user_data) != 0) {
		free(input);
		return NULL;
	}

	input->path = strdup(path);

	if (path_input_enable(&input->base) < 0) {
		libinput_unref(&input->base);
		return NULL;
	}

	return &input->base;
}
