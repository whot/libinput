/*
 * Copyright © 2012 Jonas Ådahl
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/timerfd.h>

#include "evdev.h"
#include "filter.h"
#include "libinput-private.h"

/* Default values */
#define DEFAULT_CONSTANT_ACCEL_NUMERATOR 50
#define DEFAULT_MIN_ACCEL_FACTOR 0.16
#define DEFAULT_MAX_ACCEL_FACTOR 1.0
#define DEFAULT_HYSTERESIS_MARGIN_DENOMINATOR 700.0

#define DEFAULT_TOUCHPAD_SINGLE_TAP_BUTTON BTN_LEFT
#define DEFAULT_TOUCHPAD_SINGLE_TAP_TIMEOUT 100

enum touchpad_model {
	TOUCHPAD_MODEL_UNKNOWN = 0,
	TOUCHPAD_MODEL_SYNAPTICS,
	TOUCHPAD_MODEL_ALPS,
	TOUCHPAD_MODEL_APPLETOUCH,
	TOUCHPAD_MODEL_ELANTECH
};

enum touchpad_event {
	TOUCHPAD_EVENT_NONE	    = 0,
	TOUCHPAD_EVENT_ABSOLUTE_ANY = (1 << 0),
	TOUCHPAD_EVENT_ABSOLUTE_X   = (1 << 1),
	TOUCHPAD_EVENT_ABSOLUTE_Y   = (1 << 2),
	TOUCHPAD_EVENT_REPORT	    = (1 << 3)
};

struct touchpad_model_spec {
	short vendor;
	short product;
	enum touchpad_model model;
};

static struct touchpad_model_spec touchpad_spec_table[] = {
	{0x0002, 0x0007, TOUCHPAD_MODEL_SYNAPTICS},
	{0x0002, 0x0008, TOUCHPAD_MODEL_ALPS},
	{0x05ac, 0x0000, TOUCHPAD_MODEL_APPLETOUCH},
	{0x0002, 0x000e, TOUCHPAD_MODEL_ELANTECH},
	{0x0000, 0x0000, TOUCHPAD_MODEL_UNKNOWN}
};

enum touchpad_state {
	TOUCHPAD_STATE_NONE  = 0,
	TOUCHPAD_STATE_TOUCH = (1 << 0),
	TOUCHPAD_STATE_MOVE  = (1 << 1)
};

#define TOUCHPAD_HISTORY_LENGTH 4
#define TOUCHPAD_MIN_SAMPLES 4	/* Number of samples before we start sending events */

struct touchpad_motion {
	int32_t x;
	int32_t y;
};

enum touchpad_fingers_state {
	TOUCHPAD_FINGERS_ONE   = (1 << 0),
	TOUCHPAD_FINGERS_TWO   = (1 << 1),
	TOUCHPAD_FINGERS_THREE = (1 << 2)
};

enum fsm_event {
	FSM_EVENT_TOUCH,
	FSM_EVENT_RELEASE,
	FSM_EVENT_MOTION,
	FSM_EVENT_TIMEOUT
};

enum fsm_state {
	FSM_IDLE,
	FSM_TOUCH,
	FSM_TAP,
	FSM_TAP_2,
	FSM_DRAG
};

struct touchpad_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;

	enum touchpad_model model;
	unsigned int state;
	int finger_state;
	int last_finger_state;

	double constant_accel_factor;
	double min_accel_factor;
	double max_accel_factor;

	unsigned int event_mask;
	unsigned int event_mask_filter;

	int reset;

	struct {
		bool enable;


		enum fsm_event *events;
		size_t events_len;
		size_t events_count;
		enum fsm_state state;
		struct {
			int fd;
			struct libinput_source *source;
		} timer;
	} fsm;

	struct {
		int32_t x;
		int32_t y;
	} hw_abs;

	int has_pressure;
	struct {
		int32_t touch_low;
		int32_t touch_high;
	} pressure;

	struct {
		int32_t margin_x;
		int32_t margin_y;
		int32_t center_x;
		int32_t center_y;
	} hysteresis;

	struct touchpad_motion motion_history[TOUCHPAD_HISTORY_LENGTH];
	int motion_index;
	unsigned int motion_count;

	struct motion_filter *filter;
};

