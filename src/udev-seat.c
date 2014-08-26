/*
 * Copyright © 2013 Intel Corporation
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "evdev.h"
#include "udev-seat.h"

static const char default_seat[] = "seat0";
static const char default_seat_name[] = "default";

static struct udev_seat *
udev_seat_create(struct udev_input *input,
		 const char *device_seat,
		 const char *seat_name);
static struct udev_seat *
udev_seat_get_named(struct udev_input *input, const char *seat_name);

static int
device_added(struct udev_device *udev_device, struct udev_input *input)
{
	struct evdev_device *device;
	const char *devnode;
	const char *sysname;
	const char *device_seat, *seat_name, *output_name;
	const char *calibration_values;
	float calibration[6];
	struct udev_seat *seat;

	device_seat = udev_device_get_property_value(udev_device, "ID_SEAT");
	if (!device_seat)
		device_seat = default_seat;

	if (strcmp(device_seat, input->seat_id))
		return 0;

	devnode = udev_device_get_devnode(udev_device);
	sysname = udev_device_get_sysname(udev_device);

	/* Search for matching logical seat */
	seat_name = udev_device_get_property_value(udev_device, "WL_SEAT");
	if (!seat_name)
		seat_name = default_seat_name;

	seat = udev_seat_get_named(input, seat_name);

	if (seat)
		libinput_seat_ref(&seat->base);
	else {
		seat = udev_seat_create(input, device_seat, seat_name);
		if (!seat)
			return -1;
	}

	device = evdev_device_create(&seat->base, devnode, sysname);
	libinput_seat_unref(&seat->base);

	if (device == EVDEV_UNHANDLED_DEVICE) {
		log_info(&input->base, "not using input device '%s'.\n", devnode);
		return 0;
	} else if (device == NULL) {
		log_info(&input->base, "failed to create input device '%s'.\n", devnode);
		return 0;
	}

	calibration_values =
		udev_device_get_property_value(udev_device,
					       "LIBINPUT_CALIBRATION_MATRIX");

	if (calibration_values && sscanf(calibration_values,
					 "%f %f %f %f %f %f",
					 &calibration[0],
					 &calibration[1],
					 &calibration[2],
					 &calibration[3],
					 &calibration[4],
					 &calibration[5]) == 6) {
		evdev_device_set_default_calibration(device, calibration);
		log_info(&input->base,
			 "Applying calibration: %f %f %f %f %f %f\n",
			 calibration[0],
			 calibration[1],
			 calibration[2],
			 calibration[3],
			 calibration[4],
			 calibration[5]);
	}

	output_name = udev_device_get_property_value(udev_device, "WL_OUTPUT");
	if (output_name)
		device->output_name = strdup(output_name);

	return 0;
}

static void
device_removed(struct udev_device *udev_device, struct udev_input *input)
{
	const char *devnode;
	struct evdev_device *device, *next;
	struct udev_seat *seat;

	devnode = udev_device_get_devnode(udev_device);
	list_for_each(seat, &input->base.seat_list, base.link) {
		list_for_each_safe(device, next,
				   &seat->base.devices_list, base.link) {
			if (!strcmp(device->devnode, devnode)) {
				log_info(&input->base,
					 "input device %s, %s removed\n",
					 device->devname, device->devnode);
				evdev_device_remove(device);
				break;
			}
		}
	}
}

static int
udev_input_add_devices(struct udev_input *input, struct udev *udev)
{
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	struct udev_device *device;
	const char *path, *sysname;

	e = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(e, "input");
	udev_enumerate_scan_devices(e);
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		path = udev_list_entry_get_name(entry);
		device = udev_device_new_from_syspath(udev, path);

		sysname = udev_device_get_sysname(device);
		if (strncmp("event", sysname, 5) != 0) {
			udev_device_unref(device);
			continue;
		}

		if (device_added(device, input) < 0) {
			udev_device_unref(device);
			udev_enumerate_unref(e);
			return -1;
		}

		udev_device_unref(device);
	}
	udev_enumerate_unref(e);

	return 0;
}

static void
evdev_udev_handler(void *data)
{
	struct udev_input *input = data;
	struct udev_device *udev_device;
	const char *action;

	udev_device = udev_monitor_receive_device(input->udev_monitor);
	if (!udev_device)
		return;

	action = udev_device_get_action(udev_device);
	if (!action)
		goto out;

	if (strncmp("event", udev_device_get_sysname(udev_device), 5) != 0)
		goto out;

	if (!strcmp(action, "add"))
		device_added(udev_device, input);
	else if (!strcmp(action, "remove"))
		device_removed(udev_device, input);

out:
	udev_device_unref(udev_device);
}

