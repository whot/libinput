/*
 * Copyright © 2013 Jonas Ådahl
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <assert.h>

#include "libinput.h"
#include "libinput-private.h"
#include "evdev.h"
#include "udev-seat.h"

enum libinput_event_class {
	LIBINPUT_EVENT_CLASS_BASE,
	LIBINPUT_EVENT_CLASS_SEAT,
	LIBINPUT_EVENT_CLASS_DEVICE,
};

union libinput_event_target {
	struct libinput *libinput;
	struct libinput_seat *seat;
	struct libinput_device *device;
};

struct libinput_source {
	libinput_source_dispatch_t dispatch;
	void *user_data;
	int fd;
	struct list link;
};

struct libinput_event {
	enum libinput_event_type type;
	struct libinput *libinput;
	union libinput_event_target target;
};

struct libinput_event_seat_notify {
	struct libinput_event base;
	struct libinput_seat *seat;
};

struct libinput_event_device_notify {
	struct libinput_event base;
	struct libinput_device *device;
};

struct libinput_event_device_capability_notify {
	struct libinput_event base;
	enum libinput_device_capability capability;
};

struct libinput_event_keyboard {
	struct libinput_event base;
	uint32_t time;
	uint32_t key;
	enum libinput_keyboard_key_state state;
};

struct libinput_event_pointer {
	struct libinput_event base;
	uint32_t time;
};

struct libinput_event_pointer_motion {
	struct libinput_event_pointer base;
	li_fixed_t x;
	li_fixed_t y;
};

struct libinput_event_pointer_button {
	struct libinput_event_pointer base;
	uint32_t button;
	enum libinput_pointer_button_state state;
};

struct libinput_event_pointer_axis {
	struct libinput_event_pointer base;
	enum libinput_pointer_axis axis;
	li_fixed_t value;
};

struct libinput_event_touch {
	struct libinput_event base;
	uint32_t time;
	uint32_t slot;
	li_fixed_t x;
	li_fixed_t y;
	enum libinput_touch_type touch_type;
};

static void
libinput_post_event(struct libinput *libinput,
		    struct libinput_event *event);

LIBINPUT_EXPORT enum libinput_event_type
libinput_event_get_type(struct libinput_event *event)
{
	return event->type;
}

LIBINPUT_EXPORT struct libinput*
libinput_event_get_context(struct libinput_event *event)
{
	return event->libinput;
}

LIBINPUT_EXPORT struct libinput_seat*
libinput_event_get_seat(struct libinput_event *event)
{
	struct libinput_seat *seat = NULL;

	switch (event->type) {
		case LIBINPUT_EVENT_SEAT_ADDED:
		case LIBINPUT_EVENT_SEAT_REMOVED:
			seat = ((struct libinput_event_seat_notify*)event)->seat;
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			seat = ((struct libinput_event_device_notify*)event)->device->seat;
		case LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY:
		case LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY:
		case LIBINPUT_EVENT_KEYBOARD_KEY:
		case LIBINPUT_EVENT_POINTER_MOTION:
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		case LIBINPUT_EVENT_POINTER_BUTTON:
		case LIBINPUT_EVENT_POINTER_AXIS:
		case LIBINPUT_EVENT_TOUCH_TOUCH:
			seat = event->target.device->seat;
	}

	if (seat)
		libinput_seat_ref(seat);

	return seat;
}

LIBINPUT_EXPORT struct libinput_device *
libinput_event_get_device(struct libinput_event *event)
{
	struct libinput_device *device = NULL;

	switch (event->type) {
		case LIBINPUT_EVENT_SEAT_ADDED:
		case LIBINPUT_EVENT_SEAT_REMOVED:
			break;
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			device = ((struct libinput_event_device_notify*)event)->device;
		case LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY:
		case LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY:
		case LIBINPUT_EVENT_KEYBOARD_KEY:
		case LIBINPUT_EVENT_POINTER_MOTION:
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		case LIBINPUT_EVENT_POINTER_BUTTON:
		case LIBINPUT_EVENT_POINTER_AXIS:
		case LIBINPUT_EVENT_TOUCH_TOUCH:
			device = event->target.device;
	}

	if (device)
		libinput_device_ref(device);

	return device;
}

LIBINPUT_EXPORT struct libinput_event_pointer*
libinput_event_get_pointer_event(struct libinput_event *event)
{
	switch (event->type) {
		case LIBINPUT_EVENT_SEAT_ADDED:
		case LIBINPUT_EVENT_SEAT_REMOVED:
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
		case LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY:
		case LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY:
		case LIBINPUT_EVENT_KEYBOARD_KEY:
			break;
		case LIBINPUT_EVENT_POINTER_MOTION:
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		case LIBINPUT_EVENT_POINTER_BUTTON:
		case LIBINPUT_EVENT_POINTER_AXIS:
			return (struct libinput_event_pointer*)event;
		case LIBINPUT_EVENT_TOUCH_TOUCH:
			break;
	}

	return NULL;
}

LIBINPUT_EXPORT struct libinput_event_keyboard*
libinput_event_get_keyboard_event(struct libinput_event *event)
{
	switch (event->type) {
		case LIBINPUT_EVENT_SEAT_ADDED:
		case LIBINPUT_EVENT_SEAT_REMOVED:
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
		case LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY:
		case LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY:
			break;
		case LIBINPUT_EVENT_KEYBOARD_KEY:
			return (struct libinput_event_keyboard*)event;
		case LIBINPUT_EVENT_POINTER_MOTION:
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		case LIBINPUT_EVENT_POINTER_BUTTON:
		case LIBINPUT_EVENT_POINTER_AXIS:
		case LIBINPUT_EVENT_TOUCH_TOUCH:
			break;
	}

	return NULL;
}

LIBINPUT_EXPORT struct libinput_event_touch*
libinput_event_get_touch_event(struct libinput_event *event)
{
	switch (event->type) {
		case LIBINPUT_EVENT_SEAT_ADDED:
		case LIBINPUT_EVENT_SEAT_REMOVED:
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
		case LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY:
		case LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY:
		case LIBINPUT_EVENT_KEYBOARD_KEY:
		case LIBINPUT_EVENT_POINTER_MOTION:
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		case LIBINPUT_EVENT_POINTER_BUTTON:
		case LIBINPUT_EVENT_POINTER_AXIS:
			break;
		case LIBINPUT_EVENT_TOUCH_TOUCH:
			return (struct libinput_event_touch*)event;
	}

	return NULL;
}

LIBINPUT_EXPORT struct libinput_event_seat_notify*
libinput_event_get_seat_notify_event(struct libinput_event *event)
{
	switch (event->type) {
		case LIBINPUT_EVENT_SEAT_ADDED:
		case LIBINPUT_EVENT_SEAT_REMOVED:
			return (struct libinput_event_seat_notify*)event;
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
		case LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY:
		case LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY:
		case LIBINPUT_EVENT_KEYBOARD_KEY:
		case LIBINPUT_EVENT_POINTER_MOTION:
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		case LIBINPUT_EVENT_POINTER_BUTTON:
		case LIBINPUT_EVENT_POINTER_AXIS:
		case LIBINPUT_EVENT_TOUCH_TOUCH:
			break;
	}

	return NULL;
}

LIBINPUT_EXPORT struct libinput_event_device_notify*
libinput_event_get_device_notify_event(struct libinput_event *event)
{
	switch (event->type) {
		case LIBINPUT_EVENT_SEAT_ADDED:
		case LIBINPUT_EVENT_SEAT_REMOVED:
			break;
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			return (struct libinput_event_device_notify*)event;
		case LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY:
		case LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY:
		case LIBINPUT_EVENT_KEYBOARD_KEY:
		case LIBINPUT_EVENT_POINTER_MOTION:
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		case LIBINPUT_EVENT_POINTER_BUTTON:
		case LIBINPUT_EVENT_POINTER_AXIS:
		case LIBINPUT_EVENT_TOUCH_TOUCH:
			break;
	}

	return NULL;
}

/**
 * @ingroup event
 *
 * Return the capability event that is this input event. If the event type does
 * not match the capability event types, this function returns NULL.
 *
 * @return A capability event, or NULL for other events
 */
