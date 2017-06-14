/*
 * Copyright © 2018 Red Hat, Inc.
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
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>

#include "filter.h"
#include "libinput-util.h"
#include "filter-private.h"

struct acceleration_curve_point {
	double x, fx;
};

struct custom_accelerator {
	struct motion_filter base;
	struct acceleration_curve_point points[32];
	size_t npoints;

	double last_velocity;
	struct pointer_trackers trackers;
};

double
custom_accel_profile(struct motion_filter *filter,
		     void *data,
		     double speed_in, /* in device units/µs */
		     uint64_t time)
{
	struct custom_accelerator *f =
		(struct custom_accelerator*)filter;
	double fx = 1;

	speed_in *= 1000;

	if (f->npoints == 0)
		return 1.0;

	if (f->points[0].x >= speed_in)
		return f->points[0].fx;

	for (size_t i = 0; i < f->npoints - 1; i++) {
		double a, b, fa, fb;
		double k, d;

		if (f->points[i + 1].x < speed_in)
			continue;

		/*
		   We haves points f(i), f(i+1), defining two points on the
		   curve. linear function in the form y = kx+d:

		   y = kx + d

		   y1 = kx1 + d -> d = y1 - kx1
		   y2 = kx2 + d -> d = y2 - kx2

		   y1 - kx1 = y2 - kx2
		   y1 - y2 = kx1 - kx2
		   k = y1-y2/(x1 - x2)

		 */
		a  = f->points[i].x;
		fa = f->points[i].fx;
		b  = f->points[i+1].x;
		fb = f->points[i+1].fx;

		k = (fa - fb)/(a - b);
		d = fa - k * a;

		fx = k * speed_in + d;

		return fx;
	}

	return f->points[f->npoints - 1].fx;
}

static struct normalized_coords
custom_accelerator_filter(struct motion_filter *filter,
			  const struct device_float_coords *units,
			  void *data, uint64_t time)
{
	struct custom_accelerator *f =
		(struct custom_accelerator*)filter;
	struct normalized_coords norm;
	double velocity; /* units/us in device-native dpi*/
	double accel_factor;

	trackers_feed(&f->trackers, units, time);
	velocity = trackers_velocity(&f->trackers, time);
	accel_factor = calculate_acceleration_simpsons(filter,
						       custom_accel_profile,
						       data,
						       velocity,
						       f->last_velocity,
						       time);
	f->last_velocity = velocity;

	norm.x = accel_factor * units->x;
	norm.y = accel_factor * units->y;

	return norm;
}

static bool
custom_accelerator_set_speed(struct motion_filter *filter,
			     double speed_adjustment)
{
	assert(speed_adjustment >= -1.0 && speed_adjustment <= 1.0);

	/* noop, this function has no effect in the custom interface */

	return true;
}

static void
custom_accelerator_destroy(struct motion_filter *filter)
{
	struct custom_accelerator *accel_filter =
		(struct custom_accelerator*)filter;

	trackers_free(&accel_filter->trackers);
	free(accel_filter);
}

static bool
custom_accelerator_set_curve_point(struct motion_filter *filter,
				   double a, double fa)
{
	struct custom_accelerator *f =
		(struct custom_accelerator*)filter;

	if (f->npoints == ARRAY_LENGTH(f->points))
		return false;

	if (a < 0 || a > 50000)
		return false;

	if (f->npoints == 0) {
		f->points[0].x = a;
		f->points[0].fx = fa;
		f->npoints = 1;
		return true;
	} else if (f->points[f->npoints - 1].x < a) {
		f->points[f->npoints].x = a;
		f->points[f->npoints].fx = fa;
		f->npoints++;
		return true;
	}

	for (size_t i = 0; i < f->npoints; i++) {
		if (f->points[i].x == a) {
			f->points[i].fx = fa;
			break;
		} else if (f->points[i].x > a) {
			f->npoints++;
			for (size_t j = f->npoints - 1; j > i; j--)
				f->points[j] = f->points[j-1];
			f->points[i] = (struct acceleration_curve_point){ a, fa };
			break;
		}
	}

	return true;
}

struct motion_filter_interface accelerator_interface_custom = {
	.type = LIBINPUT_CONFIG_ACCEL_PROFILE_CUSTOM_DEVICE_SPEED,
	.filter = custom_accelerator_filter,
	.filter_constant = NULL,
	.restart = NULL,
	.destroy = custom_accelerator_destroy,
	.set_speed = custom_accelerator_set_speed,
	.set_curve_point = custom_accelerator_set_curve_point,
};

struct motion_filter *
create_pointer_accelerator_filter_custom_device_speed(void)
{
	struct custom_accelerator *filter;

	filter = zalloc(sizeof *filter);
	trackers_init(&filter->trackers);

	filter->base.interface = &accelerator_interface_custom;

	return &filter->base;
}
