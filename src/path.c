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

#include <errno.h>
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
path_disable_device(struct libinput *libinput,
		    struct evdev_device *device)
{
	struct libinput_seat *seat = device->base.seat;
	struct evdev_device *dev, *next;

	list_for_each_safe(dev, next,
			   &seat->devices_list, base.link) {
		if (dev != device)
			continue;

		evdev_device_remove(device);
		break;
	}
}

static void
path_input_disable(struct libinput *libinput)
{
	struct path_input *input = (struct path_input*)libinput;
	struct path_seat *seat, *tmp;
	struct evdev_device *device, *next;

	list_for_each_safe(seat, tmp, &input->base.seat_list, base.link) {
		libinput_seat_ref(&seat->base);
		list_for_each_safe(device, next,
				   &seat->base.devices_list, base.link)
			path_disable_device(libinput, device);
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
			 char **sysname,
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

	*sysname = strdup(udev_device_get_sysname(device));
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

static struct libinput_device *
path_device_enable(struct path_input *input, const char *devnode)
{
	struct path_seat *seat;
	struct evdev_device *device = NULL;
	char *sysname = NULL, *syspath = NULL;
	char *seat_name = NULL, *seat_logical_name = NULL;

	if (path_get_udev_properties(devnode, &sysname, &syspath,
				     &seat_name, &seat_logical_name) == -1) {
		log_info(&input->base,
			 "failed to obtain sysname for device '%s'.\n",
			 devnode);
		return NULL;
	}

	seat = path_seat_get_named(input, seat_name, seat_logical_name);

	if (seat) {
		libinput_seat_ref(&seat->base);
	} else {
		seat = path_seat_create(input, seat_name, seat_logical_name);
		if (!seat) {
			log_info(&input->base,
				 "failed to create seat for device '%s'.\n",
				 devnode);
			goto out;
		}
	}

	device = evdev_device_create(&seat->base, devnode, sysname, syspath);
	libinput_seat_unref(&seat->base);

	if (device == EVDEV_UNHANDLED_DEVICE) {
		device = NULL;
		log_info(&input->base,
			 "not using input device '%s'.\n",
			 devnode);
		goto out;
	} else if (device == NULL) {
		log_info(&input->base,
			 "failed to create input device '%s'.\n",
			 devnode);
		goto out;
	}

out:
	free(sysname);
	free(syspath);
	free(seat_name);
	free(seat_logical_name);

	return device ? &device->base : NULL;
}

static int
path_input_enable(struct libinput *libinput)
{
	struct path_input *input = (struct path_input*)libinput;
	struct path_device *dev;

	list_for_each(dev, &input->path_list, link) {
		if (path_device_enable(input, dev->path) == NULL) {
			path_input_disable(libinput);
			return -1;
		}
	}

	return 0;
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
libinput_path_create_context(const struct libinput_interface *interface,
			     void *user_data)
{
	struct path_input *input;

	if (!interface)
		return NULL;

	input = zalloc(sizeof *input);
	if (!input)
		return NULL;

	if (libinput_init(&input->base, interface,
			  &interface_backend, user_data) != 0) {
		free(input);
		return NULL;
	}

	list_init(&input->path_list);

	return &input->base;
}

LIBINPUT_EXPORT struct libinput_device *
libinput_path_add_device(struct libinput *libinput,
			 const char *path)
{
	struct path_input *input = (struct path_input*)libinput;
	struct path_device *dev;
	struct libinput_device *device;

	if (libinput->interface_backend != &interface_backend) {
		log_bug_client(libinput, "Mismatching backends.\n");
		return NULL;
	}

	dev = zalloc(sizeof *dev);
	if (!dev)
		return NULL;

	dev->path = strdup(path);
	if (!dev->path) {
		free(dev);
		return NULL;
	}

	list_insert(&input->path_list, &dev->link);

	device = path_device_enable(input, dev->path);

	if (!device) {
		list_remove(&dev->link);
		free(dev->path);
		free(dev);
	}

	return device;
}

LIBINPUT_EXPORT void
libinput_path_remove_device(struct libinput_device *device)
{
	struct libinput *libinput = device->seat->libinput;
	struct path_input *input = (struct path_input*)libinput;
	struct libinput_seat *seat;
	struct evdev_device *evdev = (struct evdev_device*)device;
	struct path_device *dev;

	if (libinput->interface_backend != &interface_backend) {
		log_bug_client(libinput, "Mismatching backends.\n");
		return;
	}

	list_for_each(dev, &input->path_list, link) {
		if (strcmp(evdev->devnode, dev->path) == 0) {
			list_remove(&dev->link);
			free(dev->path);
			free(dev);
			break;
		}
	}

	seat = device->seat;
	libinput_seat_ref(seat);
	path_disable_device(libinput, evdev);
	libinput_seat_unref(seat);
}
