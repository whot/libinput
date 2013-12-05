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

#ifndef LIBINPUT_H
#define LIBINPUT_H

#include <stdlib.h>
#include <stdint.h>
#include <libudev.h>

/**
 * @mainpage
 * libinput is a generic input device handling library. It abstracts
 * commonly-used concepts such as keyboard, pointer and touchpad handling
 * behind an API.
 */

/**
 * libinput 24.8 fixed point real number.
 */
typedef int32_t li_fixed_t;

/**
 * @ingroup device
 *
 * Capabilities on a device. A device may have one or more capabilities
 * at a time, and capabilities may appear or disappear during the
 * lifteime of the device.
 */
enum libinput_device_capability {
	LIBINPUT_DEVICE_CAP_KEYBOARD = 0,
	LIBINPUT_DEVICE_CAP_POINTER = 1,
	LIBINPUT_DEVICE_CAP_TOUCH = 2,
};

/**
 * @ingroup device
 *
 * Logical state of a key. Note that the logical state may not represent
 * the physical state of the key.
 */
enum libinput_keyboard_key_state {
	LIBINPUT_KEYBOARD_KEY_STATE_RELEASED = 0,
	LIBINPUT_KEYBOARD_KEY_STATE_PRESSED = 1,
};

/**
 * @ingroup device
 *
 * Mask reflecting LEDs on a device.
 */
enum libinput_led {
	LIBINPUT_LED_NUM_LOCK = (1 << 0),
	LIBINPUT_LED_CAPS_LOCK = (1 << 1),
	LIBINPUT_LED_SCROLL_LOCK = (1 << 2),
};

/**
 * @ingroup device
 *
 * Logical state of a physical button. Note that the logical state may not
 * represent the physical state of the button.
 */
enum libinput_pointer_button_state {
	LIBINPUT_POINTER_BUTTON_STATE_RELEASED = 0,
	LIBINPUT_POINTER_BUTTON_STATE_PRESSED = 1,
};


/**
 * @ingroup device
 *
 * Axes on a device that are not x or y coordinates.
 */
enum libinput_pointer_axis {
	LIBINPUT_POINTER_AXIS_VERTICAL_SCROLL = 0,
	LIBINPUT_POINTER_AXIS_HORIZONTAL_SCROLL = 1,
};

/**
 * @ingroup device
 *
 * Logical touch state of a touch point. A touch point usually follows the
 * sequence down, motion, up, with the number of motion events being zero or
 * greater. If a touch point was used for gesture interpretation internally
 * and will not generate any further events, the touchpoint is cancelled.
 *
 * A frame event is set after a set of touchpoints that constitute one
 * logical set of points at a sampling point.
 */
enum libinput_touch_type {
	LIBINPUT_TOUCH_TYPE_DOWN = 0,
	LIBINPUT_TOUCH_TYPE_UP = 1,
	LIBINPUT_TOUCH_TYPE_MOTION = 2,
	LIBINPUT_TOUCH_TYPE_FRAME = 3,
	LIBINPUT_TOUCH_TYPE_CANCEL = 4,
};

/**
 * @ingroup base
 *
 * Event type for events returned by libinput_get_event().
 */
enum libinput_event_type {
	LIBINPUT_EVENT_ADDED_SEAT = 0,
	LIBINPUT_EVENT_REMOVED_SEAT,
	LIBINPUT_EVENT_ADDED_DEVICE,
	LIBINPUT_EVENT_REMOVED_DEVICE,

	LIBINPUT_EVENT_DEVICE_REGISTER_CAPABILITY = 200,
	LIBINPUT_EVENT_DEVICE_UNREGISTER_CAPABILITY,

	LIBINPUT_EVENT_KEYBOARD_KEY = 300,

	LIBINPUT_EVENT_POINTER_MOTION = 400,
	LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE,
	LIBINPUT_EVENT_POINTER_BUTTON,
	LIBINPUT_EVENT_POINTER_AXIS,

	LIBINPUT_EVENT_TOUCH_TOUCH = 500,
};

struct libinput;
struct libinput_device;
struct libinput_seat;

union libinput_event_target {
	struct libinput *libinput;
	struct libinput_seat *seat;
	struct libinput_device *device;
};

struct libinput_event {
	enum libinput_event_type type;
	union libinput_event_target target;
};

struct libinput_event_added_seat {
	struct libinput_event base;
	struct libinput_seat *seat;
};

struct libinput_event_removed_seat {
	struct libinput_event base;
	struct libinput_seat *seat;
};

struct libinput_event_added_device {
	struct libinput_event base;
	struct libinput_device *device;
};

struct libinput_event_removed_device {
	struct libinput_event base;
	struct libinput_device *device;
};

