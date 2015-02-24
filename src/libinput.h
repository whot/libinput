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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <libudev.h>

#define LIBINPUT_ATTRIBUTE_PRINTF(_format, _args) \
	__attribute__ ((format (printf, _format, _args)))
#define LIBINPUT_ATTRIBUTE_DEPRECATED __attribute__ ((deprecated))

/**
 * Log priority for internal logging messages.
 */
enum libinput_log_priority {
	LIBINPUT_LOG_PRIORITY_DEBUG = 10,
	LIBINPUT_LOG_PRIORITY_INFO = 20,
	LIBINPUT_LOG_PRIORITY_ERROR = 30,
};

/**
 * @ingroup device
 *
 * Capabilities on a device. A device may have one or more capabilities
 * at a time, and capabilities may appear or disappear during the
 * lifetime of the device.
 */
enum libinput_device_capability {
	LIBINPUT_DEVICE_CAP_KEYBOARD = 0,
	LIBINPUT_DEVICE_CAP_POINTER = 1,
	LIBINPUT_DEVICE_CAP_TOUCH = 2,
	LIBINPUT_DEVICE_CAP_TABLET = 3
};

/**
 * @ingroup device
 *
 * Logical state of a key. Note that the logical state may not represent
 * the physical state of the key.
 */
enum libinput_key_state {
	LIBINPUT_KEY_STATE_RELEASED = 0,
	LIBINPUT_KEY_STATE_PRESSED = 1
};

/**
 * @ingroup device
 *
 * Mask reflecting LEDs on a device.
 */
enum libinput_led {
	LIBINPUT_LED_NUM_LOCK = (1 << 0),
	LIBINPUT_LED_CAPS_LOCK = (1 << 1),
	LIBINPUT_LED_SCROLL_LOCK = (1 << 2)
};

/**
 * @ingroup device
 *
 * Logical state of a physical button. Note that the logical state may not
 * represent the physical state of the button.
 */
enum libinput_button_state {
	LIBINPUT_BUTTON_STATE_RELEASED = 0,
	LIBINPUT_BUTTON_STATE_PRESSED = 1
};

/**
 * @ingroup device
 *
 * Axes on a device that are not x or y coordinates.
 *
 * The two scroll axes @ref LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL and
 * @ref LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL are engaged separately,
 * depending on the device. libinput provides some scroll direction locking
 * but it is up to the caller to determine which axis is needed and
 * appropriate in the current interaction
 */
enum libinput_pointer_axis {
	LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL = 0,
	LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL = 1,
};

/**
 * @ingroup device
 *
 * The source for a libinput_pointer_axis event. See
 * libinput_event_pointer_get_axis_source() for details.
 */
enum libinput_pointer_axis_source {
	/**
	 * The event is caused by the rotation of a wheel.
	 */
	LIBINPUT_POINTER_AXIS_SOURCE_WHEEL = 1,
	/**
	 * The event is caused by the movement of one or more fingers on a
	 * device.
	 */
	LIBINPUT_POINTER_AXIS_SOURCE_FINGER,
	/**
	 * The event is caused by the motion of some device.
	 */
	LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS,
};

/**
 * @ingroup device
 *
 * Available axis types for a device. It must have the @ref
 * LIBINPUT_DEVICE_CAP_TABLET capability.
 */
enum libinput_tablet_axis {
	LIBINPUT_TABLET_AXIS_X = 1,
	LIBINPUT_TABLET_AXIS_Y = 2,
	LIBINPUT_TABLET_AXIS_DISTANCE = 3,
	LIBINPUT_TABLET_AXIS_PRESSURE = 4,
	LIBINPUT_TABLET_AXIS_TILT_X = 5,
	LIBINPUT_TABLET_AXIS_TILT_Y = 6,
};

/**
 * @ingroup device
 *
 * An object representing a tool being used by the device. It must have the @ref
 * LIBINPUT_DEVICE_CAP_TABLET capability.
 */
struct libinput_tool;

/**
 * @ingroup device
 *
 * Available tool types for a device. It must have the @ref
 * LIBINPUT_DEVICE_CAP_TABLET capability. The tool type defines the default
 * usage of the tool as advertised by the manufacturer. Multiple different
 * physical tools may share the same tool type, e.g. a Wacom Classic Pen,
 * Wacom Pro Pen and a Wacom Grip Pen are all of type LIBINPUT_TOOL_PEN.
 *
 * Note that on some device, the eraser tool is on the tail end of a pen
 * device. On other devices, e.g. MS Surface 3, the eraser is the pen tip
 * while a button is held down.
 *
 * @note The @ref libinput_tool_type can only describe the default physical
 * type of the device. For devices with adjustible physical properties
 * the tool type remains the same, i.e. putting a Wacom stroke nib into a
 * classic pen leaves the tool type as @ref LIBINPUT_TOOL_PEN.
 */
enum libinput_tool_type {
	LIBINPUT_TOOL_PEN = 1,		/**< A generic pen */
	LIBINPUT_TOOL_ERASER,		/**< Eraser */
	LIBINPUT_TOOL_BRUSH,		/**< A paintbrush-like tool */
	LIBINPUT_TOOL_PENCIL,		/**< Physical drawing tool, e.g.
					     Wacom Inking Pen */
	LIBINPUT_TOOL_AIRBRUSH,		/**< An airbrush-like tool */
	LIBINPUT_TOOL_FINGER,		/**< Touch */
	LIBINPUT_TOOL_MOUSE,		/**< A mouse bound to the tablet */
	LIBINPUT_TOOL_LENS,		/**< A mouse tool with a lens */
};

/**
 * @ingroup device
 *
 * The state of proximity for a tool on a device. The device must have the @ref
 * LIBINPUT_DEVICE_CAP_TABLET capability.
 */
enum libinput_tool_proximity_state {
	LIBINPUT_TOOL_PROXIMITY_OUT = 0,
	LIBINPUT_TOOL_PROXIMITY_IN = 1,
};

/**
 * @ingroup base
 *
 * Event type for events returned by libinput_get_event().
 */
enum libinput_event_type {
	/**
	 * This is not a real event type, and is only used to tell the user that
	 * no new event is available in the queue. See
	 * libinput_next_event_type().
	 */
	LIBINPUT_EVENT_NONE = 0,

	/**
	 * Signals that a device has been added to the context. The device will
	 * not be read until the next time the user calls libinput_dispatch()
	 * and data is available.
	 *
	 * This allows setting up initial device configuration before any events
	 * are created.
	 */
	LIBINPUT_EVENT_DEVICE_ADDED,

	/**
	 * Signals that a device has been removed. No more events from the
	 * associated device will be in the queue or be queued after this event.
	 */
	LIBINPUT_EVENT_DEVICE_REMOVED,

	LIBINPUT_EVENT_KEYBOARD_KEY = 300,

	LIBINPUT_EVENT_POINTER_MOTION = 400,
	LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE,
	LIBINPUT_EVENT_POINTER_BUTTON,
	LIBINPUT_EVENT_POINTER_AXIS,

	LIBINPUT_EVENT_TOUCH_DOWN = 500,
	LIBINPUT_EVENT_TOUCH_UP,
	LIBINPUT_EVENT_TOUCH_MOTION,
	LIBINPUT_EVENT_TOUCH_CANCEL,
	/**
	 * Signals the end of a set of touchpoints at one device sample
	 * time. This event has no coordinate information attached.
	 */
	LIBINPUT_EVENT_TOUCH_FRAME,

	LIBINPUT_EVENT_TABLET_AXIS = 600,
	/**
	 * Signals that a tool has come in or out of proximity of a device with
	 * the @ref LIBINPUT_DEVICE_CAP_TABLET capability.
	 *
	 * Proximity events contain each of the current values for each axis,
	 * and these values may be extracted from them in the same way they are
	 * with @ref LIBINPUT_EVENT_TABLET_AXIS events.
	 *
	 * Some tools may always be in proximity. For these tools, events with
	 * state @ref LIBINPUT_TOOL_PROXIMITY_IN are sent only once after @ref
	 * LIBINPUT_EVENT_DEVICE_ADDED, and likewise events with state @ref
	 * LIBINPUT_TOOL_PROXIMITY_OUT are sent only once before @ref
	 * LIBINPUT_EVENT_DEVICE_REMOVED.
	 *
	 * If the tool that comes into proximity supports x/y coordinates,
	 * libinput guarantees that both x and y are set in the proximity
	 * event.
	 *
	 * When a tool goes out of proximity, the value of every axis should be
	 * assumed to have a value of 0 and any buttons that are currently held
	 * down on the stylus are marked as released. Button release events for
	 * each button that was held down on the stylus are sent before the
	 * initial proximity out event.
	 */
	LIBINPUT_EVENT_TABLET_PROXIMITY,
	LIBINPUT_EVENT_TABLET_BUTTON
};

/**
 * @ingroup base
 * @struct libinput
 *
 * A handle for accessing libinput. This struct is refcounted, use
 * libinput_ref() and libinput_unref().
 */
struct libinput;

/**
 * @ingroup device
 * @struct libinput_device
 *
 * A base handle for accessing libinput devices. This struct is
 * refcounted, use libinput_device_ref() and libinput_device_unref().
 */
struct libinput_device;

/**
 * @ingroup device
 * @struct libinput_device_group
 *
 * A base handle for accessing libinput device groups. This struct is
 * refcounted, use libinput_device_group_ref() and
 * libinput_device_group_unref().
 */
struct libinput_device_group;

/**
 * @ingroup seat
 * @struct libinput_seat
 *
 * The base handle for accessing libinput seats. This struct is
 * refcounted, use libinput_seat_ref() and libinput_seat_unref().
 */
struct libinput_seat;

/**
 * @ingroup event
 * @struct libinput_event
 *
 * The base event type. Use libinput_event_get_pointer_event() or similar to
 * get the actual event type.
 *
 * @warning Unlike other structs events are considered transient and
 * <b>not</b> refcounted.
 */
struct libinput_event;

/**
 * @ingroup event
 * @struct libinput_event_device_notify
 *
 * An event notifying the caller of a device being added or removed.
 */
struct libinput_event_device_notify;

/**
 * @ingroup event_keyboard
 * @struct libinput_event_keyboard
 *
 * A keyboard event representing a key press/release.
 */
struct libinput_event_keyboard;

/**
 * @ingroup event_pointer
 * @struct libinput_event_pointer
 *
 * A pointer event representing relative or absolute pointer movement,
 * a button press/release or scroll axis events.
 */
struct libinput_event_pointer;

