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

#define DEFAULT_CONSTANT_ACCELERATION 10.0
#define DEFAULT_THRESHOLD 4.0
#define DEFAULT_ACCELERATION 2.0

/*
 * Pointer acceleration filter constants
 */

#define MAX_VELOCITY_DIFF	1.0
#define MOTION_TIMEOUT		300 /* (ms) */
#define NUM_POINTER_TRACKERS	16

struct pointer_tracker {
	double dx;
	double dy;
	uint64_t time;
	int dir;
};

struct pointer_accelerator;
struct pointer_accelerator {
	struct motion_filter base;

	accel_profile_func_t profile;

	double velocity;
	double last_velocity;
	int last_dx;
	int last_dy;

	struct pointer_tracker *trackers;
	int cur_tracker;

	double threshold;
	double accel;
};

enum directions {
	N  = 1 << 0,
	NE = 1 << 1,
	E  = 1 << 2,
	SE = 1 << 3,
	S  = 1 << 4,
	SW = 1 << 5,
	W  = 1 << 6,
	NW = 1 << 7,
	UNDEFINED_DIRECTION = 0xff
};

static int
get_direction(int dx, int dy)
{
	int dir = UNDEFINED_DIRECTION;
	int d1, d2;
	double r;

	if (abs(dx) < 2 && abs(dy) < 2) {
		if (dx > 0 && dy > 0)
			dir = S | SE | E;
		else if (dx > 0 && dy < 0)
			dir = N | NE | E;
		else if (dx < 0 && dy > 0)
			dir = S | SW | W;
		else if (dx < 0 && dy < 0)
			dir = N | NW | W;
		else if (dx > 0)
			dir = NE | E | SE;
		else if (dx < 0)
			dir = NW | W | SW;
		else if (dy > 0)
			dir = SE | S | SW;
		else if (dy < 0)
			dir = NE | N | NW;
	} else {
		/* Calculate r within the interval  [0 to 8)
		 *
		 * r = [0 .. 2π] where 0 is North
		 * d_f = r / 2π  ([0 .. 1))
		 * d_8 = 8 * d_f
		 */
		r = atan2(dy, dx);
		r = fmod(r + 2.5*M_PI, 2*M_PI);
		r *= 4*M_1_PI;

		/* Mark one or two close enough octants */
		d1 = (int)(r + 0.9) % 8;
		d2 = (int)(r + 0.1) % 8;

		dir = (1 << d1) | (1 << d2);
	}

	return dir;
}

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
	trackers[current].dir = get_direction(dx, dy);
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
	int dx;
	int dy;
	double distance;

	dx = tracker->dx;
	dy = tracker->dy;
	distance = sqrt(dx*dx + dy*dy);
	return distance / (double)(time - tracker->time);
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

	return factor;
}

static void
accelerator_filter(struct motion_filter *filter,
		   struct motion_params *motion,
		   void *data, uint64_t time)
{
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;
	double velocity;
	double accel_value;

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
	struct pointer_accelerator *accel =
		(struct pointer_accelerator *) filter;

	/* speed is in the [-1, 1] range, divide it into a couple of
	   discrete step.  */
	const struct accel {
		double threshold;
		double accel;
	} lut[11] = {
		{ 10, 0.7 },
		{ 8, 0.9 },
		{ 7, 1.0 },
		{ 6, 1.4 },
		{ 5, 1.7 },
		{ DEFAULT_THRESHOLD, DEFAULT_ACCELERATION },
		{ 3, 2.5 },
		{ 2, 3.0 },
		{ 1, 4.0 },
		{ 1, 5.0 },
		{ 1, 6.0 },
	};
	int idx;

	assert(speed >= -1.0 && speed <= 1.0);

	idx = (speed + 1.0)/2.0 * 10;
	accel->accel = lut[idx].accel;
	accel->threshold = lut[idx].threshold;

	filter->speed = (double)idx/10.0 * 2.0 - 1;

	return true;
}

struct motion_filter_interface accelerator_interface = {
	accelerator_filter,
	accelerator_destroy,
	accelerator_set_speed,
};

struct motion_filter *
create_pointer_accelator_filter(accel_profile_func_t profile)
{
	struct pointer_accelerator *filter;

	filter = malloc(sizeof *filter);
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

	return &filter->base;
}

static inline double
calc_penumbral_gradient(double x)
{
	x *= 2.0;
	x -= 1.0;
	return 0.5 + (x * sqrt(1.0 - x * x) + asin(x)) / M_PI;
}

double
pointer_accel_profile_smooth_simple(struct motion_filter *filter,
				    void *data,
				    double velocity,
				    uint64_t time)
{
	struct pointer_accelerator *accel_filter =
		(struct pointer_accelerator *) filter;

	double threshold = accel_filter->threshold;
	double accel = accel_filter->accel;
	double smooth_accel_coefficient;
	/* Increasing this makes reaching max accel take longer (min 1.0) */
	const double stretch = 3.0;

	if (threshold < 1.0)
		threshold = 1.0;
	if (accel < 1.0)
		accel = 1.0;

	velocity *= DEFAULT_CONSTANT_ACCELERATION;

	if (velocity < (threshold / 2.0))
		return calc_penumbral_gradient(0.5 + velocity / threshold) * 2.0 - 1.0;

	if (velocity <= threshold)
		return 1.0;

	velocity /= threshold;
	if (velocity < accel) {
		/* Velocity is 1.0 - accel, scale this to 0.0 - 0.5 */
		velocity = 0.5 * (velocity - 1.0) / (accel - 1.0);
	} else if (velocity < (accel * stretch)) {
		/* Velocity is accel - (accel * stretch),
		   scale this to 0.5 - 1.0 */
		velocity = 0.5 + 0.5 *
				(velocity - accel) / (accel * (stretch - 1.0));
	} else
		return accel;

	smooth_accel_coefficient = calc_penumbral_gradient(velocity);
	return 1.0 + (smooth_accel_coefficient * (accel - 1.0));
}
