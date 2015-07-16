/*
 * Copyright © 2014 Red Hat, Inc.
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

#include <config.h>

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <unistd.h>

#include "litest.h"
#include "libinput-util.h"

START_TEST(device_sendevents_config)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device;
	uint32_t modes;

	device = dev->libinput_device;

	modes = libinput_device_config_send_events_get_modes(device);
	ck_assert_int_eq(modes,
			 LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
}
END_TEST

START_TEST(device_sendevents_config_invalid)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device;
	enum libinput_config_status status;

	device = dev->libinput_device;

	status = libinput_device_config_send_events_set_mode(device,
			     LIBINPUT_CONFIG_SEND_EVENTS_DISABLED | (1 << 4));
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
}
END_TEST

START_TEST(device_sendevents_config_touchpad)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device;
	uint32_t modes, expected;

	expected = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;

	/* The wacom devices in the test suite are external */
	if (libevdev_get_id_vendor(dev->evdev) != VENDOR_ID_WACOM &&
	    libevdev_get_id_bustype(dev->evdev) != BUS_BLUETOOTH)
		expected |=
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;

	device = dev->libinput_device;

	modes = libinput_device_config_send_events_get_modes(device);
	ck_assert_int_eq(modes, expected);
}
END_TEST

START_TEST(device_sendevents_config_touchpad_superset)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device;
	enum libinput_config_status status;
	uint32_t modes;

	/* The wacom devices in the test suite are external */
	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_WACOM ||
	    libevdev_get_id_bustype(dev->evdev) == BUS_BLUETOOTH)
		return;

	device = dev->libinput_device;

	modes = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED |
		LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;

	status = libinput_device_config_send_events_set_mode(device,
							     modes);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* DISABLED supersedes the rest, expect the rest to be dropped */
	modes = libinput_device_config_send_events_get_mode(device);
	ck_assert_int_eq(modes, LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
}
END_TEST

START_TEST(device_sendevents_config_default)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device;
	uint32_t mode;

	device = dev->libinput_device;

	mode = libinput_device_config_send_events_get_mode(device);
	ck_assert_int_eq(mode,
			 LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);

	mode = libinput_device_config_send_events_get_default_mode(device);
	ck_assert_int_eq(mode,
			 LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
}
END_TEST

START_TEST(device_disable)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	enum libinput_config_status status;
	struct libinput_event *event;
	struct litest_device *tmp;

	device = dev->libinput_device;

	litest_drain_events(li);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* no event from disabling */
	litest_assert_empty_queue(li);

	/* no event from disabled device */
	litest_event(dev, EV_REL, REL_X, 10);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_assert_empty_queue(li);

	/* create a new device so the resumed fd isn't the same as the
	   suspended one */
	tmp = litest_add_device(li, LITEST_KEYBOARD);
	ck_assert_notnull(tmp);
	litest_drain_events(li);

	/* no event from resuming */
	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_assert_empty_queue(li);

	/* event from renabled device */
	litest_event(dev, EV_REL, REL_X, 10);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert_notnull(event);
	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_POINTER_MOTION);
	libinput_event_destroy(event);

	litest_delete_device(tmp);
}
END_TEST

START_TEST(device_disable_touchpad)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	enum libinput_config_status status;

	device = dev->libinput_device;

	litest_drain_events(li);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* no event from disabling */
	litest_assert_empty_queue(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_move_to(dev, 0, 50, 50, 90, 90, 10, 0);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	/* no event from resuming */
	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(device_disable_events_pending)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	enum libinput_config_status status;
	struct libinput_event *event;
	int i;

	device = dev->libinput_device;

	litest_drain_events(li);

	/* put a couple of events in the queue, enough to
	   feed the ptraccel trackers */
	for (i = 0; i < 10; i++) {
		litest_event(dev, EV_REL, REL_X, 10);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);
	}
	libinput_dispatch(li);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* expect above events */
	litest_wait_for_event(li);
	while ((event = libinput_get_event(li)) != NULL) {
	       ck_assert_int_eq(libinput_event_get_type(event),
				LIBINPUT_EVENT_POINTER_MOTION);
	       libinput_event_destroy(event);
       }
}
END_TEST