/**
 * @ingroup event_touch
 * @struct libinput_event_touch
 *
 * Touch event representing a touch down, move or up, as well as a touch
 * cancel and touch frame events. Valid event types for this event are @ref
 * LIBINPUT_EVENT_TOUCH_DOWN, @ref LIBINPUT_EVENT_TOUCH_MOTION, @ref
 * LIBINPUT_EVENT_TOUCH_UP, @ref LIBINPUT_EVENT_TOUCH_CANCEL and @ref
 * LIBINPUT_EVENT_TOUCH_FRAME.
 */
struct libinput_event_touch;

/**
 * @ingroup event_tablet
 * @struct libinput_event_tablet
 *
 * Tablet event representing an axis update, button press, or tool update. Valid
 * event types for this event are @ref LIBINPUT_EVENT_TABLET_AXIS, @ref
 * LIBINPUT_EVENT_TABLET_PROXIMITY and @ref LIBINPUT_EVENT_TABLET_BUTTON.
 */
struct libinput_event_tablet;

/**
 * @defgroup event Accessing and destruction of events
 */

/**
 * @ingroup event
 *
 * Destroy the event, freeing all associated resources. Resources obtained
 * from this event must be considered invalid after this call.
 *
 * @warning Unlike other structs events are considered transient and
 * <b>not</b> refcounted. Calling libinput_event_destroy() <b>will</b>
 * destroy the event.
 *
 * @param event An event retrieved by libinput_get_event().
 */
void
libinput_event_destroy(struct libinput_event *event);

/**
 * @ingroup event
 *
 * Get the type of the event.
 *
 * @param event An event retrieved by libinput_get_event().
 */
enum libinput_event_type
libinput_event_get_type(struct libinput_event *event);

/**
 * @ingroup event
 *
 * Get the libinput context from the event.
 *
 * @param event The libinput event
 * @return The libinput context for this event.
 */
struct libinput *
libinput_event_get_context(struct libinput_event *event);

/**
 * @ingroup event
 *
 * Return the device associated with this event, if applicable. For device
 * added/removed events this is the device added or removed. For all other
 * device events, this is the device that generated the event.
 *
 * This device is not refcounted and its lifetime is that of the event. Use
 * libinput_device_ref() before using the device outside of this scope.
 *
 * @return The device associated with this event
 */

struct libinput_device *
libinput_event_get_device(struct libinput_event *event);

/**
 * @ingroup event
 *
 * Return the pointer event that is this input event. If the event type does
 * not match the pointer event types, this function returns NULL.
 *
 * The inverse of this function is libinput_event_pointer_get_base_event().
 *
 * @return A pointer event, or NULL for other events
 */
struct libinput_event_pointer *
libinput_event_get_pointer_event(struct libinput_event *event);

/**
 * @ingroup event
 *
 * Return the keyboard event that is this input event. If the event type does
 * not match the keyboard event types, this function returns NULL.
 *
 * The inverse of this function is libinput_event_keyboard_get_base_event().
 *
 * @return A keyboard event, or NULL for other events
 */
struct libinput_event_keyboard *
libinput_event_get_keyboard_event(struct libinput_event *event);

/**
 * @ingroup event
 *
 * Return the touch event that is this input event. If the event type does
 * not match the touch event types, this function returns NULL.
 *
 * The inverse of this function is libinput_event_touch_get_base_event().
 *
 * @return A touch event, or NULL for other events
 */
struct libinput_event_touch *
libinput_event_get_touch_event(struct libinput_event *event);

/**
 * @ingroup event
 *
 * Return the tablet event that is this input event. If the event type does not
 * match the tablet event types, this function returns NULL.
 *
 * The inverse of this function is libinput_event_tablet_get_base_event().
 *
 * @return A tablet event, or NULL for other events
 */
struct libinput_event_tablet *
libinput_event_get_tablet_event(struct libinput_event *event);

/**
 * @ingroup event
 *
 * Return the device event that is this input event. If the event type does
 * not match the device event types, this function returns NULL.
 *
 * The inverse of this function is
 * libinput_event_device_notify_get_base_event().
 *
 * @return A device event, or NULL for other events
 */
struct libinput_event_device_notify *
libinput_event_get_device_notify_event(struct libinput_event *event);

/**
 * @ingroup event
 *
 * @return The generic libinput_event of this event
 */
struct libinput_event *
libinput_event_device_notify_get_base_event(struct libinput_event_device_notify *event);

/**
 * @defgroup event_keyboard Keyboard events
 *
 * Key events are generated when a key changes its logical state, usually by
 * being pressed or released.
 */

/**
 * @ingroup event_keyboard
 *
 * @return The event time for this event
 */
uint32_t
libinput_event_keyboard_get_time(struct libinput_event_keyboard *event);

/**
 * @ingroup event_keyboard
 *
 * @return The keycode that triggered this key event
 */
uint32_t
libinput_event_keyboard_get_key(struct libinput_event_keyboard *event);

/**
 * @ingroup event_keyboard
 *
 * @return The state change of the key
 */
enum libinput_key_state
libinput_event_keyboard_get_key_state(struct libinput_event_keyboard *event);

/**
 * @ingroup event_keyboard
 *
 * @return The generic libinput_event of this event
 */
struct libinput_event *
libinput_event_keyboard_get_base_event(struct libinput_event_keyboard *event);

/**
 * @ingroup event_keyboard
 *
 * For the key of a @ref LIBINPUT_EVENT_KEYBOARD_KEY event, return the total number
 * of keys pressed on all devices on the associated seat after the event was
 * triggered.
 *
 " @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_KEYBOARD_KEY. For other events, this function returns 0.
 *
 * @return the seat wide pressed key count for the key of this event
 */
uint32_t
libinput_event_keyboard_get_seat_key_count(
	struct libinput_event_keyboard *event);

/**
 * @defgroup event_pointer Pointer events
 *
 * Pointer events reflect motion, button and scroll events, as well as
 * events from other axes.
 */

/**
 * @ingroup event_pointer
 *
 * @return The event time for this event
 */
uint32_t
libinput_event_pointer_get_time(struct libinput_event_pointer *event);

/**
 * @ingroup event_pointer
 *
 * Return the delta between the last event and the current event. For pointer
 * events that are not of type @ref LIBINPUT_EVENT_POINTER_MOTION, this
 * function returns 0.
 *
 * If a device employs pointer acceleration, the delta returned by this
 * function is the accelerated delta.
 *
 * Relative motion deltas are normalized to represent those of a device with
 * 1000dpi resolution. See @ref motion_normalization for more details.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_MOTION.
 *
 * @return the relative x movement since the last event
 */
double
libinput_event_pointer_get_dx(struct libinput_event_pointer *event);

/**
 * @ingroup event_pointer
 *
 * Return the delta between the last event and the current event. For pointer
 * events that are not of type @ref LIBINPUT_EVENT_POINTER_MOTION, this
 * function returns 0.
 *
 * If a device employs pointer acceleration, the delta returned by this
 * function is the accelerated delta.
 *
 * Relative motion deltas are normalized to represent those of a device with
 * 1000dpi resolution. See @ref motion_normalization for more details.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_MOTION.
 *
 * @return the relative y movement since the last event
 */
double
libinput_event_pointer_get_dy(struct libinput_event_pointer *event);

/**
 * @ingroup event_pointer
 *
 * Return the relative delta of the unaccelerated motion vector of the
 * current event. For pointer events that are not of type @ref
 * LIBINPUT_EVENT_POINTER_MOTION, this function returns 0.
 *
 * Relative unaccelerated motion deltas are normalized to represent those of a
 * device with 1000dpi resolution. See @ref motion_normalization for more
 * details. Note that unaccelerated events are not equivalent to 'raw' events
 * as read from the device.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_MOTION.
 *
 * @return the unaccelerated relative x movement since the last event
 */
double
libinput_event_pointer_get_dx_unaccelerated(
	struct libinput_event_pointer *event);

/**
 * @ingroup event_pointer
 *
 * Return the relative delta of the unaccelerated motion vector of the
 * current event. For pointer events that are not of type @ref
 * LIBINPUT_EVENT_POINTER_MOTION, this function returns 0.
 *
 * Relative unaccelerated motion deltas are normalized to represent those of a
 * device with 1000dpi resolution. See @ref motion_normalization for more
 * details. Note that unaccelerated events are not equivalent to 'raw' events
 * as read from the device.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_MOTION.
 *
 * @return the unaccelerated relative y movement since the last event
 */
double
libinput_event_pointer_get_dy_unaccelerated(
	struct libinput_event_pointer *event);

/**
 * @ingroup event_pointer
 *
 * Return the current absolute x coordinate of the pointer event, in mm from
 * the top left corner of the device. To get the corresponding output screen
 * coordinate, use libinput_event_pointer_get_absolute_x_transformed().
 *
 * For pointer events that are not of type
 * @ref LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE, this function returns 0.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE.
 *
 * @return the current absolute x coordinate
 */
double
libinput_event_pointer_get_absolute_x(struct libinput_event_pointer *event);

/**
 * @ingroup event_pointer
 *
 * Return the current absolute y coordinate of the pointer event, in mm from
 * the top left corner of the device. To get the corresponding output screen
 * coordinate, use libinput_event_pointer_get_absolute_y_transformed().
 *
 * For pointer events that are not of type
 * @ref LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE, this function returns 0.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE.
 *
 * @return the current absolute y coordinate
 */
double
libinput_event_pointer_get_absolute_y(struct libinput_event_pointer *event);

/**
 * @ingroup event_pointer
 *
 * Return the current absolute x coordinate of the pointer event, transformed to
 * screen coordinates.
 *
 * For pointer events that are not of type
 * @ref LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE, the return value of this
 * function is undefined.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE.
 *
 * @param event The libinput pointer event
 * @param width The current output screen width
 * @return the current absolute x coordinate transformed to a screen coordinate
 */
double
libinput_event_pointer_get_absolute_x_transformed(
	struct libinput_event_pointer *event,
	uint32_t width);

/**
 * @ingroup event_pointer
 *
 * Return the current absolute y coordinate of the pointer event, transformed to
 * screen coordinates.
 *
 * For pointer events that are not of type
 * @ref LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE, the return value of this function is
 * undefined.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE.
 *
 * @param event The libinput pointer event
 * @param height The current output screen height
 * @return the current absolute y coordinate transformed to a screen coordinate
 */
double
libinput_event_pointer_get_absolute_y_transformed(
	struct libinput_event_pointer *event,
	uint32_t height);

