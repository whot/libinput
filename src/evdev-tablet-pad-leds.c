/*
 * Copyright © 2016 Red Hat, Inc.
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

#include <assert.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "evdev-tablet-pad.h"

#if HAVE_LIBWACOM
#include <libwacom/libwacom.h>

struct pad_led_group {
	struct list link;
	struct libinput *libinput;
	int refcount;
	unsigned int group; /* left or right */
	unsigned int nmodes;
	int current_mode;

	/* /sys/devices/<hid device>/wacom_led/status_led0_select */
	int led_status_fd;
	/* /sys/devices/<hid device>/wacom_led/status0_luminance */
	int led_luminance_fd;
};

#define TARGET_MODE_NEXT -1
#define TARGET_MODE_NONE -2
struct pad_mode_button {
	struct list link;
	unsigned int button_index; /* also used for ring/strip */
	struct pad_led_group *group;
	int target_mode; /* direct-target mode on the 24HD, otherwise NEXT
			    or NONE on all other buttons */
};

static inline struct pad_led_group *
pad_led_group_ref(struct pad_led_group *group)
{
	assert(group->refcount >= 1);
	group->refcount++;
	return group;
}

static inline void
pad_led_group_set_brightness(struct pad_led_group *group,
			     double brightness)
{
	char buf[4] = {0};
	int b;

	assert(brightness > 0.0);

	/* FIXME: check what the range is on all models */
	b = 127 * brightness;
	if (sprintf(buf, "%d", b) > -1) {
		write(group->led_luminance_fd, buf, strlen(buf));
		fsync(group->led_luminance_fd);
	}
}

static inline void
pad_led_group_set_mode(struct pad_led_group *group,
		       unsigned int mode)
{
	char buf[4] = {0};
	int rc;

	rc = sprintf(buf, "%d", mode);
	if (rc == -1)
		return;

	rc = write(group->led_status_fd, buf, strlen(buf));
	if (rc == -1)
		return;

	fsync(group->led_status_fd);
	group->current_mode = mode;
}

static inline void
pad_led_group_set_next_mode(struct pad_led_group *group)
{
	unsigned int next = (group->current_mode + 1) % group->nmodes;

	pad_led_group_set_mode(group, next);
}

static struct pad_led_group *
pad_led_group_unref(struct pad_led_group *group)
{
	assert(group->refcount >= 1);

	group->refcount--;
	if (group->refcount > 0)
		return group;

	list_remove(&group->link);

	close_restricted(group->libinput, group->led_status_fd);
	close_restricted(group->libinput, group->led_luminance_fd);

	free(group);
	return NULL;
}

static struct pad_led_group *
pad_group_new(struct pad_dispatch *pad,
	      unsigned int group_index,
	      int nleds,
	      const char *syspath)
{
	struct libinput *libinput = pad->device->base.seat->libinput;
	struct pad_led_group *group;
	int rc;
	char path[PATH_MAX];

	group = zalloc(sizeof *group);
	if (!group)
		return NULL;

	group->refcount = 1;
	group->group = group_index;
	group->current_mode = 0; /* FIXME: read this from the file */
	group->nmodes = nleds;
	group->libinput = libinput;
	group->led_status_fd = -1;
	group->led_luminance_fd = -1;

	rc = sprintf(path, "%s/status_led%d_select", syspath, group_index);
	if (rc == -1)
		goto error;

	group->led_status_fd = open_restricted(libinput, path, O_RDWR);
	if (group->led_status_fd < 0)
		goto error;

	rc = sprintf(path, "%s/status%d_luminance", syspath, group_index);
	if (rc == -1)
		goto error;

	group->led_luminance_fd = open_restricted(libinput, path, O_RDWR);
	if (group->led_luminance_fd < 0)
		goto error;

	return group;

error:
	log_error(libinput, "Unable to init LED group: %s\n", strerror(errno));

	close_restricted(libinput, group->led_status_fd);
	close_restricted(libinput, group->led_luminance_fd);
	free(group);

	return NULL;
}

static inline struct pad_mode_button *
pad_mode_button_new(struct pad_dispatch *pad,
		    unsigned int group_index,
		    unsigned int button_index)
{
	struct pad_mode_button *button;
	struct pad_led_group *group;

	button = zalloc(sizeof *button);
	if (!button)
		return NULL;

	button->button_index = button_index;

	list_for_each(group, &pad->leds.led_list, link) {
		if (group->group == group_index) {
			button->group = pad_led_group_ref(group);
			break;
		}
	}

	if (button->group == NULL) {
		log_bug_libinput(pad->device->base.seat->libinput,
				 "Unable to match mode group %d with leds.\n",
				 group_index);
		free(button);
		return NULL;
	}

	return button;
}