START_TEST(device_double_disable)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	enum libinput_config_status status;

	device = dev->libinput_device;

	litest_drain_events(li);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(device_double_enable)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	enum libinput_config_status status;

	device = dev->libinput_device;

	litest_drain_events(li);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(device_reenable_syspath_changed)
{
	struct libinput *li;
	struct litest_device *litest_device;
	struct libinput_device *device1, *device2;
	enum libinput_config_status status;
	struct libinput_event *event;

	li = litest_create_context();
	litest_device = litest_add_device(li, LITEST_MOUSE);
	device1 = litest_device->libinput_device;

	libinput_device_ref(device1);
	status = libinput_device_config_send_events_set_mode(device1,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_delete_device(litest_device);
	litest_drain_events(li);

	litest_device = litest_add_device(li, LITEST_MOUSE);
	device2 = litest_device->libinput_device;
	/* Note: if the sysname isn't the same, some other device got added
	 * or removed while this test was running.  This is unlikely and
	 * would result in a false positive, so let's fail the test here */
	ck_assert_str_eq(libinput_device_get_sysname(device1),
			 libinput_device_get_sysname(device2));

	status = libinput_device_config_send_events_set_mode(device1,
			LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* can't really check for much here, other than that if we pump
	   events through libinput, none of them should be from the first
	   device */
	litest_event(litest_device, EV_REL, REL_X, 1);
	litest_event(litest_device, EV_REL, REL_Y, 1);
	litest_event(litest_device, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);
	while ((event = libinput_get_event(li))) {
		ck_assert(libinput_event_get_device(event) != device1);
		libinput_event_destroy(event);
	}

	litest_delete_device(litest_device);
	libinput_device_unref(device1);
	libinput_unref(li);
}
END_TEST

START_TEST(device_reenable_device_removed)
{
	struct libinput *li;
	struct litest_device *litest_device;
	struct libinput_device *device;
	enum libinput_config_status status;

	li = litest_create_context();
	litest_device = litest_add_device(li, LITEST_MOUSE);
	device = litest_device->libinput_device;

	libinput_device_ref(device);
	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(li);

	litest_delete_device(litest_device);
	litest_drain_events(li);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* can't really check for much here, this really just exercises the
	   code path. */
	litest_assert_empty_queue(li);

	libinput_device_unref(device);
	libinput_unref(li);
}
END_TEST

START_TEST(device_disable_release_buttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	struct libinput_event *event;
	struct libinput_event_pointer *ptrevent;
	enum libinput_config_status status;

	device = dev->libinput_device;

	litest_button_click(dev, BTN_LEFT, true);
	litest_drain_events(li);
	litest_assert_empty_queue(li);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_wait_for_event(li);
	event = libinput_get_event(li);

	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_POINTER_BUTTON);
	ptrevent = libinput_event_get_pointer_event(event);
	ck_assert_int_eq(libinput_event_pointer_get_button(ptrevent),
			 BTN_LEFT);
	ck_assert_int_eq(libinput_event_pointer_get_button_state(ptrevent),
			 LIBINPUT_BUTTON_STATE_RELEASED);

	libinput_event_destroy(event);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(device_disable_release_keys)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	struct libinput_event *event;
	struct libinput_event_keyboard *kbdevent;
	enum libinput_config_status status;

	device = dev->libinput_device;

	litest_button_click(dev, KEY_A, true);
	litest_drain_events(li);
	litest_assert_empty_queue(li);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_wait_for_event(li);
	event = libinput_get_event(li);

	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_KEYBOARD_KEY);
	kbdevent = libinput_event_get_keyboard_event(event);
	ck_assert_int_eq(libinput_event_keyboard_get_key(kbdevent),
			 KEY_A);
	ck_assert_int_eq(libinput_event_keyboard_get_key_state(kbdevent),
			 LIBINPUT_KEY_STATE_RELEASED);

	libinput_event_destroy(event);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(device_disable_release_tap)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	enum libinput_config_status status;

	device = dev->libinput_device;

	libinput_device_config_tap_set_enabled(device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);

	libinput_dispatch(li);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	/* tap happened before suspending, so we still expect the event */

	litest_timeout_tap();

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);

	/* resume, make sure we don't get anything */
	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

}
END_TEST

