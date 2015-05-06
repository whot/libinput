/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
 * This list data structure is verbatim copy from wayland-util.h from the
 * Wayland project; except that wl_ prefix has been removed.
 */

#include "config.h"

#include <ctype.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "libinput-util.h"
#include "libinput-private.h"

void
list_init(struct list *list)
{
	list->prev = list;
	list->next = list;
}

void
list_insert(struct list *list, struct list *elm)
{
	elm->prev = list;
	elm->next = list->next;
	list->next = elm;
	elm->next->prev = elm;
}

void
list_remove(struct list *elm)
{
	elm->prev->next = elm->next;
	elm->next->prev = elm->prev;
	elm->next = NULL;
	elm->prev = NULL;
}

int
list_empty(const struct list *list)
{
	return list->next == list;
}

void
ratelimit_init(struct ratelimit *r, uint64_t ival_ms, unsigned int burst)
{
	r->interval = ival_ms;
	r->begin = 0;
	r->burst = burst;
	r->num = 0;
}

/*
 * Perform rate-limit test. Returns RATELIMIT_PASS if the rate-limited action
 * is still allowed, RATELIMIT_THRESHOLD if the limit has been reached with
 * this call, and RATELIMIT_EXCEEDED if you're beyond the threshold.
 * It's safe to treat the return-value as boolean, if you're not interested in
 * the exact state. It evaluates to "true" if the threshold hasn't been
 * exceeded, yet.
 *
 * The ratelimit object must be initialized via ratelimit_init().
 *
 * Modelled after Linux' lib/ratelimit.c by Dave Young
 * <hidave.darkstar@gmail.com>, which is licensed GPLv2.
 */
enum ratelimit_state
ratelimit_test(struct ratelimit *r)
{
	struct timespec ts;
	uint64_t mtime;

	if (r->interval <= 0 || r->burst <= 0)
		return RATELIMIT_PASS;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	mtime = ts.tv_sec * 1000 + ts.tv_nsec / 1000 / 1000;

	if (r->begin <= 0 || r->begin + r->interval < mtime) {
		/* reset counter */
		r->begin = mtime;
		r->num = 1;
		return RATELIMIT_PASS;
	} else if (r->num < r->burst) {
		/* continue burst */
		return (++r->num == r->burst) ? RATELIMIT_THRESHOLD
					      : RATELIMIT_PASS;
	}

	return RATELIMIT_EXCEEDED;
}

/* Helper function to parse the mouse DPI tag from udev.
 * The tag is of the form:
 * MOUSE_DPI=400 *1000 2000
 * or
 * MOUSE_DPI=400@125 *1000@125 2000@125
 * Where the * indicates the default value and @number indicates device poll
 * rate.
 * Numbers should be in ascending order, and if rates are present they should
 * be present for all entries.
 *
 * When parsing the mouse DPI property, if we find an error we just return 0
 * since it's obviously invalid, the caller will treat that as an error and
 * use a reasonable default instead. If the property contains multiple DPI
 * settings but none flagged as default, we return the last because we're
 * lazy and that's a silly way to set the property anyway.
 *
 * @param prop The value of the udev property (without the MOUSE_DPI=)
 * @return The default dpi value on success, 0 on error
 */
int
parse_mouse_dpi_property(const char *prop)
{
	bool is_default = false;
	int nread, dpi = 0, rate;

	while (*prop != 0) {
		if (*prop == ' ') {
			prop++;
			continue;
		}
		if (*prop == '*') {
			prop++;
			is_default = true;
			if (!isdigit(prop[0]))
				return 0;
		}

		/* While we don't do anything with the rate right now we
		 * will validate that, if it's present, it is non-zero and
		 * positive
		 */
		rate = 1;
		nread = 0;
		sscanf(prop, "%d@%d%n", &dpi, &rate, &nread);
		if (!nread)
			sscanf(prop, "%d%n", &dpi, &nread);
		if (!nread || dpi <= 0 || rate <= 0 || prop[nread] == '@')
			return 0;

		if (is_default)
			break;
		prop += nread;
	}
	return dpi;
}

/**
 * Helper function to parse the MOUSE_WHEEL_CLICK_ANGLE property from udev.
 * Property is of the form:
 * MOUSE_WHEEL_CLICK_ANGLE=<integer>
 * Where the number indicates the degrees travelled for each click.
 *
 * We skip preceding whitespaces and parse the first number seen. If
 * multiple numbers are specified, we ignore those.
 *
 * @param prop The value of the udev property (without the MOUSE_WHEEL_CLICK_ANGLE=)
 * @return The angle of the wheel (may be negative) or 0 on error.
 */
int
parse_mouse_wheel_click_angle_property(const char *prop)
{
	int angle = 0,
	    nread = 0;

	while(*prop != 0 && *prop == ' ')
		prop++;

	sscanf(prop, "%d%n", &angle, &nread);
	if (nread == 0 || angle == 0 || abs(angle) > 360)
		return 0;
	if (prop[nread] != ' ' && prop[nread] != '\0')
		return 0;

        return angle;
}

/**
 * Helper function to parse the TRACKPOINT_CONST_ACCEL property from udev.
 * Property is of the form:
 * TRACKPOINT_CONST_ACCEL=<float>
 *
 * @param prop The value of the udev property (without the TRACKPOINT_CONST_ACCEL=)
 * @return The acceleration, or 0.0 on error.
 */
double
parse_trackpoint_accel_property(const char *prop)
{
	locale_t c_locale;
	double accel;
	char *endp;

	/* Create a "C" locale to force strtod to use '.' as separator */
	c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
	if (c_locale == (locale_t)0)
		return 0.0;

	accel = strtod_l(prop, &endp, c_locale);

	freelocale(c_locale);

	if (*endp != '\0')
		return 0.0;

	return accel;
}

enum udev_status
udev_device_from_devnode(struct udev *udev,
			 const char *devnode,
			 struct udev_device **device_return)
{
	struct udev_device *dev;
	struct stat st;
	size_t count = 0;
	enum udev_status rc = UDEV_DEVICE_SUCCESS;

	if (stat(devnode, &st) < 0)
		return UDEV_DEVICE_INVALID;

	dev = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

	while (dev && !udev_device_get_is_initialized(dev)) {
		udev_device_unref(dev);
		msleep(10);
		dev = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

		count++;
		if (count > 10) {
			rc = UDEV_DEVICE_TIMEOUT;
			break;
		}
	}

	if (!dev)
		rc = UDEV_DEVICE_INVALID;
	else
		*device_return = dev;

	return rc;

}