static inline void
pad_mode_button_destroy(struct pad_mode_button* button)
{
	list_remove(&button->link);
	pad_led_group_unref(button->group);
	free(button);
}

static char *
pad_led_get_sysfs_base_path(struct evdev_device *device)
{
	struct udev_device *udev_device = device->udev_device,
			   *hid_device;
	const char *hid_sysfs_path;
	char path[PATH_MAX];
	char *base_path;
	int rc;

	hid_device = udev_device_get_parent_with_subsystem_devtype(udev_device,
								   "hid",
								   NULL);
	if (!hid_device)
		return NULL;

	hid_sysfs_path = udev_device_get_syspath(hid_device);

	rc = xasprintf(&base_path,
		       "%s/wacom_led",
		       hid_sysfs_path);
	if (rc == -1)
		return NULL;

	/* to check if the leds exist */
	rc = snprintf(path, sizeof(path), "%s/status_led0_select", base_path);
	if (rc == -1) {
		free(base_path);
		return NULL;
	}

	rc = access(path, R_OK|W_OK);
	if (rc == 0)
		return base_path;

	/* In theory we could return read-only LEDs here but let's make life
	 * simple and just return NULL and pretend we don't have LEDs. We
	 * can't change them anyway. Only the EKR has read-only LEDs but
	 * they're in a different sysfs path.
	 */
	if (errno != ENOENT)
		log_error(device->base.seat->libinput,
			  "Unable to access tablet LED syspath %s (%s)\n",
			  path,
			  strerror(errno));
	free(base_path);
	return NULL;
}

static int
pad_init_led_groups(struct pad_dispatch *pad,
		    struct evdev_device *device,
		    WacomDevice *wacom,
		    const char *syspath)
{
	struct libinput *libinput = device->base.seat->libinput;
	const WacomStatusLEDs *leds;
	int nleds, nmodes;
	int i;
	struct pad_led_group *group;

	leds = libwacom_get_status_leds(wacom, &nleds);
	if (nleds == 0)
		return 0;

	for (i = 0; i < nleds; i++) {
		switch(leds[i]) {
		case WACOM_STATUS_LED_UNAVAILABLE:
			log_bug_libinput(libinput,
					 "Invalid led type %d\n",
					 leds[i]);
			return 1;
		case WACOM_STATUS_LED_RING:
			/* ring is always group 0 */
			nmodes = libwacom_get_ring_num_modes(wacom);
			group = pad_group_new(pad, 0, nmodes, syspath);
			if (!group)
				return 1;
			list_insert(&pad->leds.led_list, &group->link);
			break;
		case WACOM_STATUS_LED_RING2:
			/* ring2 is always group 1 */
			nmodes = libwacom_get_ring2_num_modes(wacom);
			group = pad_group_new(pad, 1, nmodes, syspath);
			if (!group)
				return 1;
			list_insert(&pad->leds.led_list, &group->link);
			break;
		case WACOM_STATUS_LED_TOUCHSTRIP:
			nmodes = 1; /* something we know... */
			group = pad_group_new(pad, 0, nmodes, syspath);
			if (!group)
				return 1;
			list_insert(&pad->leds.led_list, &group->link);
			break;
		case WACOM_STATUS_LED_TOUCHSTRIP2:
			nmodes = 1; /* something we know... */
			group = pad_group_new(pad, 1, nmodes, syspath);
			if (!group)
				return 1;
			list_insert(&pad->leds.led_list, &group->link);
			break;
		default:
			break;
		}
	}

	return 0;
}

static int
pad_init_mode_buttons(struct pad_dispatch *pad,
		      WacomDevice *wacom)
{
	struct pad_mode_button *button;
	WacomButtonFlags flags;
	unsigned int mode;
	unsigned int group_idx;
	int i;

	/* libwacom numbers buttons as 'A', 'B', etc. We number them with 0,
	 * 1, ...
	 *
	 * We init every button as a mode button but most have a target mode
	 * of "none", i.e. nothing happens. This isn't as efficient as it
	 * could be but pad button press efficiency is not our biggest
	 * worry...
	 */
	for (i = 0; i < libwacom_get_num_buttons(wacom); i++) {
		flags = libwacom_get_button_flag(wacom, 'A' + i);

		if (flags & (WACOM_BUTTON_RING_MODESWITCH|WACOM_BUTTON_TOUCHSTRIP_MODESWITCH)) {
			group_idx = 0;
			mode = TARGET_MODE_NEXT;
		} else if (flags & (WACOM_BUTTON_RING2_MODESWITCH|WACOM_BUTTON_TOUCHSTRIP2_MODESWITCH)) {
			group_idx = 1;
			mode = TARGET_MODE_NEXT;
		} else if (flags & WACOM_BUTTON_POSITION_LEFT) {
			group_idx = 0;
			mode = TARGET_MODE_NONE;
		} else if (flags & WACOM_BUTTON_POSITION_RIGHT) {
			group_idx = 1;
			mode = TARGET_MODE_NONE;
		} else {
			continue;
		}

		button = pad_mode_button_new(pad, group_idx, i);
		if (button == NULL)
			return 1;

		button->target_mode = mode;
		list_insert(&pad->leds.mode_button_list, &button->link);
	}

	return 0;
}