START_TEST(device_disable_release_tap_n_drag)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	enum libinput_config_status status;

	device = dev->libinput_device;

	libinput_device_config_tap_set_enabled(device,
					       LIBINPUT_CONFIG_TAP_ENABLED);

	litest_drain_events(li);

	litest_touch_down(dev, 0, 50, 50);
	litest_touch_up(dev, 0);
	litest_touch_down(dev, 0, 50, 50);
	libinput_dispatch(li);
	litest_timeout_tap();
	libinput_dispatch(li);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	libinput_dispatch(li);
	litest_touch_up(dev, 0);

	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li,
				   BTN_LEFT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(device_disable_release_softbutton)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	enum libinput_config_status status;

	device = dev->libinput_device;

	litest_drain_events(li);

	litest_touch_down(dev, 0, 90, 90);
	litest_button_click(dev, BTN_LEFT, true);

	/* make sure softbutton works */
	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_PRESSED);
	/* disable */
	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_assert_button_event(li,
				   BTN_RIGHT,
				   LIBINPUT_BUTTON_STATE_RELEASED);

	litest_assert_empty_queue(li);

	litest_button_click(dev, BTN_LEFT, false);
	litest_touch_up(dev, 0);

	litest_assert_empty_queue(li);

	/* resume, make sure we don't get anything */
	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	libinput_dispatch(li);
	litest_assert_empty_queue(li);

}
END_TEST

START_TEST(device_disable_topsoftbutton)
{
	struct litest_device *dev = litest_current_device();
	struct litest_device *trackpoint;
	struct libinput *li = dev->libinput;
	struct libinput_device *device;
	enum libinput_config_status status;

	struct libinput_event *event;
	struct libinput_event_pointer *ptrevent;

	device = dev->libinput_device;

	trackpoint = litest_add_device(li, LITEST_TRACKPOINT);

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_drain_events(li);

	litest_touch_down(dev, 0, 90, 10);
	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_LEFT, false);
	litest_touch_up(dev, 0);

	litest_wait_for_event(li);
	event = libinput_get_event(li);
	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_POINTER_BUTTON);
	ck_assert_ptr_eq(libinput_event_get_device(event),
			 trackpoint->libinput_device);
	ptrevent = libinput_event_get_pointer_event(event);
	ck_assert_int_eq(libinput_event_pointer_get_button(ptrevent),
			 BTN_RIGHT);
	ck_assert_int_eq(libinput_event_pointer_get_button_state(ptrevent),
			 LIBINPUT_BUTTON_STATE_PRESSED);
	libinput_event_destroy(event);

	event = libinput_get_event(li);
	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_POINTER_BUTTON);
	ck_assert_ptr_eq(libinput_event_get_device(event),
			 trackpoint->libinput_device);
	ptrevent = libinput_event_get_pointer_event(event);
	ck_assert_int_eq(libinput_event_pointer_get_button(ptrevent),
			 BTN_RIGHT);
	ck_assert_int_eq(libinput_event_pointer_get_button_state(ptrevent),
			 LIBINPUT_BUTTON_STATE_RELEASED);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);

	litest_delete_device(trackpoint);
}
END_TEST

START_TEST(device_ids)
{
	struct litest_device *dev = litest_current_device();
	const char *name;
	unsigned int pid, vid;

	name = libevdev_get_name(dev->evdev);
	pid = libevdev_get_id_product(dev->evdev);
	vid = libevdev_get_id_vendor(dev->evdev);

	ck_assert_str_eq(name,
			 libinput_device_get_name(dev->libinput_device));
	ck_assert_int_eq(pid,
			 libinput_device_get_id_product(dev->libinput_device));
	ck_assert_int_eq(vid,
			 libinput_device_get_id_vendor(dev->libinput_device));
}
END_TEST

START_TEST(device_get_udev_handle)
{
	struct litest_device *dev = litest_current_device();
	struct udev_device *udev_device;

	udev_device = libinput_device_get_udev_device(dev->libinput_device);
	ck_assert_notnull(udev_device);
	udev_device_unref(udev_device);
}
END_TEST

START_TEST(device_context)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_seat *seat;

	ck_assert(dev->libinput == libinput_device_get_context(dev->libinput_device));
	seat = libinput_device_get_seat(dev->libinput_device);
	ck_assert(dev->libinput == libinput_seat_get_context(seat));
}
END_TEST

START_TEST(device_group_get)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device_group *group;

	int userdata = 10;

	group = libinput_device_get_device_group(dev->libinput_device);
	ck_assert_notnull(group);

	libinput_device_group_ref(group);

	libinput_device_group_set_user_data(group, &userdata);
	ck_assert_ptr_eq(&userdata,
			 libinput_device_group_get_user_data(group));

	libinput_device_group_unref(group);
}
END_TEST