struct libinput_event_device_capability_notify*
libinput_event_get_device_capability_notify_event(struct libinput_event *event)
{
	switch (event->type) {
		case LIBINPUT_EVENT_SEAT_ADDED:
		case LIBINPUT_EVENT_SEAT_REMOVED:
		case LIBINPUT_EVENT_DEVICE_ADDED:
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			break;
		case LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY:
		case LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY:
			return (struct libinput_event_device_capability_notify*)event;
		case LIBINPUT_EVENT_KEYBOARD_KEY:
		case LIBINPUT_EVENT_POINTER_MOTION:
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		case LIBINPUT_EVENT_POINTER_BUTTON:
		case LIBINPUT_EVENT_POINTER_AXIS:
		case LIBINPUT_EVENT_TOUCH_TOUCH:
			break;
	}

	return NULL;
}


LIBINPUT_EXPORT enum libinput_device_capability
libinput_event_device_capability_notify_get_capability(
	struct libinput_event_device_capability_notify *event)
{
	return event->capability;
}

LIBINPUT_EXPORT uint32_t
libinput_event_keyboard_get_time(
	struct libinput_event_keyboard *event)
{
	return event->time;
}

LIBINPUT_EXPORT uint32_t
libinput_event_keyboard_get_key(
	struct libinput_event_keyboard *event)
{
	return event->key;
}

