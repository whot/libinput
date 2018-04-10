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

/* Once normalized, touchpads see the same acceleration as mice. that is
 * technically correct but subjectively wrong, we expect a touchpad to be a
 * lot slower than a mouse. Apply a magic factor to slow down all movements
 */
#define TP_MAGIC_SLOWDOWN 0.37 /* unitless factor */

/*
 * Default parameters for pointer acceleration profiles.
 */

#define DEFAULT_THRESHOLD v_ms2us(0.4)		/* in units/us */
#define MINIMUM_THRESHOLD v_ms2us(0.2)		/* in units/us */
#define DEFAULT_ACCELERATION 2.0		/* unitless factor */
#define DEFAULT_INCLINE 1.1			/* unitless factor */

/* Touchpad acceleration */
#define TOUCHPAD_DEFAULT_THRESHOLD 254		/* mm/s */
#define TOUCHPAD_THRESHOLD_RANGE 184		/* mm/s */
#define TOUCHPAD_ACCELERATION 9.0		/* unitless factor */
#define TOUCHPAD_INCLINE 0.011			/* unitless factor */

/*
 * Pointer acceleration filter constants
 */
#define NUM_POINTER_TRACKERS	16

/**
 * Calculate the acceleration factor for the given delta with the timestamp.
 *
 * @param accel The acceleration filter
 * @param unaccelerated The raw delta in the device's dpi
 * @param data Caller-specific data
 * @param time Current time in µs
 *
 * @return A unitless acceleration factor, to be applied to the delta
 */
static inline double
calculate_acceleration_factor(struct pointer_accelerator *accel,
			      const struct device_float_coords *unaccelerated,
			      void *data,
			      uint64_t time)
{
	double velocity; /* units/us in device-native dpi*/
	double accel_factor;

	feed_trackers(&accel->trackers, unaccelerated, time);
	velocity = calculate_velocity(&accel->trackers, time);
	accel_factor = calculate_acceleration_simpsons(&accel->base,
						       accel->profile,
						       data,
						       velocity,
						       accel->last_velocity,
						       time);
	accel->last_velocity = velocity;

	return accel_factor;
}

/**
 * Generic filter that calculates the acceleration factor and applies it to
 * the coordinates.
 *
 * @param filter The acceleration filter
 * @param unaccelerated The raw delta in the device's dpi
 * @param data Caller-specific data
 * @param time Current time in µs
 *
 * @return An accelerated tuple of coordinates representing accelerated
 * motion, still in device units.
 */
static struct device_float_coords
accelerator_filter_generic(struct motion_filter *filter,
			   const struct device_float_coords *unaccelerated,
			   void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	double accel_value; /* unitless factor */
	struct device_float_coords accelerated;

	accel_value = calculate_acceleration_factor(accel,
						    unaccelerated,
						    data,
						    time);

	accelerated.x = accel_value * unaccelerated->x;
	accelerated.y = accel_value * unaccelerated->y;

	return accelerated;
}

static struct normalized_coords
accelerator_filter_post_normalized(struct motion_filter *filter,
				   const struct device_float_coords *unaccelerated,
				   void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	struct device_float_coords accelerated;

	/* Accelerate for device units, normalize afterwards */
	accelerated = accelerator_filter_generic(filter,
						 unaccelerated,
						 data,
						 time);
	return normalize_for_dpi(&accelerated, accel->dpi);
}

static bool
touchpad_accelerator_set_speed(struct motion_filter *filter,
		      double speed_adjustment)
{
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;

	assert(speed_adjustment >= -1.0 && speed_adjustment <= 1.0);

	/* Note: the numbers below are nothing but trial-and-error magic,
	   don't read more into them other than "they mostly worked ok" */

	/* adjust when accel kicks in */
	accel_filter->threshold = TOUCHPAD_DEFAULT_THRESHOLD -
		TOUCHPAD_THRESHOLD_RANGE * speed_adjustment;
	accel_filter->accel = TOUCHPAD_ACCELERATION;
	accel_filter->incline = TOUCHPAD_INCLINE;
	filter->speed_adjustment = speed_adjustment;

	return true;
}

