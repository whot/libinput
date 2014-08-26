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
#include "timer.h"

struct libinput_source {
	libinput_source_dispatch_t dispatch;
	void *user_data;
	int fd;
	struct list link;
};

struct libinput_event {
	enum libinput_event_type type;
	struct libinput_device *device;
};

struct libinput_event_device_notify {
	struct libinput_event base;
};

struct libinput_event_keyboard {
	struct libinput_event base;
	uint32_t time;
	uint32_t key;
	uint32_t seat_key_count;
	enum libinput_key_state state;
};

struct libinput_event_pointer {
	struct libinput_event base;
	uint32_t time;
	double x;
	double y;
	uint32_t button;
	uint32_t seat_button_count;
	enum libinput_button_state state;
	enum libinput_pointer_axis axis;
	double value;
};

struct libinput_event_touch {
	struct libinput_event base;
	uint32_t time;
	int32_t slot;
	int32_t seat_slot;
	double x;
	double y;
};

static void
libinput_default_log_func(struct libinput *libinput,
			  enum libinput_log_priority priority,
			  const char *format, va_list args)
{
	const char *prefix;

	switch(priority) {
	case LIBINPUT_LOG_PRIORITY_DEBUG: prefix = "debug"; break;
	case LIBINPUT_LOG_PRIORITY_INFO: prefix = "info"; break;
	case LIBINPUT_LOG_PRIORITY_ERROR: prefix = "error"; break;
	default: prefix="<invalid priority>"; break;
	}

	fprintf(stderr, "libinput %s: ", prefix);
	vfprintf(stderr, format, args);
}

void
log_msg_va(struct libinput *libinput,
	   enum libinput_log_priority priority,
	   const char *format,
	   va_list args)
{
	if (libinput->log_handler &&
	    libinput->log_priority <= priority)
		libinput->log_handler(libinput, priority, format, args);
}

void
log_msg(struct libinput *libinput,
	enum libinput_log_priority priority,
	const char *format, ...)
{
	va_list args;

	va_start(args, format);
	log_msg_va(libinput, priority, format, args);
	va_end(args);
}

LIBINPUT_EXPORT void
libinput_log_set_priority(struct libinput *libinput,
			  enum libinput_log_priority priority)
{
	libinput->log_priority = priority;
}

LIBINPUT_EXPORT enum libinput_log_priority
libinput_log_get_priority(const struct libinput *libinput)
{
	return libinput->log_priority;
}

LIBINPUT_EXPORT void
libinput_log_set_handler(struct libinput *libinput,
			 libinput_log_handler log_handler)
{
	libinput->log_handler = log_handler;
}

static void
libinput_post_event(struct libinput *libinput,
		    struct libinput_event *event);

LIBINPUT_EXPORT enum libinput_event_type
libinput_event_get_type(struct libinput_event *event)
{
	return event->type;
}

LIBINPUT_EXPORT struct libinput *
libinput_event_get_context(struct libinput_event *event)
{
	return event->device->seat->libinput;
}

LIBINPUT_EXPORT struct libinput_device *
libinput_event_get_device(struct libinput_event *event)
{
	return event->device;
}

LIBINPUT_EXPORT struct libinput_event_pointer *
libinput_event_get_pointer_event(struct libinput_event *event)
{
	switch (event->type) {
	case LIBINPUT_EVENT_NONE:
		abort(); /* not used as actual event type */
	case LIBINPUT_EVENT_DEVICE_ADDED:
	case LIBINPUT_EVENT_DEVICE_REMOVED:
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		break;
	case LIBINPUT_EVENT_POINTER_MOTION:
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
	case LIBINPUT_EVENT_POINTER_BUTTON:
	case LIBINPUT_EVENT_POINTER_AXIS:
		return (struct libinput_event_pointer *) event;
	case LIBINPUT_EVENT_TOUCH_DOWN:
	case LIBINPUT_EVENT_TOUCH_UP:
	case LIBINPUT_EVENT_TOUCH_MOTION:
	case LIBINPUT_EVENT_TOUCH_CANCEL:
	case LIBINPUT_EVENT_TOUCH_FRAME:
		break;
	}

	return NULL;
}

