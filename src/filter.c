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

/*
 * Pointer acceleration filter constants
 */

#define MAX_VELOCITY_DIFF	v_ms2us(1) /* units/us */
#define MOTION_TIMEOUT		ms2us(1000)
#define NUM_POINTER_TRACKERS	16

struct pointer_accelerator_flat {
	struct motion_filter base;

	double factor;
	int dpi;
};

void
init_trackers(struct pointer_trackers *trackers,
	      size_t ntrackers)
{
	trackers->trackers = zalloc(ntrackers *
				    sizeof(*trackers->trackers));
	trackers->ntrackers = ntrackers;
	trackers->cur_tracker = 0;
	trackers->smoothener = NULL;
}

void
free_trackers(struct pointer_trackers *trackers)
{
	free(trackers->trackers);
	free(trackers->smoothener);
}

void
reset_trackers(struct pointer_trackers *trackers,
	       uint64_t time)
{
	unsigned int offset;
	struct pointer_tracker *tracker;

	for (offset = 1; offset < trackers->ntrackers; offset++) {
		tracker = tracker_by_offset(trackers, offset);
		tracker->time = 0;
		tracker->dir = 0;
		tracker->delta.x = 0;
		tracker->delta.y = 0;
	}

	tracker = tracker_by_offset(trackers, 0);
	tracker->time = time;
	tracker->dir = UNDEFINED_DIRECTION;
}

void
feed_trackers(struct pointer_trackers *trackers,
	      const struct device_float_coords *delta,
	      uint64_t time)
{
	unsigned int i, current;
	struct pointer_tracker *ts = trackers->trackers;

	assert(trackers->ntrackers);

	for (i = 0; i < trackers->ntrackers; i++) {
		ts[i].delta.x += delta->x;
		ts[i].delta.y += delta->y;
	}

	current = (trackers->cur_tracker + 1) % trackers->ntrackers;
	trackers->cur_tracker = current;

	ts[current].delta.x = 0.0;
	ts[current].delta.y = 0.0;
	ts[current].time = time;
	ts[current].dir = device_float_get_direction(*delta);
}

struct pointer_tracker *
tracker_by_offset(struct pointer_trackers *trackers, unsigned int offset)
{
	unsigned int index =
		(trackers->cur_tracker + trackers->ntrackers - offset)
		% trackers->ntrackers;
	return &trackers->trackers[index];
}

static double
calculate_tracker_velocity(struct pointer_tracker *tracker,
			   uint64_t time,
			   struct pointer_delta_smoothener *smoothener)
{
	uint64_t tdelta = time - tracker->time + 1;

	if (smoothener && tdelta < smoothener->threshold)
		tdelta = smoothener->value;

	return hypot(tracker->delta.x, tracker->delta.y) /
	       (double)tdelta; /* units/us */
}

static double
calculate_velocity_after_timeout(struct pointer_tracker *tracker,
				 struct pointer_delta_smoothener *smoothener)
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
					  tracker->time + MOTION_TIMEOUT,
					  smoothener);
}

/**
 * Calculate the velocity based on the tracker data. Velocity is averaged
 * across multiple historical values, provided those values aren't "too
 * different" to our current one. That includes either being too far in the
 * past, moving into a different direction or having too much of a velocity
 * change between events.
 */
double
calculate_velocity(struct pointer_trackers *trackers, uint64_t time)
{
	struct pointer_tracker *tracker;
	double velocity;
	double result = 0.0;
	double initial_velocity = 0.0;
	double velocity_diff;
	unsigned int offset;

	unsigned int dir = tracker_by_offset(trackers, 0)->dir;

	/* Find least recent vector within a timelimit, maximum velocity diff
	 * and direction threshold. */
	for (offset = 1; offset < trackers->ntrackers; offset++) {
		tracker = tracker_by_offset(trackers, offset);

		/* Bug: time running backwards */
		if (tracker->time > time)
			break;

		/* Stop if too far away in time */
		if (time - tracker->time > MOTION_TIMEOUT) {
			if (offset == 1)
				result = calculate_velocity_after_timeout(
							  tracker,
							  trackers->smoothener);
			break;
		}

		velocity = calculate_tracker_velocity(tracker,
						      time,
						      trackers->smoothener);

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

	feed_trackers(&accel->trackers, unaccelerated, time);
	velocity = calculate_velocity(&accel->trackers, time);
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

	reset_trackers(&accel->trackers, time);
}

static void
accelerator_destroy(struct motion_filter *filter)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;

	free_trackers(&accel->trackers);
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
	filter->last_velocity = 0.0;

	init_trackers(&filter->trackers, NUM_POINTER_TRACKERS);

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

	accel_filter->factor = max(0.005, 1 + speed_adjustment);
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
	filter->base.interface = &accelerator_interface_flat;
	filter->dpi = dpi;

	return &filter->base;
}