/**
 * @ingroup event_pointer
 *
 * Return the button that triggered this event.
 * For pointer events that are not of type @ref
 * LIBINPUT_EVENT_POINTER_BUTTON, this function returns 0.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_BUTTON.
 *
 * @return the button triggering this event
 */
uint32_t
libinput_event_pointer_get_button(struct libinput_event_pointer *event);

/**
 * @ingroup event_pointer
 *
 * Return the button state that triggered this event.
 * For pointer events that are not of type @ref
 * LIBINPUT_EVENT_POINTER_BUTTON, this function returns 0.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_BUTTON.
 *
 * @return the button state triggering this event
 */
enum libinput_button_state
libinput_event_pointer_get_button_state(struct libinput_event_pointer *event);

/**
 * @ingroup event_pointer
 *
 * For the button of a @ref LIBINPUT_EVENT_POINTER_BUTTON event, return the
 * total number of buttons pressed on all devices on the associated seat
 * after the event was triggered.
 *
 " @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_BUTTON. For other events, this function
 * returns 0.
 *
 * @return the seat wide pressed button count for the key of this event
 */
uint32_t
libinput_event_pointer_get_seat_button_count(
	struct libinput_event_pointer *event);

/**
 * @ingroup event_pointer
 *
 * Check if the event has a valid value for the given axis.
 *
 * If this function returns non-zero for an axis and
 * libinput_event_pointer_get_axis_value() returns a value of 0, the event
 * is a scroll stop event.
 *
 * @return non-zero if this event contains a value for this axis
 */
int
libinput_event_pointer_has_axis(struct libinput_event_pointer *event,
				enum libinput_pointer_axis axis);

/**
 * @ingroup event_pointer
 *
 * Return the axis value of the given axis. The interpretation of the value
 * depends on the axis. For the two scrolling axes
 * @ref LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL and
 * @ref LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, the value of the event is in
 * relative scroll units, with the positive direction being down or right,
 * respectively. For the interpretation of the value, see
 * libinput_event_pointer_get_axis_source().
 *
 * If libinput_event_pointer_has_axis() returns 0 for an axis, this function
 * returns 0 for that axis.
 *
 * For pointer events that are not of type @ref LIBINPUT_EVENT_POINTER_AXIS,
 * this function returns 0.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_AXIS.
 *
 * @return the axis value of this event
 *
 * @see libinput_event_pointer_get_axis_value_discrete
 */
double
libinput_event_pointer_get_axis_value(struct libinput_event_pointer *event,
				      enum libinput_pointer_axis axis);

/**
 * @ingroup event_pointer
 *
 * Return the source for a given axis event. Axis events (scroll events) can
 * be caused by a hardware item such as a scroll wheel or emulated from
 * other input sources, such as two-finger or edge scrolling on a
 * touchpad.
 *
 * If the source is @ref LIBINPUT_POINTER_AXIS_SOURCE_FINGER, libinput
 * guarantees that a scroll sequence is terminated with a scroll value of 0.
 * A caller may use this information to decide on whether kinetic scrolling
 * should be triggered on this scroll sequence.
 * The coordinate system is identical to the cursor movement, i.e. a
 * scroll value of 1 represents the equivalent relative motion of 1.
 *
 * If the source is @ref LIBINPUT_POINTER_AXIS_SOURCE_WHEEL, no terminating
 * event is guaranteed (though it may happen).
 * Scrolling is in discrete steps, the value is the angle the wheel moved
 * in degrees. The default is 15 degrees per wheel click, but some mice may
 * have differently grained wheels. It is up to the caller how to interpret
 * such different step sizes.
 *
 * If the source is @ref LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS, no
 * terminating event is guaranteed (though it may happen).
 * The coordinate system is identical to the cursor movement, i.e. a
 * scroll value of 1 represents the equivalent relative motion of 1.
 *
 * For pointer events that are not of type @ref LIBINPUT_EVENT_POINTER_AXIS,
 * this function returns 0.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_POINTER_AXIS.
 *
 * @return the source for this axis event
 */
enum libinput_pointer_axis_source
libinput_event_pointer_get_axis_source(struct libinput_event_pointer *event);

/**
 * @ingroup pointer
 *
 * Return the axis value in discrete steps for a given axis event. How a
 * value translates into a discrete step depends on the source.
 *
 * If the source is @ref LIBINPUT_POINTER_AXIS_SOURCE_WHEEL, the discrete
 * value correspond to the number of physical mouse wheel clicks.
 *
 * If the source is @ref LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS or @ref
 * LIBINPUT_POINTER_AXIS_SOURCE_FINGER, the discrete value is always 0.
 *
 * @return The discrete value for the given event.
 *
 * @see libinput_event_pointer_get_axis_value
 */
double
libinput_event_pointer_get_axis_value_discrete(struct libinput_event_pointer *event,
					       enum libinput_pointer_axis axis);

/**
 * @ingroup event_pointer
 *
 * @return The generic libinput_event of this event
 */
struct libinput_event *
libinput_event_pointer_get_base_event(struct libinput_event_pointer *event);

/**
 * @defgroup event_touch Touch events
 *
 * Events from absolute touch devices.
 */

/**
 * @ingroup event_touch
 *
 * @return The event time for this event
 */
uint32_t
libinput_event_touch_get_time(struct libinput_event_touch *event);

/**
 * @ingroup event_touch
 *
 * Get the slot of this touch event. See the kernel's multitouch
 * protocol B documentation for more information.
 *
 * If the touch event has no assigned slot, for example if it is from a
 * single touch device, this function returns -1.
 *
 * @note this function should not be called for @ref
 * LIBINPUT_EVENT_TOUCH_CANCEL or @ref LIBINPUT_EVENT_TOUCH_FRAME.
 *
 * @return The slot of this touch event
 */
int32_t
libinput_event_touch_get_slot(struct libinput_event_touch *event);

/**
 * @ingroup event_touch
 *
 * Get the seat slot of the touch event. A seat slot is a non-negative seat
 * wide unique identifier of an active touch point.
 *
 * Events from single touch devices will be represented as one individual
 * touch point per device.
 *
 * @note this function should not be called for @ref
 * LIBINPUT_EVENT_TOUCH_CANCEL or @ref LIBINPUT_EVENT_TOUCH_FRAME.
 *
 * @return The seat slot of the touch event
 */
int32_t
libinput_event_touch_get_seat_slot(struct libinput_event_touch *event);

/**
 * @ingroup event_touch
 *
 * Return the current absolute x coordinate of the touch event, in mm from
 * the top left corner of the device. To get the corresponding output screen
 * coordinate, use libinput_event_touch_get_x_transformed().
 *
 * @note this function should only be called for @ref
 * LIBINPUT_EVENT_TOUCH_DOWN and @ref LIBINPUT_EVENT_TOUCH_MOTION.
 *
 * @param event The libinput touch event
 * @return the current absolute x coordinate
 */
double
libinput_event_touch_get_x(struct libinput_event_touch *event);

/**
 * @ingroup event_touch
 *
 * Return the current absolute y coordinate of the touch event, in mm from
 * the top left corner of the device. To get the corresponding output screen
 * coordinate, use libinput_event_touch_get_y_transformed().
 *
 * For @ref LIBINPUT_EVENT_TOUCH_UP 0 is returned.
 *
 * @note this function should only be called for @ref LIBINPUT_EVENT_TOUCH_DOWN and
 * @ref LIBINPUT_EVENT_TOUCH_MOTION.
 *
 * @param event The libinput touch event
 * @return the current absolute y coordinate
 */
double
libinput_event_touch_get_y(struct libinput_event_touch *event);

/**
 * @ingroup event_touch
 *
 * Return the current absolute x coordinate of the touch event, transformed to
 * screen coordinates.
 *
 * @note this function should only be called for @ref
 * LIBINPUT_EVENT_TOUCH_DOWN and @ref LIBINPUT_EVENT_TOUCH_MOTION.
 *
 * @param event The libinput touch event
 * @param width The current output screen width
 * @return the current absolute x coordinate transformed to a screen coordinate
 */
double
libinput_event_touch_get_x_transformed(struct libinput_event_touch *event,
				       uint32_t width);

/**
 * @ingroup event_touch
 *
 * Return the current absolute y coordinate of the touch event, transformed to
 * screen coordinates.
 *
 * @note this function should only be called for @ref
 * LIBINPUT_EVENT_TOUCH_DOWN and @ref LIBINPUT_EVENT_TOUCH_MOTION.
 *
 * @param event The libinput touch event
 * @param height The current output screen height
 * @return the current absolute y coordinate transformed to a screen coordinate
 */
double
libinput_event_touch_get_y_transformed(struct libinput_event_touch *event,
				       uint32_t height);

/**
 * @ingroup event_touch
 *
 * @return The generic libinput_event of this event
 */
struct libinput_event *
libinput_event_touch_get_base_event(struct libinput_event_touch *event);

/**
 * @defgroup event_tablet Tablet events
 *
 * Events that come from tablet devices.
 */

/**
 * @ingroup event_tablet
 *
 * Checks if an axis was updated in this event or return 0 otherwise.
 * For tablet events that are not of type @ref LIBINPUT_EVENT_TABLET_AXIS or
 * type @ref LIBINPUT_EVENT_TABLET_PROXIMITY, this function returns 0.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_TABLET_AXIS and @ref LIBINPUT_EVENT_TABLET_PROXIMITY.
 *
 * @param event The libinput tablet event
 * @param axis The axis to check for updates
 * @return 1 if the axis was updated or 0 otherwise
 */
int
libinput_event_tablet_axis_has_changed(struct libinput_event_tablet *event,
				       enum libinput_tablet_axis axis);

/**
 * @ingroup event_tablet
 *
 * Return the axis value of a given axis for a tablet. The interpretation of the
 * value is dependent on the axis:
 * - @ref LIBINPUT_TABLET_AXIS_X and @ref LIBINPUT_TABLET_AXIS_Y - the X and
 *   Y coordinates of the tablet tool, in mm from the top left corner of the
 *   tablet. Use libinput_event_tablet_get_x_transformed() and
 *   libinput_event_tablet_get_y_transformed() for transforming each
 *   respective axis value.
 * - @ref LIBINPUT_TABLET_AXIS_DISTANCE - The distance from the tablet's
 *   sensor, normalized from 0 to 1
 * - @ref LIBINPUT_TABLET_AXIS_PRESSURE - The current pressure being applied on
 *   the tool in use, normalized from 0 to 1
 * - @ref LIBINPUT_TABLET_AXIS_TILT_X and @ref LIBINPUT_TABLET_AXIS_TILT_Y -
 *   normalized value between -1 and 1 that indicates the X or Y tilt of the
 *   tool
 *
 * @note This function may be called for a specific axis even if
 * libinput_event_tablet_axis_has_changed() returns 0 for that axis.
 * libinput always includes all device axes in the event.
 *
 * @param event The libinput tablet event
 * @param axis The axis to retrieve the value of
 * @return The current value of the the axis
 */