LIBINPUT_EXPORT struct libinput_event_keyboard *
libinput_event_get_keyboard_event(struct libinput_event *event)
{
	switch (event->type) {
	case LIBINPUT_EVENT_NONE:
		abort(); /* not used as actual event type */
	case LIBINPUT_EVENT_DEVICE_ADDED:
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		break;
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		return (struct libinput_event_keyboard *) event;
	case LIBINPUT_EVENT_POINTER_MOTION:
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
	case LIBINPUT_EVENT_POINTER_BUTTON:
	case LIBINPUT_EVENT_POINTER_AXIS:
	case LIBINPUT_EVENT_TOUCH_DOWN:
	case LIBINPUT_EVENT_TOUCH_UP:
	case LIBINPUT_EVENT_TOUCH_MOTION:
	case LIBINPUT_EVENT_TOUCH_CANCEL:
	case LIBINPUT_EVENT_TOUCH_FRAME:
		break;
	}

	return NULL;
}

LIBINPUT_EXPORT struct libinput_event_touch *
libinput_event_get_touch_event(struct libinput_event *event)
{
	switch (event->type) {
	case LIBINPUT_EVENT_NONE:
		abort(); /* not used as actual event type */
	case LIBINPUT_EVENT_DEVICE_ADDED:
	case LIBINPUT_EVENT_DEVICE_REMOVED:
	case LIBINPUT_EVENT_KEYBOARD_KEY:
	case LIBINPUT_EVENT_POINTER_MOTION:
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
	case LIBINPUT_EVENT_POINTER_BUTTON:
	case LIBINPUT_EVENT_POINTER_AXIS:
		break;
	case LIBINPUT_EVENT_TOUCH_DOWN:
	case LIBINPUT_EVENT_TOUCH_UP:
	case LIBINPUT_EVENT_TOUCH_MOTION:
	case LIBINPUT_EVENT_TOUCH_CANCEL:
	case LIBINPUT_EVENT_TOUCH_FRAME:
		return (struct libinput_event_touch *) event;
	}

	return NULL;
}

LIBINPUT_EXPORT struct libinput_event_device_notify *
libinput_event_get_device_notify_event(struct libinput_event *event)
{
	switch (event->type) {
	case LIBINPUT_EVENT_NONE:
		abort(); /* not used as actual event type */
	case LIBINPUT_EVENT_DEVICE_ADDED:
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		return (struct libinput_event_device_notify *) event;
	case LIBINPUT_EVENT_KEYBOARD_KEY:
	case LIBINPUT_EVENT_POINTER_MOTION:
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
	case LIBINPUT_EVENT_POINTER_BUTTON:
	case LIBINPUT_EVENT_POINTER_AXIS:
	case LIBINPUT_EVENT_TOUCH_DOWN:
	case LIBINPUT_EVENT_TOUCH_UP:
	case LIBINPUT_EVENT_TOUCH_MOTION:
	case LIBINPUT_EVENT_TOUCH_CANCEL:
	case LIBINPUT_EVENT_TOUCH_FRAME:
		break;
	}

	return NULL;
}

LIBINPUT_EXPORT uint32_t
libinput_event_keyboard_get_time(struct libinput_event_keyboard *event)
{
	return event->time;
}

LIBINPUT_EXPORT uint32_t
libinput_event_keyboard_get_key(struct libinput_event_keyboard *event)
{
	return event->key;
}

LIBINPUT_EXPORT enum libinput_key_state
libinput_event_keyboard_get_key_state(struct libinput_event_keyboard *event)
{
	return event->state;
}

LIBINPUT_EXPORT uint32_t
libinput_event_keyboard_get_seat_key_count(
	struct libinput_event_keyboard *event)
{
	return event->seat_key_count;
}

LIBINPUT_EXPORT uint32_t
libinput_event_pointer_get_time(struct libinput_event_pointer *event)
{
	return event->time;
}

LIBINPUT_EXPORT double
libinput_event_pointer_get_dx(struct libinput_event_pointer *event)
{
	return event->x;
}

LIBINPUT_EXPORT double
libinput_event_pointer_get_dy(struct libinput_event_pointer *event)
{
	return event->y;
}