struct libinput_event_device_register_capability {
	struct libinput_event base;
	enum libinput_device_capability capability;
};

struct libinput_event_device_unregister_capability {
	struct libinput_event base;
	enum libinput_device_capability capability;
};

struct libinput_event_keyboard_key {
	struct libinput_event base;
	uint32_t time;
	uint32_t key;
	enum libinput_keyboard_key_state state;
};

struct libinput_event_pointer_motion {
	struct libinput_event base;
	uint32_t time;
	li_fixed_t dx;
	li_fixed_t dy;
};

struct libinput_event_pointer_motion_absolute {
	struct libinput_event base;
	uint32_t time;
	li_fixed_t x;
	li_fixed_t y;
};

struct libinput_event_pointer_button {
	struct libinput_event base;
	uint32_t time;
	uint32_t button;
	enum libinput_pointer_button_state state;
};

struct libinput_event_pointer_axis {
	struct libinput_event base;
	uint32_t time;
	enum libinput_pointer_axis axis;
	li_fixed_t value;
};

struct libinput_event_touch_touch {
	struct libinput_event base;
	uint32_t time;
	uint32_t slot;
	li_fixed_t x;
	li_fixed_t y;
	enum libinput_touch_type touch_type;
};

struct libinput_interface {
	int (*open_restricted)(const char *path, int flags, void *user_data);
	void (*close_restricted)(int fd, void *user_data);

	void (*get_current_screen_dimensions)(struct libinput_device *device,
					      int *width,
					      int *height,
					      void *user_data);
};

/**
 * @defgroup base Initialization and manipulation of libinput contexts
 */

/**
 * @ingroup base
 * Create a new libinput context from udev, for input devices matching
 * the given seat ID. New devices or devices removed will appear as events
 * during libinput_dispatch.
 *
 * @param interface The callback interface
 * @param user_data Caller-specific data passed to the various callback
 * interfaces.
 * @param udev An already initialized udev context
 * @param seat_id A seat identifier. This string must not be NULL.
 *
 * @return An initialize libinput context, ready to handle events or NULL on
 * error.
 */
struct libinput *
libinput_create_from_udev(const struct libinput_interface *interface,
			  void *user_data,
			  struct udev *udev,
			  const char *seat_id);

/**
 * @ingroup base
 *
 * libinput keeps a single file descriptor for all events. Call into
 * libinput_dispatch() if any events become available on this fd.
 *
 * @return the file descriptor used to notify of pending events.
 */
int
libinput_get_fd(struct libinput *libinput);

/**
 * @ingroup base
 *
 * Main event dispatchment function. Reads events of the file descriptors
 * and processes them internall. Use libinput_get_event() to retrieve the
 * events.
 *
 * @param libinput A previously initialized libinput context
 *
 * @return 0 on success, or a negative errno on failure
 * @retval -EAGAIN libinput_dispatch completed successfully but no events
 * are ready to read with libinput_get_event()
 */
int
libinput_dispatch(struct libinput *libinput);

/**
 * @ingroup base
 *
 * Retrieve the next event from libinput's internal event queue.
 *
 * @param libinput A previously initialized libinput context
 * @return The next available event, or NULL if no event is available.
 */
struct libinput_event *
libinput_get_event(struct libinput *libinput);

/**
 * @ingroup base
 *
 * @param libinput A previously initialized libinput context
 * @return the caller-specific data previously assigned in
 * libinput_create_udev().
 */
void *
libinput_get_user_data(struct libinput *libinput);

/**
 * @ingroup base
 *
 * Resume a suspended libinput context. This re-enables device
 * monitoring and adds existing devices.
 *
 * @param libinput A previously initialized libinput context
 * @see libinput_suspend
 *
 * @return 0 on success or -1 on failure
 */
int
libinput_resume(struct libinput *libinput);

/**
 * @ingroup base
 *
 * Suspend monitoring for new devices and close existing devices.
 * This all but terminates libinput but does keep the context
 * valid to be resumed with libinput_resume().
 *
 * @param libinput A previously initialized libinput context
 */
void
libinput_suspend(struct libinput *libinput);

/**
 * @ingroup base
 *
 * Destroy the libinput context.
 *
 * @param libinput A previously initialized libinput context
 */
void
libinput_destroy(struct libinput *libinput);

/**
 * @defgroup seat Initialization and manipulation of seats
 */

/**
 * @ingroup seat
 *
 * Increase the refcount of the seat. A seat will be freed whenever the
 * refcount reaches 0. This may happen during dispatch if the
 * seat was removed from the system. A caller must ensure to reference
 * the seat correctly to avoid dangling pointers.
 *
 * @param seat A previously obtained seat
 */
void
libinput_seat_ref(struct libinput_seat *seat);

