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
#include <string.h>
#include <sys/stat.h>
#include <libudev.h>

#include "libinput-private.h"
#include "evfd-seat.h"
#include "evdev.h"

struct evfd_input {
	struct libinput base;
	struct udev *udev;
	struct libinput_device *device;
};

struct evfd_seat {
	struct libinput_seat base;
};

static const char default_seat[] = "seat0";
static const char default_seat_name[] = "default";

static void evfd_seat_destroy(struct libinput_seat *seat);

static void
evfd_input_disable(struct libinput *libinput)
{
	return;
}

static void
evfd_seat_destroy(struct libinput_seat *seat)
{
	struct evfd_seat *s = (struct evfd_seat*)seat;
	free(s);
}

static struct evfd_seat*
evfd_seat_create(struct evfd_input *input,
		    const char *seat_name,
		    const char *seat_logical_name)
{
	struct evfd_seat *seat;

	seat = zalloc(sizeof(*seat));
	if (!seat)
		return NULL;

	libinput_seat_init(&seat->base, &input->base, seat_name,
			   seat_logical_name, evfd_seat_destroy);

	return seat;
}

static struct evfd_seat*
evfd_seat_get_named(struct evfd_input *input,
		    const char *seat_name_physical,
		    const char *seat_name_logical)
{
	struct evfd_seat *seat;

	list_for_each(seat, &input->base.seat_list, base.link) {
		if (streq(seat->base.physical_name, seat_name_physical) &&
		    streq(seat->base.logical_name, seat_name_logical))
			return seat;
	}

	return NULL;
}

static struct libinput_device *
evfd_device_enable(struct evfd_input *input,
		   struct udev_device *udev_device,
		   const char *seat_logical_name_override)
{
	struct evfd_seat *seat;
	struct evdev_device *device = NULL;
	char *seat_name = NULL, *seat_logical_name = NULL;
	const char *seat_prop;
	const char *devnode;

	devnode = udev_device_get_devnode(udev_device);

	seat_prop = udev_device_get_property_value(udev_device, "ID_SEAT");
	seat_name = strdup(seat_prop ? seat_prop : default_seat);

	if (seat_logical_name_override) {
		seat_logical_name = strdup(seat_logical_name_override);
	} else {
		seat_prop = udev_device_get_property_value(udev_device, "WL_SEAT");
		seat_logical_name = strdup(seat_prop ? seat_prop : default_seat_name);
	}

	if (!seat_logical_name) {
		log_error(&input->base,
			  "failed to create seat name for device '%s'.\n",
			  devnode);
		goto out;
	}

	seat = evfd_seat_get_named(input, seat_name, seat_logical_name);

	if (seat) {
		libinput_seat_ref(&seat->base);
	} else {
		seat = evfd_seat_create(input, seat_name, seat_logical_name);
		if (!seat) {
			log_info(&input->base,
				 "failed to create seat for device '%s'.\n",
				 devnode);
			goto out;
		}
	}

	device = evdev_device_create(&seat->base, udev_device);
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

	evdev_read_calibration_prop(device);

out:
	free(seat_name);
	free(seat_logical_name);

	return device ? &device->base : NULL;
}

static int
evfd_input_enable(struct libinput *libinput)
{
	return -1;
}

static void
evfd_input_destroy(struct libinput *input)
{
	struct evfd_input *evfd_input = (struct evfd_input*)input;

	libinput_device_unref(evfd_input->device);
	udev_unref(evfd_input->udev);
}

static struct libinput_device *
evfd_create_device(struct libinput *libinput,
		   struct udev_device *udev_device,
		   const char *seat_name)
{
	struct evfd_input *input = (struct evfd_input*)libinput;
	struct libinput_device *device;

	device = evfd_device_enable(input, udev_device, seat_name);
	input->device = device;

	return device;
}

static int
evfd_device_change_seat(struct libinput_device *device,
			const char *seat_name)
{
	abort();
}

static const struct libinput_interface_backend interface_backend = {
	.resume = evfd_input_enable,
	.suspend = evfd_input_disable,
	.destroy = evfd_input_destroy,
	.device_change_seat = evfd_device_change_seat,
};

LIBINPUT_EXPORT struct libinput *
libinput_evfd_create_context(const struct libinput_interface *interface,
				void *user_data)
{
	struct evfd_input *input;
	struct udev *udev;

	if (!interface)
		return NULL;

	udev = udev_new();
	if (!udev)
		return NULL;

	input = zalloc(sizeof *input);
	if (!input ||
	    libinput_init(&input->base, interface,
			  &interface_backend, user_data) != 0) {
		udev_unref(udev);
		free(input);
		return NULL;
	}

	input->udev = udev;

	return &input->base;
}

static inline struct udev_device *
udev_device_from_devnode(struct libinput *libinput,
			 struct udev *udev,
			 const char *devnode)
{
	struct udev_device *dev;
	struct stat st;
	size_t count = 0;

	if (stat(devnode, &st) < 0)
		return NULL;

	dev = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

	while (dev && !udev_device_get_is_initialized(dev)) {
		udev_device_unref(dev);
		msleep(10);
		dev = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

		count++;
		if (count > 200) {
			log_bug_libinput(libinput,
					"udev device never initialized (%s)\n",
					devnode);
			break;
		}
	}

	return dev;
}

static inline void
evfd_input_change_fd(struct libinput *libinput,
		      struct libinput_device *device,
		      int fd)
{
	struct evdev_device *dev = evdev_device(device);
	libinput_source_dispatch_t dispatch;

	dispatch = libinput_source_get_dispatch(dev->source);
	libinput_remove_source(libinput, dev->source);
	libevdev_change_fd(dev->evdev, fd);
	dev->source = libinput_add_fd(libinput, fd, dispatch, dev);
}

LIBINPUT_EXPORT struct libinput_device *
libinput_evfd_add_device(struct libinput *libinput,
			    const char *path,
			    int event_fd)
{
	struct evfd_input *input = (struct evfd_input *)libinput;
	struct udev *udev = input->udev;
	struct udev_device *udev_device;
	struct libinput_device *device;

	if (libinput->interface_backend != &interface_backend) {
		log_bug_client(libinput, "Mismatching backends.\n");
		return NULL;
	}

	if (input->device != NULL) {
		log_bug_client(libinput, "Only one device allowed\n");
		abort();
	}

	udev_device = udev_device_from_devnode(libinput, udev, path);
	if (!udev_device) {
		log_bug_client(libinput, "Invalid path %s\n", path);
		return NULL;
	}

	device = evfd_create_device(libinput, udev_device, NULL);
	udev_device_unref(udev_device);

	if (device)
		evfd_input_change_fd(libinput, device, event_fd);

	return device;
}
