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
	struct path_seat *seat, *tmp;
	struct evdev_device *device, *next;

	list_for_each_safe(seat, tmp, &input->base.seat_list, base.link) {
		libinput_seat_ref(&seat->base);
		list_for_each_safe(device, next,
				   &seat->base.devices_list, base.link) {
			close_restricted(&input->base, device->fd);
			evdev_device_remove(device);
			if (list_empty(&seat->base.devices_list)) {
				/* if the seat may be referenced by the
				   client, so make sure it's dropped from
				   the seat list now, to be freed whenever
				 * the device is removed */
				list_remove(&seat->base.link);
				list_init(&seat->base.link);
			}
		}
		libinput_seat_unref(&seat->base);
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

static struct path_seat*
path_seat_get_named(struct path_input *input,
		    const char *seat_name_physical,
		    const char *seat_name_logical)
{
	struct path_seat *seat;

	list_for_each(seat, &input->base.seat_list, base.link) {
		if (strcmp(seat->base.physical_name, seat_name_physical) == 0 &&
		    strcmp(seat->base.logical_name, seat_name_logical) == 0)
			return seat;
	}

	return NULL;
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
path_device_enable(struct path_input *input, const char *devnode)
{
	struct libinput *libinput = &input->base;
	struct path_seat *seat;
	char *syspath;
	int fd;
	struct evdev_device *device;
	char *seat_name, *seat_logical_name;

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

	seat = path_seat_get_named(input, seat_name, seat_logical_name);

	if (seat)
		libinput_seat_ref(&seat->base);
	else {
		seat = path_seat_create(input, seat_name, seat_logical_name);
		if (!seat) {
			free(seat_name);
			free(seat_logical_name);
			free(syspath);
			close_restricted(libinput, fd);
			log_info("failed to create seat for device '%s'.\n", devnode);
			return -1;
		}
	}

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

	return 0;
}

static int
path_input_enable(struct libinput *libinput)
{
	struct path_input *input = (struct path_input*)libinput;
	struct path_device *dev;
	int rc = 0;

	list_for_each(dev, &input->path_list, link)
		rc += path_device_enable(input, dev->path);

	return rc;
}

static void
path_input_destroy(struct libinput *input)
{
	struct path_input *path_input = (struct path_input*)input;
	struct path_device *dev, *tmp;

	list_for_each_safe(dev, tmp, &path_input->path_list, link) {
		free(dev->path);
		free(dev);
	}

}

static const struct libinput_interface_backend interface_backend = {
	.resume = path_input_enable,
	.suspend = path_input_disable,
	.destroy = path_input_destroy,
};

LIBINPUT_EXPORT struct libinput *
libinput_path_create_from_device(const struct libinput_interface *interface,
				 void *user_data,
				 const char *path)
{
	struct path_input *input;
	struct path_device *dev;

	if (!interface || !path)
		return NULL;

	input = zalloc(sizeof *input);
	if (!input)
		return NULL;

	dev = zalloc(sizeof *dev);
	if (!dev) {
		free(input);
		free(dev);
		return NULL;
	}

	if (libinput_init(&input->base, interface,
			  &interface_backend, user_data) != 0) {
		free(input);
		free(dev);
		return NULL;
	}

	list_init(&input->path_list);

	dev->path = strdup(path);
	if (!dev->path) {
		free(input);
		free(dev);
		return NULL;
	}

	list_insert(&input->path_list, &dev->link);

	if (path_input_enable(&input->base) < 0) {
		libinput_unref(&input->base);
		return NULL;
	}

	return &input->base;
}