START_TEST(device_group_ref)
{
	struct libinput *li = litest_create_context();
	struct litest_device *dev = litest_add_device(li,
						      LITEST_MOUSE);
	struct libinput_device *device = dev->libinput_device;
	struct libinput_device_group *group;

	group = libinput_device_get_device_group(device);
	ck_assert_notnull(group);
	libinput_device_group_ref(group);

	libinput_device_ref(device);
	litest_drain_events(li);
	litest_delete_device(dev);
	litest_drain_events(li);

	/* make sure the device is dead but the group is still around */
	ck_assert(libinput_device_unref(device) == NULL);

	libinput_device_group_ref(group);
	ck_assert_notnull(libinput_device_group_unref(group));
	ck_assert(libinput_device_group_unref(group) == NULL);

	libinput_unref(li);
}
END_TEST

START_TEST(abs_device_no_absx)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_device *device;

	uinput = litest_create_uinput_device("test device", NULL,
					     EV_KEY, BTN_LEFT,
					     EV_KEY, BTN_RIGHT,
					     EV_ABS, ABS_Y,
					     -1);
	li = litest_create_context();
	litest_disable_log_handler(li);
	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput));
	litest_restore_log_handler(li);
	ck_assert(device == NULL);
	libinput_unref(li);

	libevdev_uinput_destroy(uinput);
}
END_TEST

START_TEST(abs_device_no_absy)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_device *device;

	uinput = litest_create_uinput_device("test device", NULL,
					     EV_KEY, BTN_LEFT,
					     EV_KEY, BTN_RIGHT,
					     EV_ABS, ABS_X,
					     -1);
	li = litest_create_context();
	litest_disable_log_handler(li);
	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput));
	litest_restore_log_handler(li);
	ck_assert(device == NULL);
	libinput_unref(li);

	libevdev_uinput_destroy(uinput);
}
END_TEST

START_TEST(abs_mt_device_no_absy)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_device *device;

	uinput = litest_create_uinput_device("test device", NULL,
					     EV_KEY, BTN_LEFT,
					     EV_KEY, BTN_RIGHT,
					     EV_ABS, ABS_X,
					     EV_ABS, ABS_Y,
					     EV_ABS, ABS_MT_SLOT,
					     EV_ABS, ABS_MT_POSITION_X,
					     -1);
	li = litest_create_context();
	litest_disable_log_handler(li);
	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput));
	litest_restore_log_handler(li);
	ck_assert(device == NULL);
	libinput_unref(li);

	libevdev_uinput_destroy(uinput);
}
END_TEST

START_TEST(abs_mt_device_no_absx)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_device *device;

	uinput = litest_create_uinput_device("test device", NULL,
					     EV_KEY, BTN_LEFT,
					     EV_KEY, BTN_RIGHT,
					     EV_ABS, ABS_X,
					     EV_ABS, ABS_Y,
					     EV_ABS, ABS_MT_SLOT,
					     EV_ABS, ABS_MT_POSITION_Y,
					     -1);
	li = litest_create_context();
	litest_disable_log_handler(li);
	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput));
	litest_restore_log_handler(li);
	ck_assert(device == NULL);
	libinput_unref(li);

	libevdev_uinput_destroy(uinput);
}
END_TEST

static void
assert_device_ignored(struct libinput *li, struct input_absinfo *absinfo)
{
	struct libevdev_uinput *uinput;
	struct libinput_device *device;

	uinput = litest_create_uinput_abs_device("test device", NULL,
						 absinfo,
						 EV_KEY, BTN_LEFT,
						 EV_KEY, BTN_RIGHT,
						 -1);
	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput));
	litest_assert_ptr_null(device);
	libevdev_uinput_destroy(uinput);
}

START_TEST(abs_device_no_range)
{
	struct libinput *li;
	int code = _i; /* looped test */
	/* set x/y so libinput doesn't just reject for missing axes */
	struct input_absinfo absinfo[] = {
		{ ABS_X, 0, 10, 0, 0, 0 },
		{ ABS_Y, 0, 10, 0, 0, 0 },
		{ code, 0, 0, 0, 0, 0 },
		{ -1, -1, -1, -1, -1, -1 }
	};

	li = litest_create_context();
	litest_disable_log_handler(li);

	assert_device_ignored(li, absinfo);

	litest_restore_log_handler(li);
	libinput_unref(li);
}
END_TEST

