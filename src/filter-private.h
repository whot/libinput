/*
 * Copyright © 2012 Jonas Ådahl
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

#ifndef FILTER_PRIVATE_H
#define FILTER_PRIVATE_H

#include "config.h"

#include "filter.h"

struct motion_filter_interface {
	enum libinput_config_accel_profile type;
	struct normalized_coords (*filter)(
			   struct motion_filter *filter,
			   const struct device_float_coords *unaccelerated,
			   void *data, uint64_t time);
	struct normalized_coords (*filter_constant)(
			   struct motion_filter *filter,
			   const struct device_float_coords *unaccelerated,
			   void *data, uint64_t time);
	void (*restart)(struct motion_filter *filter,
			void *data,
			uint64_t time);
	void (*destroy)(struct motion_filter *filter);
	bool (*set_speed)(struct motion_filter *filter,
			  double speed_adjustment);
};

struct motion_filter {
	double speed_adjustment; /* normalized [-1, 1] */
	struct motion_filter_interface *interface;
};

#endif