static int
pad_init_mode_rings_strips(struct pad_dispatch *pad,
			   WacomDevice *wacom)
{
	struct pad_mode_button *button;
	int i;

	if (libwacom_has_ring(wacom)) {
		/* ring is index 0 group 0 */
		button = pad_mode_button_new(pad, 0, 0);
		if (button == NULL)
			return 1;

		button->target_mode = TARGET_MODE_NONE;
		list_insert(&pad->leds.mode_ring_list, &button->link);
	}

	if (libwacom_has_ring2(wacom)) {
		/* ring2 is index 1 group 1 */
		button = pad_mode_button_new(pad, 1, 1);
		if (button == NULL)
			return 1;

		button->target_mode = TARGET_MODE_NONE;
		list_insert(&pad->leds.mode_ring_list, &button->link);
	}

	/* We only get here if we have leds and all devices with LEDs
	 * have the strips in two different groups (21UX2) */
	for (i = 0; i < libwacom_get_num_strips(wacom); i++) {
		button = pad_mode_button_new(pad, i, 0);
		if (button == NULL)
			return 1;

		button->target_mode = TARGET_MODE_NONE;
		list_insert(&pad->leds.mode_strip_list, &button->link);
	}

	return 0;
}

static void
pad_init_leds_from_libwacom(struct pad_dispatch *pad,
			    struct evdev_device *device)
{
	struct libinput *libinput = device->base.seat->libinput;
	WacomDeviceDatabase *db = NULL;
	WacomDevice *wacom = NULL;
	char *syspath = NULL;
	bool success = false;

	db = libwacom_database_new();
	if (!db) {
		log_info(libinput,
			 "Failed to initialize libwacom context.\n");
		goto out;
	}

	wacom = libwacom_new_from_path(db,
				       udev_device_get_devnode(device->udev_device),
				       WFALLBACK_NONE,
				       NULL);
	if (!wacom)
		goto out;

	syspath = pad_led_get_sysfs_base_path(device);
	if (!syspath)
		goto out;

	if (pad_init_led_groups(pad, device, wacom, syspath) != 0)
		goto out;

	if (pad_init_mode_buttons(pad, wacom) != 0)
		goto out;

	if (pad_init_mode_rings_strips(pad, wacom) != 0)
		goto out;

	success = true;
out:
	if (syspath)
		free(syspath);
	if (wacom)
		libwacom_destroy(wacom);
	if (db)
		libwacom_database_destroy(db);

	if (!success)
		pad_destroy_leds(pad);
}

static inline int
pad_fetch_mode(struct pad_mode_button *button)
{
	struct pad_led_group *group = button->group;

	return group->current_mode;
}

static inline int
pad_update_leds(struct pad_mode_button *button)
{
	struct pad_led_group *group = button->group;

	if (button->target_mode == TARGET_MODE_NEXT)
		pad_led_group_set_next_mode(group);

	return pad_fetch_mode(button);
}

#endif /* HAVE_LIBWACOM */

void
pad_init_leds(struct pad_dispatch *pad,
	      struct evdev_device *device)
{
	list_init(&pad->leds.led_list);
	list_init(&pad->leds.mode_button_list);
	list_init(&pad->leds.mode_ring_list);
	list_init(&pad->leds.mode_strip_list);

#if HAVE_LIBWACOM
	pad_init_leds_from_libwacom(pad, device);
#endif
}

void
pad_destroy_leds(struct pad_dispatch *pad)
{
#if HAVE_LIBWACOM
	struct pad_led_group *group, *tmpgrp;
	struct pad_mode_button *button, *tmpbtn;

	list_for_each_safe(button, tmpbtn, &pad->leds.mode_button_list, link)
		pad_mode_button_destroy(button);
	list_for_each_safe(button, tmpbtn, &pad->leds.mode_ring_list, link)
		pad_mode_button_destroy(button);
	list_for_each_safe(button, tmpbtn, &pad->leds.mode_strip_list, link)
		pad_mode_button_destroy(button);
	list_for_each_safe(group, tmpgrp, &pad->leds.led_list, link)
		pad_led_group_unref(group);
#endif
}

