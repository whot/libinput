/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 * Copyright © 2013-2015 Red Hat, Inc.
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
	assert((list->next != NULL && list->prev != NULL) ||
	       !"list->next|prev is NULL, possibly missing list_init()");
	assert(((elm->next == NULL && elm->prev == NULL) || list_empty(elm)) ||
	       !"elm->next|prev is not NULL, list node used twice?");

	elm->prev = list;
	elm->next = list->next;
	list->next = elm;
	elm->next->prev = elm;
}

void
list_append(struct list *list, struct list *elm)
{
	assert((list->next != NULL && list->prev != NULL) ||
	       !"list->next|prev is NULL, possibly missing list_init()");
	assert(((elm->next == NULL && elm->prev == NULL) || list_empty(elm)) ||
	       !"elm->next|prev is not NULL, list node used twice?");

	elm->next = list;
	elm->prev = list->prev;
	list->prev = elm;
	elm->prev->next = elm;
}

void
list_remove(struct list *elm)
{
	assert((elm->next != NULL && elm->prev != NULL) ||
	       !"list->next|prev is NULL, possibly missing list_init()");

	elm->prev->next = elm->next;
	elm->next->prev = elm->prev;
	elm->next = NULL;
	elm->prev = NULL;
}

bool
list_empty(const struct list *list)
{
	assert((list->next != NULL && list->prev != NULL) ||
	       !"list->next|prev is NULL, possibly missing list_init()");

	return list->next == list;
}

