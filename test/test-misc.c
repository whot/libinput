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
#include <libinput-util.h>
#include <unistd.h>

#include "litest.h"
#include "libinput-util.h"

static int open_restricted(const char *path, int flags, void *data)
{
	int fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}
static void close_restricted(int fd, void *data)
{
	close(fd);
}

static const struct libinput_interface simple_interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

static struct libevdev_uinput *
create_simple_test_device(const char *name, ...)
{
	va_list args;
	struct libevdev_uinput *uinput;
	struct libevdev *evdev;
	unsigned int type, code;
	int rc;
	struct input_absinfo abs = {
		.value = -1,
		.minimum = 0,
		.maximum = 100,
		.fuzz = 0,
		.flat = 0,
		.resolution = 100,
	};

	evdev = libevdev_new();
	litest_assert_notnull(evdev);
	libevdev_set_name(evdev, name);

	va_start(args, name);

	while ((type = va_arg(args, unsigned int)) != (unsigned int)-1 &&
	       (code = va_arg(args, unsigned int)) != (unsigned int)-1) {
		const struct input_absinfo *a = NULL;
		if (type == EV_ABS)
			a = &abs;
		libevdev_enable_event_code(evdev, type, code, a);
	}

	va_end(args);

	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput);
	litest_assert_int_eq(rc, 0);
	libevdev_free(evdev);

	return uinput;
}

START_TEST(event_conversion_device_notify)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_event *event;
	int device_added = 0, device_removed = 0;

	uinput = create_simple_test_device("litest test device",
					   EV_REL, REL_X,
					   EV_REL, REL_Y,
					   EV_KEY, BTN_LEFT,
					   EV_KEY, BTN_MIDDLE,
					   EV_KEY, BTN_LEFT,
					   -1, -1);
	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, libevdev_uinput_get_devnode(uinput));

	libinput_dispatch(li);
	libinput_suspend(li);
	libinput_resume(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_DEVICE_ADDED ||
		    type == LIBINPUT_EVENT_DEVICE_REMOVED) {
			struct libinput_event_device_notify *dn;
			struct libinput_event *base;
			dn = libinput_event_get_device_notify_event(event);
			base = libinput_event_device_notify_get_base_event(dn);
			ck_assert(event == base);

			if (type == LIBINPUT_EVENT_DEVICE_ADDED)
				device_added++;
			else if (type == LIBINPUT_EVENT_DEVICE_REMOVED)
				device_removed++;

			litest_disable_log_handler(li);
			ck_assert(libinput_event_get_pointer_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
			ck_assert(libinput_event_get_gesture_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_tool_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_pad_event(event) == NULL);
			ck_assert(libinput_event_get_switch_event(event) == NULL);
			litest_restore_log_handler(li);
		}

		libinput_event_destroy(event);
	}

	libinput_unref(li);
	libevdev_uinput_destroy(uinput);

	ck_assert_int_gt(device_added, 0);
	ck_assert_int_gt(device_removed, 0);
}
END_TEST

START_TEST(event_conversion_pointer)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int motion = 0, button = 0;

	/* Queue at least two relative motion events as the first one may
	 * be absorbed by the pointer acceleration filter. */
	litest_event(dev, EV_REL, REL_X, -1);
	litest_event(dev, EV_REL, REL_Y, -1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_REL, REL_X, -1);
	litest_event(dev, EV_REL, REL_Y, -1);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_POINTER_MOTION ||
		    type == LIBINPUT_EVENT_POINTER_BUTTON) {
			struct libinput_event_pointer *p;
			struct libinput_event *base;
			p = libinput_event_get_pointer_event(event);
			base = libinput_event_pointer_get_base_event(p);
			ck_assert(event == base);

			if (type == LIBINPUT_EVENT_POINTER_MOTION)
				motion++;
			else if (type == LIBINPUT_EVENT_POINTER_BUTTON)
				button++;

			litest_disable_log_handler(li);
			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
			ck_assert(libinput_event_get_gesture_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_tool_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_pad_event(event) == NULL);
			ck_assert(libinput_event_get_switch_event(event) == NULL);
			litest_restore_log_handler(li);
		}
		libinput_event_destroy(event);
	}

	ck_assert_int_gt(motion, 0);
	ck_assert_int_gt(button, 0);
}
END_TEST

