/*
 * Copyright © 2012 Jonas Ådahl
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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>

#include "filter.h"
#include "libinput-util.h"
#include "filter-private.h"

void
filter_dispatch(struct motion_filter *filter,
		struct motion_params *motion,
		void *data, uint64_t time)
{
	filter->interface->filter(filter, motion, data, time);
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

#define DEFAULT_THRESHOLD 0.4			/* in units/ms */
#define DEFAULT_ACCELERATION 2.0		/* unitless factor */
#define DEFAULT_INCLINE 1.1			/* unitless factor */

/*
 * Pointer acceleration filter constants
 */

#define MAX_VELOCITY_DIFF	1.0 /* units/ms */
#define MOTION_TIMEOUT		300 /* (ms) */
#define NUM_POINTER_TRACKERS	16

struct pointer_tracker {
	double dx;	/* delta to most recent event, in device units */
	double dy;	/* delta to most recent event, in device units */
	uint64_t time;  /* ms */
	int dir;
};

struct pointer_accelerator;
struct pointer_accelerator {
	struct motion_filter base;

	accel_profile_func_t profile;

	double velocity;	/* units/ms */
	double last_velocity;	/* units/ms */
	int last_dx;		/* device units */
	int last_dy;		/* device units */

	struct pointer_tracker *trackers;
	int cur_tracker;

	double threshold;	/* units/ms */
	double accel;		/* unitless factor */
	double incline;		/* incline of the function */
};

static void
feed_trackers(struct pointer_accelerator *accel,
	      double dx, double dy,
	      uint64_t time)
{
	int i, current;
	struct pointer_tracker *trackers = accel->trackers;

	for (i = 0; i < NUM_POINTER_TRACKERS; i++) {
		trackers[i].dx += dx;
		trackers[i].dy += dy;
	}

	current = (accel->cur_tracker + 1) % NUM_POINTER_TRACKERS;
	accel->cur_tracker = current;

	trackers[current].dx = 0.0;
	trackers[current].dy = 0.0;
	trackers[current].time = time;
	trackers[current].dir = vector_get_direction(dx, dy);
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
	double dx;
	double dy;
	double distance;

	dx = tracker->dx;
	dy = tracker->dy;
	distance = sqrt(dx*dx + dy*dy);
	return distance / (double)(time - tracker->time); /* units/ms */
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
		    tracker->time > time)
			break;

		/* Stop if direction changed */
		dir &= tracker->dir;
		if (dir == 0)
			break;

		velocity = calculate_tracker_velocity(tracker, time);

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

	return result; /* units/ms */
}

static double
acceleration_profile(struct pointer_accelerator *accel,
		     void *data, double velocity, uint64_t time)
{
	return accel->profile(&accel->base, data, velocity, time);
}

static double
calculate_acceleration(struct pointer_accelerator *accel,
		       void *data, double velocity, uint64_t time)
{
	double factor;

	/* Use Simpson's rule to calculate the avarage acceleration between
	 * the previous motion and the most recent. */
	factor = acceleration_profile(accel, data, velocity, time);
	factor += acceleration_profile(accel, data, accel->last_velocity, time);
	factor += 4.0 *
		acceleration_profile(accel, data,
				     (accel->last_velocity + velocity) / 2,
				     time);

	factor = factor / 6.0;

	return factor; /* unitless factor */
}

static void
accelerator_filter(struct motion_filter *filter,
		   struct motion_params *motion,
		   void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	double velocity; /* units/ms */
	double accel_value; /* unitless factor */

	feed_trackers(accel, motion->dx, motion->dy, time);
	velocity = calculate_velocity(accel, time);
	accel_value = calculate_acceleration(accel, data, velocity, time);

	motion->dx = accel_value * motion->dx;
	motion->dy = accel_value * motion->dy;

	accel->last_dx = motion->dx;
	accel->last_dy = motion->dy;

	accel->last_velocity = velocity;
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
	accel_filter->threshold = DEFAULT_THRESHOLD - speed/6.0;

	/* adjust max accel factor */
	accel_filter->accel = DEFAULT_ACCELERATION + speed;

	/* higher speed -> faster to reach max */
	accel_filter->incline = DEFAULT_INCLINE + speed/2.0;

	filter->speed = speed;
	return true;
}

struct motion_filter_interface accelerator_interface = {
	accelerator_filter,
	accelerator_destroy,
	accelerator_set_speed,
};

struct motion_filter *
create_pointer_accelerator_filter(accel_profile_func_t profile)
{
	struct pointer_accelerator *filter;

	filter = zalloc(sizeof *filter);
	if (filter == NULL)
		return NULL;

	filter->base.interface = &accelerator_interface;

	filter->profile = profile;
	filter->last_velocity = 0.0;
	filter->last_dx = 0;
	filter->last_dy = 0;

	filter->trackers =
		calloc(NUM_POINTER_TRACKERS, sizeof *filter->trackers);
	filter->cur_tracker = 0;

	filter->threshold = DEFAULT_THRESHOLD;
	filter->accel = DEFAULT_ACCELERATION;
	filter->incline = DEFAULT_INCLINE;

	return &filter->base;
}

double
pointer_accel_profile_linear(struct motion_filter *filter,
			     void *data,
			     double speed_in,
			     uint64_t time)
{
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *)filter;

	double s1, s2;
	const double max_accel = accel_filter->accel; /* unitless factor */
	const double threshold = accel_filter->threshold; /* units/ms */
	const double incline = accel_filter->incline;

	s1 = min(1, speed_in * 5);
	s2 = 1 + (speed_in - threshold) * incline;

	return min(max_accel, s2 > 1 ? s2 : s1);
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