double
libinput_event_tablet_get_axis_value(struct libinput_event_tablet *event,
				     enum libinput_tablet_axis axis);

/**
 * @ingroup event_tablet
 *
 * Return the current absolute x coordinate of the tablet event, transformed to
 * screen coordinates.
 *
 * @note This function may be called for a specific axis even if
 * libinput_event_tablet_axis_has_changed() returns 0 for that axis.
 * libinput always includes all device axes in the event.
 *
 * @param event The libinput tablet event
 * @param width The current output screen width
 * @return the current absolute x coordinate transformed to a screen coordinate
 */
double
libinput_event_tablet_get_x_transformed(struct libinput_event_tablet *event,
					uint32_t width);

/**
 * @ingroup event_tablet
 *
 * Return the current absolute y coordinate of the tablet event, transformed to
 * screen coordinates.
 *
 * @note This function may be called for a specific axis even if
 * libinput_event_tablet_axis_has_changed() returns 0 for that axis.
 * libinput always includes all device axes in the event.
 *
 * @param event The libinput tablet event
 * @param height The current output screen height
 * @return the current absolute y coordinate transformed to a screen coordinate
 */
double
libinput_event_tablet_get_y_transformed(struct libinput_event_tablet *event,
					uint32_t height);

/**
 * @ingroup event_tablet
 *
 * Returns the tool that was in use during this event.
 * By default, each tool will stay valid for as long as it is being used, and is
 * destroyed when another tool comes into proximity. However, the lifetime of
 * the tool may be extended by using libinput_tool_ref() to increment the
 * reference count of the tool. This guarantees that whenever the tool comes
 * back into proximity of the tablet, that the same structure will be used to
 * represent the tool.
 *
 * @note On tablets where the serial number of tools is not reported, each tool
 * cannot be guaranteed to be unique.
 *
 * @param event The libinput tablet event
 * @return The new tool triggering this event
 */
struct libinput_tool *
libinput_event_tablet_get_tool(struct libinput_event_tablet *event);

/**
 * @ingroup event_tablet
 *
 * Returns the new proximity state of a tool from a proximity event.
 * Used to check whether or not a tool came in or out of proximity during an
 * event of type @ref LIBINPUT_EVENT_TABLET_PROXIMITY.
 *
 * @param event The libinput tablet event
 * @return The new proximity state of the tool from the event.
 */
enum libinput_tool_proximity_state
libinput_event_tablet_get_proximity_state(struct libinput_event_tablet *event);

/**
 * @ingroup event_tablet
 *
 * Return the button that triggered this event.
 * For tablet events that are not of type @ref LIBINPUT_EVENT_TABLET_BUTTON, this
 * function returns 0.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_TABLET_BUTTON.
 *
 * @param event The libinput tablet event
 * @return the button triggering this event
 */
uint32_t
libinput_event_tablet_get_button(struct libinput_event_tablet *event);

/**
 * @ingroup event_tablet
 *
 * Return the button state of the event.
 *
 * @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_TABLET_BUTTON.
 *
 * @param event The libinput tablet event
 * @return the button state triggering this event
 */
enum libinput_button_state
libinput_event_tablet_get_button_state(struct libinput_event_tablet *event);

/**
 * @ingroup event_tablet
 *
 * For the button of a @ref LIBINPUT_EVENT_TABLET_BUTTON event, return the total
 * number of buttons pressed on all devices on the associated seat after the
 * the event was triggered.
 *
 " @note It is an application bug to call this function for events other than
 * @ref LIBINPUT_EVENT_TABLET_BUTTON. For other events, this function returns 0.
 *
 * @return the seat wide pressed button count for the key of this event
 */
uint32_t
libinput_event_tablet_get_seat_button_count(struct libinput_event_tablet *event);

/**
 * @ingroup event_tablet
 *
 * @param event The libinput tablet event
 * @return The event time for this event
 */
uint32_t
libinput_event_tablet_get_time(struct libinput_event_tablet *event);

/**
 * @ingroup event_tablet
 *
 * Return the type of tool type for a tool object
 *
 * @param tool The libinput tool
 * @return The tool type for this tool object
 */
enum libinput_tool_type
libinput_tool_get_type(struct libinput_tool *tool);

/**
 * @ingroup event_tablet
 *
 * Increment the ref count of tool by one
 *
 * @param tool The tool to increment the ref count of
 * @return The passed tool
 */
struct libinput_tool *
libinput_tool_ref(struct libinput_tool *tool);

/**
 * @ingroup event_tablet
 *
 * Return whether or not a tablet tool supports the specified axis
 *
 * @param tool The tool to check the axis capabilities of
 * @param axis The axis to check for support
 * @return Whether or not the axis is supported
 */
int
libinput_tool_has_axis(struct libinput_tool *tool,
		       enum libinput_tablet_axis axis);

/**
 * @ingroup event_tablet
 *
 * Decrement the ref count of tool by one. When the ref count of tool reaches 0,
 * the memory allocated for tool will be freed.
 *
 * @param tool The tool to decrement the ref count of
 * @return NULL if the tool was destroyed otherwise the passed tool
 */
struct libinput_tool *
libinput_tool_unref(struct libinput_tool *tool);

/**
 * @ingroup event_tablet
 *
 * Return the serial number of a tool
 *
 * @note Not all tablets report a serial number along with the type of tool
 * being used. If the hardware does not provide a unique serial number, the
 * serial number is always 0.
 *
 * @param tool The libinput tool
 * @return The new tool serial triggering this event
 */
uint32_t
libinput_tool_get_serial(struct libinput_tool *tool);

/**
 * @ingroup event_tablet
 *
 * Return the user data associated with a tool object.
 *
 * @param tool The libinput tool
 * @return The user data associated with the tool object
 */
void *
libinput_tool_get_user_data(struct libinput_tool *tool);

/**
 * @ingroup event_tablet
 *
 * Set the user data associated with a tool object.
 *
 * @param tool The libinput tool
 * @param user_data The user data to associate with the tool object
 */
void
libinput_tool_set_user_data(struct libinput_tool *tool,
			    void *user_data);

/**
 * @defgroup base Initialization and manipulation of libinput contexts
 */

/**
 * @ingroup base
 * @struct libinput_interface
 *
 * libinput does not open file descriptors to devices directly, instead
 * open_restricted() and close_restricted() are called for each path that
 * must be opened.
 *
 * @see libinput_udev_create_context
 * @see libinput_path_create_context
 */
struct libinput_interface {
	/**
	 * Open the device at the given path with the flags provided and
	 * return the fd.
	 *
	 * @param path The device path to open
	 * @param flags Flags as defined by open(2)
	 * @param user_data The user_data provided in
	 * libinput_udev_create_context()
	 *
	 * @return the file descriptor, or a negative errno on failure.
	 */
	int (*open_restricted)(const char *path, int flags, void *user_data);
	/**
	 * Close the file descriptor.
	 *
	 * @param fd The file descriptor to close
	 * @param user_data The user_data provided in
	 * libinput_udev_create_context()
	 */
	void (*close_restricted)(int fd, void *user_data);
};

/**
 * @ingroup base
 *
 * Create a new libinput context from udev. This context is inactive until
 * assigned a seat ID with libinput_udev_assign_seat().
 *
 * @param interface The callback interface
 * @param user_data Caller-specific data passed to the various callback
 * interfaces.
 * @param udev An already initialized udev context
 *
 * @return An initialized, but inactive libinput context or NULL on error
 */
struct libinput *
libinput_udev_create_context(const struct libinput_interface *interface,
			     void *user_data,
			     struct udev *udev);

/**
 * @ingroup base
 *
 * Assign a seat to this libinput context. New devices or the removal of
 * existing devices will appear as events during libinput_dispatch().
 *
 * libinput_udev_assign_seat() succeeds even if no input devices are currently
 * available on this seat, or if devices are available but fail to open in
 * @ref libinput_interface::open_restricted. Devices that do not have the
 * minimum capabilities to be recognized as pointer, keyboard or touch
 * device are ignored. Such devices and those that failed to open
 * ignored until the next call to libinput_resume().
 *
 * This function may only be called once per context.
 *
 * @param libinput A libinput context initialized with
 * libinput_udev_create_context()
 * @param seat_id A seat identifier. This string must not be NULL.
 *
 * @return 0 on success or -1 on failure.
 */
int
libinput_udev_assign_seat(struct libinput *libinput,
			  const char *seat_id);

/**
 * @ingroup base
 *
 * Create a new libinput context that requires the caller to manually add or
 * remove devices with libinput_path_add_device() and
 * libinput_path_remove_device().
 *
 * The context is fully initialized but will not generate events until at
 * least one device has been added.
 *
 * The reference count of the context is initialized to 1. See @ref
 * libinput_unref.
 *
 * @param interface The callback interface
 * @param user_data Caller-specific data passed to the various callback
 * interfaces.
 *
 * @return An initialized, empty libinput context.
 */
struct libinput *
libinput_path_create_context(const struct libinput_interface *interface,
			     void *user_data);

/**
 * @ingroup base
 *
 * Add a device to a libinput context initialized with
 * libinput_path_create_context(). If successful, the device will be
 * added to the internal list and re-opened on libinput_resume(). The device
 * can be removed with libinput_path_remove_device().
 *
 * If the device was successfully initialized, it is returned in the device
 * argument. The lifetime of the returned device pointer is limited until
 * the next libinput_dispatch(), use libinput_device_ref() to keep a permanent
 * reference.
 *
 * @param libinput A previously initialized libinput context
 * @param path Path to an input device
 * @return The newly initiated device on success, or NULL on failure.
 *
 * @note It is an application bug to call this function on a libinput
 * context initialized with libinput_udev_create_context().
 */
struct libinput_device *
libinput_path_add_device(struct libinput *libinput,
			 const char *path);

/**
 * @ingroup base
 *
 * Remove a device from a libinput context initialized with
 * libinput_path_create_context() or added to such a context with
 * libinput_path_add_device().
 *
 * Events already processed from this input device are kept in the queue,
 * the @ref LIBINPUT_EVENT_DEVICE_REMOVED event marks the end of events for
 * this device.
 *
 * If no matching device exists, this function does nothing.
 *
 * @param device A libinput device
 *
 * @note It is an application bug to call this function on a libinput
 * context initialized with libinput_udev_create_context().
 */