START_TEST(event_conversion_pointer_abs)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int motion = 0, button = 0;

	litest_event(dev, EV_ABS, ABS_X, 10);
	litest_event(dev, EV_ABS, ABS_Y, 50);
	litest_event(dev, EV_KEY, BTN_LEFT, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_ABS, ABS_X, 30);
	litest_event(dev, EV_ABS, ABS_Y, 30);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE ||
		    type == LIBINPUT_EVENT_POINTER_BUTTON) {
			struct libinput_event_pointer *p;
			struct libinput_event *base;
			p = libinput_event_get_pointer_event(event);
			base = libinput_event_pointer_get_base_event(p);
			ck_assert(event == base);

			if (type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE)
				motion++;
			else if (type == LIBINPUT_EVENT_POINTER_BUTTON)
				button++;

			litest_disable_log_handler(li);
			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
			ck_assert(libinput_event_get_gesture_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_tool_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_pad_event(event) == NULL);
			ck_assert(libinput_event_get_switch_event(event) == NULL);
			litest_restore_log_handler(li);
		}
		libinput_event_destroy(event);
	}

	ck_assert_int_gt(motion, 0);
	ck_assert_int_gt(button, 0);
}
END_TEST

START_TEST(event_conversion_key)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int key = 0;

	litest_event(dev, EV_KEY, KEY_A, 1);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_event(dev, EV_KEY, KEY_A, 0);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_KEYBOARD_KEY) {
			struct libinput_event_keyboard *k;
			struct libinput_event *base;
			k = libinput_event_get_keyboard_event(event);
			base = libinput_event_keyboard_get_base_event(k);
			ck_assert(event == base);

			key++;

			litest_disable_log_handler(li);
			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_pointer_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
			ck_assert(libinput_event_get_gesture_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_tool_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_pad_event(event) == NULL);
			ck_assert(libinput_event_get_switch_event(event) == NULL);
			litest_restore_log_handler(li);
		}
		libinput_event_destroy(event);
	}

	ck_assert_int_gt(key, 0);
}
END_TEST

START_TEST(event_conversion_touch)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int touch = 0;

	libinput_dispatch(li);

	litest_event(dev, EV_KEY, BTN_TOOL_FINGER, 1);
	litest_event(dev, EV_KEY, BTN_TOUCH, 1);
	litest_event(dev, EV_ABS, ABS_X, 10);
	litest_event(dev, EV_ABS, ABS_Y, 10);
	litest_event(dev, EV_ABS, ABS_MT_SLOT, 0);
	litest_event(dev, EV_ABS, ABS_MT_TRACKING_ID, 1);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_X, 10);
	litest_event(dev, EV_ABS, ABS_MT_POSITION_Y, 10);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type >= LIBINPUT_EVENT_TOUCH_DOWN &&
		    type <= LIBINPUT_EVENT_TOUCH_FRAME) {
			struct libinput_event_touch *t;
			struct libinput_event *base;
			t = libinput_event_get_touch_event(event);
			base = libinput_event_touch_get_base_event(t);
			ck_assert(event == base);

			touch++;

			litest_disable_log_handler(li);
			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_pointer_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_gesture_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_tool_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_pad_event(event) == NULL);
			ck_assert(libinput_event_get_switch_event(event) == NULL);
			litest_restore_log_handler(li);
		}
		libinput_event_destroy(event);
	}

	ck_assert_int_gt(touch, 0);
}
END_TEST

START_TEST(event_conversion_gesture)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int gestures = 0;
	int i;

	libinput_dispatch(li);

	litest_touch_down(dev, 0, 70, 30);
	litest_touch_down(dev, 1, 30, 70);
	for (i = 0; i < 8; i++) {
		litest_push_event_frame(dev);
		litest_touch_move(dev, 0, 70 - i * 5, 30 + i * 5);
		litest_touch_move(dev, 1, 30 + i * 5, 70 - i * 5);
		litest_pop_event_frame(dev);
		libinput_dispatch(li);
	}

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type >= LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN &&
		    type <= LIBINPUT_EVENT_GESTURE_PINCH_END) {
			struct libinput_event_gesture *g;
			struct libinput_event *base;
			g = libinput_event_get_gesture_event(event);
			base = libinput_event_gesture_get_base_event(g);
			ck_assert(event == base);

			gestures++;

			litest_disable_log_handler(li);
			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_pointer_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_pad_event(event) == NULL);
			ck_assert(libinput_event_get_switch_event(event) == NULL);
			litest_restore_log_handler(li);
		}
		libinput_event_destroy(event);
	}

	ck_assert_int_gt(gestures, 0);
}
END_TEST

