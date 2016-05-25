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

#include <dirent.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "evdev-tablet-pad.h"

#if HAVE_LIBWACOM
#include <libwacom/libwacom.h>

struct pad_led_group {
	struct libinput *libinput;
	int refcount;
	unsigned int group;

	char *led_status_path;
	char *led_luminance_path;
	int led_status_fd;
	int led_luminance_fd;
};

struct pad_led {
	struct libinput_tablet_pad_led base;
	struct pad_dispatch *pad;

	struct pad_led_group *group;
};

static struct pad_led_group *
pad_led_group_ref(struct pad_led_group *group)
{
	assert(group->refcount >= 1);
	group->refcount++;
	return group;
}

static struct pad_led_group *
pad_led_group_unref(struct pad_led_group *group)
{
	assert(group->refcount >= 1);

	group->refcount--;
	if (group->refcount > 0)
		return group;

	close_restricted(group->libinput, group->led_status_fd);
	close_restricted(group->libinput, group->led_luminance_fd);

	free(group->led_status_path);
	free(group->led_luminance_path);
	free(group);
	return NULL;
}

static int
pad_led_set_brightness(struct libinput_tablet_pad_led *libinput_led,
		       double brightness)
{
	struct pad_led *led = (struct pad_led*)libinput_led;
	char buf[4] = {0};
	int b;

	/* FIXME: how to turn the LEDs off? */
	if (brightness == 0.0)
		return 0;

	sprintf(buf, "%d", led->base.index);
	write(led->group->led_status_fd, buf, strlen(buf));
	fsync(led->group->led_status_fd);

	b = 127 * brightness;
	sprintf(buf, "%d", b);
	write(led->group->led_luminance_fd, buf, strlen(buf));
	fsync(led->group->led_luminance_fd);

	/* FIXME: for the EKR we should listen to the button event and
	 * read from sysfs whenever we get one */

	return 0;
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
	 * can't change them anyway.
	 */
	if (errno != ENOENT)
		log_error(device->base.seat->libinput,
			  "Unable to access tablet LED syspath %s (%s)\n",
			  path,
			  strerror(errno));
	free(base_path);
	return NULL;
}

static char *
pad_led_get_ekr_sysfs_mode_file(struct evdev_device *device)
{
	struct udev_device *udev_device = device->udev_device,
			   *hid_device;
	const char *hid_sysfs_path;
	char path[PATH_MAX];
	char *mode_path;
	DIR *dirp;
	struct dirent *dp;
	int serial = 0;
	int rc;

	hid_device = udev_device_get_parent_with_subsystem_devtype(udev_device,
								   "hid",
								   NULL);
	if (!hid_device)
		return NULL;

	hid_sysfs_path = udev_device_get_syspath(hid_device);

	rc = snprintf(path, sizeof(path), "%s/wacom_remote/", hid_sysfs_path);
	if (rc == -1)
		return NULL;

	dirp = opendir(hid_sysfs_path);
	if (!dirp)
		return NULL;

	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.')
			continue;

		/* There should only be one entry per remote, the remote's
		 * serial in decimal notation.
		 */
		if (safe_atoi(dp->d_name, &serial))
			break;;
	}
	closedir(dirp);

	if (serial == 0)
		return NULL;

	rc = xasprintf(&mode_path,
		       "%s/wacom_remote/%d/remote_mode",
		       hid_sysfs_path,
		       serial);
	if (rc == -1)
		return NULL;

	rc = access(mode_path, R_OK);
	if (rc == 0)
		return mode_path;

	/* In theory we could return read-only LEDs here but let's make life
	 * simple and just return NULL and pretend we don't have LEDs. We
	 * can't change them anyway.
	 */
	if (errno != ENOENT)
		log_error(device->base.seat->libinput,
			  "Unable to access EKR LED syspath %s (%s)\n",
			  mode_path,
			  strerror(errno));
	free(mode_path);
	return NULL;
}

static void
pad_led_destroy(struct pad_led *led)
{
	list_remove(&led->base.link);
	pad_led_group_unref(led->group);
	free(led);
}

static struct pad_led_group *
pad_get_led_group(struct pad_dispatch *pad,
		  unsigned int group_index,
		  const char *syspath)
{
	struct libinput *libinput = pad->device->base.seat->libinput;
	struct pad_led *led;
	struct pad_led_group *group;
	int rc;

	list_for_each(led, &pad->led_list, base.link) {
		if (led->group->group == group_index)
			return pad_led_group_ref(led->group);
	}

	group = zalloc(sizeof *group);
	if (!group)
		return NULL;

	group->refcount = 1;
	group->group = group_index;
	group->libinput = libinput;
	group->led_status_fd = -1;
	group->led_luminance_fd = -1;

	/* FIXME: EKR LEDs are write-only and in wacom_remote/ */
	rc = xasprintf(&group->led_status_path,
		       "%s/status_led%d_select",
		       syspath,
		       group_index);
	if (rc == -1)
		goto error;

	rc = xasprintf(&group->led_luminance_path,
		       "%s/status%d_luminance",
		       syspath,
		       group_index);
	if (rc == -1)
		goto error;

	group->led_status_fd = open_restricted(libinput,
					       group->led_status_path,
					       O_RDWR);
	group->led_luminance_fd = open_restricted(libinput,
						  group->led_luminance_path,
						  O_RDWR);
	if (group->led_status_fd < 0 || group->led_luminance_fd < 0)
		goto error;

	/* if O_RDWR fails we could try to open with O_RDONLY and make the
	 * LED readonly. But it's likely this is a setup issue, so just fail
	 * LED init and pretend we don't have any */

	return group;

error:
	log_error(libinput, "Unable to init LED group: %s\n", strerror(errno));

	close_restricted(libinput, group->led_status_fd);
	close_restricted(libinput, group->led_luminance_fd);
	free(group->led_status_path);
	free(group->led_luminance_path);
	free(group);

	return NULL;
}