LIBINPUT_EXPORT enum libinput_keyboard_key_state
libinput_event_keyboard_get_key_state(
	struct libinput_event_keyboard *event)
{
	return event->state;
}

LIBINPUT_EXPORT uint32_t
libinput_event_pointer_get_time(
	struct libinput_event_pointer *event)
{
	return event->time;
}

LIBINPUT_EXPORT li_fixed_t
libinput_event_pointer_get_dx(
	struct libinput_event_pointer *event)
{
	struct libinput_event_pointer_motion *ptrev;
	ptrev = container_of(event, ptrev, base);
	return ptrev->x;
}

LIBINPUT_EXPORT li_fixed_t
libinput_event_pointer_get_dy(
	struct libinput_event_pointer *event)
{
	struct libinput_event_pointer_motion *ptrev;
	ptrev = container_of(event, ptrev, base);
	return ptrev->y;
}

LIBINPUT_EXPORT li_fixed_t
libinput_event_pointer_get_absolute_x(
	struct libinput_event_pointer *event)
{
	struct libinput_event_pointer_motion *ptrev;
	ptrev = container_of(event, ptrev, base);
	return ptrev->x;
}

LIBINPUT_EXPORT li_fixed_t
libinput_event_pointer_get_absolute_y(
	struct libinput_event_pointer *event)
{
	struct libinput_event_pointer_motion *ptrev;
	ptrev = container_of(event, ptrev, base);
	return ptrev->y;
}

LIBINPUT_EXPORT uint32_t
libinput_event_pointer_get_button(
	struct libinput_event_pointer *event)
{
	struct libinput_event_pointer_button *ptrev;
	ptrev = container_of(event, ptrev, base);
	return ptrev->button;
}

LIBINPUT_EXPORT enum libinput_pointer_button_state
libinput_event_pointer_get_button_state(
	struct libinput_event_pointer *event)
{
	struct libinput_event_pointer_button *ptrev;
	ptrev = container_of(event, ptrev, base);
	return ptrev->state;
}

LIBINPUT_EXPORT enum libinput_pointer_axis
libinput_event_pointer_get_axis(
	struct libinput_event_pointer *event)
{
	struct libinput_event_pointer_axis *ptrev;
	ptrev = container_of(event, ptrev, base);
	return ptrev->axis;
}

LIBINPUT_EXPORT li_fixed_t
libinput_event_pointer_get_axis_value(
	struct libinput_event_pointer *event)
{
	struct libinput_event_pointer_axis *ptrev;
	ptrev = container_of(event, ptrev, base);
	return ptrev->value;
}

LIBINPUT_EXPORT uint32_t
libinput_event_touch_get_time(
	struct libinput_event_touch *event)
{
	return event->time;
}

LIBINPUT_EXPORT uint32_t
libinput_event_touch_get_slot(
	struct libinput_event_touch *event)
{
	return event->slot;
}

