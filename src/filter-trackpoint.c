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

struct tablet_accelerator_flat {
	struct motion_filter base;

	double factor;
	int xres, yres;
	double xres_scale, /* 1000dpi : tablet res */
	       yres_scale; /* 1000dpi : tablet res */
};

struct trackpoint_accelerator {
	struct motion_filter base;

	struct pointer_trackers trackers;
	double speed_factor;
};

double
trackpoint_accel_profile(struct motion_filter *filter,
			 void *data,
			 double velocity)
{
	struct trackpoint_accelerator *accel_filter =
		(struct trackpoint_accelerator *)filter;
	double factor;

	velocity = v_us2ms(velocity); /* make it units/ms */

	/* https://mycurvefit.com/ input data
	 * 0    0
	 * 0.1  1
	 * 0.4  3
	 * 0.6  4
	 */
	factor = 17.50959 + (7.291981e-16 - 17.50959)/(1 + pow(velocity/2.371344,0.88563));
	factor = max(0.3, factor);

	factor *= accel_filter->speed_factor;
	return factor;
}

static struct normalized_coords
trackpoint_accelerator_filter(struct motion_filter *filter,
			      const struct device_float_coords *unaccelerated,
			      void *data, uint64_t time)
{
	struct trackpoint_accelerator *accel_filter =
		(struct trackpoint_accelerator *)filter;
	struct normalized_coords coords;
	double f;
	double velocity;

	trackers_feed(&accel_filter->trackers, unaccelerated, time);
	velocity = trackers_velocity(&accel_filter->trackers, time);

	f = trackpoint_accel_profile(filter, data, velocity);
	coords.x = unaccelerated->x * f;
	coords.y = unaccelerated->y * f;

	return coords;
}

static struct normalized_coords
trackpoint_accelerator_filter_noop(struct motion_filter *filter,
				   const struct device_float_coords *unaccelerated,
				   void *data, uint64_t time)
{

	struct normalized_coords coords;

	coords.x = unaccelerated->x;
	coords.y = unaccelerated->y;

	return coords;
}

/* Maps the [-1, 1] speed setting into a constant acceleration
 * range. This isn't a linear scale, we keep 0 as the 'optimized'
 * mid-point and scale down to 0 for setting -1 and up to 5 for
 * setting 1. On the premise that if you want a faster cursor, it
 * doesn't matter as much whether you have 0.56789 or 0.56790,
 * but for lower settings it does because you may lose movements.
 * *shrug*.
 *
 * Magic numbers calculated by MyCurveFit.com, data points were
 *  0.0 0.0
 *  0.1 0.1 (because we need 4 points)
 *  1   1
 *  2   5
 *
 *  This curve fits nicely into the range necessary.
 */
static inline double
speed_factor(double s)
{
	s += 1; /* map to [0, 2] */
	return 435837.2 + (0.04762636 - 435837.2)/(1 + pow(s/240.4549,
							   2.377168));
}

static bool
trackpoint_accelerator_set_speed(struct motion_filter *filter,
				 double speed_adjustment)
{
	struct trackpoint_accelerator *accel_filter =
		(struct trackpoint_accelerator*)filter;

	assert(speed_adjustment >= -1.0 && speed_adjustment <= 1.0);

	filter->speed_adjustment = speed_adjustment;
	accel_filter->speed_factor = speed_factor(speed_adjustment);


	return true;
}

static void
trackpoint_accelerator_restart(struct motion_filter *filter,
			       void *data,
			       uint64_t time)
{
	struct trackpoint_accelerator *accel =
		(struct trackpoint_accelerator *) filter;

	trackers_reset(&accel->trackers, time);
}

static void
trackpoint_accelerator_destroy(struct motion_filter *filter)
{
	struct trackpoint_accelerator *accel_filter =
		(struct trackpoint_accelerator *)filter;

	trackers_free(&accel_filter->trackers);
	free(accel_filter);
}

struct motion_filter_interface accelerator_interface_trackpoint = {
	.type = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE,
	.filter = trackpoint_accelerator_filter,
	.filter_constant = trackpoint_accelerator_filter_noop,
	.restart = trackpoint_accelerator_restart,
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

	/* FIXME: should figure out some thing here to deal with the
	 * trackpoint range/max hw delta */

	trackers_init(&filter->trackers);

	filter->base.interface = &accelerator_interface_trackpoint;

	return &filter->base;
}