START_TEST(event_conversion_tablet)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int events = 0;
	struct axis_replacement axes[] = {
		{ ABS_DISTANCE, 10 },
		{ -1, -1 }
	};

	litest_tablet_proximity_in(dev, 50, 50, axes);
	litest_tablet_motion(dev, 60, 50, axes);
	litest_button_click(dev, BTN_STYLUS, true);
	litest_button_click(dev, BTN_STYLUS, false);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type >= LIBINPUT_EVENT_TABLET_TOOL_AXIS &&
		    type <= LIBINPUT_EVENT_TABLET_TOOL_BUTTON) {
			struct libinput_event_tablet_tool *t;
			struct libinput_event *base;
			t = libinput_event_get_tablet_tool_event(event);
			base = libinput_event_tablet_tool_get_base_event(t);
			ck_assert(event == base);

			events++;

			litest_disable_log_handler(li);
			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_pointer_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_pad_event(event) == NULL);
			ck_assert(libinput_event_get_switch_event(event) == NULL);
			litest_restore_log_handler(li);
		}
		libinput_event_destroy(event);
	}

	ck_assert_int_gt(events, 0);
}
END_TEST

START_TEST(event_conversion_tablet_pad)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int events = 0;

	litest_button_click(dev, BTN_0, true);
	litest_pad_ring_start(dev, 10);
	litest_pad_ring_end(dev);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type >= LIBINPUT_EVENT_TABLET_PAD_BUTTON &&
		    type <= LIBINPUT_EVENT_TABLET_PAD_STRIP) {
			struct libinput_event_tablet_pad *p;
			struct libinput_event *base;

			p = libinput_event_get_tablet_pad_event(event);
			base = libinput_event_tablet_pad_get_base_event(p);
			ck_assert(event == base);

			events++;

			litest_disable_log_handler(li);
			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_pointer_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_tool_event(event) == NULL);
			ck_assert(libinput_event_get_switch_event(event) == NULL);
			litest_restore_log_handler(li);
		}
		libinput_event_destroy(event);
	}

	ck_assert_int_gt(events, 0);
}
END_TEST

START_TEST(event_conversion_switch)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	int sw = 0;

	litest_lid_action(dev, LIBINPUT_SWITCH_STATE_ON);
	litest_lid_action(dev, LIBINPUT_SWITCH_STATE_OFF);
	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_SWITCH_TOGGLE) {
			struct libinput_event_switch *s;
			struct libinput_event *base;
			s = libinput_event_get_switch_event(event);
			base = libinput_event_switch_get_base_event(s);
			ck_assert(event == base);

			sw++;

			litest_disable_log_handler(li);
			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_pointer_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
			ck_assert(libinput_event_get_gesture_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_tool_event(event) == NULL);
			ck_assert(libinput_event_get_tablet_pad_event(event) == NULL);
			litest_restore_log_handler(li);
		}
		libinput_event_destroy(event);
	}

	ck_assert_int_gt(sw, 0);
}
END_TEST

START_TEST(bitfield_helpers)
{
	/* This value has a bit set on all of the word boundaries we want to
	 * test: 0, 1, 7, 8, 31, 32, and 33
	 */
	unsigned char read_bitfield[] = { 0x83, 0x1, 0x0, 0x80, 0x3 };
	unsigned char write_bitfield[ARRAY_LENGTH(read_bitfield)] = {0};
	size_t i;

	/* Now check that the bitfield we wrote to came out to be the same as
	 * the bitfield we were writing from */
	for (i = 0; i < ARRAY_LENGTH(read_bitfield) * 8; i++) {
		switch (i) {
		case 0:
		case 1:
		case 7:
		case 8:
		case 31:
		case 32:
		case 33:
			ck_assert(bit_is_set(read_bitfield, i));
			set_bit(write_bitfield, i);
			break;
		default:
			ck_assert(!bit_is_set(read_bitfield, i));
			clear_bit(write_bitfield, i);
			break;
		}
	}

	ck_assert_int_eq(memcmp(read_bitfield,
				write_bitfield,
				sizeof(read_bitfield)),
			 0);
}
END_TEST