START_TEST(abs_mt_device_no_range)
{
	struct libinput *li;
	int code = _i; /* looped test */
	/* set x/y so libinput doesn't just reject for missing axes */
	struct input_absinfo absinfo[] = {
		{ ABS_X, 0, 10, 0, 0, 0 },
		{ ABS_Y, 0, 10, 0, 0, 0 },
		{ ABS_MT_SLOT, 0, 10, 0, 0, 0 },
		{ ABS_MT_TRACKING_ID, 0, 255, 0, 0, 0 },
		{ ABS_MT_POSITION_X, 0, 10, 0, 0, 0 },
		{ ABS_MT_POSITION_Y, 0, 10, 0, 0, 0 },
		{ code, 0, 0, 0, 0, 0 },
		{ -1, -1, -1, -1, -1, -1 }
	};

	li = litest_create_context();
	litest_disable_log_handler(li);

	if (code != ABS_MT_TOOL_TYPE &&
	    code != ABS_MT_TRACKING_ID) /* kernel overrides it */
		assert_device_ignored(li, absinfo);

	litest_restore_log_handler(li);
	libinput_unref(li);
}
END_TEST

START_TEST(abs_device_missing_res)
{
	struct libinput *li;
	struct input_absinfo absinfo[] = {
		{ ABS_X, 0, 10, 0, 0, 10 },
		{ ABS_Y, 0, 10, 0, 0, 0 },
		{ -1, -1, -1, -1, -1, -1 }
	};

	li = litest_create_context();
	litest_disable_log_handler(li);

	assert_device_ignored(li, absinfo);

	absinfo[0].resolution = 0;
	absinfo[1].resolution = 20;

	assert_device_ignored(li, absinfo);

	litest_restore_log_handler(li);
	libinput_unref(li);
}
END_TEST

START_TEST(abs_mt_device_missing_res)
{
	struct libinput *li;
	struct input_absinfo absinfo[] = {
		{ ABS_X, 0, 10, 0, 0, 10 },
		{ ABS_Y, 0, 10, 0, 0, 10 },
		{ ABS_MT_SLOT, 0, 2, 0, 0, 0 },
		{ ABS_MT_TRACKING_ID, 0, 255, 0, 0, 0 },
		{ ABS_MT_POSITION_X, 0, 10, 0, 0, 10 },
		{ ABS_MT_POSITION_Y, 0, 10, 0, 0, 0 },
		{ -1, -1, -1, -1, -1, -1 }
	};

	li = litest_create_context();
	litest_disable_log_handler(li);
	assert_device_ignored(li, absinfo);

	absinfo[4].resolution = 0;
	absinfo[5].resolution = 20;

	assert_device_ignored(li, absinfo);

	litest_restore_log_handler(li);
	libinput_unref(li);

}
END_TEST

START_TEST(device_wheel_only)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;

	ck_assert(libinput_device_has_capability(device,
						 LIBINPUT_DEVICE_CAP_POINTER));
}
END_TEST

START_TEST(device_udev_tag_alps)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct udev_device *d;
	const char *prop;

	d = libinput_device_get_udev_device(device);
	prop = udev_device_get_property_value(d,
					      "LIBINPUT_MODEL_ALPS_TOUCHPAD");

	if (strstr(libinput_device_get_name(device), "ALPS"))
		ck_assert_notnull(prop);
	else
		ck_assert(prop == NULL);

	udev_device_unref(d);
}
END_TEST

START_TEST(device_udev_tag_wacom)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct udev_device *d;
	const char *prop;

	d = libinput_device_get_udev_device(device);
	prop = udev_device_get_property_value(d,
					      "LIBINPUT_MODEL_WACOM_TOUCHPAD");

	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_WACOM)
		ck_assert_notnull(prop);
	else
		ck_assert(prop == NULL);

	udev_device_unref(d);
}
END_TEST

START_TEST(device_udev_tag_apple)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct udev_device *d;
	const char *prop;

	d = libinput_device_get_udev_device(device);
	prop = udev_device_get_property_value(d,
					      "LIBINPUT_MODEL_WACOM_TOUCHPAD");

	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_WACOM)
		ck_assert_notnull(prop);
	else
		ck_assert(prop == NULL);

	udev_device_unref(d);
}
END_TEST