static enum touchpad_model
get_touchpad_model(struct evdev_device *device)
{
	int vendor, product;
	unsigned int i;

	vendor = libevdev_get_id_vendor(device->evdev);
	product = libevdev_get_id_product(device->evdev);

	for (i = 0; i < ARRAY_LENGTH(touchpad_spec_table); i++)
		if (touchpad_spec_table[i].vendor == vendor &&
		    (!touchpad_spec_table[i].product ||
		     touchpad_spec_table[i].product == product))
			return touchpad_spec_table[i].model;

	return TOUCHPAD_MODEL_UNKNOWN;
}

static void
configure_touchpad_pressure(struct touchpad_dispatch *touchpad,
			    int32_t pressure_min, int32_t pressure_max)
{
	int32_t range = pressure_max - pressure_min + 1;

	touchpad->has_pressure = 1;

	/* Magic numbers from xf86-input-synaptics */
	switch (touchpad->model) {
	case TOUCHPAD_MODEL_ELANTECH:
		touchpad->pressure.touch_low = pressure_min + 1;
		touchpad->pressure.touch_high = pressure_min + 1;
		break;
	default:
		touchpad->pressure.touch_low =
			pressure_min + range * (25.0/256.0);
		touchpad->pressure.touch_high =
			pressure_min + range * (30.0/256.0);
	}
}

static double
touchpad_profile(struct motion_filter *filter,
		 void *data,
		 double velocity,
		 uint64_t time)
{
	struct touchpad_dispatch *touchpad =
		(struct touchpad_dispatch *) data;

	double accel_factor;

	accel_factor = velocity * touchpad->constant_accel_factor;

	if (accel_factor > touchpad->max_accel_factor)
		accel_factor = touchpad->max_accel_factor;
	else if (accel_factor < touchpad->min_accel_factor)
		accel_factor = touchpad->min_accel_factor;

	return accel_factor;
}

static inline struct touchpad_motion *
motion_history_offset(struct touchpad_dispatch *touchpad, int offset)
{
	int offset_index =
		(touchpad->motion_index - offset + TOUCHPAD_HISTORY_LENGTH) %
		TOUCHPAD_HISTORY_LENGTH;

	return &touchpad->motion_history[offset_index];
}

static double
estimate_delta(int x0, int x1, int x2, int x3)
{
	return (x0 + x1 - x2 - x3) / 4;
}

static int
hysteresis(int in, int center, int margin)
{
	int diff = in - center;
	if (abs(diff) <= margin)
		return center;

	if (diff > margin)
		return center + diff - margin;
	else if (diff < -margin)
		return center + diff + margin;
	return center + diff;
}

static void
touchpad_get_delta(struct touchpad_dispatch *touchpad, double *dx, double *dy)
{
	*dx = estimate_delta(motion_history_offset(touchpad, 0)->x,
			     motion_history_offset(touchpad, 1)->x,
			     motion_history_offset(touchpad, 2)->x,
			     motion_history_offset(touchpad, 3)->x);
	*dy = estimate_delta(motion_history_offset(touchpad, 0)->y,
			     motion_history_offset(touchpad, 1)->y,
			     motion_history_offset(touchpad, 2)->y,
			     motion_history_offset(touchpad, 3)->y);
}

static void
filter_motion(struct touchpad_dispatch *touchpad,
	      double *dx, double *dy, uint64_t time)
{
	struct motion_params motion;

	motion.dx = *dx;
	motion.dy = *dy;

	filter_dispatch(touchpad->filter, &motion, touchpad, time);

	*dx = motion.dx;
	*dy = motion.dy;
}

static void
notify_button_pressed(struct touchpad_dispatch *touchpad, uint64_t time)
{
	pointer_notify_button(
		&touchpad->device->base,
		time,
		DEFAULT_TOUCHPAD_SINGLE_TAP_BUTTON,
		LIBINPUT_POINTER_BUTTON_STATE_PRESSED);
}

static void
notify_button_released(struct touchpad_dispatch *touchpad, uint64_t time)
{
	pointer_notify_button(
		&touchpad->device->base,
		time,
		DEFAULT_TOUCHPAD_SINGLE_TAP_BUTTON,
		LIBINPUT_POINTER_BUTTON_STATE_RELEASED);
}