LIBINPUT_EXPORT li_fixed_t
libinput_event_touch_get_x(
	struct libinput_event_touch *event)
{
	return event->x;
}

LIBINPUT_EXPORT li_fixed_t
libinput_event_touch_get_y(
	struct libinput_event_touch *event)
{
	return event->y;
}

LIBINPUT_EXPORT enum libinput_touch_type
libinput_event_touch_get_touch_type(
	struct libinput_event_touch *event)
{
	return event->touch_type;
}

struct libinput_source *
libinput_add_fd(struct libinput *libinput,
		int fd,
		libinput_source_dispatch_t dispatch,
		void *user_data)
{
	struct libinput_source *source;
	struct epoll_event ep;

	source = malloc(sizeof *source);
	if (!source)
		return NULL;

	source->dispatch = dispatch;
	source->user_data = user_data;
	source->fd = fd;

	memset(&ep, 0, sizeof ep);
	ep.events = EPOLLIN;
	ep.data.ptr = source;

	if (epoll_ctl(libinput->epoll_fd, EPOLL_CTL_ADD, fd, &ep) < 0) {
		close(source->fd);
		free(source);
		return NULL;
	}

	return source;
}

void
libinput_remove_source(struct libinput *libinput,
		       struct libinput_source *source)
{
	epoll_ctl(libinput->epoll_fd, EPOLL_CTL_DEL, source->fd, NULL);
	close(source->fd);
	source->fd = -1;
	list_insert(&libinput->source_destroy_list, &source->link);
}

int
libinput_init(struct libinput *libinput,
	      const struct libinput_interface *interface,
	      void *user_data)
{
	libinput->epoll_fd = epoll_create1(EPOLL_CLOEXEC);;
	if (libinput->epoll_fd < 0)
		return -1;

	libinput->interface = interface;
	libinput->user_data = user_data;
	list_init(&libinput->source_destroy_list);
	list_init(&libinput->seat_list);

	return 0;
}

LIBINPUT_EXPORT void
libinput_destroy(struct libinput *libinput)
{
	struct libinput_event *event;

	while ((event = libinput_get_event(libinput)))
	       free(event);
	free(libinput->events);

	close(libinput->epoll_fd);
	free(libinput);
}

static enum libinput_event_class
libinput_event_get_class(struct libinput_event *event)
{
	switch (event->type) {
	case LIBINPUT_EVENT_SEAT_ADDED:
	case LIBINPUT_EVENT_SEAT_REMOVED:
	case LIBINPUT_EVENT_DEVICE_ADDED:
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		return LIBINPUT_EVENT_CLASS_BASE;

	case LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY:
	case LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY:
	case LIBINPUT_EVENT_KEYBOARD_KEY:
	case LIBINPUT_EVENT_POINTER_MOTION:
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
	case LIBINPUT_EVENT_POINTER_BUTTON:
	case LIBINPUT_EVENT_POINTER_AXIS:
	case LIBINPUT_EVENT_TOUCH_TOUCH:
		return LIBINPUT_EVENT_CLASS_DEVICE;
	}

	/* We should never end up here. */
	abort();
}

LIBINPUT_EXPORT void
libinput_event_destroy(struct libinput_event *event)
{
	if (event == NULL)
		return;

	switch (libinput_event_get_class(event)) {
	case LIBINPUT_EVENT_CLASS_BASE:
		break;
	case LIBINPUT_EVENT_CLASS_SEAT:
		libinput_seat_unref(event->target.seat);
		break;
	case LIBINPUT_EVENT_CLASS_DEVICE:
		libinput_device_unref(event->target.device);
		break;
	}

	free(event);
}

int
open_restricted(struct libinput *libinput,
		const char *path, int flags)
{
	return libinput->interface->open_restricted(path,
						    flags,
						    libinput->user_data);
}

void
close_restricted(struct libinput *libinput, int fd)
{
	return libinput->interface->close_restricted(fd, libinput->user_data);
}

void
libinput_seat_init(struct libinput_seat *seat,
		   struct libinput *libinput,
		   const char *name)
{
	seat->refcount = 1;
	seat->libinput = libinput;
	seat->name = strdup(name);
	list_init(&seat->devices_list);
}

