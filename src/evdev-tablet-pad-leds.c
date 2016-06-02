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

struct pad_led_group {
	struct libinput_tablet_pad_mode_group base;
};

static void
pad_led_group_destroy(struct libinput_tablet_pad_mode_group *g)
{
	struct pad_led_group *group = (struct pad_led_group *)g;

	free(group);
}

static struct pad_led_group *
pad_group_new_basic(struct pad_dispatch *pad,
		    unsigned int group_index,
		    int nleds)
{
	struct pad_led_group *group;

	group = zalloc(sizeof *group);
	if (!group)
		return NULL;

	group->base.device = &pad->device->base;
	group->base.refcount = 1;
	group->base.index = group_index;
	group->base.current_mode = 0;
	group->base.num_modes = nleds;
	group->base.destroy = pad_led_group_destroy;

	return group;
}

static inline struct libinput_tablet_pad_mode_group *
pad_get_mode_group(struct pad_dispatch *pad, unsigned int index)
{
	struct libinput_tablet_pad_mode_group *group;

	list_for_each(group, &pad->modes.mode_group_list, link) {
		if (group->index == index)
			return group;
	}

	return NULL;
}

static int
pad_init_fallback_group(struct pad_dispatch *pad)
{
	struct pad_led_group *group;

	group = pad_group_new_basic(pad, 0, 1);
	if (!group)
		return 1;

	/* If we only have one group, all buttons/strips/rings are part of
	 * that group. We rely on the other layers to filter out invalid
	 * indices */
	group->base.button_mask = -1;
	group->base.strip_mask = -1;
	group->base.ring_mask = -1;
	group->base.toggle_button_mask = 0;

	list_insert(&pad->modes.mode_group_list, &group->base.link);

	return 0;
}

int
pad_init_leds(struct pad_dispatch *pad,
	      struct evdev_device *device)
{
	int rc = 1;

	list_init(&pad->modes.mode_group_list);

	if (pad->nbuttons > 32) {
		log_bug_libinput(device->base.seat->libinput,
				 "Too many pad buttons for modes %d\n",
				 pad->nbuttons);
		return rc;
	}

	/* Eventually we slot the libwacom-based led detection in here. That
	 * requires getting the kernel ready first. For now we just init the
	 * fallback single-mode group.
	 */
	rc = pad_init_fallback_group(pad);

	return rc;
}

void
pad_destroy_leds(struct pad_dispatch *pad)
{
	struct libinput_tablet_pad_mode_group *group, *tmpgrp;

	list_for_each_safe(group, tmpgrp, &pad->modes.mode_group_list, link)
		libinput_tablet_pad_mode_group_unref(group);
}

void
pad_button_update_mode(struct libinput_tablet_pad_mode_group *g,
		       unsigned int button_index,
		       enum libinput_button_state state)
{
	struct pad_led_group *group = (struct pad_led_group*)g;

	if (state != LIBINPUT_BUTTON_STATE_PRESSED)
		return;

	if (!libinput_tablet_pad_mode_group_button_is_toggle(g, button_index))
		return;

	log_bug_libinput(group->base.device->seat->libinput,
			 "Button %d should not be a toggle button",
			 button_index);
}

int
evdev_device_tablet_pad_get_num_mode_groups(struct evdev_device *device)
{
	struct pad_dispatch *pad = (struct pad_dispatch*)device->dispatch;
	struct libinput_tablet_pad_mode_group *group;
	int num_groups = 0;

	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return -1;

	list_for_each(group, &pad->modes.mode_group_list, link)
		num_groups++;

	return num_groups;
}

struct libinput_tablet_pad_mode_group *
evdev_device_tablet_pad_get_mode_group(struct evdev_device *device,
				       unsigned int index)
{
	struct pad_dispatch *pad = (struct pad_dispatch*)device->dispatch;

	if (!(device->seat_caps & EVDEV_DEVICE_TABLET_PAD))
		return NULL;

	return pad_get_mode_group(pad, index);
}