static void
notify_tap(struct touchpad_dispatch *touchpad, uint64_t time)
{
	notify_button_pressed(touchpad, time);
	notify_button_released(touchpad, time);
}

static void
process_fsm_events(struct touchpad_dispatch *touchpad, uint64_t time)
{
	uint64_t timeout = UINT64_MAX;
	enum fsm_event event;
	unsigned int i;

	if (!touchpad->fsm.enable)
		return;

	if (touchpad->fsm.events_count == 0)
		return;

	for (i = 0; i < touchpad->fsm.events_count; ++i) {
		event = touchpad->fsm.events[i];
		timeout = 0;

		switch (touchpad->fsm.state) {
		case FSM_IDLE:
			switch (event) {
			case FSM_EVENT_TOUCH:
				touchpad->fsm.state = FSM_TOUCH;
				break;
			default:
				break;
			}
			break;
		case FSM_TOUCH:
			switch (event) {
			case FSM_EVENT_RELEASE:
				timeout = DEFAULT_TOUCHPAD_SINGLE_TAP_TIMEOUT;
				touchpad->fsm.state = FSM_TAP;
				break;
			default:
				touchpad->fsm.state = FSM_IDLE;
				break;
			}
			break;
		case FSM_TAP:
			switch (event) {
			case FSM_EVENT_TIMEOUT:
				notify_tap(touchpad, time);
				touchpad->fsm.state = FSM_IDLE;
				break;
			case FSM_EVENT_TOUCH:
				notify_button_pressed(touchpad, time);
				touchpad->fsm.state = FSM_TAP_2;
				break;
			default:
				touchpad->fsm.state = FSM_IDLE;
				break;
			}
			break;
		case FSM_TAP_2:
			switch (event) {
			case FSM_EVENT_MOTION:
				touchpad->fsm.state = FSM_DRAG;
				break;
			case FSM_EVENT_RELEASE:
				notify_button_released(touchpad, time);
				notify_tap(touchpad, time);
				touchpad->fsm.state = FSM_IDLE;
				break;
			default:
				touchpad->fsm.state = FSM_IDLE;
				break;
			}
			break;
		case FSM_DRAG:
			switch (event) {
			case FSM_EVENT_RELEASE:
				notify_button_released(touchpad, time);
				touchpad->fsm.state = FSM_IDLE;
				break;
			default:
				touchpad->fsm.state = FSM_IDLE;
				break;
			}
			break;
		default:
			touchpad->fsm.state = FSM_IDLE;
			break;
		}
	}

	if (timeout != UINT64_MAX) {
		struct itimerspec its;

		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = timeout / 1000;
		its.it_value.tv_nsec = (timeout % 1000) * 1000 * 1000;
		timerfd_settime(touchpad->fsm.timer.fd, 0, &its, NULL);
	}

	touchpad->fsm.events_count = 0;
}

static void
push_fsm_event(struct touchpad_dispatch *touchpad,
	       enum fsm_event event)
{
	enum fsm_event *events;
	size_t new_len = touchpad->fsm.events_len;

	if (!touchpad->fsm.enable)
		return;

	if (touchpad->fsm.events_count + 1 >= touchpad->fsm.events_len) {
		if (new_len == 0)
			new_len = 4;
		else
			new_len *= 2;
		events = realloc(touchpad->fsm.events,
				 sizeof(enum fsm_event) * new_len);
		if (!events) {
			touchpad->fsm.state = FSM_IDLE;
			return;
		}

		touchpad->fsm.events = events;
		touchpad->fsm.events_len = new_len;
	}

	touchpad->fsm.events[touchpad->fsm.events_count++] = event;
}

static void
fsm_timeout_handler(void *data)
{
	struct touchpad_dispatch *touchpad = data;
	uint64_t expires;
	int len;
	struct timespec ts;
	uint64_t now;

	len = read(touchpad->fsm.timer.fd, &expires, sizeof expires);
	if (len != sizeof expires)
		/* This will only happen if the application made the fd
		 * non-blocking, but this function should only be called
		 * upon the timeout, so lets continue anyway. */
		log_error("timerfd read error: %m\n");

	if (touchpad->fsm.events_count == 0) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		now = ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000;

		push_fsm_event(touchpad, FSM_EVENT_TIMEOUT);
		process_fsm_events(touchpad, now);
	}
}

