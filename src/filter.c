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

/* Convert speed/velocity from units/us to units/ms */
static inline double
v_us2ms(double units_per_us)
{
	return units_per_us * 1000.0;
}

static inline double
v_us2s(double units_per_us)
{
	return units_per_us * 1000000.0;
}

/* Convert speed/velocity from units/ms to units/us */
static inline double
v_ms2us(double units_per_ms)
{
	return units_per_ms/1000.0;
}

static inline struct normalized_coords
normalize_for_dpi(const struct device_float_coords *coords, int dpi)
{
	struct normalized_coords norm;

	norm.x = coords->x * DEFAULT_MOUSE_DPI/dpi;
	norm.y = coords->y * DEFAULT_MOUSE_DPI/dpi;

	return norm;
}

struct normalized_coords
filter_dispatch(struct motion_filter *filter,
		const struct device_float_coords *unaccelerated,
		void *data, uint64_t time)
{
	return filter->interface->filter(filter, unaccelerated, data, time);
}

struct normalized_coords
filter_dispatch_constant(struct motion_filter *filter,
			 const struct device_float_coords *unaccelerated,
			 void *data, uint64_t time)
{
	return filter->interface->filter_constant(filter, unaccelerated, data, time);
}

void
filter_restart(struct motion_filter *filter,
	       void *data, uint64_t time)
{
	if (filter->interface->restart)
		filter->interface->restart(filter, data, time);
}

void
filter_destroy(struct motion_filter *filter)
{
	if (!filter || !filter->interface->destroy)
		return;

	filter->interface->destroy(filter);
}

bool
filter_set_speed(struct motion_filter *filter,
		 double speed_adjustment)
{
	return filter->interface->set_speed(filter, speed_adjustment);
}

double
filter_get_speed(struct motion_filter *filter)
{
	return filter->speed_adjustment;
}

enum libinput_config_accel_profile
filter_get_type(struct motion_filter *filter)
{
	return filter->interface->type;
}

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

/* for the Lenovo x230 custom accel. do not touch */
#define X230_THRESHOLD v_ms2us(0.4)		/* in units/us */
#define X230_ACCELERATION 2.0			/* unitless factor */
#define X230_INCLINE 1.1			/* unitless factor */
#define X230_MAGIC_SLOWDOWN 0.4			/* unitless */
#define X230_TP_MAGIC_LOW_RES_FACTOR 4.0	/* unitless */

/* Trackpoint acceleration */
#define TRACKPOINT_DEFAULT_MAX_ACCEL 2.0	/* in units/us */
#define TRACKPOINT_DEFAULT_MAX_DELTA 60
/* As measured on a Lenovo T440 at kernel-default sensitivity 128 */
#define TRACKPOINT_DEFAULT_RANGE 20		/* max value */

/*
 * Pointer acceleration filter constants
 */

#define MAX_VELOCITY_DIFF	v_ms2us(1) /* units/us */
#define MOTION_TIMEOUT		ms2us(1000)
#define NUM_POINTER_TRACKERS	16

struct pointer_tracker {
	struct device_float_coords delta; /* delta to most recent event */
	uint64_t time;  /* us */
	uint32_t dir;
};

struct pointer_accelerator {
	struct motion_filter base;

	accel_profile_func_t profile;

	double velocity;	/* units/us */
	double last_velocity;	/* units/us */

	struct pointer_tracker *trackers;
	int cur_tracker;

	double threshold;	/* units/us */
	double accel;		/* unitless factor */
	double incline;		/* incline of the function */

	int dpi;
};

struct pointer_accelerator_flat {
	struct motion_filter base;

	double factor;
	int dpi;
};

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