/**
 * @ingroup seat
 *
 * Decrease the refcount of the seat. A seat will be freed whenever the
 * refcount reaches 0. This may happen during dispatch if the
 * seat was removed from the system. A caller must ensure to reference
 * the seat correctly to avoid dangling pointers.
 *
 * @param seat A previously obtained seat
 */
void
libinput_seat_unref(struct libinput_seat *seat);

/**
 * @ingroup seat
 *
 * Set caller-specific data associated with this seat. libinput does
 * not manage, look at, or modify this data. The caller must ensure the
 * data is valid.
 *
 * @param seat A previously obtained seat
 * @param user_data Caller-specific data pointer
 * @see libinput_seat_get_user_data
 */
void
libinput_seat_set_user_data(struct libinput_seat *seat, void *user_data);

/**
 * @ingroup seat
 *
 * Get the caller-specific data associated with this seat, if any.
 *
 * @param seat A previously obtained seat
 * @return Caller-specific data pointer or NULL if none was set
 * @see libinput_seat_set_user_data
 */
void *
libinput_seat_get_user_data(struct libinput_seat *seat);

/**
 * @ingroup seat
 *
 * @param seat A previously obtained seat
 * @return the name of this seat
 */
const char *
libinput_seat_get_name(struct libinput_seat *seat);

/**
 * @defgroup device Initialization and manipulation of input devices
 */

/**
 * @ingroup device
 *
 * Increase the refcount of the input device. An input device will be freed
 * whenever the refcount reaches 0. This may happen during dispatch if the
 * device was removed from the system. A caller must ensure to reference
 * the device correctly to avoid dangling pointers.
 *
 * @param device A previously obtained device
 */
void
libinput_device_ref(struct libinput_device *device);

/**
 * @ingroup device
 *
 * Decrease the refcount of the input device. An input device will be freed
 * whenever the refcount reaches 0. This may happen during dispatch if the
 * device was removed from the system. A caller must ensure to reference
 * the device correctly to avoid dangling pointers.
 *
 * @param device A previously obtained device
 */
void
libinput_device_unref(struct libinput_device *device);

/**
 * @ingroup device
 *
 * Set caller-specific data associated with this input device. libinput does
 * not manage, look at, or modify this data. The caller must ensure the
 * data is valid.
 *
 * @param device A previously obtained device
 * @param user_data Caller-specific data pointer
 * @see libinput_device_get_user_data
 */
void
libinput_device_set_user_data(struct libinput_device *device, void *user_data);

/**
 * @ingroup device
 *
 * Get the caller-specific data associated with this input device, if any.
 *
 * @param device A previously obtained device
 * @return Caller-specific data pointer or NULL if none was set
 * @see libinput_device_set_user_data
 */
void *
libinput_device_get_user_data(struct libinput_device *device);

/**
 * @ingroup device
 *
 * A device may be mapped to a single output, or all available outputs. If a
 * device is mapped to a single output only, a relative device may not move
 * beyond the boundaries of this output. An absolute device has its input
 * coordinates mapped to the extents of this output.
 *
 * @return the name of the output this device is mapped to, or NULL if no
 * output is set
 */
const char *
libinput_device_get_output_name(struct libinput_device *device);

/**
 * @ingroup device
 *
 * Get the seat associated with this input device.
 *
 * @param device A previously obtained device
 * @return The seat this input device belongs to
 */
struct libinput_seat *
libinput_device_get_seat(struct libinput_device *device);

/**
 * @ingroup device
 *
 * Update the LEDs on the device, if any. If the device does not have
 * LEDs, or does not have one or more of the LEDs given in the mask, this
 * function does nothing.
 *
 * @param device A previously obtained device
 * @param leds A mask of the LEDs to set, or unset.
 */
void
libinput_device_led_update(struct libinput_device *device,
			   enum libinput_led leds);

/**
 * @ingroup device
 *
 * Set the bitmask in keys to the bitmask of the keys present on the device
 * (see linux/input.h), up to size characters.
 *
 * @param device A current input device
 * @param keys An array filled with the bitmask for the keys
 * @param size Size of the keys array
 */
int
libinput_device_get_keys(struct libinput_device *device,
			 char *keys, size_t size);

/**
 * @ingroup device
 *
 * Apply the 3x3 transformation matrix to absolute device coordinates. This
 * matrix has no effect on relative events.
 *
 * Given a 6-element array [a, b, c, d, e, f], the matrix is applied as
 * @code
 * [ a  b  c ]   [ x ]
 * [ d  e  f ] * [ y ]
 * [ 0  0  1 ]   [ 1 ]
 * @endcode
 */
void
libinput_device_calibrate(struct libinput_device *device,
			  float calibration[6]);

#endif /* LIBINPUT_H */
