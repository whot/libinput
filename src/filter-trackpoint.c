/*
 * Copyright © 2006-2009 Simon Thum
 * Copyright © 2012 Jonas Ådahl
 * Copyright © 2014-2015 Red Hat, Inc.
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>

#include "filter.h"
#include "libinput-util.h"
#include "filter-private.h"

/* Trackpoint acceleration */
#define TRACKPOINT_DEFAULT_MAX_ACCEL 2.0	/* in units/us */
#define TRACKPOINT_DEFAULT_MAX_DELTA 60
/* As measured on a Lenovo T440 at kernel-default sensitivity 128 */
#define TRACKPOINT_DEFAULT_RANGE 20		/* max value */

struct tablet_accelerator_flat {
	struct motion_filter base;

	double factor;
	int xres, yres;
	double xres_scale, /* 1000dpi : tablet res */
	       yres_scale; /* 1000dpi : tablet res */
};

struct trackpoint_accelerator {
	struct motion_filter base;

	struct device_float_coords history[4];
	size_t history_size;

	double scale_factor;
	double max_accel;
	double max_delta;

	double incline; /* incline of the function */
	double offset; /* offset of the function */
};

double
trackpoint_accel_profile(struct motion_filter *filter,
			 void *data,
			 double delta)
{
	struct trackpoint_accelerator *accel_filter =
		(struct trackpoint_accelerator *)filter;
	const double max_accel = accel_filter->max_accel;
	double factor;

	delta = fabs(delta);

	/* This is almost the equivalent of the xserver acceleration
	   at sensitivity 128 and speed 0.0 */
	factor = delta * accel_filter->incline + accel_filter->offset;
	factor = min(factor, max_accel);

	return factor;
}

/**
 * Average the deltas, they are messy and can provide sequences like 7, 7,
 * 9, 8, 14, 7, 9, 8 ... The outliers cause unpredictable jumps, so average
 * them out.
 */
static inline struct device_float_coords
trackpoint_average_delta(struct trackpoint_accelerator *filter,
			 const struct device_float_coords *unaccelerated)
{
	size_t i;
	struct device_float_coords avg = {0};

	memmove(&filter->history[1],
		&filter->history[0],
		sizeof(*filter->history) * (filter->history_size - 1));
	filter->history[0] = *unaccelerated;

	for (i = 0; i < filter->history_size; i++) {
		avg.x += filter->history[i].x;
		avg.y += filter->history[i].y;
	}
	avg.x /= filter->history_size;
	avg.y /= filter->history_size;

	return avg;
}

/**
 * Undo any system-wide magic scaling, so we're behaving the same regardless
 * of the trackpoint hardware. This way we can apply our profile independent
 * of any other configuration that messes with things.
 */
static inline struct device_float_coords
trackpoint_normalize_deltas(const struct trackpoint_accelerator *accel_filter,
			    const struct device_float_coords *delta)
{
	struct device_float_coords scaled = *delta;

	scaled.x *= accel_filter->scale_factor;
	scaled.y *= accel_filter->scale_factor;

	return scaled;
}

/**
 * We set a max delta per event, to avoid extreme jumps once we exceed the
 * expected pressure. Trackpoint hardware is inconsistent once the pressure
 * gets high, so we can expect sequences like 30, 40, 35, 55, etc. This may
 * be caused by difficulty keeping up high consistent pressures or just
 * measuring errors in the hardware. Either way, we cap to a max delta so
 * once we hit the high pressures, movement is capped and consistent.
 */
static inline struct normalized_coords
trackpoint_clip_to_max_delta(const struct trackpoint_accelerator *accel_filter,
			     struct normalized_coords coords)
{
	const double max_delta = accel_filter->max_delta;

	if (abs(coords.x) > max_delta)
		coords.x = copysign(max_delta, coords.x);
	if (abs(coords.y) > max_delta)
		coords.y = copysign(max_delta, coords.y);

	return coords;
}

static struct normalized_coords
trackpoint_accelerator_filter(struct motion_filter *filter,
			      const struct device_float_coords *unaccelerated,
			      void *data, uint64_t time)
{
	struct trackpoint_accelerator *accel_filter =
		(struct trackpoint_accelerator *)filter;
	struct device_float_coords scaled;
	struct device_float_coords avg;
	struct normalized_coords coords;
	double f;
	double delta;

	scaled = trackpoint_normalize_deltas(accel_filter, unaccelerated);
	avg = trackpoint_average_delta(accel_filter, &scaled);

	delta = hypot(avg.x, avg.y);

	f = trackpoint_accel_profile(filter, data, delta);

	coords.x = avg.x * f;
	coords.y = avg.y * f;

	coords = trackpoint_clip_to_max_delta(accel_filter, coords);

	return coords;
}

