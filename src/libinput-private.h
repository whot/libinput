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

#include <errno.h>

#include "linux/input.h"

#include "libinput.h"
#include "libinput-util.h"

#define LIBINPUT_TABLET_AXIS_MAX LIBINPUT_TABLET_AXIS_TILT_Y

struct libinput_source;

struct libinput_interface_backend {
	int (*resume)(struct libinput *libinput);
	void (*suspend)(struct libinput *libinput);
	void (*destroy)(struct libinput *libinput);
	int (*device_change_seat)(struct libinput_device *device,
				  const char *seat_name);
};

struct libinput {
	int epoll_fd;
	struct list source_destroy_list;

	struct list seat_list;

	struct {
		struct list list;
		struct libinput_source *source;
		int fd;
	} timer;

	struct libinput_event **events;
	size_t events_count;
	size_t events_len;
	size_t events_in;
	size_t events_out;

	struct list tool_list;

	const struct libinput_interface *interface;
	const struct libinput_interface_backend *interface_backend;

	libinput_log_handler log_handler;
	enum libinput_log_priority log_priority;
	void *user_data;
	int refcount;
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
	enum libinput_config_status (*set_enabled)(struct libinput_device *device,
						   enum libinput_config_tap_state enable);
	enum libinput_config_tap_state (*get_enabled)(struct libinput_device *device);
	enum libinput_config_tap_state (*get_default)(struct libinput_device *device);
};

struct libinput_device_config_calibration {
	int (*has_matrix)(struct libinput_device *device);
	enum libinput_config_status (*set_matrix)(struct libinput_device *device,
						  const float matrix[6]);
	int (*get_matrix)(struct libinput_device *device,
			  float matrix[6]);
	int (*get_default_matrix)(struct libinput_device *device,
							  float matrix[6]);
};

struct libinput_device_config_send_events {
	uint32_t (*get_modes)(struct libinput_device *device);
	enum libinput_config_status (*set_mode)(struct libinput_device *device,
						   enum libinput_config_send_events_mode mode);
	enum libinput_config_send_events_mode (*get_mode)(struct libinput_device *device);
	enum libinput_config_send_events_mode (*get_default_mode)(struct libinput_device *device);
};

struct libinput_device_config_accel {
	int (*available)(struct libinput_device *device);
	enum libinput_config_status (*set_speed)(struct libinput_device *device,
						 double speed);
	double (*get_speed)(struct libinput_device *device);
	double (*get_default_speed)(struct libinput_device *device);
};

struct libinput_device_config_natural_scroll {
	int (*has)(struct libinput_device *device);
	enum libinput_config_status (*set_enabled)(struct libinput_device *device,
						   int enabled);
	int (*get_enabled)(struct libinput_device *device);
	int (*get_default_enabled)(struct libinput_device *device);
};

struct libinput_device_config_left_handed {
	int (*has)(struct libinput_device *device);
	enum libinput_config_status (*set)(struct libinput_device *device, int left_handed);
	int (*get)(struct libinput_device *device);
	int (*get_default)(struct libinput_device *device);
};

struct libinput_device_config_scroll_method {
	uint32_t (*get_methods)(struct libinput_device *device);
	enum libinput_config_status (*set_method)(struct libinput_device *device,
						  enum libinput_config_scroll_method method);
	enum libinput_config_scroll_method (*get_method)(struct libinput_device *device);
	enum libinput_config_scroll_method (*get_default_method)(struct libinput_device *device);
	enum libinput_config_status (*set_button)(struct libinput_device *device,
						  uint32_t button);
	uint32_t (*get_button)(struct libinput_device *device);
	uint32_t (*get_default_button)(struct libinput_device *device);
};

struct libinput_device_config_click_method {
	uint32_t (*get_methods)(struct libinput_device *device);
	enum libinput_config_status (*set_method)(struct libinput_device *device,
						  enum libinput_config_click_method method);
	enum libinput_config_click_method (*get_method)(struct libinput_device *device);
	enum libinput_config_click_method (*get_default_method)(struct libinput_device *device);
};

struct libinput_device_config {
	struct libinput_device_config_tap *tap;
	struct libinput_device_config_calibration *calibration;
	struct libinput_device_config_send_events *sendevents;
	struct libinput_device_config_accel *accel;
	struct libinput_device_config_natural_scroll *natural_scroll;
	struct libinput_device_config_left_handed *left_handed;
	struct libinput_device_config_scroll_method *scroll_method;
	struct libinput_device_config_click_method *click_method;
};

struct libinput_device_group {
	int refcount;
	void *user_data;
	char *identifier; /* unique identifier or NULL for singletons */
};