void
libinput_path_remove_device(struct libinput_device *device);

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
 * and processes them internally. Use libinput_get_event() to retrieve the
 * events.
 *
 * Dispatching does not necessarily queue libinput events.
 *
 * @param libinput A previously initialized libinput context
 *
 * @return 0 on success, or a negative errno on failure
 */
int
libinput_dispatch(struct libinput *libinput);

/**
 * @ingroup base
 *
 * Retrieve the next event from libinput's internal event queue.
 *
 * After handling the retrieved event, the caller must destroy it using
 * libinput_event_destroy().
 *
 * @param libinput A previously initialized libinput context
 * @return The next available event, or NULL if no event is available.
 */
struct libinput_event *
libinput_get_event(struct libinput *libinput);

/**
 * @ingroup base
 *
 * Return the type of the next event in the internal queue. This function
 * does not pop the event off the queue and the next call to
 * libinput_get_event() returns that event.
 *
 * @param libinput A previously initialized libinput context
 * @return The event type of the next available event or LIBINPUT_EVENT_NONE
 * if no event is availble.
 */
enum libinput_event_type
libinput_next_event_type(struct libinput *libinput);

/**
 * @ingroup base
 *
 * @param libinput A previously initialized libinput context
 * @param user_data Caller-specific data passed to the various callback
 * interfaces.
 */
void
libinput_set_user_data(struct libinput *libinput,
		       void *user_data);

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
 * Add a reference to the context. A context is destroyed whenever the
 * reference count reaches 0. See @ref libinput_unref.
 *
 * @param libinput A previously initialized valid libinput context
 * @return The passed libinput context
 */
struct libinput *
libinput_ref(struct libinput *libinput);

/**
 * @ingroup base
 *
 * Dereference the libinput context. After this, the context may have been
 * destroyed, if the last reference was dereferenced. If so, the context is
 * invalid and may not be interacted with.
 *
 * @param libinput A previously initialized libinput context
 * @return NULL if context was destroyed otherwise the passed context
 */
struct libinput *
libinput_unref(struct libinput *libinput);

/**
 * @ingroup base
 *
 * Set the global log priority. Messages with priorities equal to or
 * higher than the argument will be printed to the current log handler.
 *
 * The default log priority is LIBINPUT_LOG_PRIORITY_ERROR.
 *
 * @param libinput A previously initialized libinput context
 * @param priority The minimum priority of log messages to print.
 *
 * @see libinput_log_set_handler
 * @see libinput_log_get_priority
 */
void
libinput_log_set_priority(struct libinput *libinput,
			  enum libinput_log_priority priority);

/**
 * @ingroup base
 *
 * Get the global log priority. Messages with priorities equal to or
 * higher than the argument will be printed to the current log handler.
 *
 * The default log priority is LIBINPUT_LOG_PRIORITY_ERROR.
 *
 * @param libinput A previously initialized libinput context
 * @return The minimum priority of log messages to print.
 *
 * @see libinput_log_set_handler
 * @see libinput_log_set_priority
 */
enum libinput_log_priority
libinput_log_get_priority(const struct libinput *libinput);

/**
 * @ingroup base
 *
 * Log handler type for custom logging.
 *
 * @param libinput The libinput context
 * @param priority The priority of the current message
 * @param format Message format in printf-style
 * @param args Message arguments
 *
 * @see libinput_log_set_priority
 * @see libinput_log_get_priority
 * @see libinput_log_set_handler
 */
typedef void (*libinput_log_handler)(struct libinput *libinput,
				     enum libinput_log_priority priority,
				     const char *format, va_list args)
	   LIBINPUT_ATTRIBUTE_PRINTF(3, 0);

/**
 * @ingroup base
 *
 * Set the global log handler. Messages with priorities equal to or higher
 * than the current log priority will be passed to the given
 * log handler.
 *
 * The default log handler prints to stderr.
 *
 * @param libinput A previously initialized libinput context
 * @param log_handler The log handler for library messages.
 *
 * @see libinput_log_set_priority
 * @see libinput_log_get_priority
 */
void
libinput_log_set_handler(struct libinput *libinput,
			 libinput_log_handler log_handler);

/**
 * @defgroup seat Initialization and manipulation of seats
 *
 * A seat has two identifiers, the physical name and the logical name. A
 * device is always assigned to exactly one seat. It may change to a
 * different logical seat but it cannot change physical seats. See @ref
 * seats for details.
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
 * @return The passed seat
 */
struct libinput_seat *
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
 * @return NULL if seat was destroyed, otherwise the passed seat
 */
struct libinput_seat *
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
 * Get the libinput context from the seat.
 *
 * @param seat A previously obtained seat
 * @return The libinput context for this seat.
 */
struct libinput *
libinput_seat_get_context(struct libinput_seat *seat);

/**
 * @ingroup seat
 *
 * Return the physical name of the seat. For libinput contexts created from
 * udev, this is always the same value as passed into
 * libinput_udev_assign_seat() and all seats from that context will have
 * the same physical name.
 *
 * The physical name of the seat is one that is usually set by the system or
 * lower levels of the stack. In most cases, this is the base filter for
 * devices - devices assigned to seats outside the current seat will not
 * be available to the caller.
 *
 * @param seat A previously obtained seat
 * @return the physical name of this seat
 */
const char *
libinput_seat_get_physical_name(struct libinput_seat *seat);

/**
 * @ingroup seat
 *
 * Return the logical name of the seat. This is an identifier to group sets
 * of devices within the compositor.
 *
 * @param seat A previously obtained seat
 * @return the logical name of this seat
 */
const char *
libinput_seat_get_logical_name(struct libinput_seat *seat);

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
 * @return The passed device
 */
struct libinput_device *
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
 * @return NULL if the device was destroyed, otherwise the passed device
 */
struct libinput_device *
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
 * Get the libinput context from the device.
 *
 * @param device A previously obtained device
 * @return The libinput context for this device.
 */
struct libinput *
libinput_device_get_context(struct libinput_device *device);

/**
 * @ingroup device
 *
 * Get the device group this device is assigned to. Some physical
 * devices like graphics tablets are represented by multiple kernel
 * devices and thus by multiple struct @ref libinput_device.
 *
 * libinput assigns these devices to the same @ref libinput_device_group
 * allowing the caller to identify such devices and adjust configuration
 * settings accordingly. For example, setting a tablet to left-handed often
 * means turning it upside down. A touch device on the same tablet would
 * need to be turned upside down too to work correctly.
 *
 * All devices are part of a device group though for most devices the group
 * will be a singleton. A device is assigned to a device group on @ref
 * LIBINPUT_EVENT_DEVICE_ADDED and removed from that group on @ref
 * LIBINPUT_EVENT_DEVICE_REMOVED. It is up to the caller to track how many
 * devices are in each device group.
 *
 * @dot
 * digraph groups_libinput {
 *   rankdir="TB";
 *   node [
 *     shape="box";
 *   ]
 *
 *   mouse [ label="mouse"; URL="\ref libinput_device"];
 *   kbd [ label="keyboard"; URL="\ref libinput_device"];
 *
 *   pen [ label="tablet pen"; URL="\ref libinput_device"];
 *   touch [ label="tablet touch"; URL="\ref libinput_device"];
 *   pad [ label="tablet pad"; URL="\ref libinput_device"];
 *
 *   group1 [ label="group 1"; URL="\ref libinput_device_group"];
 *   group2 [ label="group 2"; URL="\ref libinput_device_group"];
 *   group3 [ label="group 3"; URL="\ref libinput_device_group"];
 *
 *   mouse -> group1
 *   kbd -> group2
 *
 *   pen -> group3;
 *   touch -> group3;
 *   pad -> group3;
 * }
 * @enddot
 *
 * Device groups do not get re-used once the last device in the group was
 * removed, i.e. unplugging and re-plugging a physical device with grouped
 * devices will return a different device group after every unplug.
 *
 * The returned device group is not refcounted and may become invalid after
 * the next call to libinput. Use libinput_device_group_ref() and
 * libinput_device_group_unref() to continue using the handle outside of the
 * immediate scope.
 *
 * Device groups are assigned based on the <b>LIBINPUT_DEVICE_GROUP</b> udev
 * property, see @ref udev_config.
 *
 * @return The device group this device belongs to
 */
struct libinput_device_group *
libinput_device_get_device_group(struct libinput_device *device);

/**
 * @ingroup device
 *
 * Get the system name of the device.
 *
 * To get the descriptive device name, use libinput_device_get_name().
 *
 * @param device A previously obtained device
 * @return System name of the device
 *
 */
const char *
libinput_device_get_sysname(struct libinput_device *device);

/**
 * @ingroup device
 *
 * The descriptive device name as advertised by the kernel and/or the
 * hardware itself. To get the sysname for this device, use
 * libinput_device_get_sysname().
 *
 * The lifetime of the returned string is tied to the struct
 * libinput_device. The string may be the empty string but is never NULL.
 *
 * @param device A previously obtained device
 * @return The device name
 */
const char *
libinput_device_get_name(struct libinput_device *device);

/**
 * @ingroup device
 *
 * Get the product ID for this device.
 *
 * @param device A previously obtained device
 * @return The product ID of this device
 */
unsigned int
libinput_device_get_id_product(struct libinput_device *device);

/**
 * @ingroup device
 *
 * Get the vendor ID for this device.
 *
 * @param device A previously obtained device
 * @return The vendor ID of this device
 */
unsigned int
libinput_device_get_id_vendor(struct libinput_device *device);

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
 * A seat can be uniquely identified by the physical and logical seat name.
 * There will ever be only one seat instance with a given physical and logical
 * seat name pair at any given time, but if no external reference is kept, it
 * may be destroyed if no device belonging to it is left.
 *
 * @param device A previously obtained device
 * @return The seat this input device belongs to
 */
struct libinput_seat *
libinput_device_get_seat(struct libinput_device *device);

/**
 * @ingroup device
 *
 * Change the logical seat associated with this device by removing the
 * device and adding it to the new seat.
 *
 * This command is identical to physically unplugging the device, then
 * re-plugging it as member of the new seat,
 * @ref LIBINPUT_EVENT_DEVICE_REMOVED and @ref LIBINPUT_EVENT_DEVICE_ADDED
 * events are sent accordingly. Those events mark the end of the lifetime
 * of this device and the start of a new device.
 *
 * If the logical seat name already exists in the device's physical seat,
 * the device is added to this seat. Otherwise, a new seat is created.
 *
 * @note This change applies to this device until removal or @ref
 * libinput_suspend(), whichever happens earlier.
 *
 * @param device A previously obtained device
 * @param name The new logical seat name
 * @return 0 on success, non-zero on error
 */
