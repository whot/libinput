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
#include "evdev.h"

/* FIXME: update kernel header */
#ifndef MT_TOOL_DIAL
#define MT_TOOL_DIAL 0xa
#endif

enum totem_slot_state {
	SLOT_STATE_NONE,
	SLOT_STATE_BEGIN,
	SLOT_STATE_UPDATE,
	SLOT_STATE_END,
};

struct totem_slot {
	bool dirty;
	unsigned int index;
	enum totem_slot_state state;
	struct libinput_tablet_tool *tool;
	struct tablet_axes axes;
	unsigned char changed_axes[NCHARS(LIBINPUT_TABLET_TOOL_AXIS_MAX + 1)];

	struct device_coords last_point;
};

struct totem_dispatch {
	struct evdev_dispatch base;
	struct evdev_device *device;

	int slot; /* current slot */
	struct totem_slot *slots;
	size_t nslots;

	struct evdev_device *touch_device;
};

static inline struct totem_dispatch*
totem_dispatch(struct evdev_dispatch *totem)
{
	evdev_verify_dispatch_type(totem, DISPATCH_TOTEM);

	return container_of(totem, struct totem_dispatch, base);
}

static inline struct libinput *
totem_libinput_context(const struct totem_dispatch *totem)
{
	return evdev_libinput_context(totem->device);
}

static struct libinput_tablet_tool *
totem_new_tool(struct totem_dispatch *totem)
{
	struct libinput *libinput = totem_libinput_context(totem);
	struct libinput_tablet_tool *tool;

	tool = zalloc(sizeof *tool);

	*tool = (struct libinput_tablet_tool) {
		.type = LIBINPUT_TABLET_TOOL_TYPE_TOTEM,
		.serial = 0,
		.tool_id = 0,
		.refcount = 1,
	};

	tool->pressure_offset = 0;
	tool->has_pressure_offset = false;
	tool->pressure_threshold.lower = 0;
	tool->pressure_threshold.upper = 1;

	set_bit(tool->axis_caps, LIBINPUT_TABLET_TOOL_AXIS_X);
	set_bit(tool->axis_caps, LIBINPUT_TABLET_TOOL_AXIS_Y);
	set_bit(tool->axis_caps, LIBINPUT_TABLET_TOOL_AXIS_ROTATION_Z);
	set_bit(tool->axis_caps, LIBINPUT_TABLET_TOOL_AXIS_SIZE_MAJOR);
	set_bit(tool->axis_caps, LIBINPUT_TABLET_TOOL_AXIS_SIZE_MINOR);

	list_insert(&libinput->tool_list, &tool->link);

	return tool;
}

static inline void
totem_set_touch_device_enabled(struct evdev_device *touch_device,
				bool enable,
				uint64_t time)
{
	struct evdev_dispatch *dispatch;

	if (touch_device == NULL)
		return;

	dispatch = touch_device->dispatch;
	if (dispatch->interface->toggle_touch)
		dispatch->interface->toggle_touch(dispatch,
						  touch_device,
						  enable,
						  time);
}

static void
totem_process_key(struct totem_dispatch *totem,
		  struct evdev_device *device,
		  struct input_event *e,
		  uint64_t time)
{
	switch(e->code) {
	case BTN_0:
		/* FIXME: How does this work for multiple totems?? */
		break;
	default:
		evdev_log_info(device,
			       "Unhandled KEY event code %#x\n",
			       e->code);
		break;
	}
}

