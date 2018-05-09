/*
 * Copyright © 2018 Red Hat, Inc.
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

#include <varlink.h>

#include "libinput-private.h"

struct varlink {
	struct libinput *libinput;
	VarlinkService *service;
	struct libinput_source *source;
};

static long
org_freedesktop_libinput_Devices(VarlinkService *service,
				 VarlinkCall *call,
				 VarlinkObject *parameters,
				 uint64_t flags,
				 void *userdata)
{
	struct libinput  *libinput = userdata;
	VarlinkObject *out = NULL;
	VarlinkArray *array = NULL;
	long rc;
	struct libinput_seat *seat;
	struct libinput_device *device;

	rc = varlink_object_new(&out);
	if (rc != 0)
		goto out;

	rc = varlink_array_new(&array);
	if (rc != 0)
		goto out;

	list_for_each(seat, &libinput->seat_list, link) {
		list_for_each(device, &seat->devices_list, link) {
			varlink_array_append_string(array,
						    libinput_device_get_sysname(device));

		}
	}

	varlink_object_set_array(out, "devices", array);

	rc = varlink_call_reply(call, out, 0);
out:
	if (out)
		varlink_object_unref(out);
	if (array)
		varlink_array_unref(array);
	return rc;
}

static void
varlink_dispatch_event(void *data)
{
	VarlinkService *service = data;

	varlink_service_process_events(service);
}

const char *interface =
	"interface org.freedesktop.libinput\n"
	"method Devices() -> (devices: []string)\n"
	"";


static struct varlink *
varlink_setup(struct libinput *libinput)
{
	VarlinkService *service = NULL;
	struct varlink *v = NULL;
	long rc;


	/* FIXME: do we have permissions? */
	rc = varlink_service_new(&service,
				 "Freedesktop",
				 "libinput",
				 "1",
				 "https://wayland.freedesktop.org/libinput/",
				 "unix:@libinput.socket",
				 -1);
	if (rc != 0)
		goto error;

	rc = varlink_service_add_interface(service,
					   interface,
					   "Devices", org_freedesktop_libinput_Devices, libinput,
					   NULL);
	if (rc != 0)
		goto error;

	v = zalloc(sizeof *v);
	v->libinput = libinput;
	v->service = service;
	v->source = libinput_add_fd(libinput,
				    varlink_service_get_fd(service),
				    varlink_dispatch_event,
				    service);
	return v;
error:
	free(v);
	if (service)
		varlink_service_free(service);
	return NULL;
}

/* FIXME: probably needs a name passed through */
LIBINPUT_EXPORT int
libinput_add_varlink_socket(struct libinput *libinput)
{
	libinput->varlink = varlink_setup(libinput);
	return libinput->varlink != NULL;
}