LIBINPUT_EXPORT void
libinput_seat_ref(struct libinput_seat *seat)
{
	seat->refcount++;
}

LIBINPUT_EXPORT void
libinput_seat_unref(struct libinput_seat *seat)
{
	seat->refcount--;
	if (seat->refcount == 0) {
		free(seat->name);
		udev_seat_destroy((struct udev_seat *) seat);
	}
}

LIBINPUT_EXPORT void
libinput_seat_set_user_data(struct libinput_seat *seat, void *user_data)
{
	seat->user_data = user_data;
}

LIBINPUT_EXPORT void *
libinput_seat_get_user_data(struct libinput_seat *seat)
{
	return seat->user_data;
}

LIBINPUT_EXPORT const char *
libinput_seat_get_name(struct libinput_seat *seat)
{
	return seat->name;
}

void
libinput_device_init(struct libinput_device *device,
		     struct libinput_seat *seat)
{
	device->seat = seat;
	device->refcount = 1;
}

LIBINPUT_EXPORT void
libinput_device_ref(struct libinput_device *device)
{
	device->refcount++;
}

LIBINPUT_EXPORT void
libinput_device_unref(struct libinput_device *device)
{
	device->refcount--;
	if (device->refcount == 0)
		evdev_device_destroy((struct evdev_device *) device);
}

LIBINPUT_EXPORT int
libinput_get_fd(struct libinput *libinput)
{
	return libinput->epoll_fd;
}

LIBINPUT_EXPORT int
libinput_dispatch(struct libinput *libinput)
{
	struct libinput_source *source, *next;
	struct epoll_event ep[32];
	int i, count;

	count = epoll_wait(libinput->epoll_fd, ep, ARRAY_LENGTH(ep), 0);
	if (count < 0)
		return -errno;

	for (i = 0; i < count; ++i) {
		source = ep[i].data.ptr;
		if (source->fd == -1)
			continue;

		source->dispatch(source->user_data);
	}

	list_for_each_safe(source, next, &libinput->source_destroy_list, link)
		free(source);
	list_init(&libinput->source_destroy_list);

	return libinput->events_count > 0 ? 0 : -EAGAIN;
}

static void
init_event_base(struct libinput_event *event,
		struct libinput *libinput,
		enum libinput_event_type type,
		union libinput_event_target target)
{
	event->type = type;
	event->libinput = libinput;
	event->target = target;
}

static void
post_base_event(struct libinput *libinput,
		enum libinput_event_type type,
		struct libinput_event *event)
{
	init_event_base(event, libinput, type,
			(union libinput_event_target) { .libinput = libinput });
	libinput_post_event(libinput, event);
}

static void
post_device_event(struct libinput_device *device,
		  enum libinput_event_type type,
		  struct libinput_event *event)
{
	init_event_base(event, device->seat->libinput, type,
			(union libinput_event_target) { .device = device });
	libinput_post_event(device->seat->libinput, event);
}

static void
post_pointer_event(struct libinput_device *device,
		   enum libinput_event_type type,
		   uint32_t time,
		   struct libinput_event_pointer *event)
{
	event->time = time;
	post_device_event(device, type, &event->base);
}

static void
notify_seat(struct libinput_seat *seat,
	    enum libinput_event_type which)
{
	struct libinput_event_seat_notify *seat_event;

	seat_event = malloc(sizeof *seat_event);
	if (!seat_event)
		return;

	*seat_event = (struct libinput_event_seat_notify) {
		.seat = seat,
	};

	post_base_event(seat->libinput,
			which,
			&seat_event->base);
}

void
notify_added_seat(struct libinput_seat *seat)
{
	notify_seat(seat, LIBINPUT_EVENT_SEAT_ADDED);
}

void
notify_removed_seat(struct libinput_seat *seat)
{
	notify_seat(seat, LIBINPUT_EVENT_SEAT_REMOVED);
}

static void
notify_device(struct libinput_device *device,
		enum libinput_event_type which)
{
	struct libinput_event_device_notify *notify_device_event;

	notify_device_event = malloc(sizeof *notify_device_event);
	if (!notify_device_event)
		return;

	*notify_device_event = (struct libinput_event_device_notify) {
		.device = device,
	};