static void
feed_trackers(struct pointer_accelerator *accel,
	      const struct device_float_coords *delta,
	      uint64_t time)
{
	int i, current;
	struct pointer_tracker *trackers = accel->trackers;

	for (i = 0; i < NUM_POINTER_TRACKERS; i++) {
		trackers[i].delta.x += delta->x;
		trackers[i].delta.y += delta->y;
	}

	current = (accel->cur_tracker + 1) % NUM_POINTER_TRACKERS;
	accel->cur_tracker = current;

	trackers[current].delta.x = 0.0;
	trackers[current].delta.y = 0.0;
	trackers[current].time = time;
	trackers[current].dir = device_float_get_direction(*delta);
}

static struct pointer_tracker *
tracker_by_offset(struct pointer_accelerator *accel, unsigned int offset)
{
	unsigned int index =
		(accel->cur_tracker + NUM_POINTER_TRACKERS - offset)
		% NUM_POINTER_TRACKERS;
	return &accel->trackers[index];
}

static double
calculate_tracker_velocity(struct pointer_tracker *tracker, uint64_t time)
{
	double tdelta = time - tracker->time + 1;
	return hypot(tracker->delta.x, tracker->delta.y) / tdelta; /* units/us */
}

static inline double
calculate_velocity_after_timeout(struct pointer_tracker *tracker)
{
	/* First movement after timeout needs special handling.
	 *
	 * When we trigger the timeout, the last event is too far in the
	 * past to use it for velocity calculation across multiple tracker
	 * values.
	 *
	 * Use the motion timeout itself to calculate the speed rather than
	 * the last tracker time. This errs on the side of being too fast
	 * for really slow movements but provides much more useful initial
	 * movement in normal use-cases (pause, move, pause, move)
	 */
	return calculate_tracker_velocity(tracker,
					  tracker->time + MOTION_TIMEOUT);
}

/**
 * Calculate the velocity based on the tracker data. Velocity is averaged
 * across multiple historical values, provided those values aren't "too
 * different" to our current one. That includes either being too far in the
 * past, moving into a different direction or having too much of a velocity
 * change between events.
 */
static double
calculate_velocity(struct pointer_accelerator *accel, uint64_t time)
{
	struct pointer_tracker *tracker;
	double velocity;
	double result = 0.0;
	double initial_velocity = 0.0;
	double velocity_diff;
	unsigned int offset;

	unsigned int dir = tracker_by_offset(accel, 0)->dir;

	/* Find least recent vector within a timelimit, maximum velocity diff
	 * and direction threshold. */
	for (offset = 1; offset < NUM_POINTER_TRACKERS; offset++) {
		tracker = tracker_by_offset(accel, offset);

		/* Bug: time running backwards */
		if (tracker->time > time)
			break;

		/* Stop if too far away in time */
		if (time - tracker->time > MOTION_TIMEOUT) {
			if (offset == 1)
				result = calculate_velocity_after_timeout(tracker);
			break;
		}

		velocity = calculate_tracker_velocity(tracker, time);

		/* Stop if direction changed */
		dir &= tracker->dir;
		if (dir == 0) {
			/* First movement after dirchange - velocity is that
			 * of the last movement */
			if (offset == 1)
				result = velocity;
			break;
		}

		if (initial_velocity == 0.0) {
			result = initial_velocity = velocity;
		} else {
			/* Stop if velocity differs too much from initial */
			velocity_diff = fabs(initial_velocity - velocity);
			if (velocity_diff > MAX_VELOCITY_DIFF)
				break;

			result = velocity;
		}
	}

	return result; /* units/us */
}

/**
 * Apply the acceleration profile to the given velocity.
 *
 * @param accel The acceleration filter
 * @param data Caller-specific data
 * @param velocity Velocity in device-units per µs
 * @param time Current time in µs
 *
 * @return A unitless acceleration factor, to be applied to the delta
 */
static double
acceleration_profile(struct pointer_accelerator *accel,
		     void *data, double velocity, uint64_t time)
{
	return accel->profile(&accel->base, data, velocity, time);
}