static struct normalized_coords
trackpoint_accelerator_filter_noop(struct motion_filter *filter,
				   const struct device_float_coords *unaccelerated,
				   void *data, uint64_t time)
{

	struct trackpoint_accelerator *accel_filter =
		(struct trackpoint_accelerator *)filter;
	struct device_float_coords scaled;
	struct device_float_coords avg;
	struct normalized_coords coords;

	scaled = trackpoint_normalize_deltas(accel_filter, unaccelerated);
	avg = trackpoint_average_delta(accel_filter, &scaled);

	coords.x = avg.x;
	coords.y = avg.y;

	coords = trackpoint_clip_to_max_delta(accel_filter, coords);

	return coords;
}

static bool
trackpoint_accelerator_set_speed(struct motion_filter *filter,
				 double speed_adjustment)
{
	struct trackpoint_accelerator *accel_filter =
		(struct trackpoint_accelerator*)filter;
	double incline, offset, max;

	assert(speed_adjustment >= -1.0 && speed_adjustment <= 1.0);

	/* Helloooo, magic numbers.

	   These numbers were obtained by finding an acceleration curve that
	   provides precision at slow speeds but still provides a good
	   acceleration at higher pressure - and a quick ramp-up to that
	   acceleration.

	   Trackpoints have built-in acceleration curves already, so we
	   don't put a new function on top, we merely scale the output from
	   those curves (re-calculating the pressure values from the
	   firmware-defined curve and applying a new curve is unreliable).

	   For that basic scaling, we assume a constant factor f based on
	   the speed setting together with a maximum factor m (for this
	   speed setting). Delta acceleration is thus:
	      factor = max(m, f)
	      accelerated_delta = delta * factor;

	   Trial and error showed a couple of pairs that work well for the
	   various speed settings (Lenovo T440, sensitivity 128):

	       -1.0: f = 0.3, m = 1
	       -0.5: f = 0.6, m = 2
	        0.0: f = 1.0, m = 6
	        0.5: f = 1.4, m = 8
	        1.0: f = 1.9, m = 15

	   Note: if f >= 2.0, some pixels are unaddressable

	   Those pairs were fed into the linear/exponential regression tool
	   at http://www.xuru.org/rt/LR.asp and show two functions that map
	   speed settings to the respective f and m.
	   Given a speed setting s in [-1.0, 1.0]
		   f(s) = 0.8 * s + 1.04
		   m(s) = 4.6 * e**(1.2 * s)
	   These are close enough to the tested pairs.
	*/

	max = 4.6 * pow(M_E, 1.2 * speed_adjustment);
	incline = 0.8 * speed_adjustment + 1.04;
	offset = 0;

	accel_filter->max_accel = max;
	accel_filter->incline = incline;
	accel_filter->offset = offset;
	filter->speed_adjustment = speed_adjustment;

	return true;
}

static void
trackpoint_accelerator_destroy(struct motion_filter *filter)
{
	struct trackpoint_accelerator *accel_filter =
		(struct trackpoint_accelerator *)filter;

	free(accel_filter);
}

struct motion_filter_interface accelerator_interface_trackpoint = {
	.type = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE,
	.filter = trackpoint_accelerator_filter,
	.filter_constant = trackpoint_accelerator_filter_noop,
	.restart = NULL,
	.destroy = trackpoint_accelerator_destroy,
	.set_speed = trackpoint_accelerator_set_speed,
};

struct motion_filter *
create_pointer_accelerator_filter_trackpoint(int max_hw_delta)
{
	struct trackpoint_accelerator *filter;

	/* Trackpoints are special. They don't have a movement speed like a
	 * mouse or a finger, instead they send a constant stream of events
	 * based on the pressure applied.
	 *
	 * Physical ranges on a trackpoint are the max values for relative
	 * deltas, but these are highly device-specific.
	 *
	 */

	filter = zalloc(sizeof *filter);
	if (!filter)
		return NULL;

	filter->history_size = ARRAY_LENGTH(filter->history);
	filter->scale_factor = 1.0 * TRACKPOINT_DEFAULT_RANGE / max_hw_delta;
	filter->max_accel = TRACKPOINT_DEFAULT_MAX_ACCEL;
	filter->max_delta = TRACKPOINT_DEFAULT_MAX_DELTA;

	filter->base.interface = &accelerator_interface_trackpoint;

	return &filter->base;
}
