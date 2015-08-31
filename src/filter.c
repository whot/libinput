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
#define TP_MAGIC_SLOWDOWN 0.4 /* unitless factor */

/* Convert speed/velocity from units/us to units/ms */
static inline double
v_us2ms(double units_per_us)
{
	return units_per_us * 1000.0;
}

/* Convert speed/velocity from units/ms to units/us */
static inline double
v_ms2us(double units_per_ms)
{
	return units_per_ms/1000.0;
}

struct normalized_coords
filter_dispatch(struct motion_filter *filter,
		const struct normalized_coords *unaccelerated,
		void *data, uint64_t time)
{
	return filter->interface->filter(filter, unaccelerated, data, time);
}

struct normalized_coords
filter_dispatch_constant(struct motion_filter *filter,
			 const struct normalized_coords *unaccelerated,
			 void *data, uint64_t time)
{
	return filter->interface->filter_constant(filter, unaccelerated, data, time);
}

void
filter_restart(struct motion_filter *filter,
	       void *data, uint64_t time)
{
	filter->interface->restart(filter, data, time);
}

void
filter_destroy(struct motion_filter *filter)
{
	if (!filter)
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

/*
 * Default parameters for pointer acceleration profiles.
 */

#define DEFAULT_THRESHOLD v_ms2us(0.4)		/* in units/us */
#define MINIMUM_THRESHOLD v_ms2us(0.2)		/* in units/us */
#define DEFAULT_ACCELERATION 2.0		/* unitless factor */
#define DEFAULT_INCLINE 1.1			/* unitless factor */

/* for the Lenovo x230 custom accel. do not touch */
#define X230_THRESHOLD v_ms2us(0.4)		/* in units/us */
#define X230_ACCELERATION 2.0			/* unitless factor */
#define X230_INCLINE 1.1			/* unitless factor */
#define X230_MAGIC_SLOWDOWN 0.4			/* unitless */
#define X230_TP_MAGIC_LOW_RES_FACTOR 4.0	/* unitless */

/*
 * Pointer acceleration filter constants
 */

#define MAX_VELOCITY_DIFF	v_ms2us(1) /* units/us */
#define MOTION_TIMEOUT		ms2us(1000)
#define NUM_POINTER_TRACKERS	16

struct pointer_tracker {
	struct normalized_coords delta; /* delta to most recent event */
	uint64_t time;  /* us */
	int dir;
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

	double dpi_factor;
};

static void
feed_trackers(struct pointer_accelerator *accel,
	      const struct normalized_coords *delta,
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
	trackers[current].dir = normalized_get_direction(*delta);
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
	return normalized_length(tracker->delta) / tdelta; /* units/us */
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

		/* Stop if too far away in time */
		if (time - tracker->time > MOTION_TIMEOUT ||
		    tracker->time > time) {
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

static double
acceleration_profile(struct pointer_accelerator *accel,
		     void *data, double velocity, uint64_t time)
{
	return accel->profile(&accel->base, data, velocity, time);
}

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

static inline double
calculate_acceleration_factor(struct pointer_accelerator *accel,
			      const struct normalized_coords *unaccelerated,
			      void *data,
			      uint64_t time)
{
	double velocity; /* units/us */
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

static struct normalized_coords
accelerator_filter(struct motion_filter *filter,
		   const struct normalized_coords *unaccelerated,
		   void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	double accel_value; /* unitless factor */
	struct normalized_coords accelerated;

	accel_value = calculate_acceleration_factor(accel,
						    unaccelerated,
						    data,
						    time);

	accelerated.x = accel_value * unaccelerated->x;
	accelerated.y = accel_value * unaccelerated->y;

	return accelerated;
}

static struct normalized_coords
accelerator_filter_noop(struct motion_filter *filter,
			const struct normalized_coords *unaccelerated,
			void *data, uint64_t time)
{
	return *unaccelerated;
}

static struct normalized_coords
accelerator_filter_low_dpi(struct motion_filter *filter,
			   const struct normalized_coords *unaccelerated,
			   void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	double accel_value; /* unitless factor */
	struct normalized_coords accelerated;
	struct normalized_coords unnormalized;
	double dpi_factor = accel->dpi_factor;

	/* For low-dpi mice, use device units, everything else uses
	   1000dpi normalized */
	dpi_factor = min(1.0, dpi_factor);
	unnormalized.x = unaccelerated->x * dpi_factor;
	unnormalized.y = unaccelerated->y * dpi_factor;

	accel_value = calculate_acceleration_factor(accel,
						    &unnormalized,
						    data,
						    time);

	accelerated.x = accel_value * unnormalized.x;
	accelerated.y = accel_value * unnormalized.y;

	return accelerated;
}

static struct normalized_coords
accelerator_filter_trackpoint(struct motion_filter *filter,
			      const struct normalized_coords *unaccelerated,
			      void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	double accel_value; /* unitless factor */
	struct normalized_coords accelerated;
	struct normalized_coords unnormalized;
	double dpi_factor = accel->dpi_factor;

	/* trackpoints with a dpi factor have a const accel set, remove that
	 * and restore device units. The accel profile takes const accel
	 * into account */
	dpi_factor = min(1.0, dpi_factor);
	unnormalized.x = unaccelerated->x * dpi_factor;
	unnormalized.y = unaccelerated->y * dpi_factor;

	accel_value = calculate_acceleration_factor(accel,
						    &unnormalized,
						    data,
						    time);

	accelerated.x = accel_value * unnormalized.x;
	accelerated.y = accel_value * unnormalized.y;

	return accelerated;
}

static struct normalized_coords
accelerator_filter_x230(struct motion_filter *filter,
			const struct normalized_coords *unaccelerated,
			void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	double accel_factor; /* unitless factor */
	struct normalized_coords accelerated;
	double velocity; /* units/us */

	feed_trackers(accel, unaccelerated, time);
	velocity = calculate_velocity(accel, time);
	accel_factor = calculate_acceleration(accel,
					      data,
					      velocity,
					      accel->last_velocity,
					      time);
	accel->last_velocity = velocity;

	accelerated.x = accel_factor * unaccelerated->x;
	accelerated.y = accel_factor * unaccelerated->y;

	return accelerated;
}

static struct normalized_coords
accelerator_filter_constant_x230(struct motion_filter *filter,
				 const struct normalized_coords *unaccelerated,
				 void *data, uint64_t time)
{
	struct normalized_coords normalized;
	const double factor =
		X230_MAGIC_SLOWDOWN/X230_TP_MAGIC_LOW_RES_FACTOR;

	normalized.x = factor * unaccelerated->x;
	normalized.y = factor * unaccelerated->y;

	return normalized;
}

static struct normalized_coords
touchpad_constant_filter(struct motion_filter *filter,
			 const struct normalized_coords *unaccelerated,
			 void *data, uint64_t time)
{
	struct normalized_coords normalized;

	normalized.x = TP_MAGIC_SLOWDOWN * unaccelerated->x;
	normalized.y = TP_MAGIC_SLOWDOWN * unaccelerated->y;

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
	double factor; /* unitless */
	double dpi_factor = accel_filter->dpi_factor;

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
			     double speed_in, /* 1000-dpi normalized */
			     uint64_t time)
{
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;
	const double max_accel = accel_filter->accel; /* unitless factor */
	const double threshold = accel_filter->threshold; /* units/us */
	const double incline = accel_filter->incline;
	double factor; /* unitless */

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
		       0.3 = a * 0.00 + 0.3 => a := 10
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
                              double speed_in, /* units/us */
                              uint64_t time)
{
	double factor; /* unitless */

	speed_in *= TP_MAGIC_SLOWDOWN;

	factor = pointer_accel_profile_linear(filter, data, speed_in, time);

	return factor * TP_MAGIC_SLOWDOWN;
}

double
touchpad_lenovo_x230_accel_profile(struct motion_filter *filter,
				      void *data,
				      double speed_in,
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
	 * function for an explaination of what the min/max/etc. does.
	 */
	speed_in *= X230_MAGIC_SLOWDOWN / X230_TP_MAGIC_LOW_RES_FACTOR;

	f1 = min(1, v_us2ms(speed_in) * 5);
	f2 = 1 + (v_us2ms(speed_in) - v_us2ms(threshold)) * incline;

	factor = min(max_accel, f2 > 1 ? f2 : f1);

	return factor * X230_MAGIC_SLOWDOWN / X230_TP_MAGIC_LOW_RES_FACTOR;
}

double
trackpoint_accel_profile(struct motion_filter *filter,
				void *data,
				double speed_in, /* 1000-dpi normalized */
				uint64_t time)
{
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;
	double max_accel = accel_filter->accel; /* unitless factor */
	double threshold = accel_filter->threshold; /* units/ms */
	const double incline = accel_filter->incline;
	double factor;
	double dpi_factor = accel_filter->dpi_factor;

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

struct motion_filter_interface accelerator_interface = {
	.filter = accelerator_filter,
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

	filter->dpi_factor = dpi/(double)DEFAULT_MOUSE_DPI;

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
	.filter = accelerator_filter_low_dpi,
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
	.filter = accelerator_filter,
	.filter_constant = touchpad_constant_filter,
	.restart = accelerator_restart,
	.destroy = accelerator_destroy,
	.set_speed = accelerator_set_speed,
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

	filter->dpi_factor = 1; /* unused for this accel method */

	return &filter->base;
}

struct motion_filter_interface accelerator_interface_trackpoint = {
	.filter = accelerator_filter_trackpoint,
	.filter_constant = accelerator_filter_noop,
	.restart = accelerator_restart,
	.destroy = accelerator_destroy,
	.set_speed = accelerator_set_speed,
};

struct motion_filter *
create_pointer_accelerator_filter_trackpoint(int dpi)
{
	struct pointer_accelerator *filter;

	filter = create_default_filter(dpi);
	if (!filter)
		return NULL;

	filter->base.interface = &accelerator_interface_trackpoint;
	filter->profile = trackpoint_accel_profile;
	filter->threshold = DEFAULT_THRESHOLD;
	filter->accel = DEFAULT_ACCELERATION;
	filter->incline = DEFAULT_INCLINE;

	return &filter->base;
}