void
ratelimit_init(struct ratelimit *r, uint64_t ival_us, unsigned int burst)
{
	r->interval = ival_us;
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
	uint64_t utime;

	if (r->interval <= 0 || r->burst <= 0)
		return RATELIMIT_PASS;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	utime = s2us(ts.tv_sec) + ns2us(ts.tv_nsec);

	if (r->begin <= 0 || r->begin + r->interval < utime) {
		/* reset counter */
		r->begin = utime;
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

	if (!prop)
		return 0;

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
 * Helper function to parse the MOUSE_WHEEL_CLICK_COUNT property from udev.
 * Property is of the form:
 * MOUSE_WHEEL_CLICK_COUNT=<integer>
 * Where the number indicates the number of wheel clicks per 360 deg
 * rotation.
 *
 * @param prop The value of the udev property (without the MOUSE_WHEEL_CLICK_COUNT=)
 * @return The click count of the wheel (may be negative) or 0 on error.
 */
int
parse_mouse_wheel_click_count_property(const char *prop)
{
	int count = 0;

	if (!prop)
		return 0;

	if (!safe_atoi(prop, &count) || abs(count) > 360)
		return 0;

        return count;
}

/**
 *
 * Helper function to parse the MOUSE_WHEEL_CLICK_ANGLE property from udev.
 * Property is of the form:
 * MOUSE_WHEEL_CLICK_ANGLE=<integer>
 * Where the number indicates the degrees travelled for each click.
 *
 * @param prop The value of the udev property (without the MOUSE_WHEEL_CLICK_ANGLE=)
 * @return The angle of the wheel (may be negative) or 0 on error.
 */
int
parse_mouse_wheel_click_angle_property(const char *prop)
{
	int angle = 0;

	if (!prop)
		return 0;

	if (!safe_atoi(prop, &angle) || abs(angle) > 360)
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
	double accel;

	if (!prop)
		return 0.0;

	if (!safe_atod(prop, &accel))
		accel = 0.0;

	return accel;
}

/**
 * Parses a simple dimension string in the form of "10x40". The two
 * numbers must be positive integers in decimal notation.
 * On success, the two numbers are stored in w and h. On failure, w and h
 * are unmodified.
 *
 * @param prop The value of the property
 * @param w Returns the first component of the dimension
 * @param h Returns the second component of the dimension
 * @return true on success, false otherwise
 */
bool
parse_dimension_property(const char *prop, size_t *w, size_t *h)
{
	int x, y;

	if (!prop)
		return false;

	if (sscanf(prop, "%dx%d", &x, &y) != 2)
		return false;

	if (x < 0 || y < 0)
		return false;

	*w = (size_t)x;
	*h = (size_t)y;
	return true;
}

/**
 * Parses a set of 6 space-separated floats.
 *
 * @param prop The string value of the property
 * @param calibration Returns the six components
 * @return true on success, false otherwise
 */
bool
parse_calibration_property(const char *prop, float calibration_out[6])
{
	int idx;
	char **strv;
	float calibration[6];

	if (!prop)
		return false;

	strv = strv_from_string(prop, " ");
	if (!strv)
		return false;

	for (idx = 0; idx < 6; idx++) {
		double v;
		if (strv[idx] == NULL || !safe_atod(strv[idx], &v)) {
			strv_free(strv);
			return false;
		}

		calibration[idx] = v;
	}

	strv_free(strv);

	memcpy(calibration_out, calibration, sizeof(calibration));

	return true;
}

bool
parse_switch_reliability_property(const char *prop,
				  enum switch_reliability *reliability)
{
	if (!prop) {
		*reliability = RELIABILITY_UNKNOWN;
		return true;
	}

	if (streq(prop, "reliable"))
		*reliability = RELIABILITY_RELIABLE;
	else if (streq(prop, "write_open"))
		*reliability = RELIABILITY_WRITE_OPEN;
	else
		return false;

	return true;
}

/**
 * Parses a string with the allowed values: "below"
 * The value refers to the position of the touchpad (relative to the
 * keyboard, i.e. your average laptop would be 'below')
 *
 * @param prop The value of the property
 * @param layout The layout
 * @return true on success, false otherwise
 */
bool
parse_tpkbcombo_layout_poperty(const char *prop,
			       enum tpkbcombo_layout *layout)
{
	if (!prop)
		return false;

	if (streq(prop, "below")) {
		*layout = TPKBCOMBO_LAYOUT_BELOW;
		return true;
	}

	return false;
}

/**
 * Parses a string of the format "a:b" where both a and b must be integer
 * numbers and a > b. Also allowed is the special string vaule "none" which
 * amounts to unsetting the property.
 *
 * @param prop The value of the property
 * @param hi Set to the first digit or 0 in case of 'none'
 * @param lo Set to the second digit or 0 in case of 'none'
 * @return true on success, false otherwise
 */
bool
parse_range_property(const char *prop, int *hi, int *lo)
{
	int first, second;

	if (!prop)
		return false;

	if (streq(prop, "none")) {
		*hi = 0;
		*lo = 0;
		return true;
	}

	if (sscanf(prop, "%d:%d", &first, &second) != 2)
		return false;

	if (second >= first)
		return false;

	*hi = first;
	*lo = second;

	return true;
}

/**
 * Helper function to parse the LIBINPUT_ATTR_PALM_PRESSURE_THRESHOLD
 * property from udev. Property is of the form:
 * LIBINPUT_ATTR_PALM_PRESSURE_THRESHOLD=<integer>
 * Where the number indicates the minimum threshold to consider a touch to
 * be a palm.
 *
 * @param prop The value of the udev property (without the *
 * LIBINPUT_ATTR_PALM_PRESSURE_THRESHOLD=)
 * @return The pressure threshold or 0 on error
 */
int
parse_palm_pressure_property(const char *prop)
{
	int threshold = 0;

	if (!prop)
		return 0;

	if (!safe_atoi(prop, &threshold) || threshold < 0)
		return 0;

        return threshold;
}

/**
 * Helper function to parse the LIBINPUT_ATTR_PALM_SIZE_THRESHOLD property
 * from udev. Property is of the form:
 * LIBINPUT_ATTR_PALM_SIZE_THRESHOLD=<integer>
 * Where the number indicates the minimum threshold to consider a touch to
 * be a palm.
 *
 * @param prop The value of the udev property (without the
 * LIBINPUT_ATTR_PALM_SIZE_THRESHOLD=)
 * @return The pressure threshold or 0 on error
 */
int
parse_palm_size_property(const char *prop)
{
	int thr = 0;

	if (!prop)
		return 0;

	if (!safe_atoi(prop, &thr) || thr < 0 || thr > 2028)
		return 0;

        return thr;
}

/**
 * Helper function to parse the LIBINPUT_ATTR_THUMB_PRESSURE_THRESHOLD
 * property from udev. Property is of the form:
 * LIBINPUT_ATTR_THUMB_PRESSURE_THRESHOLD=<integer>
 * Where the number indicates the minimum threshold to consider a touch to
 * be a thumb.
 *
 * @param prop The value of the udev property (without the
 * LIBINPUT_ATTR_THUMB_PRESSURE_THRESHOLD=)
 * @return The pressure threshold or 0 on error
 */
int
parse_thumb_pressure_property(const char *prop)
{
	int threshold = 0;

	if (!prop)
		return 0;

	if (!safe_atoi(prop, &threshold) || threshold < 0)
		return 0;

        return threshold;
}

/**
 * Return the next word in a string pointed to by state before the first
 * separator character. Call repeatedly to tokenize a whole string.
 *
 * @param state Current state
 * @param len String length of the word returned
 * @param separators List of separator characters
 *
 * @return The first word in *state, NOT null-terminated
 */
static const char *
next_word(const char **state, size_t *len, const char *separators)
{
	const char *next = *state;
	size_t l;

	if (!*next)
		return NULL;

	next += strspn(next, separators);
	if (!*next) {
		*state = next;
		return NULL;
	}

	l = strcspn(next, separators);
	*state = next + l;
	*len = l;

	return next;
}

/**
 * Return a null-terminated string array with the tokens in the input
 * string, e.g. "one two\tthree" with a separator list of " \t" will return
 * an array [ "one", "two", "three", NULL ].
 *
 * Use strv_free() to free the array.
 *
 * @param in Input string
 * @param separators List of separator characters
 *
 * @return A null-terminated string array or NULL on errors
 */
char **
strv_from_string(const char *in, const char *separators)
{
	const char *s, *word;
	char **strv = NULL;
	int nelems = 0, idx;
	size_t l;

	assert(in != NULL);

	s = in;
	while ((word = next_word(&s, &l, separators)) != NULL)
	       nelems++;

	if (nelems == 0)
		return NULL;

	nelems++; /* NULL-terminated */
	strv = zalloc(nelems * sizeof *strv);

	idx = 0;

	s = in;
	while ((word = next_word(&s, &l, separators)) != NULL) {
		char *copy = strndup(word, l);
		if (!copy) {
			strv_free(strv);
			return NULL;
		}

		strv[idx++] = copy;
	}

	return strv;
}

/**
 * Return a newly allocated string with all elements joined by the
 * joiner, same as Python's string.join() basically.
 * A strv of ["one", "two", "three", NULL] with a joiner of ", " results
 * in "one, two, three".
 *
 * An empty strv ([NULL]) returns NULL, same for passing NULL as either
 * argument.
 *
 * @param strv Input string arrray
 * @param joiner Joiner between the elements in the final string
 *
 * @return A null-terminated string joining all elements
 */
char *
strv_join(char **strv, const char *joiner)
{
	char **s;
	char *str;
	size_t slen = 0;
	size_t count = 0;

	if (!strv || !joiner)
		return NULL;

	for (s = strv, count = 0; *s; s++, count++) {
		slen += strlen(*s);
	}

	assert(slen < 1000);

	slen += (count - 1) * strlen(joiner);

	str = zalloc(slen + 1); /* trailing \0 */
	for (s = strv; *s; s++) {
		strcat(str, *s);
		--count;
		if (count > 0)
			strcat(str, joiner);
	}

	return str;
}
