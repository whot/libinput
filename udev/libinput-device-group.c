/*
 * Copyright © 2015 Red Hat, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libudev.h>

#include "libinput-util.h"

#if HAVE_LIBWACOM_GET_PAIRED_DEVICE
#include <libwacom/libwacom.h>

static void
wacom_handle_paired(struct udev_device *device,
		    int *vendor_id,
		    int *product_id)
{
	WacomDeviceDatabase *db = NULL;
	WacomDevice *tablet = NULL;
	const WacomMatch *paired;

	db = libwacom_database_new();
	if (!db)
		goto out;

	tablet = libwacom_new_from_usbid(db, *vendor_id, *product_id, NULL);
	if (!tablet)
		goto out;
	paired = libwacom_get_paired_device(tablet);
	if (!paired)
		goto out;

	*vendor_id = libwacom_match_get_vendor_id(paired);
	*product_id = libwacom_match_get_product_id(paired);

out:
	if (tablet)
		libwacom_destroy(tablet);
	if (db)
		libwacom_database_destroy(db);
}
#endif

int main(int argc, char **argv)
{
	int rc = 1;
	struct udev *udev = NULL;
	struct udev_device *device = NULL;
	const char *syspath,
	           *phys = NULL;
	const char *product;
	int bustype, vendor_id, product_id, version;
	char group[1024];
	char *str;

	if (argc != 2)
		return 1;

	syspath = argv[1];

	udev = udev_new();
	if (!udev)
		goto out;

	device = udev_device_new_from_syspath(udev, syspath);
	if (!device)
		goto out;

	/* Find the first parent with ATTRS{phys} set. For tablets that
	 * value looks like usb-0000:00:14.0-1/input1. Drop the /input1
	 * bit and use the remainder as device group identifier */
	while (device != NULL) {
		struct udev_device *parent;

		phys = udev_device_get_sysattr_value(device, "phys");
		if (phys)
			break;

		parent = udev_device_get_parent(device);
		udev_device_ref(parent);
		udev_device_unref(device);
		device = parent;
	}

	if (!phys)
		goto out;

	/* udev sets PRODUCT on the same device we find PHYS on, let's rely
	   on that*/
	product = udev_device_get_property_value(device, "PRODUCT");
	if (!product)
		product = "00/00/00/00";

	if (sscanf(product,
		   "%x/%x/%x/%x",
		   &bustype,
		   &vendor_id,
		   &product_id,
		   &version) != 4) {
		snprintf(group, sizeof(group), "%s:%s", product, phys);
	} else {
#if HAVE_LIBWACOM_GET_PAIRED_DEVICE
	    if (vendor_id == VENDOR_ID_WACOM)
		    wacom_handle_paired(device, &vendor_id, &product_id);
#endif
	    snprintf(group,
		     sizeof(group),
		     "%x/%x/%x/%x:%s",
		     bustype,
		     vendor_id,
		     product_id,
		     version,
		     phys);
	}

	str = strstr(group, "/input");
	if (str)
		*str = '\0';

	/* Cintiq 22HD Touch has
	   usb-0000:00:14.0-6.3.1/input0 for the touch
	   usb-0000:00:14.0-6.3.0/input0 for the pen
	   Check if there's a . after the last -, if so, cut off the string
	   there.
	  */
	str = strrchr(group, '.');
	if (str && str > strrchr(group, '-'))
		*str = '\0';

	printf("%s\n", group);

	rc = 0;
out:
	if (device)
		udev_device_unref(device);
	if (udev)
		udev_unref(udev);

	return rc;
}