LIBINPUT_EXPORT double
libinput_event_pointer_get_absolute_x(struct libinput_event_pointer *event)
{
	struct evdev_device *device =
		(struct evdev_device *) event->base.device;

	return evdev_convert_to_mm(device->abs.absinfo_x, event->x);
}

LIBINPUT_EXPORT double
libinput_event_pointer_get_absolute_y(struct libinput_event_pointer *event)
{
	struct evdev_device *device =
		(struct evdev_device *) event->base.device;

	return evdev_convert_to_mm(device->abs.absinfo_y, event->y);
}

LIBINPUT_EXPORT double
libinput_event_pointer_get_absolute_x_transformed(
	struct libinput_event_pointer *event,
	uint32_t width)
{
	struct evdev_device *device =
		(struct evdev_device *) event->base.device;

	return evdev_device_transform_x(device, event->x, width);
}

LIBINPUT_EXPORT double
libinput_event_pointer_get_absolute_y_transformed(
	struct libinput_event_pointer *event,
	uint32_t height)
{
	struct evdev_device *device =
		(struct evdev_device *) event->base.device;

	return evdev_device_transform_y(device, event->y, height);
}

LIBINPUT_EXPORT uint32_t
libinput_event_pointer_get_button(struct libinput_event_pointer *event)
{
	return event->button;
}

LIBINPUT_EXPORT enum libinput_button_state
libinput_event_pointer_get_button_state(struct libinput_event_pointer *event)
{
	return event->state;
}

LIBINPUT_EXPORT uint32_t
libinput_event_pointer_get_seat_button_count(
	struct libinput_event_pointer *event)
{
	return event->seat_button_count;
}

LIBINPUT_EXPORT enum libinput_pointer_axis
libinput_event_pointer_get_axis(struct libinput_event_pointer *event)
{
	return event->axis;
}

LIBINPUT_EXPORT double
libinput_event_pointer_get_axis_value(struct libinput_event_pointer *event)
{
	return event->value;
}

LIBINPUT_EXPORT uint32_t
libinput_event_touch_get_time(struct libinput_event_touch *event)
{
	return event->time;
}

LIBINPUT_EXPORT int32_t
libinput_event_touch_get_slot(struct libinput_event_touch *event)
{
	return event->slot;
}

LIBINPUT_EXPORT int32_t
libinput_event_touch_get_seat_slot(struct libinput_event_touch *event)
{
	return event->seat_slot;
}

LIBINPUT_EXPORT double
libinput_event_touch_get_x(struct libinput_event_touch *event)
{
	struct evdev_device *device =
		(struct evdev_device *) event->base.device;

	return evdev_convert_to_mm(device->abs.absinfo_x, event->x);
}

LIBINPUT_EXPORT double
libinput_event_touch_get_x_transformed(struct libinput_event_touch *event,
				       uint32_t width)
{
	struct evdev_device *device =
		(struct evdev_device *) event->base.device;

	return evdev_device_transform_x(device, event->x, width);
}

LIBINPUT_EXPORT double
libinput_event_touch_get_y_transformed(struct libinput_event_touch *event,
				       uint32_t height)
{
	struct evdev_device *device =
		(struct evdev_device *) event->base.device;

	return evdev_device_transform_y(device, event->y, height);
}

LIBINPUT_EXPORT double
libinput_event_touch_get_y(struct libinput_event_touch *event)
{
	struct evdev_device *device =
		(struct evdev_device *) event->base.device;

	return evdev_convert_to_mm(device->abs.absinfo_y, event->y);
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
	source->fd = -1;
	list_insert(&libinput->source_destroy_list, &source->link);
}

int
libinput_init(struct libinput *libinput,
	      const struct libinput_interface *interface,
	      const struct libinput_interface_backend *interface_backend,
	      void *user_data)
{
	libinput->epoll_fd = epoll_create1(EPOLL_CLOEXEC);;
	if (libinput->epoll_fd < 0)
		return -1;

	libinput->events_len = 4;
	libinput->events = zalloc(libinput->events_len * sizeof(*libinput->events));
	if (!libinput->events) {
		close(libinput->epoll_fd);
		return -1;
	}

	libinput->log_handler = libinput_default_log_func;
	libinput->log_priority = LIBINPUT_LOG_PRIORITY_ERROR;
	libinput->interface = interface;
	libinput->interface_backend = interface_backend;
	libinput->user_data = user_data;
	libinput->refcount = 1;
	list_init(&libinput->source_destroy_list);
	list_init(&libinput->seat_list);

	if (libinput_timer_subsys_init(libinput) != 0) {
		free(libinput->events);
		close(libinput->epoll_fd);
		return -1;
	}

	return 0;
}