START_TEST(device_udev_tag_synaptics_serial)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct udev_device *d;
	const char *prop;

	d = libinput_device_get_udev_device(device);
	prop = udev_device_get_property_value(d,
					      "LIBINPUT_MODEL_SYNAPTICS_SERIAL_TOUCHPAD");

	if (libevdev_get_id_vendor(dev->evdev) == VENDOR_ID_SYNAPTICS_SERIAL &&
	    libevdev_get_id_product(dev->evdev) == PRODUCT_ID_SYNAPTICS_SERIAL)
		ck_assert_notnull(prop);
	else
		ck_assert(prop == NULL);

	udev_device_unref(d);
}
END_TEST

void
litest_setup_tests(void)
{
	struct range abs_range = { 0, ABS_MISC };
	struct range abs_mt_range = { ABS_MT_SLOT + 1, ABS_CNT };

	litest_add("device:sendevents", device_sendevents_config, LITEST_ANY, LITEST_TOUCHPAD);
	litest_add("device:sendevents", device_sendevents_config_invalid, LITEST_ANY, LITEST_ANY);
	litest_add("device:sendevents", device_sendevents_config_touchpad, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("device:sendevents", device_sendevents_config_touchpad_superset, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("device:sendevents", device_sendevents_config_default, LITEST_ANY, LITEST_ANY);
	litest_add("device:sendevents", device_disable, LITEST_RELATIVE, LITEST_ANY);
	litest_add("device:sendevents", device_disable_touchpad, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("device:sendevents", device_disable_events_pending, LITEST_RELATIVE, LITEST_TOUCHPAD);
	litest_add("device:sendevents", device_double_disable, LITEST_ANY, LITEST_ANY);
	litest_add("device:sendevents", device_double_enable, LITEST_ANY, LITEST_ANY);
	litest_add_no_device("device:sendevents", device_reenable_syspath_changed);
	litest_add_no_device("device:sendevents", device_reenable_device_removed);
	litest_add_for_device("device:sendevents", device_disable_release_buttons, LITEST_MOUSE);
	litest_add_for_device("device:sendevents", device_disable_release_keys, LITEST_KEYBOARD);
	litest_add("device:sendevents", device_disable_release_tap, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("device:sendevents", device_disable_release_tap_n_drag, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("device:sendevents", device_disable_release_softbutton, LITEST_CLICKPAD, LITEST_APPLE_CLICKPAD);
	litest_add("device:sendevents", device_disable_topsoftbutton, LITEST_TOPBUTTONPAD, LITEST_ANY);
	litest_add("device:id", device_ids, LITEST_ANY, LITEST_ANY);
	litest_add_for_device("device:context", device_context, LITEST_SYNAPTICS_CLICKPAD);

	litest_add("device:udev", device_get_udev_handle, LITEST_ANY, LITEST_ANY);

	litest_add("device:group", device_group_get, LITEST_ANY, LITEST_ANY);
	litest_add_no_device("device:group", device_group_ref);

	litest_add_no_device("device:invalid devices", abs_device_no_absx);
	litest_add_no_device("device:invalid devices", abs_device_no_absy);
	litest_add_no_device("device:invalid devices", abs_mt_device_no_absx);
	litest_add_no_device("device:invalid devices", abs_mt_device_no_absy);
	litest_add_ranged_no_device("device:invalid devices", abs_device_no_range, &abs_range);
	litest_add_ranged_no_device("device:invalid devices", abs_mt_device_no_range, &abs_mt_range);
	litest_add_no_device("device:invalid devices", abs_device_missing_res);
	litest_add_no_device("device:invalid devices", abs_mt_device_missing_res);

	litest_add("device:wheel", device_wheel_only, LITEST_WHEEL, LITEST_RELATIVE|LITEST_ABSOLUTE);

	litest_add("device:udev tags", device_udev_tag_alps, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("device:udev tags", device_udev_tag_wacom, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("device:udev tags", device_udev_tag_apple, LITEST_TOUCHPAD, LITEST_ANY);
	litest_add("device:udev tags", device_udev_tag_synaptics_serial, LITEST_TOUCHPAD, LITEST_ANY);
}