static void
touchpad_update_state(struct touchpad_dispatch *touchpad, uint64_t time)
{
	int motion_index;
	int center_x, center_y;
	double dx = 0.0, dy = 0.0;
	struct libinput_device *base = &touchpad->device->base;

	if (touchpad->reset ||
	    touchpad->last_finger_state != touchpad->finger_state) {
		touchpad->reset = 0;
		touchpad->motion_count = 0;
		touchpad->event_mask = TOUCHPAD_EVENT_NONE;
		touchpad->event_mask_filter =
			TOUCHPAD_EVENT_ABSOLUTE_X | TOUCHPAD_EVENT_ABSOLUTE_Y;

		touchpad->last_finger_state = touchpad->finger_state;

		process_fsm_events(touchpad, time);

		return;
	}
	touchpad->last_finger_state = touchpad->finger_state;

	if (!(touchpad->event_mask & TOUCHPAD_EVENT_REPORT))
		return;
	else
		touchpad->event_mask &= ~TOUCHPAD_EVENT_REPORT;

	if ((touchpad->event_mask & touchpad->event_mask_filter) !=
	    touchpad->event_mask_filter)
		return;

	touchpad->event_mask_filter = TOUCHPAD_EVENT_ABSOLUTE_ANY;
	touchpad->event_mask = 0;

	/* Avoid noise by moving center only when delta reaches a threshold
	 * distance from the old center. */
	if (touchpad->motion_count > 0) {
		center_x = hysteresis(touchpad->hw_abs.x,
				      touchpad->hysteresis.center_x,
				      touchpad->hysteresis.margin_x);
		center_y = hysteresis(touchpad->hw_abs.y,
				      touchpad->hysteresis.center_y,
				      touchpad->hysteresis.margin_y);
	} else {
		center_x = touchpad->hw_abs.x;
		center_y = touchpad->hw_abs.y;
	}
	touchpad->hysteresis.center_x = center_x;
	touchpad->hysteresis.center_y = center_y;
	touchpad->hw_abs.x = center_x;
	touchpad->hw_abs.y = center_y;

	/* Update motion history tracker */
	motion_index = (touchpad->motion_index + 1) % TOUCHPAD_HISTORY_LENGTH;
	touchpad->motion_index = motion_index;
	touchpad->motion_history[motion_index].x = touchpad->hw_abs.x;
	touchpad->motion_history[motion_index].y = touchpad->hw_abs.y;
	if (touchpad->motion_count < TOUCHPAD_HISTORY_LENGTH)
		touchpad->motion_count++;

	if (touchpad->motion_count >= TOUCHPAD_MIN_SAMPLES) {
		touchpad_get_delta(touchpad, &dx, &dy);

		filter_motion(touchpad, &dx, &dy, time);

		if (touchpad->finger_state == TOUCHPAD_FINGERS_ONE) {
		    if (dx != 0 || dy != 0)
				pointer_notify_motion(
					base,
					time,
					li_fixed_from_double(dx),
					li_fixed_from_double(dy));
		} else if (touchpad->finger_state == TOUCHPAD_FINGERS_TWO) {
			if (dx != 0.0)
				pointer_notify_axis(
					base,
					time,
					LIBINPUT_POINTER_AXIS_HORIZONTAL_SCROLL,
					li_fixed_from_double(dx));
			if (dy != 0.0)
				pointer_notify_axis(
					base,
					time,
					LIBINPUT_POINTER_AXIS_VERTICAL_SCROLL,
					li_fixed_from_double(dy));
		}
	}

	if (!(touchpad->state & TOUCHPAD_STATE_MOVE) &&
	    ((int)dx || (int)dy)) {
		touchpad->state |= TOUCHPAD_STATE_MOVE;
		push_fsm_event(touchpad, FSM_EVENT_MOTION);
	}

	process_fsm_events(touchpad, time);
}

static void
on_touch(struct touchpad_dispatch *touchpad)
{
	touchpad->state |= TOUCHPAD_STATE_TOUCH;

	push_fsm_event(touchpad, FSM_EVENT_TOUCH);
}