static void
libinput_device_destroy(struct libinput_device *device);

static void
libinput_seat_destroy(struct libinput_seat *seat);

static void
libinput_drop_destroyed_sources(struct libinput *libinput)
{
	struct libinput_source *source, *next;

	list_for_each_safe(source, next, &libinput->source_destroy_list, link)
		free(source);
	list_init(&libinput->source_destroy_list);
}

LIBINPUT_EXPORT struct libinput *
libinput_ref(struct libinput *libinput)
{
	libinput->refcount++;
	return libinput;
}

LIBINPUT_EXPORT struct libinput *
libinput_unref(struct libinput *libinput)
{
	struct libinput_event *event;
	struct libinput_device *device, *next_device;
	struct libinput_seat *seat, *next_seat;

	if (libinput == NULL)
		return NULL;

	assert(libinput->refcount > 0);
	libinput->refcount--;
	if (libinput->refcount > 0)
		return libinput;

	libinput_suspend(libinput);

	libinput->interface_backend->destroy(libinput);

	while ((event = libinput_get_event(libinput)))
	       libinput_event_destroy(event);

	free(libinput->events);

	list_for_each_safe(seat, next_seat, &libinput->seat_list, link) {
		list_for_each_safe(device, next_device,
				   &seat->devices_list,
				   link)
			libinput_device_destroy(device);

		libinput_seat_destroy(seat);
	}

	libinput_timer_subsys_destroy(libinput);
	libinput_drop_destroyed_sources(libinput);
	close(libinput->epoll_fd);
	free(libinput);

	return NULL;
}

LIBINPUT_EXPORT void
libinput_event_destroy(struct libinput_event *event)
{
	if (event == NULL)
		return;

	if (event->device)
		libinput_device_unref(event->device);

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
		   const char *physical_name,
		   const char *logical_name,
		   libinput_seat_destroy_func destroy)
{
	seat->refcount = 1;
	seat->libinput = libinput;
	seat->physical_name = strdup(physical_name);
	seat->logical_name = strdup(logical_name);
	seat->destroy = destroy;
	list_init(&seat->devices_list);
	list_insert(&libinput->seat_list, &seat->link);
}

LIBINPUT_EXPORT struct libinput_seat *
libinput_seat_ref(struct libinput_seat *seat)
{
	seat->refcount++;
	return seat;
}

static void
libinput_seat_destroy(struct libinput_seat *seat)
{
	list_remove(&seat->link);
	free(seat->logical_name);
	free(seat->physical_name);
	seat->destroy(seat);
}