START_TEST(context_ref_counting)
{
	struct libinput *li;

	/* These tests rely on valgrind to detect memory leak and use after
	 * free errors. */

	li = libinput_path_create_context(&simple_interface, NULL);
	ck_assert_notnull(li);
	ck_assert_ptr_eq(libinput_unref(li), NULL);

	li = libinput_path_create_context(&simple_interface, NULL);
	ck_assert_notnull(li);
	ck_assert_ptr_eq(libinput_ref(li), li);
	ck_assert_ptr_eq(libinput_unref(li), li);
	ck_assert_ptr_eq(libinput_unref(li), NULL);
}
END_TEST

START_TEST(config_status_string)
{
	const char *strs[3];
	const char *invalid;
	size_t i, j;

	strs[0] = libinput_config_status_to_str(LIBINPUT_CONFIG_STATUS_SUCCESS);
	strs[1] = libinput_config_status_to_str(LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	strs[2] = libinput_config_status_to_str(LIBINPUT_CONFIG_STATUS_INVALID);

	for (i = 0; i < ARRAY_LENGTH(strs) - 1; i++)
		for (j = i + 1; j < ARRAY_LENGTH(strs); j++)
			ck_assert_str_ne(strs[i], strs[j]);

	invalid = libinput_config_status_to_str(LIBINPUT_CONFIG_STATUS_INVALID + 1);
	ck_assert(invalid == NULL);
	invalid = libinput_config_status_to_str(LIBINPUT_CONFIG_STATUS_SUCCESS - 1);
	ck_assert(invalid == NULL);
}
END_TEST

START_TEST(matrix_helpers)
{
	struct matrix m1, m2, m3;
	float f[6] = { 1, 2, 3, 4, 5, 6 };
	int x, y;
	int row, col;

	matrix_init_identity(&m1);

	for (row = 0; row < 3; row++) {
		for (col = 0; col < 3; col++) {
			ck_assert_int_eq(m1.val[row][col],
					 (row == col) ? 1 : 0);
		}
	}
	ck_assert(matrix_is_identity(&m1));

	matrix_from_farray6(&m2, f);
	ck_assert_int_eq(m2.val[0][0], 1);
	ck_assert_int_eq(m2.val[0][1], 2);
	ck_assert_int_eq(m2.val[0][2], 3);
	ck_assert_int_eq(m2.val[1][0], 4);
	ck_assert_int_eq(m2.val[1][1], 5);
	ck_assert_int_eq(m2.val[1][2], 6);
	ck_assert_int_eq(m2.val[2][0], 0);
	ck_assert_int_eq(m2.val[2][1], 0);
	ck_assert_int_eq(m2.val[2][2], 1);

	x = 100;
	y = 5;
	matrix_mult_vec(&m1, &x, &y);
	ck_assert_int_eq(x, 100);
	ck_assert_int_eq(y, 5);

	matrix_mult(&m3, &m1, &m1);
	ck_assert(matrix_is_identity(&m3));

	matrix_init_scale(&m2, 2, 4);
	ck_assert_int_eq(m2.val[0][0], 2);
	ck_assert_int_eq(m2.val[0][1], 0);
	ck_assert_int_eq(m2.val[0][2], 0);
	ck_assert_int_eq(m2.val[1][0], 0);
	ck_assert_int_eq(m2.val[1][1], 4);
	ck_assert_int_eq(m2.val[1][2], 0);
	ck_assert_int_eq(m2.val[2][0], 0);
	ck_assert_int_eq(m2.val[2][1], 0);
	ck_assert_int_eq(m2.val[2][2], 1);

	matrix_mult_vec(&m2, &x, &y);
	ck_assert_int_eq(x, 200);
	ck_assert_int_eq(y, 20);

	matrix_init_translate(&m2, 10, 100);
	ck_assert_int_eq(m2.val[0][0], 1);
	ck_assert_int_eq(m2.val[0][1], 0);
	ck_assert_int_eq(m2.val[0][2], 10);
	ck_assert_int_eq(m2.val[1][0], 0);
	ck_assert_int_eq(m2.val[1][1], 1);
	ck_assert_int_eq(m2.val[1][2], 100);
	ck_assert_int_eq(m2.val[2][0], 0);
	ck_assert_int_eq(m2.val[2][1], 0);
	ck_assert_int_eq(m2.val[2][2], 1);

	matrix_mult_vec(&m2, &x, &y);
	ck_assert_int_eq(x, 210);
	ck_assert_int_eq(y, 120);

	matrix_to_farray6(&m2, f);
	ck_assert_int_eq(f[0], 1);
	ck_assert_int_eq(f[1], 0);
	ck_assert_int_eq(f[2], 10);
	ck_assert_int_eq(f[3], 0);
	ck_assert_int_eq(f[4], 1);
	ck_assert_int_eq(f[5], 100);
}
END_TEST

START_TEST(ratelimit_helpers)
{
	struct ratelimit rl;
	unsigned int i, j;

	/* 10 attempts every 100ms */
	ratelimit_init(&rl, ms2us(100), 10);

	for (j = 0; j < 3; ++j) {
		/* a burst of 9 attempts must succeed */
		for (i = 0; i < 9; ++i) {
			ck_assert_int_eq(ratelimit_test(&rl),
					 RATELIMIT_PASS);
		}

		/* the 10th attempt reaches the threshold */
		ck_assert_int_eq(ratelimit_test(&rl), RATELIMIT_THRESHOLD);

		/* ..then further attempts must fail.. */
		ck_assert_int_eq(ratelimit_test(&rl), RATELIMIT_EXCEEDED);

		/* ..regardless of how often we try. */
		for (i = 0; i < 100; ++i) {
			ck_assert_int_eq(ratelimit_test(&rl),
					 RATELIMIT_EXCEEDED);
		}

		/* ..even after waiting 20ms */
		msleep(20);
		for (i = 0; i < 100; ++i) {
			ck_assert_int_eq(ratelimit_test(&rl),
					 RATELIMIT_EXCEEDED);
		}

		/* but after 100ms the counter is reset */
		msleep(90); /* +10ms to account for time drifts */
	}
}
END_TEST

struct parser_test {
	char *tag;
	int expected_value;
};

START_TEST(dpi_parser)
{
	struct parser_test tests[] = {
		{ "450 *1800 3200", 1800 },
		{ "*450 1800 3200", 450 },
		{ "450 1800 *3200", 3200 },
		{ "450 1800 3200", 3200 },
		{ "450 1800 failboat", 0 },
		{ "450 1800 *failboat", 0 },
		{ "0 450 1800 *3200", 0 },
		{ "450@37 1800@12 *3200@6", 3200 },
		{ "450@125 1800@125   *3200@125  ", 3200 },
		{ "450@125 *1800@125  3200@125", 1800 },
		{ "*this @string fails", 0 },
		{ "12@34 *45@", 0 },
		{ "12@a *45@", 0 },
		{ "12@a *45@25", 0 },
		{ "                                      * 12, 450, 800", 0 },
		{ "                                      *12, 450, 800", 12 },
		{ "*12, *450, 800", 12 },
		{ "*-23412, 450, 800", 0 },
		{ "112@125, 450@125, 800@125, 900@-125", 0 },
		{ "", 0 },
		{ "   ", 0 },
		{ "* ", 0 },
		{ NULL, 0 }
	};
	int i, dpi;

	for (i = 0; tests[i].tag != NULL; i++) {
		dpi = parse_mouse_dpi_property(tests[i].tag);
		ck_assert_int_eq(dpi, tests[i].expected_value);
	}

	dpi = parse_mouse_dpi_property(NULL);
	ck_assert_int_eq(dpi, 0);
}
END_TEST

START_TEST(wheel_click_parser)
{
	struct parser_test tests[] = {
		{ "1", 1 },
		{ "10", 10 },
		{ "-12", -12 },
		{ "360", 360 },

		{ "0", 0 },
		{ "-0", 0 },
		{ "a", 0 },
		{ "10a", 0 },
		{ "10-", 0 },
		{ "sadfasfd", 0 },
		{ "361", 0 },
		{ NULL, 0 }
	};

	int i, angle;

	for (i = 0; tests[i].tag != NULL; i++) {
		angle = parse_mouse_wheel_click_angle_property(tests[i].tag);
		ck_assert_int_eq(angle, tests[i].expected_value);
	}
}
END_TEST

START_TEST(wheel_click_count_parser)
{
	struct parser_test tests[] = {
		{ "1", 1 },
		{ "10", 10 },
		{ "-12", -12 },
		{ "360", 360 },

		{ "0", 0 },
		{ "-0", 0 },
		{ "a", 0 },
		{ "10a", 0 },
		{ "10-", 0 },
		{ "sadfasfd", 0 },
		{ "361", 0 },
		{ NULL, 0 }
	};

	int i, angle;

	for (i = 0; tests[i].tag != NULL; i++) {
		angle = parse_mouse_wheel_click_count_property(tests[i].tag);
		ck_assert_int_eq(angle, tests[i].expected_value);
	}

	angle = parse_mouse_wheel_click_count_property(NULL);
	ck_assert_int_eq(angle, 0);
}
END_TEST

struct parser_test_float {
	char *tag;
	double expected_value;
};

START_TEST(trackpoint_accel_parser)
{
	struct parser_test_float tests[] = {
		{ "0.5", 0.5 },
		{ "1.0", 1.0 },
		{ "2.0", 2.0 },
		{ "fail1.0", 0.0 },
		{ "1.0fail", 0.0 },
		{ "0,5", 0.0 },
		{ NULL, 0.0 }
	};
	int i;
	double accel;

	for (i = 0; tests[i].tag != NULL; i++) {
		accel = parse_trackpoint_accel_property(tests[i].tag);
		ck_assert(accel == tests[i].expected_value);
	}

	accel = parse_trackpoint_accel_property(NULL);
	ck_assert_double_eq(accel, 0.0);
}
END_TEST

struct parser_test_dimension {
	char *tag;
	bool success;
	int x, y;
};

START_TEST(dimension_prop_parser)
{
	struct parser_test_dimension tests[] = {
		{ "10x10", true, 10, 10 },
		{ "1x20", true, 1, 20 },
		{ "1x8000", true, 1, 8000 },
		{ "238492x428210", true, 238492, 428210 },
		{ "0x0", true, 0, 0 },
		{ "-10x10", false, 0, 0 },
		{ "-1", false, 0, 0 },
		{ "1x-99", false, 0, 0 },
		{ "0", false, 0, 0 },
		{ "100", false, 0, 0 },
		{ "", false, 0, 0 },
		{ "abd", false, 0, 0 },
		{ "xabd", false, 0, 0 },
		{ "0xaf", false, 0, 0 },
		{ "0x0x", true, 0, 0 },
		{ "x10", false, 0, 0 },
		{ NULL, false, 0, 0 }
	};
	int i;
	size_t x, y;
	bool success;

	for (i = 0; tests[i].tag != NULL; i++) {
		x = y = 0xad;
		success = parse_dimension_property(tests[i].tag, &x, &y);
		ck_assert(success == tests[i].success);
		if (success) {
			ck_assert_int_eq(x, tests[i].x);
			ck_assert_int_eq(y, tests[i].y);
		} else {
			ck_assert_int_eq(x, 0xad);
			ck_assert_int_eq(y, 0xad);
		}
	}

	success = parse_dimension_property(NULL, &x, &y);
	ck_assert(success == false);
}
END_TEST

START_TEST(time_conversion)
{
	ck_assert_int_eq(us(10), 10);
	ck_assert_int_eq(ns2us(10000), 10);
	ck_assert_int_eq(ms2us(10), 10000);
	ck_assert_int_eq(s2us(1), 1000000);
	ck_assert_int_eq(us2ms(10000), 10);
}
END_TEST

struct atoi_test {
	char *str;
	bool success;
	int val;
};

START_TEST(safe_atoi_test)
{
	struct atoi_test tests[] = {
		{ "10", true, 10 },
		{ "20", true, 20 },
		{ "-1", true, -1 },
		{ "2147483647", true, 2147483647 },
		{ "-2147483648", true, -2147483648 },
		{ "4294967295", false, 0 },
		{ "0x0", false, 0 },
		{ "-10x10", false, 0 },
		{ "1x-99", false, 0 },
		{ "", false, 0 },
		{ "abd", false, 0 },
		{ "xabd", false, 0 },
		{ "0xaf", false, 0 },
		{ "0x0x", false, 0 },
		{ "x10", false, 0 },
		{ NULL, false, 0 }
	};
	int v;
	bool success;

	for (int i = 0; tests[i].str != NULL; i++) {
		v = 0xad;
		success = safe_atoi(tests[i].str, &v);
		ck_assert(success == tests[i].success);
		if (success)
			ck_assert_int_eq(v, tests[i].val);
		else
			ck_assert_int_eq(v, 0xad);
	}
}
END_TEST

struct atod_test {
	char *str;
	bool success;
	double val;
};

START_TEST(safe_atod_test)
{
	struct atod_test tests[] = {
		{ "10", true, 10 },
		{ "20", true, 20 },
		{ "-1", true, -1 },
		{ "2147483647", true, 2147483647 },
		{ "-2147483648", true, -2147483648 },
		{ "4294967295", true, 4294967295 },
		{ "0x0", true, 0 },
		{ "0x10", true, 0x10 },
		{ "0xaf", true, 0xaf },
		{ "x80", false, 0 },
		{ "0.0", true, 0.0 },
		{ "0.1", true, 0.1 },
		{ "1.2", true, 1.2 },
		{ "-324.9", true, -324.9 },
		{ "9324.9", true, 9324.9 },
		{ "NAN", false, 0 },
		{ "INFINITY", false, 0 },
		{ "-10x10", false, 0 },
		{ "1x-99", false, 0 },
		{ "", false, 0 },
		{ "abd", false, 0 },
		{ "xabd", false, 0 },
		{ "0x0x", false, 0 },
		{ NULL, false, 0 }
	};
	double v;
	bool success;

	for (int i = 0; tests[i].str != NULL; i++) {
		v = 0xad;
		success = safe_atod(tests[i].str, &v);
		ck_assert(success == tests[i].success);
		if (success)
			ck_assert_int_eq(v, tests[i].val);
		else
			ck_assert_int_eq(v, 0xad);
	}
}
END_TEST

struct strsplit_test {
	const char *string;
	const char *delim;
	const char *results[10];
};

START_TEST(strsplit_test)
{
	struct strsplit_test tests[] = {
		{ "one two three", " ", { "one", "two", "three", NULL } },
		{ "one", " ", { "one", NULL } },
		{ "one two ", " ", { "one", "two", NULL } },
		{ "one  two", " ", { "one", "two", NULL } },
		{ " one two", " ", { "one", "two", NULL } },
		{ "one", "\t \r", { "one", NULL } },
		{ "one two three", " t", { "one", "wo", "hree", NULL } },
		{ " one two three", "te", { " on", " ", "wo ", "hr", NULL } },
		{ "one", "ne", { "o", NULL } },
		{ "onene", "ne", { "o", NULL } },
		{ NULL, NULL, { NULL }}
	};
	struct strsplit_test *t = tests;

	while (t->string) {
		char **strv;
		int idx = 0;
		strv = strv_from_string(t->string, t->delim);
		while (t->results[idx]) {
			ck_assert_str_eq(t->results[idx], strv[idx]);
			idx++;
		}
		ck_assert_ptr_eq(strv[idx], NULL);
		strv_free(strv);
		t++;
	}

	/* Special cases */
	ck_assert_ptr_eq(strv_from_string("", " "), NULL);
	ck_assert_ptr_eq(strv_from_string(" ", " "), NULL);
	ck_assert_ptr_eq(strv_from_string("     ", " "), NULL);
	ck_assert_ptr_eq(strv_from_string("oneoneone", "one"), NULL);
}
END_TEST

static int open_restricted_leak(const char *path, int flags, void *data)
{
	return *(int*)data;
}

static void close_restricted_leak(int fd, void *data)
{
	/* noop */
}

const struct libinput_interface leak_interface = {
	.open_restricted = open_restricted_leak,
	.close_restricted = close_restricted_leak,
};

LIBINPUT_ATTRIBUTE_PRINTF(3, 0)
static void
simple_log_handler(struct libinput *libinput,
		   enum libinput_log_priority priority,
		   const char *format,
		   va_list args)
{
	vfprintf(stderr, format, args);
}

START_TEST(fd_no_event_leak)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_device *device;
	int fd = -1;
	const char *path;
	struct libinput_event *event;

	uinput = create_simple_test_device("litest test device",
					   EV_REL, REL_X,
					   EV_REL, REL_Y,
					   EV_KEY, BTN_LEFT,
					   EV_KEY, BTN_MIDDLE,
					   EV_KEY, BTN_LEFT,
					   -1, -1);
	path = libevdev_uinput_get_devnode(uinput);

	fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	ck_assert_int_gt(fd, -1);

	li = libinput_path_create_context(&leak_interface, &fd);
	libinput_log_set_priority(li, LIBINPUT_LOG_PRIORITY_DEBUG);
	libinput_log_set_handler(li, simple_log_handler);

	/* Add the device, trigger an event, then remove it again.
	 * Without it, we get a SYN_DROPPED immediately and no events.
	 */
	device = libinput_path_add_device(li, path);
	libevdev_uinput_write_event(uinput, EV_REL, REL_X, 1);
	libevdev_uinput_write_event(uinput, EV_SYN, SYN_REPORT, 0);
	libinput_path_remove_device(device);
	libinput_dispatch(li);
	litest_drain_events(li);

	/* Device is removed, but fd is still open. Queue an event, add a
	 * new device with the same fd, the queued event must be discarded
	 * by libinput */
	libevdev_uinput_write_event(uinput, EV_REL, REL_Y, 1);
	libevdev_uinput_write_event(uinput, EV_SYN, SYN_REPORT, 0);
	libinput_dispatch(li);

	libinput_path_add_device(li, path);
	libinput_dispatch(li);
	event = libinput_get_event(li);
	ck_assert_int_eq(libinput_event_get_type(event),
			 LIBINPUT_EVENT_DEVICE_ADDED);
	libinput_event_destroy(event);

	litest_assert_empty_queue(li);

	close(fd);
	libinput_unref(li);
	libevdev_uinput_destroy(uinput);
}
END_TEST