int
libinput_device_set_seat_logical_name(struct libinput_device *device,
				      const char *name);

/**
 * @ingroup device
 *
 * Return a udev handle to the device that is this libinput device, if any.
 * The returned handle has a refcount of at least 1, the caller must call
 * udev_device_unref() once to release the associated resources.
 *
 * Some devices may not have a udev device, or the udev device may be
 * unobtainable. This function returns NULL if no udev device was available.
 *
 * Calling this function multiple times for the same device may not
 * return the same udev handle each time.
 *
 * @param device A previously obtained device
 * @return A udev handle to the device with a refcount of >= 1 or NULL.
 * @retval NULL This device is not represented by a udev device
 */
struct udev_device *
libinput_device_get_udev_device(struct libinput_device *device);

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
 * Check if the given device has the specified capability
 *
 * @return 1 if the given device has the capability or 0 if not
 */
int
libinput_device_has_capability(struct libinput_device *device,
			       enum libinput_device_capability capability);

/**
 * @ingroup device
 *
 * Get the physical size of a device in mm, where meaningful. This function
 * only succeeds on devices with the required data, i.e. tablets, touchpads
 * and touchscreens.
 *
 * If this function returns nonzero, width and height are unmodified.
 *
 * @param device The device
 * @param width Set to the width of the device
 * @param height Set to the height of the device
 * @return 0 on success, or nonzero otherwise
 */
int
libinput_device_get_size(struct libinput_device *device,
			 double *width,
			 double *height);

/**
 * @ingroup device
 *
 * Check if a @ref LIBINPUT_DEVICE_CAP_POINTER device has a button with the
 * passed in code (see linux/input.h).
 *
 * @param device A current input device
 * @param code button code to check for
 *
 * @return 1 if the device supports this button code, 0 if it does not, -1
 * on error.
 */
int
libinput_device_pointer_has_button(struct libinput_device *device, uint32_t code);

/**
 * @ingroup device
 *
 * @deprecated Use libinput_device_pointer_has_button() instead.
 */
int
libinput_device_has_button(struct libinput_device *device, uint32_t code) LIBINPUT_ATTRIBUTE_DEPRECATED;

/**
 * @ingroup device
 *
 * Increase the refcount of the device group. A device group will be freed
 * whenever the refcount reaches 0. This may happen during dispatch if all
 * devices of this group were removed from the system. A caller must ensure
 * to reference the device group correctly to avoid dangling pointers.
 *
 * @param group A previously obtained device group
 * @return The passed device group
 */
struct libinput_device_group *
libinput_device_group_ref(struct libinput_device_group *group);

/**
 * @ingroup device
 *
 * Decrease the refcount of the device group. A device group will be freed
 * whenever the refcount reaches 0. This may happen during dispatch if all
 * devices of this group were removed from the system. A caller must ensure
 * to reference the device group correctly to avoid dangling pointers.
 *
 * @param group A previously obtained device group
 * @return NULL if the device group was destroyed, otherwise the passed
 * device group
 */
struct libinput_device_group *
libinput_device_group_unref(struct libinput_device_group *group);

/**
 * @ingroup device
 *
 * Set caller-specific data associated with this device group. libinput does
 * not manage, look at, or modify this data. The caller must ensure the
 * data is valid.
 *
 * @param group A previously obtained device group
 * @param user_data Caller-specific data pointer
 * @see libinput_device_group_get_user_data
 */
void
libinput_device_group_set_user_data(struct libinput_device_group *group,
				    void *user_data);

/**
 * @ingroup device
 *
 * Get the caller-specific data associated with this input device group, if
 * any.
 *
 * @param group A previously obtained group
 * @return Caller-specific data pointer or NULL if none was set
 * @see libinput_device_group_set_user_data
 */
void *
libinput_device_group_get_user_data(struct libinput_device_group *group);

/**
 * @defgroup config Device configuration
 *
 * Enable, disable, change and/or check for device-specific features. For
 * all features, libinput assigns a default based on the hardware
 * configuration. This default can be obtained with the respective
 * get_default call.
 *
 * Some configuration option may be dependent on or mutually exclusive with
 * with other options. The behavior in those cases is
 * implementation-defined, the caller must ensure that the options are set
 * in the right order.
 */

/**
 * @ingroup config
 *
 * Status codes returned when applying configuration settings.
 */
enum libinput_config_status {
	LIBINPUT_CONFIG_STATUS_SUCCESS = 0,	/**< Config applied successfully */
	LIBINPUT_CONFIG_STATUS_UNSUPPORTED,	/**< Configuration not available on
						     this device */
	LIBINPUT_CONFIG_STATUS_INVALID,		/**< Invalid parameter range */
};

/**
 * @ingroup config
 *
 * Return a string describing the error.
 *
 * @param status The status to translate to a string
 * @return A human-readable string representing the error or NULL for an
 * invalid status.
 */
const char *
libinput_config_status_to_str(enum libinput_config_status status);

/**
 * @ingroup config
 */
enum libinput_config_tap_state {
	LIBINPUT_CONFIG_TAP_DISABLED, /**< Tapping is to be disabled, or is
					currently disabled */
	LIBINPUT_CONFIG_TAP_ENABLED, /**< Tapping is to be enabled, or is
				       currently enabled */
};

/**
 * @ingroup config
 *
 * Check if the device supports tap-to-click. See
 * libinput_device_config_tap_set_enabled() for more information.
 *
 * @param device The device to configure
 * @return The number of fingers that can generate a tap event, or 0 if the
 * device does not support tapping.
 *
 * @see libinput_device_config_tap_set_enabled
 * @see libinput_device_config_tap_get_enabled
 * @see libinput_device_config_tap_get_default_enabled
 */
