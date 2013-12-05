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
udev_seat_create(struct udev_input *input, const char *seat_name);
static struct udev_seat *
udev_seat_get_named(struct udev_input *input, const char *seat_name);

static int
device_added(struct udev_device *udev_device, struct udev_input *input)
{
	struct libinput *libinput = &input->base;
	struct evdev_device *device;
	const char *devnode;
	const char *device_seat, *seat_name, *output_name;
	const char *calibration_values;
	int fd;
	struct udev_seat *seat;

	device_seat = udev_device_get_property_value(udev_device, "ID_SEAT");
	if (!device_seat)
		device_seat = default_seat;

	if (strcmp(device_seat, input->seat_id))
		return 0;

	devnode = udev_device_get_devnode(udev_device);

	/* Search for matching logical seat */
	seat_name = udev_device_get_property_value(udev_device, "WL_SEAT");
	if (!seat_name)
		seat_name = default_seat_name;

	seat = udev_seat_get_named(input, seat_name);

	if (seat == NULL)
		return -1;

	/* Use non-blocking mode so that we can loop on read on
	 * evdev_device_data() until all events on the fd are
	 * read.  mtdev_get() also expects this. */
	fd = open_restricted(libinput, devnode, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		log_info("opening input device '%s' failed.\n", devnode);
		return 0;
	}

	device = evdev_device_create(&seat->base, devnode, fd);
	if (device == EVDEV_UNHANDLED_DEVICE) {
		close_restricted(libinput, fd);
		log_info("not using input device '%s'.\n", devnode);
		return 0;
	} else if (device == NULL) {
		close_restricted(libinput, fd);
		log_info("failed to create input device '%s'.\n", devnode);
		return 0;
	}

	calibration_values =
		udev_device_get_property_value(udev_device,
					       "WL_CALIBRATION");

	if (calibration_values && sscanf(calibration_values,
					 "%f %f %f %f %f %f",
					 &device->abs.calibration[0],
					 &device->abs.calibration[1],
					 &device->abs.calibration[2],
					 &device->abs.calibration[3],
					 &device->abs.calibration[4],
					 &device->abs.calibration[5]) == 6) {
		device->abs.apply_calibration = 1;
		log_info("Applying calibration: %f %f %f %f %f %f\n",
			 device->abs.calibration[0],
			 device->abs.calibration[1],
			 device->abs.calibration[2],
			 device->abs.calibration[3],
			 device->abs.calibration[4],
			 device->abs.calibration[5]);
	}

	output_name = udev_device_get_property_value(udev_device, "WL_OUTPUT");
	if (output_name)
		device->output_name = strdup(output_name);

	return 0;
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
	struct libinput *libinput = &input->base;
	struct udev_device *udev_device;
	struct evdev_device *device, *next;
	const char *action;
	const char *devnode;
	struct udev_seat *seat;

	udev_device = udev_monitor_receive_device(input->udev_monitor);
	if (!udev_device)
		return;

	action = udev_device_get_action(udev_device);
	if (!action)
		goto out;

	if (strncmp("event", udev_device_get_sysname(udev_device), 5) != 0)
		goto out;

	if (!strcmp(action, "add")) {
		device_added(udev_device, input);
	}
	else if (!strcmp(action, "remove")) {
		devnode = udev_device_get_devnode(udev_device);
		list_for_each(seat, &input->base.seat_list, base.link) {
			list_for_each_safe(device, next,
					   &seat->base.devices_list, base.link)
				if (!strcmp(device->devnode, devnode)) {
					log_info("input device %s, %s removed\n",
						 device->devname, device->devnode);
					close_restricted(libinput, device->fd);
					evdev_device_remove(device);
					break;
				}
		}
	}

out:
	udev_device_unref(udev_device);
}

int
udev_input_enable(struct udev_input *input)
{
	struct udev *udev = input->udev;
	int fd;

	input->udev_monitor = udev_monitor_new_from_netlink(udev, "udev");
	if (!input->udev_monitor) {
		log_info("udev: failed to create the udev monitor\n");
		return -1;
	}

	udev_monitor_filter_add_match_subsystem_devtype(input->udev_monitor,
			"input", NULL);

	if (udev_monitor_enable_receiving(input->udev_monitor)) {
		log_info("udev: failed to bind the udev monitor\n");
		udev_monitor_unref(input->udev_monitor);
		return -1;
	}

	fd = udev_monitor_get_fd(input->udev_monitor);
	input->udev_monitor_source = libinput_add_fd(&input->base,
						     fd,
						     evdev_udev_handler,
						     input);
	if (!input->udev_monitor_source) {
		udev_monitor_unref(input->udev_monitor);
		return -1;
	}

	if (udev_input_add_devices(input, udev) < 0) {
		udev_input_disable(input);
		return -1;
	}

	return 0;
}

static void
udev_input_remove_devices(struct udev_input *input)
{
	struct evdev_device *device, *next;
	struct udev_seat *seat;

	list_for_each(seat, &input->base.seat_list, base.link) {
		list_for_each_safe(device, next,
				   &seat->base.devices_list, base.link) {
			close_restricted(&input->base, device->fd);
			evdev_device_remove(device);
		}
	}
}

void
udev_input_disable(struct udev_input *input)
{
	if (!input->udev_monitor)
		return;

	udev_monitor_unref(input->udev_monitor);
	input->udev_monitor = NULL;
	libinput_remove_source(&input->base, input->udev_monitor_source);
	input->udev_monitor_source = NULL;

	udev_input_remove_devices(input);
}

void
udev_input_destroy(struct udev_input *input)
{
	struct libinput_seat *seat, *next;
	udev_input_disable(input);
	list_for_each_safe(seat, next, &input->base.seat_list, link) {
		notify_removed_seat(seat);
		libinput_seat_unref(seat);
	}
	udev_unref(input->udev);
	free(input->seat_id);
}

static struct udev_seat *
udev_seat_create(struct udev_input *input, const char *seat_name)
{
	struct udev_seat *seat;

	seat = zalloc(sizeof *seat);
	if (!seat)
		return NULL;

	libinput_seat_init(&seat->base, &input->base, seat_name);
	list_insert(&input->base.seat_list, &seat->base.link);
	notify_added_seat(&seat->base);

	return seat;
}

void
udev_seat_destroy(struct udev_seat *seat)
{
	list_remove(&seat->base.link);
	free(seat);
}

static struct udev_seat *
udev_seat_get_named(struct udev_input *input, const char *seat_name)
{
	struct udev_seat *seat;

	list_for_each(seat, &input->base.seat_list, base.link) {
		if (strcmp(seat->base.name, seat_name) == 0)
			return seat;
	}

	seat = udev_seat_create(input, seat_name);

	if (!seat)
		return NULL;

	return seat;
}

LIBINPUT_EXPORT struct libinput *
libinput_create_udev(const struct libinput_interface *interface,
		     void *user_data,
		     struct udev *udev,
		     const char *seat_id)
{
	struct udev_input *input;

	input = zalloc(sizeof *input);
	if (!input)
		return NULL;

	if (libinput_init(&input->base, interface, user_data) != 0) {
		free(input);
		return NULL;
	}

	input->udev = udev_ref(udev);
	input->seat_id = strdup(seat_id);

	if (udev_input_enable(input) < 0) {
		udev_unref(udev);
		libinput_destroy(&input->base);
		free(input);
		return NULL;
	}

	return &input->base;
}