START_TEST(library_version)
{
	const char *version = LIBINPUT_LT_VERSION;
	int C, R, A;
	int rc;

	rc = sscanf(version, "%d:%d:%d", &C, &R, &A);
	ck_assert_int_eq(rc, 3);

	ck_assert_int_ge(C, 17);
	ck_assert_int_ge(R, 0);
	ck_assert_int_ge(A, 7);

	/* Binary compatibility broken? */
	ck_assert(R != 0 || A != 0);

	/* The first stable API in 0.12 had 10:0:0  */
	ck_assert_int_eq(C - A, 10);
}
END_TEST

void
litest_setup_tests_misc(void)
{
	litest_add_no_device("events:conversion", event_conversion_device_notify);
	litest_add_for_device("events:conversion", event_conversion_pointer, LITEST_MOUSE);
	litest_add_for_device("events:conversion", event_conversion_pointer, LITEST_MOUSE);
	litest_add_for_device("events:conversion", event_conversion_pointer_abs, LITEST_XEN_VIRTUAL_POINTER);
	litest_add_for_device("events:conversion", event_conversion_key, LITEST_KEYBOARD);
	litest_add_for_device("events:conversion", event_conversion_touch, LITEST_WACOM_TOUCH);
	litest_add_for_device("events:conversion", event_conversion_gesture, LITEST_BCM5974);
	litest_add_for_device("events:conversion", event_conversion_tablet, LITEST_WACOM_CINTIQ);
	litest_add_for_device("events:conversion", event_conversion_tablet_pad, LITEST_WACOM_INTUOS5_PAD);
	litest_add_for_device("events:conversion", event_conversion_switch, LITEST_LID_SWITCH);
	litest_add_no_device("misc:bitfield_helpers", bitfield_helpers);

	litest_add_no_device("context:refcount", context_ref_counting);
	litest_add_no_device("config:status string", config_status_string);

	litest_add_no_device("misc:matrix", matrix_helpers);
	litest_add_no_device("misc:ratelimit", ratelimit_helpers);
	litest_add_no_device("misc:parser", dpi_parser);
	litest_add_no_device("misc:parser", wheel_click_parser);
	litest_add_no_device("misc:parser", wheel_click_count_parser);
	litest_add_no_device("misc:parser", trackpoint_accel_parser);
	litest_add_no_device("misc:parser", dimension_prop_parser);
	litest_add_no_device("misc:parser", safe_atoi_test);
	litest_add_no_device("misc:parser", safe_atod_test);
	litest_add_no_device("misc:parser", strsplit_test);
	litest_add_no_device("misc:time", time_conversion);

	litest_add_no_device("misc:fd", fd_no_event_leak);

	litest_add_no_device("misc:library_version", library_version);
}