LIBINPUT_EXPORT struct libinput_seat *
libinput_seat_unref(struct libinput_seat *seat)
{
	assert(seat->refcount > 0);
	seat->refcount--;
	if (seat->refcount == 0) {
		libinput_seat_destroy(seat);
		return NULL;
	} else {
		return seat;
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
libinput_seat_get_physical_name(struct libinput_seat *seat)
{
	return seat->physical_name;
}

LIBINPUT_EXPORT const char *
libinput_seat_get_logical_name(struct libinput_seat *seat)
{
	return seat->logical_name;
}

void
libinput_device_init(struct libinput_device *device,
		     struct libinput_seat *seat)
{
	device->seat = seat;
	device->refcount = 1;
}

LIBINPUT_EXPORT struct libinput_device *
libinput_device_ref(struct libinput_device *device)
{
	device->refcount++;
	return device;
}

static void
libinput_device_destroy(struct libinput_device *device)
{
	evdev_device_destroy((struct evdev_device *) device);
}

LIBINPUT_EXPORT struct libinput_device *
libinput_device_unref(struct libinput_device *device)
{
	assert(device->refcount > 0);
	device->refcount--;
	if (device->refcount == 0) {
		libinput_device_destroy(device);
		return NULL;
	} else {
		return device;
	}
}

LIBINPUT_EXPORT int
libinput_get_fd(struct libinput *libinput)
{
	return libinput->epoll_fd;
}

LIBINPUT_EXPORT int
libinput_dispatch(struct libinput *libinput)
{
	struct libinput_source *source;
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

	libinput_drop_destroyed_sources(libinput);

	return 0;
}

static uint32_t
update_seat_key_count(struct libinput_seat *seat,
		      int32_t key,
		      enum libinput_key_state state)
{
	assert(key >= 0 && key <= KEY_MAX);

	switch (state) {
	case LIBINPUT_KEY_STATE_PRESSED:
		return ++seat->button_count[key];
	case LIBINPUT_KEY_STATE_RELEASED:
		/* We might not have received the first PRESSED event. */
		if (seat->button_count[key] == 0)
			return 0;

		return --seat->button_count[key];
	}

	return 0;
}

static uint32_t
update_seat_button_count(struct libinput_seat *seat,
			 int32_t button,
			 enum libinput_button_state state)
{
	assert(button >= 0 && button <= KEY_MAX);

	switch (state) {
	case LIBINPUT_BUTTON_STATE_PRESSED:
		return ++seat->button_count[button];
	case LIBINPUT_BUTTON_STATE_RELEASED:
		/* We might not have received the first PRESSED event. */
		if (seat->button_count[button] == 0)
			return 0;

		return --seat->button_count[button];
	}

	return 0;
}

static void
init_event_base(struct libinput_event *event,
		struct libinput_device *device,
		enum libinput_event_type type)
{
	event->type = type;
	event->device = device;
}

static void
post_base_event(struct libinput_device *device,
		enum libinput_event_type type,
		struct libinput_event *event)
{
	struct libinput *libinput = device->seat->libinput;
	init_event_base(event, device, type);
	libinput_post_event(libinput, event);
}

static void
post_device_event(struct libinput_device *device,
		  enum libinput_event_type type,
		  struct libinput_event *event)
{
	init_event_base(event, device, type);
	libinput_post_event(device->seat->libinput, event);
}

void
notify_added_device(struct libinput_device *device)
{
	struct libinput_event_device_notify *added_device_event;

	added_device_event = zalloc(sizeof *added_device_event);
	if (!added_device_event)
		return;

	post_base_event(device,
			LIBINPUT_EVENT_DEVICE_ADDED,
			&added_device_event->base);
}

void
notify_removed_device(struct libinput_device *device)
{
	struct libinput_event_device_notify *removed_device_event;

	removed_device_event = zalloc(sizeof *removed_device_event);
	if (!removed_device_event)
		return;

	post_base_event(device,
			LIBINPUT_EVENT_DEVICE_REMOVED,
			&removed_device_event->base);
}

void
keyboard_notify_key(struct libinput_device *device,
		    uint32_t time,
		    uint32_t key,
		    enum libinput_key_state state)
{
	struct libinput_event_keyboard *key_event;
	uint32_t seat_key_count;

	key_event = zalloc(sizeof *key_event);
	if (!key_event)
		return;

	seat_key_count = update_seat_key_count(device->seat, key, state);

	*key_event = (struct libinput_event_keyboard) {
		.time = time,
		.key = key,
		.state = state,
		.seat_key_count = seat_key_count,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_KEYBOARD_KEY,
			  &key_event->base);
}

void
pointer_notify_motion(struct libinput_device *device,
		      uint32_t time,
		      double dx,
		      double dy)
{
	struct libinput_event_pointer *motion_event;

	motion_event = zalloc(sizeof *motion_event);
	if (!motion_event)
		return;

	*motion_event = (struct libinput_event_pointer) {
		.time = time,
		.x = dx,
		.y = dy,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_POINTER_MOTION,
			  &motion_event->base);
}

void
pointer_notify_motion_absolute(struct libinput_device *device,
			       uint32_t time,
			       double x,
			       double y)
{
	struct libinput_event_pointer *motion_absolute_event;

	motion_absolute_event = zalloc(sizeof *motion_absolute_event);
	if (!motion_absolute_event)
		return;

	*motion_absolute_event = (struct libinput_event_pointer) {
		.time = time,
		.x = x,
		.y = y,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE,
			  &motion_absolute_event->base);
}

void
pointer_notify_button(struct libinput_device *device,
		      uint32_t time,
		      int32_t button,
		      enum libinput_button_state state)
{
	struct libinput_event_pointer *button_event;
	int32_t seat_button_count;

	button_event = zalloc(sizeof *button_event);
	if (!button_event)
		return;

	seat_button_count = update_seat_button_count(device->seat,
						     button,
						     state);

	*button_event = (struct libinput_event_pointer) {
		.time = time,
		.button = button,
		.state = state,
		.seat_button_count = seat_button_count,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_POINTER_BUTTON,
			  &button_event->base);
}

void
pointer_notify_axis(struct libinput_device *device,
		    uint32_t time,
		    enum libinput_pointer_axis axis,
		    double value)
{
	struct libinput_event_pointer *axis_event;

	axis_event = zalloc(sizeof *axis_event);
	if (!axis_event)
		return;

	*axis_event = (struct libinput_event_pointer) {
		.time = time,
		.axis = axis,
		.value = value,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_POINTER_AXIS,
			  &axis_event->base);
}

void
touch_notify_touch_down(struct libinput_device *device,
			uint32_t time,
			int32_t slot,
			int32_t seat_slot,
			double x,
			double y)
{
	struct libinput_event_touch *touch_event;

	touch_event = zalloc(sizeof *touch_event);
	if (!touch_event)
		return;

	*touch_event = (struct libinput_event_touch) {
		.time = time,
		.slot = slot,
		.seat_slot = seat_slot,
		.x = x,
		.y = y,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_TOUCH_DOWN,
			  &touch_event->base);
}

void
touch_notify_touch_motion(struct libinput_device *device,
			  uint32_t time,
			  int32_t slot,
			  int32_t seat_slot,
			  double x,
			  double y)
{
	struct libinput_event_touch *touch_event;

	touch_event = zalloc(sizeof *touch_event);
	if (!touch_event)
		return;

	*touch_event = (struct libinput_event_touch) {
		.time = time,
		.slot = slot,
		.seat_slot = seat_slot,
		.x = x,
		.y = y,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_TOUCH_MOTION,
			  &touch_event->base);
}

void
touch_notify_touch_up(struct libinput_device *device,
		      uint32_t time,
		      int32_t slot,
		      int32_t seat_slot)
{
	struct libinput_event_touch *touch_event;

	touch_event = zalloc(sizeof *touch_event);
	if (!touch_event)
		return;

	*touch_event = (struct libinput_event_touch) {
		.time = time,
		.slot = slot,
		.seat_slot = seat_slot,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_TOUCH_UP,
			  &touch_event->base);
}

void
touch_notify_frame(struct libinput_device *device,
		   uint32_t time)
{
	struct libinput_event_touch *touch_event;

	touch_event = zalloc(sizeof *touch_event);
	if (!touch_event)
		return;

	*touch_event = (struct libinput_event_touch) {
		.time = time,
	};

	post_device_event(device,
			  LIBINPUT_EVENT_TOUCH_FRAME,
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
				events + libinput->events_out,
				move_len * sizeof *events);
			libinput->events_out = new_out;
		}

		libinput->events = events;
		libinput->events_len = events_len;
	}

	if (event->device)
		libinput_device_ref(event->device);

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

LIBINPUT_EXPORT enum libinput_event_type
libinput_next_event_type(struct libinput *libinput)
{
	struct libinput_event *event;

	if (libinput->events_count == 0)
		return LIBINPUT_EVENT_NONE;

	event = libinput->events[libinput->events_out];
	return event->type;
}

LIBINPUT_EXPORT void *
libinput_get_user_data(struct libinput *libinput)
{
	return libinput->user_data;
}

LIBINPUT_EXPORT int
libinput_resume(struct libinput *libinput)
{
	return libinput->interface_backend->resume(libinput);
}

LIBINPUT_EXPORT void
libinput_suspend(struct libinput *libinput)
{
	libinput->interface_backend->suspend(libinput);
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
libinput_device_get_name(struct libinput_device *device)
{
	return evdev_device_get_name((struct evdev_device *) device);
}

LIBINPUT_EXPORT unsigned int
libinput_device_get_id_product(struct libinput_device *device)
{
	return evdev_device_get_id_product((struct evdev_device *) device);
}

LIBINPUT_EXPORT unsigned int
libinput_device_get_id_vendor(struct libinput_device *device)
{
	return evdev_device_get_id_vendor((struct evdev_device *) device);
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

LIBINPUT_EXPORT int
libinput_device_get_size(struct libinput_device *device,
			 double *width,
			 double *height)
{
	return evdev_device_get_size((struct evdev_device *)device,
				     width,
				     height);
}

LIBINPUT_EXPORT struct libinput_event *
libinput_event_device_notify_get_base_event(struct libinput_event_device_notify *event)
{
	return &event->base;
}

LIBINPUT_EXPORT struct libinput_event *
libinput_event_keyboard_get_base_event(struct libinput_event_keyboard *event)
{
	return &event->base;
}

LIBINPUT_EXPORT struct libinput_event *
libinput_event_pointer_get_base_event(struct libinput_event_pointer *event)
{
	return &event->base;
}

LIBINPUT_EXPORT struct libinput_event *
libinput_event_touch_get_base_event(struct libinput_event_touch *event)
{
	return &event->base;
}

LIBINPUT_EXPORT const char *
libinput_config_status_to_str(enum libinput_config_status status)
{
	const char *str = NULL;

	switch(status) {
	case LIBINPUT_CONFIG_STATUS_SUCCESS:
		str = "Success";
		break;
	case LIBINPUT_CONFIG_STATUS_UNSUPPORTED:
		str = "Unsupported configuration option";
		break;
	case LIBINPUT_CONFIG_STATUS_INVALID:
		str = "Invalid argument range";
		break;
	}

	return str;
}

LIBINPUT_EXPORT int
libinput_device_config_tap_get_finger_count(struct libinput_device *device)
{
	return device->config.tap ? device->config.tap->count(device) : 0;
}

LIBINPUT_EXPORT enum libinput_config_status
libinput_device_config_tap_set_enabled(struct libinput_device *device,
				       enum libinput_config_tap_state enable)
{
	if (enable != LIBINPUT_CONFIG_TAP_ENABLED &&
	    enable != LIBINPUT_CONFIG_TAP_DISABLED)
		return LIBINPUT_CONFIG_STATUS_INVALID;

	if (enable &&
	    libinput_device_config_tap_get_finger_count(device) == 0)
		return LIBINPUT_CONFIG_STATUS_UNSUPPORTED;

	return device->config.tap->set_enabled(device, enable);
}

LIBINPUT_EXPORT enum libinput_config_tap_state
libinput_device_config_tap_get_enabled(struct libinput_device *device)
{
	if (libinput_device_config_tap_get_finger_count(device) == 0)
		return LIBINPUT_CONFIG_TAP_DISABLED;

	return device->config.tap->get_enabled(device);
}

LIBINPUT_EXPORT enum libinput_config_tap_state
libinput_device_config_tap_get_default_enabled(struct libinput_device *device)
{
	if (libinput_device_config_tap_get_finger_count(device) == 0)
		return LIBINPUT_CONFIG_TAP_DISABLED;

	return device->config.tap->get_default(device);
}

LIBINPUT_EXPORT int
libinput_device_config_calibration_has_matrix(struct libinput_device *device)
{
	return device->config.calibration ?
		device->config.calibration->has_matrix(device) : 0;
}

LIBINPUT_EXPORT enum libinput_config_status
libinput_device_config_calibration_set_matrix(struct libinput_device *device,
					      const float matrix[6])
{
	if (!libinput_device_config_calibration_has_matrix(device))
		return LIBINPUT_CONFIG_STATUS_UNSUPPORTED;

	return device->config.calibration->set_matrix(device, matrix);
}

LIBINPUT_EXPORT int
libinput_device_config_calibration_get_matrix(struct libinput_device *device,
					      float matrix[6])
{
	if (!libinput_device_config_calibration_has_matrix(device))
		return 0;

	return device->config.calibration->get_matrix(device, matrix);
}

LIBINPUT_EXPORT int
libinput_device_config_calibration_get_default_matrix(struct libinput_device *device,
						      float matrix[6])
{
	if (!libinput_device_config_calibration_has_matrix(device))
		return 0;

	return device->config.calibration->get_default_matrix(device, matrix);
}