static void
totem_process_abs(struct totem_dispatch *totem,
		  struct evdev_device *device,
		  struct input_event *e,
		  uint64_t time)
{
	struct totem_slot *slot = &totem->slots[totem->slot];

	switch(e->code) {
	case ABS_MT_SLOT:
		if ((size_t)e->value >= totem->nslots) {
			evdev_log_bug_libinput(device,
					       "exceeded slot count (%d of max %zd)\n",
					       e->value,
					       totem->nslots);
			e->value = totem->nslots - 1;
		}
		totem->slot = e->value;
		return;
	case ABS_MT_TRACKING_ID:
		if (e->value >= 0)
			slot->state = SLOT_STATE_BEGIN;
		/* Ignore the totem if it's down on init */
		else if (slot->state != SLOT_STATE_NONE)
			slot->state = SLOT_STATE_END;
		break;
	case ABS_MT_POSITION_X:
		set_bit(slot->changed_axes, LIBINPUT_TABLET_TOOL_AXIS_X);
		break;
	case ABS_MT_POSITION_Y:
		set_bit(slot->changed_axes, LIBINPUT_TABLET_TOOL_AXIS_Y);
		break;
	case ABS_MT_TOUCH_MAJOR:
		set_bit(slot->changed_axes,
			LIBINPUT_TABLET_TOOL_AXIS_SIZE_MAJOR);
		break;
	case ABS_MT_TOUCH_MINOR:
		set_bit(slot->changed_axes,
			LIBINPUT_TABLET_TOOL_AXIS_SIZE_MINOR);
		break;
	case ABS_MT_ORIENTATION:
		set_bit(slot->changed_axes,
			LIBINPUT_TABLET_TOOL_AXIS_ROTATION_Z);
		break;
	case ABS_MT_TOOL_TYPE:
		if (e->value != MT_TOOL_DIAL) {
			evdev_log_info(device,
				       "Unexpected tool type %#x, pretending it's a dial.\n",
				       e->code);
		}
		break;
	default:
		evdev_log_info(device,
			       "Unhandled ABS event code %#x\n",
			       e->code);
		break;
	}
}

static bool
totem_slot_fetch_axes(struct totem_dispatch *totem,
		      struct totem_slot *slot,
		      struct libinput_tablet_tool *tool,
		      struct tablet_axes *axes_out,
		      uint64_t time)
{
	struct evdev_device *device = totem->device;
	const char tmp[sizeof(slot->changed_axes)] = {0};
	struct tablet_axes axes = {0};
	bool rc = false;

	if (memcmp(tmp, slot->changed_axes, sizeof(tmp)) == 0) {
		axes = slot->axes;
		goto out;
	}

	if (bit_is_set(slot->changed_axes, LIBINPUT_TABLET_TOOL_AXIS_X) ||
	    bit_is_set(slot->changed_axes, LIBINPUT_TABLET_TOOL_AXIS_Y)) {
		slot->axes.point.x = libevdev_get_slot_value(device->evdev,
							     slot->index,
							     ABS_MT_POSITION_X);
		slot->axes.point.y = libevdev_get_slot_value(device->evdev,
							     slot->index,
							     ABS_MT_POSITION_Y);
	}

	if (bit_is_set(slot->changed_axes,
		       LIBINPUT_TABLET_TOOL_AXIS_ROTATION_Z)) {
		int angle = libevdev_get_slot_value(device->evdev,
						    slot->index,
						    ABS_MT_ORIENTATION);
		/* The kernel gives us ±90 degrees off neutral */
		slot->axes.rotation = (360 - angle) % 360;
	}

	if (bit_is_set(slot->changed_axes,
		       LIBINPUT_TABLET_TOOL_AXIS_SIZE_MAJOR) ||
	    bit_is_set(slot->changed_axes,
		       LIBINPUT_TABLET_TOOL_AXIS_SIZE_MINOR)) {
		int major, minor;
		unsigned int rmajor, rminor;

		major = libevdev_get_slot_value(device->evdev,
						slot->index,
						ABS_MT_TOUCH_MAJOR);
		minor = libevdev_get_slot_value(device->evdev,
						slot->index,
						ABS_MT_TOUCH_MINOR);
		rmajor = libevdev_get_abs_resolution(device->evdev, ABS_MT_TOUCH_MAJOR);
		rminor = libevdev_get_abs_resolution(device->evdev, ABS_MT_TOUCH_MINOR);
		rmajor = max(1, rmajor);
		rminor = max(1, rminor);
		slot->axes.size.major = (double)major/rmajor;
		slot->axes.size.minor = (double)minor/rminor;
	}

	axes.point = slot->axes.point;
	axes.rotation = slot->axes.rotation;
	axes.size = slot->axes.size;

	/* delta is done elsewhere */

	rc = true;
out:
	*axes_out = axes;
	return rc;

}

static void
totem_slot_mark_all_axes_changed(struct totem_dispatch *totem,
			    struct totem_slot *slot,
			    struct libinput_tablet_tool *tool)
{
	static_assert(sizeof(slot->changed_axes) ==
			      sizeof(tool->axis_caps),
		      "Mismatching array sizes");

	memcpy(slot->changed_axes,
	       tool->axis_caps,
	       sizeof(slot->changed_axes));
}