static struct pad_led *
pad_init_one_led(struct pad_dispatch *pad,
		 unsigned int group,
		 int idx)
{
	struct pad_led *led;

	led = zalloc(sizeof *led);
	if (!led)
		return NULL;

	led->base.refcount = 1;
	led->base.group = group;
	led->base.index = idx;

	list_insert(&pad->led_list, &led->base.link);

	return led;
}

static void
pad_init_ekr_leds(struct pad_dispatch *pad,
		  struct evdev_device *device)
{
	char *syspath = NULL;
	const int nleds = 3; /* The EKR has 3 read-only LEDs */
	int i;

	syspath = pad_led_get_ekr_sysfs_mode_file(device);
	if (!syspath)
		return;

	for (i = 0; i < nleds; i++) {
		struct pad_led *led = pad_init_one_led(pad, 0, i);
		if (!led)
			return;

		led->base.set_brightness = NULL;
	}
}

static void
pad_init_one_normal_led(struct pad_dispatch *pad,
			unsigned int group,
			int idx,
			const char *syspath)
{
	struct pad_led *led;

	led = pad_init_one_led(pad, group, idx);
	if (!led)
		return;

	led->group = pad_get_led_group(pad, group, syspath);
	if (!led->group)
		goto error;

	led->base.set_brightness = pad_led_set_brightness;
	led->base.capabilities = LIBINPUT_TABLET_PAD_LED_CAP_WRITABLE;
	/* FIXME: need to get the brightness ranges from libwacom? */
	led->base.capabilities |= LIBINPUT_TABLET_PAD_LED_CAP_BRIGHTNESS;

	return;

error:
	pad_led_destroy(led);
}

static void
pad_init_normal_leds(struct pad_dispatch *pad,
		     struct evdev_device *device,
		     WacomDevice *wacom)
{
	struct libinput *libinput = device->base.seat->libinput;
	const WacomStatusLEDs *leds;
	char *syspath = NULL;
	int nleds, nmodes;
	int i;

	syspath = pad_led_get_sysfs_base_path(device);
	if (!syspath)
		return;

	leds = libwacom_get_status_leds(wacom, &nleds);
	for (i = 0; i < nleds; i++) {
		switch(leds[i]) {
		case WACOM_STATUS_LED_UNAVAILABLE:
			log_bug_libinput(libinput,
					 "Invalid led type %d\n",
					 leds[i]);
			goto out;
		case WACOM_STATUS_LED_RING:
			nmodes = libwacom_get_ring_num_modes(wacom);
			/* ring is always group 0 */
			while (nmodes--)
				pad_init_one_normal_led(pad,
							0,
							nmodes,
							syspath);
			break;
		case WACOM_STATUS_LED_RING2:
			nmodes = libwacom_get_ring2_num_modes(wacom);
			/* ring2 is always group 1 */
			while (nmodes--)
				pad_init_one_normal_led(pad,
							1,
							nmodes,
							syspath);
			break;
		/* FIXME: handle touchstrip LEDs? */
		default:
			break;
		}
	}

out:
	free(syspath);
}

static void
pad_init_leds_from_libwacom(struct pad_dispatch *pad,
			    struct evdev_device *device)
{
	struct libinput *libinput = device->base.seat->libinput;
	WacomDeviceDatabase *db = NULL;
	WacomDevice *wacom = NULL;

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

	if (libwacom_get_class(wacom) == WCLASS_REMOTE) {
		pad_init_ekr_leds(pad, device);
	} else
		pad_init_normal_leds(pad, device, wacom);

out:
	if (wacom)
		libwacom_destroy(wacom);
	if (db)
		libwacom_database_destroy(db);
}

#endif /* HAVE_LIBWACOM */

void
pad_init_leds(struct pad_dispatch *pad,
	      struct evdev_device *device)
{
	list_init(&pad->led_list);

#if HAVE_LIBWACOM
	pad_init_leds_from_libwacom(pad, device);
#endif
}

void
pad_destroy_leds(struct pad_dispatch *pad)
{
#if HAVE_LIBWACOM
	struct pad_led *led, *tmp;

	list_for_each_safe(led, tmp, &pad->led_list, base.link)
		pad_led_destroy(led);
#endif
}
