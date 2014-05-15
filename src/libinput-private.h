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

#ifndef LIBINPUT_PRIVATE_H
#define LIBINPUT_PRIVATE_H

#include <linux/input.h>

#include "libinput.h"
#include "libinput-util.h"

struct libinput_interface_backend {
	int (*resume)(struct libinput *libinput);
	void (*suspend)(struct libinput *libinput);
	void (*destroy)(struct libinput *libinput);
};

struct libinput {
	int epoll_fd;
	struct list source_destroy_list;

	struct list seat_list;

	struct libinput_event **events;
	size_t events_count;
	size_t events_len;
	size_t events_in;
	size_t events_out;

	const struct libinput_interface *interface;
	const struct libinput_interface_backend *interface_backend;
	void *user_data;
};

typedef void (*libinput_seat_destroy_func) (struct libinput_seat *seat);

struct libinput_seat {
	struct libinput *libinput;
	struct list link;
	struct list devices_list;
	void *user_data;
	int refcount;
	libinput_seat_destroy_func destroy;

	char *physical_name;
	char *logical_name;

	uint32_t slot_map;

	uint32_t button_count[KEY_CNT];
};

struct libinput_device_config_tap {
	int (*count)(struct libinput_device *device);
	int (*set)(struct libinput_device *device,
		   unsigned int nfingers,
		   uint32_t button);
	uint32_t (*get)(struct libinput_device *device,
			unsigned int nfingers);
	void (*reset)(struct libinput_device *device);
};

struct libinput_device_config_scroll {
	int (*methods)(struct libinput_device *device);
	int (*set)(struct libinput_device *device,
		   enum libinput_scroll_method method);
	enum libinput_scroll_method (*get)(struct libinput_device *device);
	void (*reset)(struct libinput_device *device);
};

struct libinput_device_config_rotation {
	int (*increment)(struct libinput_device *device);
	int (*set)(struct libinput_device *device, int degrees_cw);
	int (*get)(struct libinput_device *device);
	void (*reset)(struct libinput_device *device);
};

struct libinput_device_config_accel {
	int (*available)(struct libinput_device *device);
	int (*set_speed)(struct libinput_device *device, unsigned int speed);
	int (*set_precision)(struct libinput_device *device, unsigned int precision);
	int (*get_speed)(struct libinput_device *device);
	int (*get_precision)(struct libinput_device *device);
	void (*reset)(struct libinput_device *device);
};

struct libinput_device_config_disable_while_typing {
	int (*available)(struct libinput_device *device);
	int (*enable)(struct libinput_device *device, int enable);
	int (*is_enabled)(struct libinput_device *device);
	void (*reset)(struct libinput_device *device);
};

struct libinput_device_config {
	struct libinput_device_config_tap *tap;
	struct libinput_device_config_scroll *scroll;
	struct libinput_device_config_rotation *rotation;
	struct libinput_device_config_accel *accel;
	struct libinput_device_config_disable_while_typing *dwt;
};

struct libinput_device {
	struct libinput_seat *seat;
	struct list link;
	void *user_data;
	int terminated;
	int refcount;
	struct libinput_device_config config;
};

typedef void (*libinput_source_dispatch_t)(void *data);

struct libinput_source;

#define log_debug(...) log_msg(LIBINPUT_LOG_PRIORITY_DEBUG, __VA_ARGS__)
#define log_info(...) log_msg(LIBINPUT_LOG_PRIORITY_INFO, __VA_ARGS__)
#define log_error(...) log_msg(LIBINPUT_LOG_PRIORITY_ERROR, __VA_ARGS__)
#define log_bug(...) log_msg(LIBINPUT_LOG_PRIORITY_ERROR, "BUG: "__VA_ARGS__)

void
log_msg(enum libinput_log_priority priority, const char *format, ...);

int
libinput_init(struct libinput *libinput,
	      const struct libinput_interface *interface,
	      const struct libinput_interface_backend *interface_backend,
	      void *user_data);

struct libinput_source *
libinput_add_fd(struct libinput *libinput,
		int fd,
		libinput_source_dispatch_t dispatch,
		void *data);

void
libinput_remove_source(struct libinput *libinput,
		       struct libinput_source *source);

int
open_restricted(struct libinput *libinput,
		const char *path, int flags);

void
close_restricted(struct libinput *libinput, int fd);

void
libinput_seat_init(struct libinput_seat *seat,
		   struct libinput *libinput,
		   const char *physical_name,
		   const char *logical_name,
		   libinput_seat_destroy_func destroy);

void
libinput_device_init(struct libinput_device *device,
		     struct libinput_seat *seat);

void
notify_added_device(struct libinput_device *device);

void
notify_removed_device(struct libinput_device *device);

void
keyboard_notify_key(struct libinput_device *device,
		    uint32_t time,
		    uint32_t key,
		    enum libinput_keyboard_key_state state);

void
pointer_notify_motion(struct libinput_device *device,
		      uint32_t time,
		      li_fixed_t dx,
		      li_fixed_t dy);

void
pointer_notify_motion_absolute(struct libinput_device *device,
			       uint32_t time,
			       li_fixed_t x,
			       li_fixed_t y);

void
pointer_notify_button(struct libinput_device *device,
		      uint32_t time,
		      int32_t button,
		      enum libinput_pointer_button_state state);

void
pointer_notify_axis(struct libinput_device *device,
		    uint32_t time,
		    enum libinput_pointer_axis axis,
		    li_fixed_t value);

void
touch_notify_touch_down(struct libinput_device *device,
			uint32_t time,
			int32_t slot,
			int32_t seat_slot,
			li_fixed_t x,
			li_fixed_t y);

void
touch_notify_touch_motion(struct libinput_device *device,
			  uint32_t time,
			  int32_t slot,
			  int32_t seat_slot,
			  li_fixed_t x,
			  li_fixed_t y);

void
touch_notify_touch_up(struct libinput_device *device,
		      uint32_t time,
		      int32_t slot,
		      int32_t seat_slot);

void
touch_notify_frame(struct libinput_device *device,
		   uint32_t time);

#endif /* LIBINPUT_PRIVATE_H */