static inline void
totem_slot_reset_changed_axes(struct totem_dispatch *totem,
			      struct totem_slot *slot)
{
	memset(slot->changed_axes, 0, sizeof(slot->changed_axes));
}

static enum totem_slot_state
totem_handle_slot_state(struct totem_dispatch *totem,
			struct totem_slot *slot,
			uint64_t time)
{
	struct evdev_device *device = totem->device;
	struct tablet_axes axes;
	bool updated;

	switch (slot->state) {
	case SLOT_STATE_BEGIN:
		if (!slot->tool)
			slot->tool = totem_new_tool(totem);
		totem_slot_mark_all_axes_changed(totem, slot, slot->tool);
		break;
	case SLOT_STATE_UPDATE:
		assert(slot->tool);
		break;
	case SLOT_STATE_END:
		assert(slot->tool);
		break;
	case SLOT_STATE_NONE:
		return SLOT_STATE_NONE;
	}

	updated = totem_slot_fetch_axes(totem, slot, slot->tool, &axes, time);

	switch (slot->state) {
	case SLOT_STATE_BEGIN:
		slot->last_point.x = slot->last_point.y = 0;

		/* delta reset */
		slot->axes.point.x = libevdev_get_slot_value(device->evdev,
							     slot->index,
							     ABS_MT_POSITION_X);
		slot->axes.point.y = libevdev_get_slot_value(device->evdev,
							     slot->index,
							     ABS_MT_POSITION_Y);

		tablet_notify_proximity(&device->base,
					time,
					slot->tool,
					LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN,
					slot->changed_axes,
					&axes);
		totem_slot_reset_changed_axes(totem, slot);
		tablet_notify_tip(&device->base,
				  time,
				  slot->tool,
				  LIBINPUT_TABLET_TOOL_TIP_DOWN,
				  slot->changed_axes,
				  &axes);
		slot->state = SLOT_STATE_UPDATE;
		/* FIXME: force button presses if any are down */
		break;
	case SLOT_STATE_UPDATE:
		if (updated) {
			/* FIXME: needs to be normalized */
			axes.delta.x = slot->last_point.x - slot->axes.point.x;
			axes.delta.y = slot->last_point.y - slot->axes.point.y;

			tablet_notify_axis(&device->base,
					   time,
					   slot->tool,
					   LIBINPUT_TABLET_TOOL_TIP_DOWN,
					   slot->changed_axes,
					   &axes);
		}
		break;
	case SLOT_STATE_END:
		tablet_notify_tip(&device->base,
				  time,
				  slot->tool,
				  LIBINPUT_TABLET_TOOL_TIP_UP,
				  slot->changed_axes,
				  &axes);
		totem_slot_reset_changed_axes(totem, slot);
		tablet_notify_proximity(&device->base,
					time,
					slot->tool,
					LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT,
					slot->changed_axes,
					&axes);
		slot->state = SLOT_STATE_NONE;
		break;
	case SLOT_STATE_NONE:
		abort();
		break;
	}

	slot->last_point = slot->axes.point;
	totem_slot_reset_changed_axes(totem, slot);

	return slot->state;
}

static enum totem_slot_state
totem_handle_state(struct totem_dispatch *totem,
		   uint64_t time)
{
	enum totem_slot_state global_state = SLOT_STATE_NONE;

	for (size_t i = 0; i < totem->nslots; i++) {
		enum totem_slot_state s;

		s = totem_handle_slot_state(totem,
					    &totem->slots[i],
					    time);

		/* If one slot is active, the totem is active */
		if (s != SLOT_STATE_NONE)
			global_state = SLOT_STATE_UPDATE;
	}

	return global_state;
}

static void
totem_interface_process(struct evdev_dispatch *dispatch,
			struct evdev_device *device,
			struct input_event *e,
			uint64_t time)
{
	struct totem_dispatch *totem = totem_dispatch(dispatch);
	enum totem_slot_state global_state;
	bool enable_touch;

	switch(e->type) {
	case EV_ABS:
		totem_process_abs(totem, device, e, time);
		break;
	case EV_KEY:
		totem_process_key(totem, device, e, time);
		break;
	case EV_MSC:
		/* timestamp, ignore */
		break;
	case EV_SYN:
		global_state = totem_handle_state(totem, time);
		enable_touch = (global_state == SLOT_STATE_NONE);
		totem_set_touch_device_enabled(totem->touch_device,
					       enable_touch,
					       time);
		break;
	default:
		evdev_log_error(device,
				"Unexpected event type %s (%#x)\n",
				libevdev_event_type_get_name(e->type),
				e->type);
		break;
	}
}