static void
udev_input_remove_devices(struct udev_input *input)
{
	struct evdev_device *device, *next;
	struct udev_seat *seat, *tmp;

	list_for_each_safe(seat, tmp, &input->base.seat_list, base.link) {
		libinput_seat_ref(&seat->base);
		list_for_each_safe(device, next,
				   &seat->base.devices_list, base.link) {
			evdev_device_remove(device);
		}
		libinput_seat_unref(&seat->base);
	}
}

static void
udev_input_disable(struct libinput *libinput)
{
	struct udev_input *input = (struct udev_input*)libinput;

	if (!input->udev_monitor)
		return;

	udev_monitor_unref(input->udev_monitor);
	input->udev_monitor = NULL;
	libinput_remove_source(&input->base, input->udev_monitor_source);
	input->udev_monitor_source = NULL;

	udev_input_remove_devices(input);
}

static int
udev_input_enable(struct libinput *libinput)
{
	struct udev_input *input = (struct udev_input*)libinput;
	struct udev *udev = input->udev;
	int fd;

	if (input->udev_monitor)
		return 0;

	input->udev_monitor = udev_monitor_new_from_netlink(udev, "udev");
	if (!input->udev_monitor) {
		log_info(libinput,
			 "udev: failed to create the udev monitor\n");
		return -1;
	}

	udev_monitor_filter_add_match_subsystem_devtype(input->udev_monitor,
			"input", NULL);

	if (udev_monitor_enable_receiving(input->udev_monitor)) {
		log_info(libinput, "udev: failed to bind the udev monitor\n");
		udev_monitor_unref(input->udev_monitor);
		input->udev_monitor = NULL;
		return -1;
	}

	fd = udev_monitor_get_fd(input->udev_monitor);
	input->udev_monitor_source = libinput_add_fd(&input->base,
						     fd,
						     evdev_udev_handler,
						     input);
	if (!input->udev_monitor_source) {
		udev_monitor_unref(input->udev_monitor);
		input->udev_monitor = NULL;
		return -1;
	}

	if (udev_input_add_devices(input, udev) < 0) {
		udev_input_disable(libinput);
		return -1;
	}

	return 0;
}

static void
udev_input_destroy(struct libinput *input)
{
	struct udev_input *udev_input = (struct udev_input*)input;

	if (input == NULL)
		return;

	udev_unref(udev_input->udev);
	free(udev_input->seat_id);
}

static void
udev_seat_destroy(struct libinput_seat *seat)
{
	struct udev_seat *useat = (struct udev_seat*)seat;
	free(useat);
}

static struct udev_seat *
udev_seat_create(struct udev_input *input,
		 const char *device_seat,
		 const char *seat_name)
{
	struct udev_seat *seat;

	seat = zalloc(sizeof *seat);
	if (!seat)
		return NULL;

	libinput_seat_init(&seat->base, &input->base,
			   device_seat, seat_name,
			   udev_seat_destroy);

	return seat;
}

static struct udev_seat *
udev_seat_get_named(struct udev_input *input, const char *seat_name)
{
	struct udev_seat *seat;

	list_for_each(seat, &input->base.seat_list, base.link) {
		if (strcmp(seat->base.logical_name, seat_name) == 0)
			return seat;
	}

	return NULL;
}

static const struct libinput_interface_backend interface_backend = {
	.resume = udev_input_enable,
	.suspend = udev_input_disable,
	.destroy = udev_input_destroy,
};

LIBINPUT_EXPORT struct libinput *
libinput_udev_create_context(const struct libinput_interface *interface,
			     void *user_data,
			     struct udev *udev)
{
	struct udev_input *input;

	if (!interface || !udev)
		return NULL;

	input = zalloc(sizeof *input);
	if (!input)
		return NULL;

	if (libinput_init(&input->base, interface,
			  &interface_backend, user_data) != 0) {
		libinput_unref(&input->base);
		free(input);
		return NULL;
	}

	input->udev = udev_ref(udev);

	return &input->base;
}

LIBINPUT_EXPORT int
libinput_udev_assign_seat(struct libinput *libinput,
			  const char *seat_id)
{
	struct udev_input *input = (struct udev_input*)libinput;

	if (!seat_id)
		return -1;
	if (input->seat_id != NULL)
		return -1;

	if (libinput->interface_backend != &interface_backend) {
		log_bug_client(libinput, "Mismatching backends.\n");
		return -1;
	}

	input->seat_id = strdup(seat_id);

	if (udev_input_enable(&input->base) < 0)
		return -1;

	return 0;
}
