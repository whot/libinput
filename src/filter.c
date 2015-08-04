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

struct normalized_coords
filter_dispatch(struct motion_filter *filter,
		const struct normalized_coords *unaccelerated,
		void *data, uint64_t time)
{
	return filter->interface->filter(filter, unaccelerated, data, time);
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
		 double speed)
{
	return filter->interface->set_speed(filter, speed);
}

double
filter_get_speed(struct motion_filter *filter)
{
	return filter->speed;
}

/*
 * Default parameters for pointer acceleration profiles.
 */

#define DEFAULT_THRESHOLD 0.0004		/* in units/us */
#define MINIMUM_THRESHOLD 0.0002		/* in units/us */
#define DEFAULT_ACCELERATION 2.0		/* unitless factor */
#define DEFAULT_INCLINE 1.1			/* unitless factor */

/*
 * Pointer acceleration filter constants
 */

#define MAX_VELOCITY_DIFF	0.001 /* units/us */
#define MOTION_TIMEOUT		ms2us(1000)
#define NUM_POINTER_TRACKERS	16

struct pointer_tracker {
	struct normalized_coords delta; /* delta to most recent event */
	uint64_t time;  /* us */
	int dir;
};

struct pointer_accelerator;
struct pointer_accelerator {
	struct motion_filter base;

	accel_profile_func_t profile;

	double velocity;	/* units/us */
	double last_velocity;	/* units/us */
	struct normalized_coords last;

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

static struct normalized_coords
accelerator_filter(struct motion_filter *filter,
		   const struct normalized_coords *unaccelerated,
		   void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	double velocity; /* units/us */
	double accel_value; /* unitless factor */
	struct normalized_coords accelerated;
	struct normalized_coords unnormalized;
	double dpi_factor = accel->dpi_factor;

	/* For low-dpi mice, use device units, everything else uses
	   1000dpi normalized */
	dpi_factor = min(1.0, dpi_factor);
	unnormalized.x = unaccelerated->x * dpi_factor;
	unnormalized.y = unaccelerated->y * dpi_factor;

	feed_trackers(accel, &unnormalized, time);
	velocity = calculate_velocity(accel, time);
	accel_value = calculate_acceleration(accel,
					     data,
					     velocity,
					     accel->last_velocity,
					     time);

	accelerated.x = accel_value * unnormalized.x;
	accelerated.y = accel_value * unnormalized.y;

	accel->last = unnormalized;

	accel->last_velocity = velocity;

	return accelerated;
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
		      double speed)
{
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;

	assert(speed >= -1.0 && speed <= 1.0);

	/* delay when accel kicks in */
	accel_filter->threshold = DEFAULT_THRESHOLD - speed / 4000.0;
	if (accel_filter->threshold < MINIMUM_THRESHOLD)
		accel_filter->threshold = MINIMUM_THRESHOLD;

	/* adjust max accel factor */
	accel_filter->accel = DEFAULT_ACCELERATION + speed * 1.5;

	/* higher speed -> faster to reach max */
	accel_filter->incline = DEFAULT_INCLINE + speed * 0.75;

	filter->speed = speed;
	return true;
}

struct motion_filter_interface accelerator_interface = {
	accelerator_filter,
	accelerator_restart,
	accelerator_destroy,
	accelerator_set_speed,
};

struct motion_filter *
create_pointer_accelerator_filter(accel_profile_func_t profile,
				  int dpi)
{
	struct pointer_accelerator *filter;

	filter = zalloc(sizeof *filter);
	if (filter == NULL)
		return NULL;

	filter->base.interface = &accelerator_interface;

	filter->profile = profile;
	filter->last_velocity = 0.0;
	filter->last.x = 0;
	filter->last.y = 0;

	filter->trackers =
		calloc(NUM_POINTER_TRACKERS, sizeof *filter->trackers);
	filter->cur_tracker = 0;

	filter->threshold = DEFAULT_THRESHOLD;
	filter->accel = DEFAULT_ACCELERATION;
	filter->incline = DEFAULT_INCLINE;

	filter->dpi_factor = dpi/(double)DEFAULT_MOUSE_DPI;

	return &filter->base;
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

	double s1, s2;
	double max_accel = accel_filter->accel; /* unitless factor */
	const double threshold = accel_filter->threshold; /* units/us */
	const double incline = accel_filter->incline;
	double factor;
	double dpi_factor = accel_filter->dpi_factor;

	max_accel /= dpi_factor;

	s1 = min(1, 0.3 + speed_in * 10000.0);
	s2 = 1 + (speed_in * 1000.0 - threshold * dpi_factor * 1000.0) * incline;

	factor = min(max_accel, s2 > 1 ? s2 : s1);

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

	double s1, s2;
	const double max_accel = accel_filter->accel; /* unitless factor */
	const double threshold = accel_filter->threshold; /* units/us */
	const double incline = accel_filter->incline;
	double factor;

	s1 = min(1, 0.3 + speed_in * 10 * 1000.0);
	s2 = 1 + (speed_in * 1000.0 - threshold * 1000.0) * incline;

	factor =  min(max_accel, s2 > 1 ? s2 : s1);

	return factor;
}

double
touchpad_accel_profile_linear(struct motion_filter *filter,
                              void *data,
                              double speed_in,
                              uint64_t time)
{
	/* Once normalized, touchpads see the same
	   acceleration as mice. that is technically correct but
	   subjectively wrong, we expect a touchpad to be a lot
	   slower than a mouse. Apply a magic factor here and proceed
	   as normal.  */
	const double TP_MAGIC_SLOWDOWN = 0.4;
	double speed_out;

	speed_in *= TP_MAGIC_SLOWDOWN;

	speed_out = pointer_accel_profile_linear(filter, data, speed_in, time);

	return speed_out * TP_MAGIC_SLOWDOWN;
}

double
touchpad_lenovo_x230_accel_profile(struct motion_filter *filter,
				      void *data,
				      double speed_in,
				      uint64_t time)
{
	/* Keep the magic factor from touchpad_accel_profile_linear.  */
	const double TP_MAGIC_SLOWDOWN = 0.4;

	/* Those touchpads presents an actual lower resolution that what is
	 * advertised. We see some jumps from the cursor due to the big steps
	 * in X and Y when we are receiving data.
	 * Apply a factor to minimize those jumps at low speed, and try
	 * keeping the same feeling as regular touchpads at high speed.
	 * It still feels slower but it is usable at least */
	const double TP_MAGIC_LOW_RES_FACTOR = 4.0;
	double speed_out;
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;

	double s1, s2;
	const double max_accel = accel_filter->accel *
				  TP_MAGIC_LOW_RES_FACTOR; /* unitless factor */
	const double threshold = accel_filter->threshold /
				  TP_MAGIC_LOW_RES_FACTOR; /* units/us */
	const double incline = accel_filter->incline * TP_MAGIC_LOW_RES_FACTOR;

	speed_in *= TP_MAGIC_SLOWDOWN / TP_MAGIC_LOW_RES_FACTOR;

	s1 = min(1, speed_in * 5 * 1000.0);
	s2 = 1 + (speed_in  * 1000.0 - threshold * 1000.0) * incline;

	speed_out = min(max_accel, s2 > 1 ? s2 : s1);

	return speed_out * TP_MAGIC_SLOWDOWN / TP_MAGIC_LOW_RES_FACTOR;
}