static void
totem_interface_suspend(struct evdev_dispatch *dispatch,
			struct evdev_device *device)
{
	/* FIXME: need to force a prox-out here if we are in prox */
}

static void
totem_interface_destroy(struct evdev_dispatch *dispatch)
{
	struct totem_dispatch *totem = totem_dispatch(dispatch);

	free(totem);
}

static void
totem_interface_device_added(struct evdev_device *device,
			     struct evdev_device *added_device)
{
	struct totem_dispatch *totem = totem_dispatch(device->dispatch);

	if ((evdev_device_get_id_vendor(added_device) !=
	    evdev_device_get_id_vendor(device)) ||
	    (evdev_device_get_id_product(added_device) !=
	     evdev_device_get_id_product(device)))
	    return;

	/* FIXME:
	   On the real canvas this works but not for testing with
	   libinput replay */
#if 0
	if (libinput_device_get_device_group(&device->base) !=
	    libinput_device_get_device_group(&added_device->base))
		return;
#endif

	if (totem->touch_device != NULL) {
		evdev_log_bug_libinput(device,
				       "already have a paired touch device, ignoring (%s)\n",
				       added_device->devname);
		return;
	}

	totem->touch_device = added_device;
}

static void
totem_interface_device_removed(struct evdev_device *device,
			       struct evdev_device *removed_device)
{
	struct totem_dispatch *totem = totem_dispatch(device->dispatch);

	if (totem->touch_device != removed_device)
		return;

	totem->touch_device = NULL;
}

struct evdev_dispatch_interface totem_interface = {
	.process = totem_interface_process,
	.suspend = totem_interface_suspend,
	.remove = NULL,
	.destroy = totem_interface_destroy,
	.device_added = totem_interface_device_added,
	.device_removed = totem_interface_device_removed,
	.device_suspended = totem_interface_device_added, /* treat as remove */
	.device_resumed = totem_interface_device_removed, /* treat as add */
	.post_added = NULL,
	.toggle_touch = NULL,
	.get_switch_state = NULL,
};

static bool
totem_reject_device(struct evdev_device *device)
{
	struct libevdev *evdev = device->evdev;
	bool has_xy, has_slot, has_tool_dial, has_size;
	double w, h;

	has_xy = libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X) &&
	         libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y);
	has_slot = libevdev_has_event_code(evdev, EV_ABS, ABS_MT_SLOT);
	has_tool_dial = libevdev_has_event_code(evdev, EV_ABS, ABS_MT_TOOL_TYPE) &&
			libevdev_get_abs_maximum(evdev, ABS_MT_TOOL_TYPE) >= MT_TOOL_DIAL;
	has_size = evdev_device_get_size(device, &w, &h) == 0;

	if (has_xy && has_slot && has_tool_dial && has_size)
		return false;

	evdev_log_bug_libinput(device,
			       "missing totem capabilities:%s%s%s%s. "
			       "Ignoring this device.\n",
			       has_xy ? "" : " xy",
			       has_slot ? "" : " slot",
			       has_tool_dial ? "" : " dial",
			       has_size ? "" : " resolution");
	return true;
}

struct evdev_dispatch *
evdev_totem_create(struct evdev_device *device)
{
	struct totem_dispatch *totem;
	struct totem_slot *slots;
	int num_slots;

	if (totem_reject_device(device))
		return NULL;

	totem = zalloc(sizeof *totem);
	totem->device = device;
	totem->base.dispatch_type = DISPATCH_TOTEM;
	totem->base.interface = &totem_interface;

	num_slots = libevdev_get_num_slots(device->evdev);
	if (num_slots <= 0)
		goto error;

	totem->slot = libevdev_get_current_slot(device->evdev);
	slots = zalloc(num_slots * sizeof(*totem->slots));

	for (int slot = 0; slot < num_slots; ++slot) {
		slots[slot].index = slot;
	}

	totem->slots = slots;
	totem->nslots = num_slots;

	return &totem->base;
error:
	totem_interface_destroy(&totem->base);
	return NULL;
}