static struct normalized_coords
touchpad_constant_filter(struct motion_filter *filter,
			 const struct device_float_coords *unaccelerated,
			 void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *)filter;
	struct normalized_coords normalized;

	normalized = normalize_for_dpi(unaccelerated, accel->dpi);
	normalized.x = TP_MAGIC_SLOWDOWN * normalized.x;
	normalized.y = TP_MAGIC_SLOWDOWN * normalized.y;

	return normalized;
}

static void
touchpad_accelerator_restart(struct motion_filter *filter,
			     void *data,
			     uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;

	reset_trackers(&accel->trackers, time);
}

static void
touchpad_accelerator_destroy(struct motion_filter *filter)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;

	free_trackers(&accel->trackers);
	free(accel);
}

double
touchpad_accel_profile_linear(struct motion_filter *filter,
			      void *data,
			      double speed_in, /* in device units/µs */
			      uint64_t time)
{
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;
	const double max_accel = accel_filter->accel; /* unitless factor */
	const double threshold = accel_filter->threshold; /* units/us */
	const double incline = accel_filter->incline;
	double factor; /* unitless */

	/* Convert to mm/s because that's something one can understand */
	speed_in = v_us2s(speed_in) * 25.4/accel_filter->dpi;

	/*
	   Our acceleration function calculates a factor to accelerate input
	   deltas with. The function is a double incline with a plateau,
	   with a rough shape like this:

	  accel
	 factor
	   ^
	   |        /
	   |  _____/
	   | /
	   |/
	   +-------------> speed in

	   The two inclines are linear functions in the form
		   y = ax + b
		   where y is speed_out
		         x is speed_in
			 a is the incline of acceleration
			 b is minimum acceleration factor

	   for speeds up to the lower threshold, we decelerate, down to 30%
	   of input speed.
		   hence 1 = a * 7 + 0.3
		       0.7 = a * 7  => a := 0.1
		   deceleration function is thus:
			y = 0.1x + 0.3

	  Note:
	  * The minimum threshold is a result of trial-and-error and
	    has no other intrinsic meaning.
	  * 0.3 is chosen simply because it is above the Nyquist frequency
	    for subpixel motion within a pixel.
	*/
	if (speed_in < 7.0) {
		factor = 0.1 * speed_in + 0.3;
	/* up to the threshold, we keep factor 1, i.e. 1:1 movement */
	} else if (speed_in < threshold) {
		factor = 1;
	} else {
	/* Acceleration function above the threshold:
		y = ax' + b
		where T is threshold
		      x is speed_in
		      x' is speed
	        and
			y(T) == 1
		hence 1 = ax' + 1
			=> x' := (x - T)
	 */
		factor = incline * (speed_in - threshold) + 1;
	}

	/* Cap at the maximum acceleration factor */
	factor = min(max_accel, factor);

	/* Scale everything depending on the acceleration set */
	factor *= 1 + 0.5 * filter->speed_adjustment;

	return factor * TP_MAGIC_SLOWDOWN;
}

static struct pointer_accelerator *
create_default_filter(int dpi)
{
	struct pointer_accelerator *filter;

	filter = zalloc(sizeof *filter);
	filter->last_velocity = 0.0;

	init_trackers(&filter->trackers, NUM_POINTER_TRACKERS);

	filter->threshold = DEFAULT_THRESHOLD;
	filter->accel = DEFAULT_ACCELERATION;
	filter->incline = DEFAULT_INCLINE;
	filter->dpi = dpi;

	return filter;
}

struct motion_filter_interface accelerator_interface_touchpad = {
	.type = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE,
	.filter = accelerator_filter_post_normalized,
	.filter_constant = touchpad_constant_filter,
	.restart = touchpad_accelerator_restart,
	.destroy = touchpad_accelerator_destroy,
	.set_speed = touchpad_accelerator_set_speed,
};

struct motion_filter *
create_pointer_accelerator_filter_touchpad(int dpi,
	uint64_t event_delta_smooth_threshold,
	uint64_t event_delta_smooth_value)
{
	struct pointer_accelerator *filter;
	struct pointer_delta_smoothener *smoothener;

	filter = create_default_filter(dpi);
	if (!filter)
		return NULL;

	filter->base.interface = &accelerator_interface_touchpad;
	filter->profile = touchpad_accel_profile_linear;

	smoothener = zalloc(sizeof(*smoothener));
	smoothener->threshold = event_delta_smooth_threshold,
	smoothener->value = event_delta_smooth_value,
	filter->trackers.smoothener = smoothener;

	return &filter->base;
}