unsigned int
pad_button_update_mode(struct pad_dispatch *pad,
		       unsigned int pressed_button,
		       enum libinput_button_state state)
{
	unsigned int mode = 0;

#if HAVE_LIBWACOM
	struct pad_mode_button *button;

	list_for_each(button, &pad->leds.mode_button_list, link) {
		if (button->button_index != pressed_button)
			continue;

		if (state == LIBINPUT_BUTTON_STATE_PRESSED)
			mode = pad_update_leds(button);
		else
			mode = pad_fetch_mode(button);
	}
#endif
	return mode;
}

unsigned int
pad_ring_update_mode(struct pad_dispatch *pad,
		     unsigned int ring_idx)
{
	unsigned int mode = 0;

#if HAVE_LIBWACOM
	struct pad_mode_button *button;

	list_for_each(button, &pad->leds.mode_ring_list, link) {
		if (button->button_index != ring_idx)
			continue;

		mode = pad_fetch_mode(button);
	}
#endif
	return mode;
}

unsigned int
pad_strip_update_mode(struct pad_dispatch *pad,
		      unsigned int strip_idx)
{
	unsigned int mode = 0;

#if HAVE_LIBWACOM
	struct pad_mode_button *button;

	list_for_each(button, &pad->leds.mode_strip_list, link) {
		if (button->button_index != strip_idx)
			continue;

		mode = pad_fetch_mode(button);
	}
#endif
	return mode;
}

unsigned int
evdev_device_tablet_pad_get_button_mode(struct evdev_device *device,
					unsigned int button_idx)
{
	struct pad_dispatch *pad = (struct pad_dispatch*)device->dispatch;
	struct pad_mode_button *button;
	unsigned int mode = 0;

	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return 0;

#if HAVE_LIBWACOM
	list_for_each(button, &pad->leds.mode_button_list, link) {
		if (button->button_index != button_idx)
			continue;

		mode = pad_fetch_mode(button);
	}
#endif

	return mode;
}

unsigned int
evdev_device_tablet_pad_get_ring_mode(struct evdev_device *device,
				      unsigned int ring_idx)
{
	struct pad_dispatch *pad = (struct pad_dispatch*)device->dispatch;
	struct pad_mode_button *button;
	unsigned int mode = 0;

	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return 0;

#if HAVE_LIBWACOM
	list_for_each(button, &pad->leds.mode_ring_list, link) {
		if (button->button_index != ring_idx)
			continue;

		mode = pad_fetch_mode(button);
	}
#endif

	return mode;
}

unsigned int
evdev_device_tablet_pad_get_strip_mode(struct evdev_device *device,
				      unsigned int strip_idx)
{
	struct pad_dispatch *pad = (struct pad_dispatch*)device->dispatch;
	struct pad_mode_button *button;
	unsigned int mode = 0;

	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return 0;

#if HAVE_LIBWACOM
	list_for_each(button, &pad->leds.mode_strip_list, link) {
		if (button->button_index != strip_idx)
			continue;

		mode = pad_fetch_mode(button);
	}
#endif

	return mode;
}

unsigned int
evdev_device_tablet_pad_get_button_mode_group(struct evdev_device *device,
					      unsigned int button_idx)
{
	struct pad_dispatch *pad = (struct pad_dispatch*)device->dispatch;
	struct pad_mode_button *button;
	unsigned int group = 0;

	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return 0;

#if HAVE_LIBWACOM
	list_for_each(button, &pad->leds.mode_button_list, link) {
		if (button->button_index != button_idx)
			continue;

		group = button->group->group;
	}
#endif

	return group;
}

unsigned int
evdev_device_tablet_pad_get_ring_mode_group(struct evdev_device *device,
					    unsigned int ring_idx)
{
	struct pad_dispatch *pad = (struct pad_dispatch*)device->dispatch;
	struct pad_mode_button *button;
	unsigned int group = 0;

	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return 0;

#if HAVE_LIBWACOM
	list_for_each(button, &pad->leds.mode_ring_list, link) {
		if (button->button_index != ring_idx)
			continue;

		group = button->group->group;
	}
#endif

	return group;
}

unsigned int
evdev_device_tablet_pad_get_strip_mode_group(struct evdev_device *device,
					     unsigned int strip_idx)
{
	struct pad_dispatch *pad = (struct pad_dispatch*)device->dispatch;
	struct pad_mode_button *button;
	unsigned int group = 0;

	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return 0;

#if HAVE_LIBWACOM
	list_for_each(button, &pad->leds.mode_strip_list, link) {
		if (button->button_index != strip_idx)
			continue;

		group = button->group->group;
	}
#endif

	return group;
}