	post_base_event(device->seat->libinput,
			which,
			&notify_device_event->base);
}

void
notify_added_device(struct libinput_device *device)
{
	notify_device(device, LIBINPUT_EVENT_DEVICE_ADDED);
}

void
notify_removed_device(struct libinput_device *device)
{
	notify_device(device, LIBINPUT_EVENT_DEVICE_REMOVED);
}

static void
device_capability_notify(struct libinput_device *device,
			 enum libinput_event_type which,
			 enum libinput_device_capability capability)
{
	struct libinput_event_device_capability_notify *capability_event;

	capability_event = malloc(sizeof *capability_event);

	*capability_event = (struct libinput_event_device_capability_notify) {
		.capability = capability,
	};

	post_device_event(device,
			  which,
			  &capability_event->base);
}

void
device_register_capability(struct libinput_device *device,
			   enum libinput_device_capability capability)
{
	device_capability_notify(device,
				 LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY,
				 capability);
}

void
device_unregister_capability(struct libinput_device *device,
			     enum libinput_device_capability capability)
{
	device_capability_notify(device,
				 LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY,
				 capability);
}

void
keyboard_notify_key(struct libinput_device *device,
		    uint32_t time,
		    uint32_t key,
		    enum libinput_keyboard_key_state state)
{
	struct libinput_event_keyboard *key_event;

	key_event = malloc(sizeof *key_event);
	if (!key_event)
		return;

	*key_event = (struct libinput_event_keyboard) {
		.time = time,
		.key = key,
		.state = state,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_KEYBOARD_KEY,
			  &key_event->base);
}

void
pointer_notify_motion(struct libinput_device *device,
		      uint32_t time,
		      li_fixed_t dx,
		      li_fixed_t dy)
{
	struct libinput_event_pointer_motion *motion_event;

	motion_event = malloc(sizeof *motion_event);
	if (!motion_event)
		return;

	*motion_event = (struct libinput_event_pointer_motion) {
		.x = dx,
		.y = dy,
	};

	post_pointer_event(device,
			   LIBINPUT_EVENT_POINTER_MOTION,
			   time,
			   &motion_event->base);
}

void
pointer_notify_motion_absolute(struct libinput_device *device,
			       uint32_t time,
			       li_fixed_t x,
			       li_fixed_t y)
{
	struct libinput_event_pointer_motion *motion_absolute_event;

	motion_absolute_event = malloc(sizeof *motion_absolute_event);
	if (!motion_absolute_event)
		return;

	*motion_absolute_event = (struct libinput_event_pointer_motion) {
		.x = x,
		.y = y,
	};

	post_pointer_event(device,
			   LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE,
			   time,
			   &motion_absolute_event->base);
}

void
pointer_notify_button(struct libinput_device *device,
		      uint32_t time,
		      int32_t button,
		      enum libinput_pointer_button_state state)
{
	struct libinput_event_pointer_button *button_event;

	button_event = malloc(sizeof *button_event);
	if (!button_event)
		return;

	*button_event = (struct libinput_event_pointer_button) {
		.button = button,
		.state = state,
	};

	post_pointer_event(device,
			   LIBINPUT_EVENT_POINTER_BUTTON,
			   time,
			   &button_event->base);
}

void
pointer_notify_axis(struct libinput_device *device,
		    uint32_t time,
		    enum libinput_pointer_axis axis,
		    li_fixed_t value)
{
	struct libinput_event_pointer_axis *axis_event;

	axis_event = malloc(sizeof *axis_event);
	if (!axis_event)
		return;

	*axis_event = (struct libinput_event_pointer_axis) {
		.axis = axis,
		.value = value,
	};

	post_pointer_event(device,
			   LIBINPUT_EVENT_POINTER_AXIS,
			   time,
			   &axis_event->base);
}

void
touch_notify_touch(struct libinput_device *device,
		   uint32_t time,
		   int32_t slot,
		   li_fixed_t x,
		   li_fixed_t y,
		   enum libinput_touch_type touch_type)
{
	struct libinput_event_touch *touch_event;

	touch_event = malloc(sizeof *touch_event);
	if (!touch_event)
		return;

