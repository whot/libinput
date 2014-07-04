/*
 * Copyright © 2014 Red Hat, Inc.
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
#define _GNU_SOURCE
#include <config.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <filter.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static double
units_per_ms_to_mm_per_s(double units)
{
	units *= 1000; /* units/s */
	units /= 400.0; /* assume 400 dpi -> in/s */
	units = units * 2.54 * 10; /* mm/s */

	return units;
}

static void
print_accel_func(struct motion_filter *filter)
{
	double vel;

	if (isatty(STDOUT_FILENO)|| isatty(STDERR_FILENO)) {
		printf("Usage: %s >gnuplot-data-file 2>gnuplot-command-file\n",
		       program_invocation_short_name);
		return;
       }

	/* Print gnuplot file to stderr, data to stdout */
	fprintf(stderr,
	"set terminal png\n"
	"set output 'ptraccel-profiles.png'\n"
	"set style data lines\n"
	"set xlabel 'velocity in mm/s'\n"
	"set ylabel 'accel factor'\n"
	"set yrange [0:3]\n"
	"plot 'ptraccel-profiles.data' using 1:2 title 'smooth', \\\n"
	"     '' using 1:3 title 'stretched', \\\n"
	"     '' using 1:4 title 'linear'\n"
	);

	for (vel = 0.0; vel < 3.0; vel += .01) {
		double result[3];
		double speed = units_per_ms_to_mm_per_s(vel);

		/* profiles take velocity in units/ms */
		result[0] = pointer_accel_profile_smooth_simple(filter,
								NULL,
								vel,
								0 /* time */);
		result[1] = pointer_accel_profile_smooth_stretched(filter,
								NULL,
								vel,
								0 /* time */);
		result[2] = pointer_accel_profile_linear(filter,
								NULL,
								vel,
								0 /* time */);
		printf("%.4f\t%.4f\t%.4f\t%.4f\n",
		       speed, result[0], result[1], result[2]);
	}
}

int
main(int argc, char **argv)
{
	struct motion_filter *filter;

	filter = create_pointer_accelator_filter(pointer_accel_profile_smooth_simple);
	assert(filter != NULL);

	print_accel_func(filter);
	filter_destroy(filter);

	return 0;
}