/**
 * Calculate the acceleration factor for our current velocity, averaging
 * between our current and the most recent velocity to smoothen out changes.
 *
 * @param accel The acceleration filter
 * @param data Caller-specific data
 * @param velocity Velocity in device-units per µs
 * @param last_velocity Previous velocity in device-units per µs
 * @param time Current time in µs
 *
 * @return A unitless acceleration factor, to be applied to the delta
 */
static double
calculate_acceleration(struct pointer_accelerator *accel,
		       void *data,
		       double velocity,
		       double last_velocity,
		       uint64_t time)
{
	double factor;

	/* Use Simpson's rule to calculate the avarage acceleration between
	 * the previous motion and the most recent. */
	factor = acceleration_profile(accel, data, velocity, time);
	factor += acceleration_profile(accel, data, last_velocity, time);
	factor += 4.0 *
		acceleration_profile(accel, data,
				     (last_velocity + velocity) / 2,
				     time);

	factor = factor / 6.0;

	return factor; /* unitless factor */
}

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

	feed_trackers(accel, unaccelerated, time);
	velocity = calculate_velocity(accel, time);
	accel_factor = calculate_acceleration(accel,
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

static struct normalized_coords
accelerator_filter_pre_normalized(struct motion_filter *filter,
				  const struct device_float_coords *unaccelerated,
				  void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	struct normalized_coords normalized;
	struct device_float_coords converted, accelerated;

	/* Accelerate for normalized units and return normalized units.
	   API requires device_floats, so we just copy the bits around */
	normalized = normalize_for_dpi(unaccelerated, accel->dpi);
	converted.x = normalized.x;
	converted.y = normalized.y;

	accelerated = accelerator_filter_generic(filter,
						 &converted,
						 data,
						 time);
	normalized.x = accelerated.x;
	normalized.y = accelerated.y;
	return normalized;
}

static struct normalized_coords
accelerator_filter_unnormalized(struct motion_filter *filter,
				const struct device_float_coords *unaccelerated,
				void *data, uint64_t time)
{
	struct device_float_coords accelerated;
	struct normalized_coords normalized;

	/* Accelerate for device units and return device units */
	accelerated = accelerator_filter_generic(filter,
						 unaccelerated,
						 data,
						 time);
	normalized.x = accelerated.x;
	normalized.y = accelerated.y;
	return normalized;
}

/**
 * Generic filter that does nothing beyond converting from the device's
 * native dpi into normalized coordinates.
 *
 * @param filter The acceleration filter
 * @param unaccelerated The raw delta in the device's dpi
 * @param data Caller-specific data
 * @param time Current time in µs
 *
 * @return An accelerated tuple of coordinates representing normalized
 * motion
 */
static struct normalized_coords
accelerator_filter_noop(struct motion_filter *filter,
			const struct device_float_coords *unaccelerated,
			void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;

	return normalize_for_dpi(unaccelerated, accel->dpi);
}

static struct normalized_coords
accelerator_filter_x230(struct motion_filter *filter,
			const struct device_float_coords *raw,
			void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	double accel_factor; /* unitless factor */
	struct normalized_coords accelerated;
	struct device_float_coords delta_normalized;
	struct normalized_coords unaccelerated;
	double velocity; /* units/us */

	/* This filter is a "do not touch me" filter. So the hack here is
	 * just to replicate the old behavior before filters switched to
	 * device-native dpi:
	 * 1) convert from device-native to 1000dpi normalized
	 * 2) run all calculation on 1000dpi-normalized data
	 * 3) apply accel factor no normalized data
	 */
	unaccelerated = normalize_for_dpi(raw, accel->dpi);
	delta_normalized.x = unaccelerated.x;
	delta_normalized.y = unaccelerated.y;

	feed_trackers(accel, &delta_normalized, time);
	velocity = calculate_velocity(accel, time);
	accel_factor = calculate_acceleration(accel,
					      data,
					      velocity,
					      accel->last_velocity,
					      time);
	accel->last_velocity = velocity;

	accelerated.x = accel_factor * delta_normalized.x;
	accelerated.y = accel_factor * delta_normalized.y;

	return accelerated;
}

static struct normalized_coords
accelerator_filter_constant_x230(struct motion_filter *filter,
				 const struct device_float_coords *unaccelerated,
				 void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	struct normalized_coords normalized;
	const double factor =
		X230_MAGIC_SLOWDOWN/X230_TP_MAGIC_LOW_RES_FACTOR;

	normalized = normalize_for_dpi(unaccelerated, accel->dpi);
	normalized.x = factor * normalized.x;
	normalized.y = factor * normalized.y;

	return normalized;
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
accelerator_restart(struct motion_filter *filter,
		    void *data,
		    uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	unsigned int offset;
	struct pointer_tracker *tracker;

	for (offset = 1; offset < NUM_POINTER_TRACKERS; offset++) {
		tracker = tracker_by_offset(accel, offset);
		tracker->time = 0;
		tracker->dir = 0;
		tracker->delta.x = 0;
		tracker->delta.y = 0;
	}

	tracker = tracker_by_offset(accel, 0);
	tracker->time = time;
	tracker->dir = UNDEFINED_DIRECTION;
}

static void
accelerator_destroy(struct motion_filter *filter)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;

	free(accel->trackers);
	free(accel);
}

static bool
accelerator_set_speed(struct motion_filter *filter,
		      double speed_adjustment)
{
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;

	assert(speed_adjustment >= -1.0 && speed_adjustment <= 1.0);

	/* Note: the numbers below are nothing but trial-and-error magic,
	   don't read more into them other than "they mostly worked ok" */

	/* delay when accel kicks in */
	accel_filter->threshold = DEFAULT_THRESHOLD -
					v_ms2us(0.25) * speed_adjustment;
	if (accel_filter->threshold < MINIMUM_THRESHOLD)
		accel_filter->threshold = MINIMUM_THRESHOLD;

	/* adjust max accel factor */
	accel_filter->accel = DEFAULT_ACCELERATION + speed_adjustment * 1.5;

	/* higher speed -> faster to reach max */
	accel_filter->incline = DEFAULT_INCLINE + speed_adjustment * 0.75;

	filter->speed_adjustment = speed_adjustment;
	return true;
}

/**
 * Custom acceleration function for mice < 1000dpi.
 * At slow motion, a single device unit causes a one-pixel movement.
 * The threshold/max accel depends on the DPI, the smaller the DPI the
 * earlier we accelerate and the higher the maximum acceleration is. Result:
 * at low speeds we get pixel-precision, at high speeds we get approx. the
 * same movement as a high-dpi mouse.
 *
 * Note: data fed to this function is in device units, not normalized.
 */
double
pointer_accel_profile_linear_low_dpi(struct motion_filter *filter,
				     void *data,
				     double speed_in, /* in device units (units/us) */
				     uint64_t time)
{
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;

	double max_accel = accel_filter->accel; /* unitless factor */
	double threshold = accel_filter->threshold; /* units/us */
	const double incline = accel_filter->incline;
	double dpi_factor = accel_filter->dpi/(double)DEFAULT_MOUSE_DPI;
	double factor; /* unitless */

	/* dpi_factor is always < 1.0, increase max_accel, reduce
	   the threshold so it kicks in earlier */
	max_accel /= dpi_factor;
	threshold *= dpi_factor;

	/* see pointer_accel_profile_linear for a long description */
	if (v_us2ms(speed_in) < 0.07)
		factor = 10 * v_us2ms(speed_in) + 0.3;
	else if (speed_in < threshold)
		factor = 1;
	else
		factor = incline * v_us2ms(speed_in - threshold) + 1;

	factor = min(max_accel, factor);

	return factor;
}

double
pointer_accel_profile_linear(struct motion_filter *filter,
			     void *data,
			     double speed_in, /* in device units (units/µs) */
			     uint64_t time)
{
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;
	const double max_accel = accel_filter->accel; /* unitless factor */
	const double threshold = accel_filter->threshold; /* units/us */
	const double incline = accel_filter->incline;
	double factor; /* unitless */

	/* Normalize to 1000dpi, because the rest below relies on that */
	speed_in = speed_in * DEFAULT_MOUSE_DPI/accel_filter->dpi;

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

	   for speeds up to 0.07 u/ms, we decelerate, down to 30% of input
	   speed.
		   hence 1 = a * 0.07 + 0.3
		       0.7 = a * 0.07 => a := 10
		   deceleration function is thus:
			y = 10x + 0.3

	  Note:
	  * 0.07u/ms as threshold is a result of trial-and-error and
	    has no other intrinsic meaning.
	  * 0.3 is chosen simply because it is above the Nyquist frequency
	    for subpixel motion within a pixel.
	*/
	if (v_us2ms(speed_in) < 0.07) {
		factor = 10 * v_us2ms(speed_in) + 0.3;
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
		factor = incline * v_us2ms(speed_in - threshold) + 1;
	}

	/* Cap at the maximum acceleration factor */
	factor = min(max_accel, factor);

	return factor;
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

double
touchpad_lenovo_x230_accel_profile(struct motion_filter *filter,
				      void *data,
				      double speed_in, /* 1000dpi-units/µs */
				      uint64_t time)
{
	/* Those touchpads presents an actual lower resolution that what is
	 * advertised. We see some jumps from the cursor due to the big steps
	 * in X and Y when we are receiving data.
	 * Apply a factor to minimize those jumps at low speed, and try
	 * keeping the same feeling as regular touchpads at high speed.
	 * It still feels slower but it is usable at least */
	double factor; /* unitless */
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;

	double f1, f2; /* unitless */
	const double max_accel = accel_filter->accel *
				  X230_TP_MAGIC_LOW_RES_FACTOR; /* unitless factor */
	const double threshold = accel_filter->threshold /
				  X230_TP_MAGIC_LOW_RES_FACTOR; /* units/us */
	const double incline = accel_filter->incline * X230_TP_MAGIC_LOW_RES_FACTOR;

	/* Note: the magic values in this function are obtained by
	 * trial-and-error. No other meaning should be interpreted.
	 * The calculation is a compressed form of
	 * pointer_accel_profile_linear(), look at the git history of that
	 * function for an explanation of what the min/max/etc. does.
	 */
	speed_in *= X230_MAGIC_SLOWDOWN / X230_TP_MAGIC_LOW_RES_FACTOR;

	f1 = min(1, v_us2ms(speed_in) * 5);
	f2 = 1 + (v_us2ms(speed_in) - v_us2ms(threshold)) * incline;

	factor = min(max_accel, f2 > 1 ? f2 : f1);

	return factor * X230_MAGIC_SLOWDOWN / X230_TP_MAGIC_LOW_RES_FACTOR;
}

struct motion_filter_interface accelerator_interface = {
	.type = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE,
	.filter = accelerator_filter_pre_normalized,
	.filter_constant = accelerator_filter_noop,
	.restart = accelerator_restart,
	.destroy = accelerator_destroy,
	.set_speed = accelerator_set_speed,
};

static struct pointer_accelerator *
create_default_filter(int dpi)
{
	struct pointer_accelerator *filter;

	filter = zalloc(sizeof *filter);
	if (filter == NULL)
		return NULL;

	filter->last_velocity = 0.0;

	filter->trackers =
		calloc(NUM_POINTER_TRACKERS, sizeof *filter->trackers);
	filter->cur_tracker = 0;

	filter->threshold = DEFAULT_THRESHOLD;
	filter->accel = DEFAULT_ACCELERATION;
	filter->incline = DEFAULT_INCLINE;
	filter->dpi = dpi;

	return filter;
}

struct motion_filter *
create_pointer_accelerator_filter_linear(int dpi)
{
	struct pointer_accelerator *filter;

	filter = create_default_filter(dpi);
	if (!filter)
		return NULL;

	filter->base.interface = &accelerator_interface;
	filter->profile = pointer_accel_profile_linear;

	return &filter->base;
}

struct motion_filter_interface accelerator_interface_low_dpi = {
	.type = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE,
	.filter = accelerator_filter_unnormalized,
	.filter_constant = accelerator_filter_noop,
	.restart = accelerator_restart,
	.destroy = accelerator_destroy,
	.set_speed = accelerator_set_speed,
};

struct motion_filter *
create_pointer_accelerator_filter_linear_low_dpi(int dpi)
{
	struct pointer_accelerator *filter;

	filter = create_default_filter(dpi);
	if (!filter)
		return NULL;

	filter->base.interface = &accelerator_interface_low_dpi;
	filter->profile = pointer_accel_profile_linear_low_dpi;

	return &filter->base;
}

struct motion_filter_interface accelerator_interface_touchpad = {
	.type = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE,
	.filter = accelerator_filter_post_normalized,
	.filter_constant = touchpad_constant_filter,
	.restart = accelerator_restart,
	.destroy = accelerator_destroy,
	.set_speed = touchpad_accelerator_set_speed,
};

struct motion_filter *
create_pointer_accelerator_filter_touchpad(int dpi)
{
	struct pointer_accelerator *filter;

	filter = create_default_filter(dpi);
	if (!filter)
		return NULL;

	filter->base.interface = &accelerator_interface_touchpad;
	filter->profile = touchpad_accel_profile_linear;

	return &filter->base;
}

struct motion_filter_interface accelerator_interface_x230 = {
	.type = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE,
	.filter = accelerator_filter_x230,
	.filter_constant = accelerator_filter_constant_x230,
	.restart = accelerator_restart,
	.destroy = accelerator_destroy,
	.set_speed = accelerator_set_speed,
};

/* The Lenovo x230 has a bad touchpad. This accel method has been
 * trial-and-error'd, any changes to it will require re-testing everything.
 * Don't touch this.
 */
struct motion_filter *
create_pointer_accelerator_filter_lenovo_x230(int dpi)
{
	struct pointer_accelerator *filter;

	filter = zalloc(sizeof *filter);
	if (filter == NULL)
		return NULL;

	filter->base.interface = &accelerator_interface_x230;
	filter->profile = touchpad_lenovo_x230_accel_profile;
	filter->last_velocity = 0.0;

	filter->trackers =
		calloc(NUM_POINTER_TRACKERS, sizeof *filter->trackers);
	filter->cur_tracker = 0;

	filter->threshold = X230_THRESHOLD;
	filter->accel = X230_ACCELERATION; /* unitless factor */
	filter->incline = X230_INCLINE; /* incline of the acceleration function */
	filter->dpi = dpi;

	return &filter->base;
}

double
trackpoint_accel_profile(struct motion_filter *filter,
			 void *data,
			 double delta)
{
	struct trackpoint_accelerator *accel_filter =
		(struct trackpoint_accelerator *)filter;
	const double max_accel = accel_filter->max_accel;
	double factor;
	double max;

	delta = fabs(delta);

#if 0
#if 1
	/* good default */
	//factor = 0.6 * delta + 0.8;
	factor = 1 * delta;
	max = 6;
#endif

#if 1
	/* slow, good for -1.0 */
	//factor = 0.6 * delta + 0.8; /* This doesn't really take effect
	//			       anyway because max is too low */
	factor = 0.3 * delta;
	max = 1;
#endif

#if 0
	/* slow, maybe good for -0.5 */
	//factor = 0.6 * delta + 0.8;
	factor = 0.6 * delta;
	max = 2.0;
#endif

#if 0
	/* ok, maybe for 0.5 */
	//factor = 0.7 * delta + 0.8;
	factor = 1.4 * delta;
	max = 8;
#endif

#if 0
	/* ok for 1.0 */
	factor = 1.9 * delta;
	max = 15;
#endif

	factor = min(max, factor);

	printf("factor %.2f %s\n", factor,
	       factor == max ? "capped" : "");

	return factor;

#endif
	/* This is almost the equivalent of the xserver acceleration
	   at sensitivity 128 and speed 0.0 */
	factor = delta * accel_filter->incline + accel_filter->offset;
	factor = min(factor, max_accel);
	printf("factor %.2f %s\n", factor,
	       factor == max_accel ? "capped" : "");

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
	struct device_float_coords avg;

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
	double fx, fy;

	scaled = trackpoint_normalize_deltas(accel_filter, unaccelerated);
	avg = trackpoint_average_delta(accel_filter, &scaled);

	fx = trackpoint_accel_profile(filter, data, avg.x);
	fy = trackpoint_accel_profile(filter, data, avg.y);

	coords.x = avg.x * fx;
	coords.y = avg.y * fy;

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

static struct normalized_coords
accelerator_filter_flat(struct motion_filter *filter,
			const struct device_float_coords *unaccelerated,
			void *data, uint64_t time)
{
	struct pointer_accelerator_flat *accel_filter =
		(struct pointer_accelerator_flat *)filter;
	double factor; /* unitless factor */
	struct normalized_coords accelerated;

	/* You want flat acceleration, you get flat acceleration for the
	 * device */
	factor = accel_filter->factor;
	accelerated.x = factor * unaccelerated->x;
	accelerated.y = factor * unaccelerated->y;

	return accelerated;
}

static bool
accelerator_set_speed_flat(struct motion_filter *filter,
			   double speed_adjustment)
{
	struct pointer_accelerator_flat *accel_filter =
		(struct pointer_accelerator_flat *)filter;

	assert(speed_adjustment >= -1.0 && speed_adjustment <= 1.0);

	/* Speed rage is 0-200% of the nominal speed, with 0 mapping to the
	 * nominal speed. Anything above 200 is pointless, we're already
	 * skipping over ever second pixel at 200% speed.
	 */

	accel_filter->factor = 1 + speed_adjustment;
	filter->speed_adjustment = speed_adjustment;

	return true;
}

static void
accelerator_destroy_flat(struct motion_filter *filter)
{
	struct pointer_accelerator_flat *accel =
		(struct pointer_accelerator_flat *) filter;

	free(accel);
}

struct motion_filter_interface accelerator_interface_flat = {
	.type = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT,
	.filter = accelerator_filter_flat,
	.filter_constant = accelerator_filter_noop,
	.restart = NULL,
	.destroy = accelerator_destroy_flat,
	.set_speed = accelerator_set_speed_flat,
};

struct motion_filter *
create_pointer_accelerator_filter_flat(int dpi)
{
	struct pointer_accelerator_flat *filter;

	filter = zalloc(sizeof *filter);
	if (filter == NULL)
		return NULL;

	filter->base.interface = &accelerator_interface_flat;
	filter->dpi = dpi;

	return &filter->base;
}

static inline struct normalized_coords
tablet_accelerator_filter_flat_mouse(struct tablet_accelerator_flat *filter,
				     const struct device_float_coords *units)
{
	struct normalized_coords accelerated;

	/*
	   Tablets are high res (Intuos 4 is 5080 dpi) and unmodified deltas
	   are way too high. Slow it down to the equivalent of a 1000dpi
	   mouse. The ratio of that is:
		ratio = 1000/(resolution_per_mm * 25.4)

	   i.e. on the Intuos4 it's a ratio of ~1/5.
	 */

	accelerated.x = units->x * filter->xres_scale;
	accelerated.y = units->y * filter->yres_scale;

	accelerated.x *= filter->factor;
	accelerated.y *= filter->factor;

	return accelerated;
}

static struct normalized_coords
tablet_accelerator_filter_flat_pen(struct tablet_accelerator_flat *filter,
				   const struct device_float_coords *units)
{
	struct normalized_coords accelerated;

	/* Tablet input is in device units, output is supposed to be in
	 * logical pixels roughly equivalent to a mouse/touchpad.
	 *
	 * This is a magical constant found by trial and error. On a 96dpi
	 * screen 0.4mm of movement correspond to 1px logical pixel which
	 * is almost identical to the tablet mapped to screen in absolute
	 * mode. Tested on a Intuos5, other tablets may vary.
	 */
       const double DPI_CONVERSION = 96.0/25.4 * 2.5; /* unitless factor */
       struct normalized_coords mm;

       mm.x = 1.0 * units->x/filter->xres;
       mm.y = 1.0 * units->y/filter->yres;
       accelerated.x = mm.x * filter->factor * DPI_CONVERSION;
       accelerated.y = mm.y * filter->factor * DPI_CONVERSION;

       return accelerated;
}

static struct normalized_coords
tablet_accelerator_filter_flat(struct motion_filter *filter,
			       const struct device_float_coords *units,
			       void *data, uint64_t time)
{
	struct tablet_accelerator_flat *accel_filter =
		(struct tablet_accelerator_flat *)filter;
	struct libinput_tablet_tool *tool = (struct libinput_tablet_tool*)data;
	enum libinput_tablet_tool_type type;
	struct normalized_coords accel;

	type = libinput_tablet_tool_get_type(tool);

	switch (type) {
	case LIBINPUT_TABLET_TOOL_TYPE_MOUSE:
	case LIBINPUT_TABLET_TOOL_TYPE_LENS:
		accel = tablet_accelerator_filter_flat_mouse(accel_filter,
							     units);
		break;
	default:
		accel = tablet_accelerator_filter_flat_pen(accel_filter,
							   units);
		break;
	}

	return accel;
}

static bool
tablet_accelerator_set_speed(struct motion_filter *filter,
			     double speed_adjustment)
{
	struct tablet_accelerator_flat *accel_filter =
		(struct tablet_accelerator_flat *)filter;

	assert(speed_adjustment >= -1.0 && speed_adjustment <= 1.0);

	accel_filter->factor = speed_adjustment + 1.0;

	return true;
}

static void
tablet_accelerator_destroy(struct motion_filter *filter)
{
	struct tablet_accelerator_flat *accel_filter =
		(struct tablet_accelerator_flat *)filter;

	free(accel_filter);
}

struct motion_filter_interface accelerator_interface_tablet = {
	.type = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT,
	.filter = tablet_accelerator_filter_flat,
	.filter_constant = NULL,
	.restart = NULL,
	.destroy = tablet_accelerator_destroy,
	.set_speed = tablet_accelerator_set_speed,
};

static struct tablet_accelerator_flat *
create_tablet_filter_flat(int xres, int yres)
{
	struct tablet_accelerator_flat *filter;

	filter = zalloc(sizeof *filter);
	if (filter == NULL)
		return NULL;

	filter->factor = 1.0;
	filter->xres = xres;
	filter->yres = yres;
	filter->xres_scale = DEFAULT_MOUSE_DPI/(25.4 * xres);
	filter->yres_scale = DEFAULT_MOUSE_DPI/(25.4 * yres);

	return filter;
}

struct motion_filter *
create_pointer_accelerator_filter_tablet(int xres, int yres)
{
	struct tablet_accelerator_flat *filter;

	filter = create_tablet_filter_flat(xres, yres);
	if (!filter)
		return NULL;

	filter->base.interface = &accelerator_interface_tablet;

	return &filter->base;
}