struct libinput_device {
	struct libinput_seat *seat;
	struct libinput_device_group *group;
	struct list link;
	struct list event_listeners;
	void *user_data;
	int refcount;
	struct libinput_device_config config;
};

struct libinput_tool {
	struct list link;
	uint32_t serial;
	uint32_t tool_id;
	enum libinput_tool_type type;
	unsigned char axis_caps[NCHARS(LIBINPUT_TABLET_AXIS_MAX + 1)];
	unsigned char buttons[NCHARS(KEY_MAX) + 1];
	int refcount;
	void *user_data;
};

struct libinput_event {
	enum libinput_event_type type;
	struct libinput_device *device;
};

struct libinput_event_listener {
	struct list link;
	void (*notify_func)(uint64_t time, struct libinput_event *ev, void *notify_func_data);
	void *notify_func_data;
};

typedef void (*libinput_source_dispatch_t)(void *data);

#define log_debug(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_DEBUG, __VA_ARGS__)
#define log_info(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_INFO, __VA_ARGS__)
#define log_error(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_ERROR, __VA_ARGS__)
#define log_bug_kernel(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_ERROR, "kernel bug: " __VA_ARGS__)
#define log_bug_libinput(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_ERROR, "libinput bug: " __VA_ARGS__)
#define log_bug_client(li_, ...) log_msg((li_), LIBINPUT_LOG_PRIORITY_ERROR, "client bug: " __VA_ARGS__)

void
log_msg(struct libinput *libinput,
	enum libinput_log_priority priority,
	const char *format, ...);

void
log_msg_va(struct libinput *libinput,
	   enum libinput_log_priority priority,
	   const char *format,
	   va_list args);

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

struct libinput_device_group *
libinput_device_group_create(const char *identifier);

void
libinput_device_set_device_group(struct libinput_device *device,
				 struct libinput_device_group *group);

void
libinput_device_add_event_listener(struct libinput_device *device,
				   struct libinput_event_listener *listener,
				   void (*notify_func)(
						uint64_t time,
						struct libinput_event *event,
						void *notify_func_data),
				   void *notify_func_data);

void
libinput_device_remove_event_listener(struct libinput_event_listener *listener);

void
notify_added_device(struct libinput_device *device);

void
notify_removed_device(struct libinput_device *device);

void
keyboard_notify_key(struct libinput_device *device,
		    uint64_t time,
		    uint32_t key,
		    enum libinput_key_state state);

void
pointer_notify_motion(struct libinput_device *device,
		      uint64_t time,
		      double dx,
		      double dy,
		      double dx_unaccel,
		      double dy_unaccel);

void
pointer_notify_motion_absolute(struct libinput_device *device,
			       uint64_t time,
			       double x,
			       double y);

void
pointer_notify_button(struct libinput_device *device,
		      uint64_t time,
		      int32_t button,
		      enum libinput_button_state state);

void
pointer_notify_axis(struct libinput_device *device,
		    uint64_t time,
		    uint32_t axes,
		    enum libinput_pointer_axis_source source,
		    double x,
		    double y,
		    double x_discrete,
		    double y_discrete);

void
touch_notify_touch_down(struct libinput_device *device,
			uint64_t time,
			int32_t slot,
			int32_t seat_slot,
			double x,
			double y);

void
touch_notify_touch_motion(struct libinput_device *device,
			  uint64_t time,
			  int32_t slot,
			  int32_t seat_slot,
			  double x,
			  double y);

void
touch_notify_touch_up(struct libinput_device *device,
		      uint64_t time,
		      int32_t slot,
		      int32_t seat_slot);

void
tablet_notify_axis(struct libinput_device *device,
		   uint32_t time,
		   struct libinput_tool *tool,
		   unsigned char *changed_axes,
		   double *axes);

void
tablet_notify_proximity(struct libinput_device *device,
			uint32_t time,
			struct libinput_tool *tool,
			enum libinput_tool_proximity_state state,
			unsigned char *changed_axes,
			double *axes);

void
tablet_notify_button(struct libinput_device *device,
		     uint32_t time,
		     struct libinput_tool *tool,
		     double *axes,
		     int32_t button,
		     enum libinput_button_state state);
void
touch_notify_frame(struct libinput_device *device,
		   uint64_t time);

static inline uint64_t
libinput_now(struct libinput *libinput)
{
	struct timespec ts = { 0, 0 };

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		log_error(libinput, "clock_gettime failed: %s\n", strerror(errno));
		return 0;
	}

	return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000;
}
#endif /* LIBINPUT_PRIVATE_H */