static void
on_release(struct touchpad_dispatch *touchpad)
{

	touchpad->reset = 1;
	touchpad->state &= ~(TOUCHPAD_STATE_MOVE | TOUCHPAD_STATE_TOUCH);

	push_fsm_event(touchpad, FSM_EVENT_RELEASE);
}

static inline void
process_absolute(struct touchpad_dispatch *touchpad,
		 struct evdev_device *device,
		 struct input_event *e)
{
	switch (e->code) {
	case ABS_PRESSURE:
		if (e->value > touchpad->pressure.touch_high &&
		    !(touchpad->state & TOUCHPAD_STATE_TOUCH))
			on_touch(touchpad);
		else if (e->value < touchpad->pressure.touch_low &&
			 touchpad->state & TOUCHPAD_STATE_TOUCH)
			on_release(touchpad);

		break;
	case ABS_X:
		if (touchpad->state & TOUCHPAD_STATE_TOUCH) {
			touchpad->hw_abs.x = e->value;
			touchpad->event_mask |= TOUCHPAD_EVENT_ABSOLUTE_ANY;
			touchpad->event_mask |= TOUCHPAD_EVENT_ABSOLUTE_X;
		}
		break;
	case ABS_Y:
		if (touchpad->state & TOUCHPAD_STATE_TOUCH) {
			touchpad->hw_abs.y = e->value;
			touchpad->event_mask |= TOUCHPAD_EVENT_ABSOLUTE_ANY;
			touchpad->event_mask |= TOUCHPAD_EVENT_ABSOLUTE_Y;
		}
		break;
	}
}

static inline void
process_key(struct touchpad_dispatch *touchpad,
	    struct evdev_device *device,
	    struct input_event *e,
	    uint64_t time)
{
	uint32_t code;

	switch (e->code) {
	case BTN_TOUCH:
		if (!touchpad->has_pressure) {
			if (e->value && !(touchpad->state & TOUCHPAD_STATE_TOUCH))
				on_touch(touchpad);
			else if (!e->value)
				on_release(touchpad);
		}
		break;
	case BTN_LEFT:
	case BTN_RIGHT:
	case BTN_MIDDLE:
	case BTN_SIDE:
	case BTN_EXTRA:
	case BTN_FORWARD:
	case BTN_BACK:
	case BTN_TASK:
		if (!touchpad->fsm.enable && e->code == BTN_LEFT &&
		    touchpad->finger_state == TOUCHPAD_FINGERS_TWO)
			code = BTN_RIGHT;
		else
			code = e->code;
		pointer_notify_button(
			&touchpad->device->base,
			time,
			code,
			e->value ? LIBINPUT_POINTER_BUTTON_STATE_PRESSED :
				   LIBINPUT_POINTER_BUTTON_STATE_RELEASED);
		break;
	case BTN_TOOL_PEN:
	case BTN_TOOL_RUBBER:
	case BTN_TOOL_BRUSH:
	case BTN_TOOL_PENCIL:
	case BTN_TOOL_AIRBRUSH:
	case BTN_TOOL_MOUSE:
	case BTN_TOOL_LENS:
		touchpad->reset = 1;
		break;
	case BTN_TOOL_FINGER:
		if (e->value)
			touchpad->finger_state |= TOUCHPAD_FINGERS_ONE;
		else
			touchpad->finger_state &= ~TOUCHPAD_FINGERS_ONE;
		break;
	case BTN_TOOL_DOUBLETAP:
		if (e->value)
			touchpad->finger_state |= TOUCHPAD_FINGERS_TWO;
		else
			touchpad->finger_state &= ~TOUCHPAD_FINGERS_TWO;
		break;
	case BTN_TOOL_TRIPLETAP:
		if (e->value)
			touchpad->finger_state |= TOUCHPAD_FINGERS_THREE;
		else
			touchpad->finger_state &= ~TOUCHPAD_FINGERS_THREE;
		break;
	}
}

static void
touchpad_process(struct evdev_dispatch *dispatch,
		 struct evdev_device *device,
		 struct input_event *e,
		 uint64_t time)
{
	struct touchpad_dispatch *touchpad =
		(struct touchpad_dispatch *) dispatch;

	switch (e->type) {
	case EV_SYN:
		if (e->code == SYN_REPORT)
			touchpad->event_mask |= TOUCHPAD_EVENT_REPORT;
		break;
	case EV_ABS:
		process_absolute(touchpad, device, e);
		break;
	case EV_KEY:
		process_key(touchpad, device, e, time);
		break;
	}

	touchpad_update_state(touchpad, time);
}