int
libinput_device_config_tap_get_finger_count(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Enable or disable tap-to-click on this device, with a default mapping of
 * 1, 2, 3 finger tap mapping to left, right, middle click, respectively.
 * Tapping is limited by the number of simultaneous touches
 * supported by the device, see
 * libinput_device_config_tap_get_finger_count().
 *
 * @param device The device to configure
 * @param enable @ref LIBINPUT_CONFIG_TAP_ENABLED to enable tapping or @ref
 * LIBINPUT_CONFIG_TAP_DISABLED to disable tapping
 *
 * @return A config status code. Disabling tapping on a device that does not
 * support tapping always succeeds.
 *
 * @see libinput_device_config_tap_get_finger_count
 * @see libinput_device_config_tap_get_enabled
 * @see libinput_device_config_tap_get_default_enabled
 */
enum libinput_config_status
libinput_device_config_tap_set_enabled(struct libinput_device *device,
				       enum libinput_config_tap_state enable);

/**
 * @ingroup config
 *
 * Check if tap-to-click is enabled on this device. If the device does not
 * support tapping, this function always returns 0.
 *
 * @param device The device to configure
 *
 * @return @ref LIBINPUT_CONFIG_TAP_ENABLED if tapping is currently enabled,
 * or @ref LIBINPUT_CONFIG_TAP_DISABLED is currently disabled
 *
 * @see libinput_device_config_tap_get_finger_count
 * @see libinput_device_config_tap_set_enabled
 * @see libinput_device_config_tap_get_default_enabled
 */
enum libinput_config_tap_state
libinput_device_config_tap_get_enabled(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Return the default setting for whether tapping is enabled on this device.
 *
 * @param device The device to configure
 * @return @ref LIBINPUT_CONFIG_TAP_ENABLED if tapping is enabled by default,
 * or @ref LIBINPUT_CONFIG_TAP_DISABLED is disabled by default
 *
 * @see libinput_device_config_tap_get_finger_count
 * @see libinput_device_config_tap_set_enabled
 * @see libinput_device_config_tap_get_enabled
 */
enum libinput_config_tap_state
libinput_device_config_tap_get_default_enabled(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Check if the device can be calibrated via a calibration matrix.
 *
 * @param device The device to check
 * @return non-zero if the device can be calibrated, zero otherwise.
 *
 * @see libinput_device_config_calibration_set_matrix
 * @see libinput_device_config_calibration_get_matrix
 * @see libinput_device_config_calibration_get_default_matrix
 */
int
libinput_device_config_calibration_has_matrix(struct libinput_device *device);

/**
 * @ingroup config
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
 *
 * The translation component (c, f) is expected to be normalized to the
 * device coordinate range. For example, the matrix
 * @code
 * [ 1 0  1 ]
 * [ 0 1 -1 ]
 * [ 0 0  1 ]
 * @endcode
 * moves all coordinates by 1 device-width to the right and 1 device-height
 * up.
 *
 * The rotation matrix for rotation around the origin is defined as
 * @code
 * [ cos(a) -sin(a) 0 ]
 * [ sin(a)  cos(a) 0 ]
 * [   0      0     1 ]
 * @endcode
 * Note that any rotation requires an additional translation component to
 * translate the rotated coordinates back into the original device space.
 * The rotation matrixes for 90, 180 and 270 degrees clockwise are:
 * @code
 * 90 deg cw:		180 deg cw:		270 deg cw:
 * [ 0 -1 1]		[ -1  0 1]		[  0 1 0 ]
 * [ 1  0 0]		[  0 -1 1]		[ -1 0 1 ]
 * [ 0  0 1]		[  0  0 1]		[  0 0 1 ]
 * @endcode
 *
 * @param device The device to configure
 * @param matrix An array representing the first two rows of a 3x3 matrix as
 * described above.
 *
 * @return A config status code.
 *
 * @see libinput_device_config_calibration_has_matrix
 * @see libinput_device_config_calibration_get_matrix
 * @see libinput_device_config_calibration_get_default_matrix
 */
enum libinput_config_status
libinput_device_config_calibration_set_matrix(struct libinput_device *device,
					      const float matrix[6]);

/**
 * @ingroup config
 *
 * Return the current calibration matrix for this device.
 *
 * @param device The device to configure
 * @param matrix Set to the array representing the first two rows of a 3x3 matrix as
 * described in libinput_device_config_calibration_set_matrix().
 *
 * @return 0 if no calibration is set and the returned matrix is the
 * identity matrix, 1 otherwise
 *
 * @see libinput_device_config_calibration_has_matrix
 * @see libinput_device_config_calibration_set_matrix
 * @see libinput_device_config_calibration_get_default_matrix
 */
int
libinput_device_config_calibration_get_matrix(struct libinput_device *device,
					      float matrix[6]);

/**
 * @ingroup config
 *
 * Return the default calibration matrix for this device. On most devices,
 * this is the identity matrix. If the udev property
 * <b>LIBINPUT_CALIBRATION_MATRIX</b> is set on the respective udev device,
 * that property's value becomes the default matrix.
 *
 * The udev property is parsed as 6 floating point numbers separated by a
 * single space each (scanf(3) format "%f %f %f %f %f %f").
 * The 6 values represent the first two rows of the calibration matrix as
 * described in libinput_device_config_calibration_set_matrix().
 *
 * Example values are:
 * @code
 * ENV{LIBINPUT_CALIBRATION_MATRIX}="1 0 0 0 1 0" # default
 * ENV{LIBINPUT_CALIBRATION_MATRIX}="0 -1 1 1 0 0" # 90 degree clockwise
 * ENV{LIBINPUT_CALIBRATION_MATRIX}="-1 0 1 0 -1 1" # 180 degree clockwise
 * ENV{LIBINPUT_CALIBRATION_MATRIX}="0 1 0 -1 0 1" # 270 degree clockwise
 * ENV{LIBINPUT_CALIBRATION_MATRIX}="-1 0 1 1 0 0" # reflect along y axis
 * @endcode
 *
 * @param device The device to configure
 * @param matrix Set to the array representing the first two rows of a 3x3 matrix as
 * described in libinput_device_config_calibration_set_matrix().
 *
 * @return 0 if no calibration is set and the returned matrix is the
 * identity matrix, 1 otherwise
 *
 * @see libinput_device_config_calibration_has_matrix
 * @see libinput_device_config_calibration_set_matrix
 * @see libinput_device_config_calibration_get_default_matrix
 */
int
libinput_device_config_calibration_get_default_matrix(struct libinput_device *device,
						      float matrix[6]);

/**
 * The send-event mode of a device defines when a device may generate events
 * and pass those events to the caller.
 */
enum libinput_config_send_events_mode {
	/**
	 * Send events from this device normally. This is a placeholder
	 * mode only, any device detected by libinput can be enabled. Do not
	 * test for this value as bitmask.
	 */
	LIBINPUT_CONFIG_SEND_EVENTS_ENABLED = 0,
	/**
	 * Do not send events through this device. Depending on the device,
	 * this may close all file descriptors on the device or it may leave
	 * the file descriptors open and route events through a different
	 * device.
	 *
	 * If this bit field is set, other disable modes may be
	 * ignored. For example, if both @ref
	 * LIBINPUT_CONFIG_SEND_EVENTS_DISABLED and @ref
	 * LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE are set,
	 * the device remains disabled when all external pointer devices are
	 * unplugged.
	 */
	LIBINPUT_CONFIG_SEND_EVENTS_DISABLED = (1 << 0),
	/**
	 * If an external pointer device is plugged in, do not send events
	 * from this device. This option may be available on built-in
	 * touchpads.
	 */
	LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE = (1 << 1),
};

/**
 * @ingroup config
 *
 * Return the possible send-event modes for this device. These modes define
 * when a device may process and send events.
 *
 * @param device The device to configure
 *
 * @return A bitmask of possible modes.
 *
 * @see libinput_device_config_send_events_set_mode
 * @see libinput_device_config_send_events_get_mode
 * @see libinput_device_config_send_events_get_default_mode
 */
uint32_t
libinput_device_config_send_events_get_modes(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Set the send-event mode for this device. The mode defines when the device
 * processes and sends events to the caller.
 *
 * The selected mode may not take effect immediately. Events already
 * received and processed from this device are unaffected and will be passed
 * to the caller on the next call to libinput_get_event().
 *
 * If the mode is a bitmask of @ref libinput_config_send_events_mode,
 * the device may wait for or generate events until it is in a neutral
 * state. For example, this may include waiting for or generating button
 * release events.
 *
 * If the device is already suspended, this function does nothing and
 * returns success. Changing the send-event mode on a device that has been
 * removed is permitted.
 *
 * @param device The device to configure
 * @param mode A bitmask of send-events modes
 *
 * @return A config status code.
 *
 * @see libinput_device_config_send_events_get_modes
 * @see libinput_device_config_send_events_get_mode
 * @see libinput_device_config_send_events_get_default_mode
 */
enum libinput_config_status
libinput_device_config_send_events_set_mode(struct libinput_device *device,
					    uint32_t mode);

/**
 * @ingroup config
 *
 * Get the send-event mode for this device. The mode defines when the device
 * processes and sends events to the caller.
 *
 * If a caller enables the bits for multiple modes, some of which are
 * subsets of another mode libinput may drop the bits that are subsets. In
 * other words, don't expect libinput_device_config_send_events_get_mode()
 * to always return exactly the same bitmask as passed into
 * libinput_device_config_send_events_set_mode().
 *
 * @param device The device to configure
 * @return The current bitmask of the send-event mode for this device.
 *
 * @see libinput_device_config_send_events_get_modes
 * @see libinput_device_config_send_events_set_mode
 * @see libinput_device_config_send_events_get_default_mode
 */
uint32_t
libinput_device_config_send_events_get_mode(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Get the default send-event mode for this device. The mode defines when
 * the device processes and sends events to the caller.
 *
 * @param device The device to configure
 * @return The bitmask of the send-event mode for this device.
 *
 * @see libinput_device_config_send_events_get_modes
 * @see libinput_device_config_send_events_set_mode
 * @see libinput_device_config_send_events_get_mode
 */
uint32_t
libinput_device_config_send_events_get_default_mode(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Check if a device uses libinput-internal pointer-acceleration.
 *
 * @param device The device to configure
 *
 * @return 0 if the device is not accelerated, nonzero if it is accelerated
 */
int
libinput_device_config_accel_is_available(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Set the pointer acceleration speed of this pointer device within a range
 * of [-1, 1], where 0 is the default acceleration for this device, -1 is
 * the slowest acceleration and 1 is the maximum acceleration available on
 * this device. The actual pointer acceleration mechanism is
 * implementation-dependent, as is the number of steps available within the
 * range. libinput picks the semantically closest acceleration step if the
 * requested value does not match a discrete setting.
 *
 * @param device The device to configure
 * @param speed The normalized speed, in a range of [-1, 1]
 *
 * @return A config status code
 */
enum libinput_config_status
libinput_device_config_accel_set_speed(struct libinput_device *device,
				       double speed);

/**
 * @ingroup config
 *
 * Get the current pointer acceleration setting for this pointer device. The
 * returned value is normalized to a range of [-1, 1].
 * See libinput_device_config_accel_set_speed() for details.
 *
 * @param device The device to configure
 *
 * @return The current speed, range -1 to 1
 */
double
libinput_device_config_accel_get_speed(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Return the default speed setting for this device, normalized to a range
 * of [-1, 1].
 * See libinput_device_config_accel_set_speed() for details.
 *
 * @param device The device to configure
 * @return The default speed setting for this device.
 */
double
libinput_device_config_accel_get_default_speed(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Return non-zero if the device supports "natural scrolling".
 *
 * In traditional scroll mode, the movement of fingers on a touchpad when
 * scrolling matches the movement of the scroll bars. When the fingers move
 * down, the scroll bar moves down, a line of text on the screen moves
 * towards the upper end of the screen. This also matches scroll wheels on
 * mice (wheel down, content moves up).
 *
 * Natural scrolling is the term coined by Apple for inverted scrolling.
 * In this mode, the effect of scrolling movement of fingers on a touchpad
 * resemble physical manipulation of paper. When the fingers move down, a
 * line of text on the screen moves down (scrollbars move up). This is the
 * opposite of scroll wheels on mice.
 *
 * A device supporting natural scrolling can be switched between traditional
 * scroll mode and natural scroll mode.
 *
 * @param device The device to configure
 *
 * @return 0 if natural scrolling is not supported, non-zero if natural
 * scrolling is supported by this device
 *
 * @see libinput_device_config_set_natural_scroll_enabled
 * @see libinput_device_config_get_natural_scroll_enabled
 * @see libinput_device_config_get_default_natural_scroll_enabled
 */
int
libinput_device_config_scroll_has_natural_scroll(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Enable or disable natural scrolling on the device.
 *
 * @param device The device to configure
 * @param enable non-zero to enable, zero to disable natural scrolling
 *
 * @return a config status code
 *
 * @see libinput_device_config_has_natural_scroll
 * @see libinput_device_config_get_natural_scroll_enabled
 * @see libinput_device_config_get_default_natural_scroll_enabled
 */
enum libinput_config_status
libinput_device_config_scroll_set_natural_scroll_enabled(struct libinput_device *device,
							 int enable);
/**
 * @ingroup config
 *
 * Get the current mode for scrolling on this device
 *
 * @param device The device to configure
 *
 * @return zero if natural scrolling is disabled, non-zero if enabled
 *
 * @see libinput_device_config_has_natural_scroll
 * @see libinput_device_config_set_natural_scroll_enabled
 * @see libinput_device_config_get_default_natural_scroll_enabled
 */
int
libinput_device_config_scroll_get_natural_scroll_enabled(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Get the default mode for scrolling on this device
 *
 * @param device The device to configure
 *
 * @return zero if natural scrolling is disabled by default, non-zero if enabled
 *
 * @see libinput_device_config_has_natural_scroll
 * @see libinput_device_config_set_natural_scroll_enabled
 * @see libinput_device_config_get_natural_scroll_enabled
 */
int
libinput_device_config_scroll_get_default_natural_scroll_enabled(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Check if a device has a configuration that supports left-handed usage.
 *
 * @param device The device to configure
 * @return Non-zero if the device can be set to left-handed, or zero
 * otherwise
 *
 * @see libinput_device_config_left_handed_set
 * @see libinput_device_config_left_handed_get
 * @see libinput_device_config_left_handed_get_default
 */
int
libinput_device_config_left_handed_is_available(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Set the left-handed configuration of the device. For example, a pointing
 * device may reverse it's buttons and send a right button click when the
 * left button is pressed, and vice versa.
 *
 * The exact behavior is device-dependent. On a mouse and most pointing
 * devices, left and right buttons are swapped but the middle button is
 * unmodified. On a touchpad, physical buttons (if present) are swapped. On a
 * clickpad, the top and bottom software-emulated buttons are swapped where
 * present, the main area of the touchpad remains a left button. Tapping and
 * clickfinger behavior is not affected by this setting.
 *
 * Changing the left-handed configuration of a device may not take effect
 * until all buttons have been logically released.
 *
 * @param device The device to configure
 * @param left_handed Zero to disable, non-zero to enable left-handed mode
 * @return A configuration status code
 *
 * @see libinput_device_config_left_handed_is_available
 * @see libinput_device_config_left_handed_get
 * @see libinput_device_config_left_handed_get_default
 */
enum libinput_config_status
libinput_device_config_left_handed_set(struct libinput_device *device,
				       int left_handed);

/**
 * @ingroup config
 *
 * Get the current left-handed configuration of the device.
 *
 * @param device The device to configure
 * @return Zero if the device is in right-handed mode, non-zero if the
 * device is in left-handed mode
 *
 * @see libinput_device_config_left_handed_is_available
 * @see libinput_device_config_left_handed_set
 * @see libinput_device_config_left_handed_get_default
 */
int
libinput_device_config_left_handed_get(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Get the default left-handed configuration of the device.
 *
 * @param device The device to configure
 * @return Zero if the device is in right-handed mode by default, or non-zero
 * if the device is in left-handed mode by default
 *
 * @see libinput_device_config_left_handed_is_available
 * @see libinput_device_config_left_handed_set
 * @see libinput_device_config_left_handed_get
 */
int
libinput_device_config_left_handed_get_default(struct libinput_device *device);

/**
 * @ingroup config
 *
 * The click method defines when to generate software-emulated
 * buttons, usually on a device that does not have a specific physical
 * button available.
 */
enum libinput_config_click_method {
	/**
	 * Do not send software-emulated button events. This has no effect
	 * on physical button generations.
	 */
	LIBINPUT_CONFIG_CLICK_METHOD_NONE = 0,
	/**
	 * Use software-button areas (see @ref clickfinger) to generate
	 * button events.
	 */
	LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS = (1 << 0),
	/**
	 * The number of fingers decides which button press to generate.
	 */
	LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER = (1 << 1),
};

/**
 * @ingroup config
 *
 * Check which button click methods a device supports. The button click
 * method defines when to generate software-emulated buttons, usually on a
 * device that does not have a specific physical button available.
 *
 * @param device The device to configure
 *
 * @return A bitmask of possible methods.
 *
 * @see libinput_device_config_click_get_methods
 * @see libinput_device_config_click_set_method
 * @see libinput_device_config_click_get_method
 */
uint32_t
libinput_device_config_click_get_methods(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Set the button click method for this device. The button click
 * method defines when to generate software-emulated buttons, usually on a
 * device that does not have a specific physical button available.
 *
 * @note The selected click method may not take effect immediately. The
 * device may require changing to a neutral state first before activating
 * the new method.
 *
 * @param device The device to configure
 * @param method The button click method
 *
 * @return A config status code
 *
 * @see libinput_device_config_click_get_methods
 * @see libinput_device_config_click_get_method
 * @see libinput_device_config_click_get_default_method
 */
enum libinput_config_status
libinput_device_config_click_set_method(struct libinput_device *device,
					enum libinput_config_click_method method);
/**
 * @ingroup config
 *
 * Get the button click method for this device. The button click
 * method defines when to generate software-emulated buttons, usually on a
 * device that does not have a specific physical button available.
 *
 * @param device The device to configure
 *
 * @return The current button click method for this device
 *
 * @see libinput_device_config_click_get_methods
 * @see libinput_device_config_click_set_method
 * @see libinput_device_config_click_get_default_method
 */
enum libinput_config_click_method
libinput_device_config_click_get_method(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Get the default button click method for this device. The button click
 * method defines when to generate software-emulated buttons, usually on a
 * device that does not have a specific physical button available.
 *
 * @param device The device to configure
 *
 * @return The default button click method for this device
 *
 * @see libinput_device_config_click_get_methods
 * @see libinput_device_config_click_set_method
 * @see libinput_device_config_click_get_method
 */
enum libinput_config_click_method
libinput_device_config_click_get_default_method(struct libinput_device *device);

/**
 * @ingroup config
 *
 * The scroll method of a device selects when to generate scroll axis events
 * instead of pointer motion events.
 */
enum libinput_config_scroll_method {
	/**
	 * Never send scroll events instead of pointer motion events.
	 * Note scroll wheels, etc. will still send scroll events.
	 */
	LIBINPUT_CONFIG_SCROLL_NO_SCROLL = 0,
	/**
	 * Send scroll events when 2 fingers are down on the device.
	 */
	LIBINPUT_CONFIG_SCROLL_2FG = (1 << 0),
	/**
	 * Send scroll events when a finger is moved along the bottom or
	 * right edge of a device.
	 */
	LIBINPUT_CONFIG_SCROLL_EDGE = (1 << 1),
	/**
	 * Send scroll events when a button is down and the device moves
	 * along a scroll-capable axis.
	 */
	LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN = (1 << 2),
};

/**
 * @ingroup config
 *
 * Check which scroll methods a device supports. The method defines when to
 * generate scroll axis events instead of pointer motion events.
 *
 * @param device The device to configure
 *
 * @return A bitmask of possible methods.
 *
 * @see libinput_device_config_scroll_set_method
 * @see libinput_device_config_scroll_get_method
 * @see libinput_device_config_scroll_get_default_method
 * @see libinput_device_config_scroll_set_button
 * @see libinput_device_config_scroll_get_button
 * @see libinput_device_config_scroll_get_default_button
 */
uint32_t
libinput_device_config_scroll_get_methods(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Set the scroll method for this device. The method defines when to
 * generate scroll axis events instead of pointer motion events.
 *
 * @note Setting @ref LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN enables
 * the scroll method, but scrolling is only activated when the configured
 * button is held down. If no button is set, i.e.
 * libinput_device_config_scroll_get_button() returns 0, scrolling
 * cannot activate.
 *
 * @param device The device to configure
 * @param method The scroll method for this device.
 *
 * @return A config status code.
 *
 * @see libinput_device_config_scroll_get_methods
 * @see libinput_device_config_scroll_get_method
 * @see libinput_device_config_scroll_get_default_method
 * @see libinput_device_config_scroll_set_button
 * @see libinput_device_config_scroll_get_button
 * @see libinput_device_config_scroll_get_default_button
 */
enum libinput_config_status
libinput_device_config_scroll_set_method(struct libinput_device *device,
					 enum libinput_config_scroll_method method);

/**
 * @ingroup config
 *
 * Get the scroll method for this device. The method defines when to
 * generate scroll axis events instead of pointer motion events.
 *
 * @param device The device to configure
 * @return The current scroll method for this device.
 *
 * @see libinput_device_config_scroll_get_methods
 * @see libinput_device_config_scroll_set_method
 * @see libinput_device_config_scroll_get_default_method
 * @see libinput_device_config_scroll_set_button
 * @see libinput_device_config_scroll_get_button
 * @see libinput_device_config_scroll_get_default_button
 */
enum libinput_config_scroll_method
libinput_device_config_scroll_get_method(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Get the default scroll method for this device. The method defines when to
 * generate scroll axis events instead of pointer motion events.
 *
 * @param device The device to configure
 * @return The default scroll method for this device.
 *
 * @see libinput_device_config_scroll_get_methods
 * @see libinput_device_config_scroll_set_method
 * @see libinput_device_config_scroll_get_method
 * @see libinput_device_config_scroll_set_button
 * @see libinput_device_config_scroll_get_button
 * @see libinput_device_config_scroll_get_default_button
 */
enum libinput_config_scroll_method
libinput_device_config_scroll_get_default_method(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Set the button for the @ref LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN method
 * for this device.
 *
 * When the current scroll method is set to @ref
 * LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN, no button press/release events
 * will be send for the configured button.
 *
 * When the configured button is pressed, any motion events along a
 * scroll-capable axis are turned into scroll axis events.
 *
 * @note Setting the button does not change the scroll method. To change the
 * scroll method call libinput_device_config_scroll_set_method().
 *
 * If the button is 0, button scrolling is effectively disabled.
 *
 * @param device The device to configure
 * @param button The button which when pressed switches to sending scroll events
 *
 * @return a config status code
 * @retval LIBINPUT_CONFIG_STATUS_SUCCESS on success
 * @retval LIBINPUT_CONFIG_STATUS_UNSUPPORTED if @ref LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN is not supported
 * @retval LIBINPUT_CONFIG_STATUS_INVALID the given button does not
 * exist on this device
 *
 * @see libinput_device_config_scroll_get_methods
 * @see libinput_device_config_scroll_set_method
 * @see libinput_device_config_scroll_get_method
 * @see libinput_device_config_scroll_get_default_method
 * @see libinput_device_config_scroll_get_button
 * @see libinput_device_config_scroll_get_default_button
 */
enum libinput_config_status
libinput_device_config_scroll_set_button(struct libinput_device *device,
					 uint32_t button);

/**
 * @ingroup config
 *
 * Get the button for the @ref LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN method for
 * this device.
 *
 * If @ref LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN scroll method is not supported,
 * or no button is set, this function returns 0.
 *
 * @note The return value is independent of the currently selected
 * scroll-method. For button scrolling to activate, a device must have the
 * @ref LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN method enabled, and a non-zero
 * button set as scroll button.
 *
 * @param device The device to configure
 * @return The button which when pressed switches to sending scroll events
 *
 * @see libinput_device_config_scroll_get_methods
 * @see libinput_device_config_scroll_set_method
 * @see libinput_device_config_scroll_get_method
 * @see libinput_device_config_scroll_get_default_method
 * @see libinput_device_config_scroll_set_button
 * @see libinput_device_config_scroll_get_default_button
 */
uint32_t
libinput_device_config_scroll_get_button(struct libinput_device *device);

/**
 * @ingroup config
 *
 * Get the default button for LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN method
 * for this device.
 *
 * If @ref LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN scroll method is not supported,
 * or no default button is set, this function returns 0.
 *
 * @param device The device to configure
 * @return The default button for LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN method
 *
 * @see libinput_device_config_scroll_get_methods
 * @see libinput_device_config_scroll_set_method
 * @see libinput_device_config_scroll_get_method
 * @see libinput_device_config_scroll_get_default_method
 * @see libinput_device_config_scroll_set_button
 * @see libinput_device_config_scroll_get_button
 */
uint32_t
libinput_device_config_scroll_get_default_button(struct libinput_device *device);

#ifdef __cplusplus
}
#endif
#endif /* LIBINPUT_H */