	*touch_event = (struct libinput_event_touch) {
		.time = time,
		.slot = slot,
		.x = x,
		.y = y,
		.touch_type = touch_type,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_TOUCH_TOUCH,
			  &touch_event->base);
}

static void
libinput_post_event(struct libinput *libinput,
		    struct libinput_event *event)
{
	struct libinput_event **events = libinput->events;
	size_t events_len = libinput->events_len;
	size_t events_count = libinput->events_count;
	size_t move_len;
	size_t new_out;

	events_count++;
	if (events_count > events_len) {
		if (events_len == 0)
			events_len = 4;
		else
			events_len *= 2;
		events = realloc(events, events_len * sizeof *events);
		if (!events) {
			fprintf(stderr, "Failed to reallocate event ring "
				"buffer");
			return;
		}

		if (libinput->events_count > 0 && libinput->events_in == 0) {
			libinput->events_in = libinput->events_len;
		} else if (libinput->events_count > 0 &&
			   libinput->events_out >= libinput->events_in) {
			move_len = libinput->events_len - libinput->events_out;
			new_out = events_len - move_len;
			memmove(events + new_out,
				libinput->events + libinput->events_out,
				move_len * sizeof *events);
			libinput->events_out = new_out;
		}

		libinput->events = events;
		libinput->events_len = events_len;
	}

	switch (libinput_event_get_class(event)) {
	case LIBINPUT_EVENT_CLASS_BASE:
		break;
	case LIBINPUT_EVENT_CLASS_SEAT:
		libinput_seat_ref(event->target.seat);
		break;
	case LIBINPUT_EVENT_CLASS_DEVICE:
		libinput_device_ref(event->target.device);
		break;
	}

	libinput->events_count = events_count;
	events[libinput->events_in] = event;
	libinput->events_in = (libinput->events_in + 1) % libinput->events_len;
}

LIBINPUT_EXPORT struct libinput_event *
libinput_get_event(struct libinput *libinput)
{
	struct libinput_event *event;

	if (libinput->events_count == 0)
		return NULL;

	event = libinput->events[libinput->events_out];
	libinput->events_out =
		(libinput->events_out + 1) % libinput->events_len;
	libinput->events_count--;

	return event;
}

LIBINPUT_EXPORT void *
libinput_get_user_data(struct libinput *libinput)
{
	return libinput->user_data;
}

LIBINPUT_EXPORT int
libinput_resume(struct libinput *libinput)
{
	return udev_input_enable((struct udev_input *) libinput);
}

LIBINPUT_EXPORT void
libinput_suspend(struct libinput *libinput)
{
	udev_input_disable((struct udev_input *) libinput);
}

LIBINPUT_EXPORT void
libinput_device_set_user_data(struct libinput_device *device, void *user_data)
{
	device->user_data = user_data;
}

LIBINPUT_EXPORT void *
libinput_device_get_user_data(struct libinput_device *device)
{
	return device->user_data;
}

LIBINPUT_EXPORT const char *
libinput_device_get_sysname(struct libinput_device *device)
{
	return evdev_device_get_sysname((struct evdev_device *) device);
}

LIBINPUT_EXPORT const char *
libinput_device_get_output_name(struct libinput_device *device)
{
	return evdev_device_get_output((struct evdev_device *) device);
}

LIBINPUT_EXPORT struct libinput_seat *
libinput_device_get_seat(struct libinput_device *device)
{
	return device->seat;
}

LIBINPUT_EXPORT void
libinput_device_led_update(struct libinput_device *device,
			   enum libinput_led leds)
{
	evdev_device_led_update((struct evdev_device *) device, leds);
}

LIBINPUT_EXPORT int
libinput_device_get_keys(struct libinput_device *device,
			 char *keys, size_t size)
{
	return evdev_device_get_keys((struct evdev_device *) device,
				     keys,
				     size);
}

LIBINPUT_EXPORT void
libinput_device_calibrate(struct libinput_device *device,
			  float calibration[6])
{
	evdev_device_calibrate((struct evdev_device *) device, calibration);
}

LIBINPUT_EXPORT int
libinput_device_has_capability(struct libinput_device *device,
			       enum libinput_device_capability capability)
{
	return evdev_device_has_capability((struct evdev_device *) device,
					   capability);
}