static void
touchpad_destroy(struct evdev_dispatch *dispatch)
{
	struct touchpad_dispatch *touchpad =
		(struct touchpad_dispatch *) dispatch;
	struct libinput *libinput = touchpad->device->base.seat->libinput;

	touchpad->filter->interface->destroy(touchpad->filter);
	close(touchpad->fsm.timer.fd);
	libinput_remove_source(libinput, touchpad->fsm.timer.source);
	free(touchpad->fsm.events);
	free(dispatch);
}

struct evdev_dispatch_interface touchpad_interface = {
	touchpad_process,
	touchpad_destroy
};

static int
touchpad_init(struct touchpad_dispatch *touchpad,
	      struct evdev_device *device)
{
	struct motion_filter *accel;

	const struct input_absinfo *absinfo;

	bool has_buttonpad;

	double width;
	double height;
	double diagonal;

	touchpad->base.interface = &touchpad_interface;
	touchpad->device = device;

	/* Detect model */
	touchpad->model = get_touchpad_model(device);

	has_buttonpad = libevdev_has_property(device->evdev, INPUT_PROP_BUTTONPAD);

	/* Configure pressure */
	if ((absinfo = libevdev_get_abs_info(device->evdev, ABS_PRESSURE))) {
		configure_touchpad_pressure(touchpad,
					    absinfo->minimum,
					    absinfo->maximum);
	}

	/* Configure acceleration factor */
	width = abs(device->abs.max_x - device->abs.min_x);
	height = abs(device->abs.max_y - device->abs.min_y);
	diagonal = sqrt(width*width + height*height);

	/* Set default parameters */
	touchpad->constant_accel_factor =
		DEFAULT_CONSTANT_ACCEL_NUMERATOR / diagonal;
	touchpad->min_accel_factor = DEFAULT_MIN_ACCEL_FACTOR;
	touchpad->max_accel_factor = DEFAULT_MAX_ACCEL_FACTOR;

	touchpad->hysteresis.margin_x =
		diagonal / DEFAULT_HYSTERESIS_MARGIN_DENOMINATOR;
	touchpad->hysteresis.margin_y =
		diagonal / DEFAULT_HYSTERESIS_MARGIN_DENOMINATOR;
	touchpad->hysteresis.center_x = 0;
	touchpad->hysteresis.center_y = 0;

	/* Configure acceleration profile */
	accel = create_pointer_accelator_filter(touchpad_profile);
	if (accel == NULL)
		return -1;
	touchpad->filter = accel;

	/* Setup initial state */
	touchpad->reset = 1;

	memset(touchpad->motion_history, 0, sizeof touchpad->motion_history);
	touchpad->motion_index = 0;
	touchpad->motion_count = 0;

	touchpad->state = TOUCHPAD_STATE_NONE;
	touchpad->last_finger_state = 0;
	touchpad->finger_state = 0;

	touchpad->fsm.events = NULL;
	touchpad->fsm.events_count = 0;
	touchpad->fsm.events_len = 0;
	touchpad->fsm.state = FSM_IDLE;

	touchpad->fsm.timer.fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	touchpad->fsm.timer.source =
		libinput_add_fd(touchpad->device->base.seat->libinput,
				touchpad->fsm.timer.fd,
				fsm_timeout_handler,
				touchpad);

	if (touchpad->fsm.timer.source == NULL) {
		close(touchpad->fsm.timer.fd);
		accel->interface->destroy(accel);
		return -1;
	}

	/* Configure */
	touchpad->fsm.enable = !has_buttonpad;

	return 0;
}

struct evdev_dispatch *
evdev_touchpad_create(struct evdev_device *device)
{
	struct touchpad_dispatch *touchpad;

	touchpad = malloc(sizeof *touchpad);
	if (touchpad == NULL)
		return NULL;

	if (touchpad_init(touchpad, device) != 0) {
		free(touchpad);
		return NULL;
	}

	return &touchpad->base;
}
