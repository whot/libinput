/*
 * Copyright © 2015 Red Hat, Inc.
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
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "filter.h"
#include "libinput-util.h"

static void
print_ptraccel_deltas(struct motion_filter *filter, double step)
{
	struct device_float_coords motion;
	struct normalized_coords accel;
	uint64_t time = 0;
	double i;

	printf("# gnuplot:\n");
	printf("# set xlabel dx unaccelerated\n");
	printf("# set ylabel dx accelerated\n");
	printf("# set style data lines\n");
	printf("# plot \"gnuplot.data\" using 1:2 title \"step %.2f\"\n", step);
	printf("#\n");

	/* Accel flattens out after 15 and becomes linear */
	for (i = 0.0; i < 15.0; i += step) {
		motion.x = i;
		motion.y = 0;
		time += us(12500); /* pretend 80Hz data */

		accel = filter_dispatch(filter, &motion, NULL, time);

		printf("%.2f	%.3f\n", i, accel.x);
	}
}

static void
print_ptraccel_movement(struct motion_filter *filter,
			int nevents,
			double max_dx,
			double step)
{
	struct device_float_coords motion;
	struct normalized_coords accel;
	uint64_t time = 0;
	double dx;
	int i;

	printf("# gnuplot:\n");
	printf("# set xlabel \"event number\"\n");
	printf("# set ylabel \"delta motion\"\n");
	printf("# set style data lines\n");
	printf("# plot \"gnuplot.data\" using 1:2 title \"dx out\", \\\n");
	printf("#      \"gnuplot.data\" using 1:3 title \"dx in\"\n");
	printf("#\n");

	if (nevents == 0) {
		if (step > 1.0)
			nevents = max_dx;
		else
			nevents = 1.0 * max_dx/step + 0.5;

		/* Print more events than needed so we see the curve
		 * flattening out */
		nevents *= 1.5;
	}

	dx = 0;

	for (i = 0; i < nevents; i++) {
		motion.x = dx;
		motion.y = 0;
		time += us(12500); /* pretend 80Hz data */

		accel = filter_dispatch(filter, &motion, NULL, time);

		printf("%d	%.3f	%.3f\n", i, accel.x, dx);

		if (dx < max_dx)
			dx += step;
	}
}

static void
print_ptraccel_sequence(struct motion_filter *filter,
			int nevents,
			double *deltas)
{
	struct device_float_coords motion;
	struct normalized_coords accel;
	uint64_t time = 0;
	double *dx;
	int i;

	printf("# gnuplot:\n");
	printf("# set xlabel \"event number\"\n");
	printf("# set ylabel \"delta motion\"\n");
	printf("# set style data lines\n");
	printf("# plot \"gnuplot.data\" using 1:2 title \"dx out\", \\\n");
	printf("#      \"gnuplot.data\" using 1:3 title \"dx in\"\n");
	printf("#\n");

	dx = deltas;

	for (i = 0; i < nevents; i++, dx++) {
		motion.x = *dx;
		motion.y = 0;
		time += us(12500); /* pretend 80Hz data */

		accel = filter_dispatch(filter, &motion, NULL, time);

		printf("%d	%.3f	%.3f\n", i, accel.x, *dx);
	}
}

/* mm/s → units/µs */
static inline double
mmps_to_upus(double mmps, int dpi)
{
	return mmps * (dpi/25.4) / 1e6;
}

static void
print_accel_func(struct motion_filter *filter,
		 accel_profile_func_t profile,
		 int dpi)
{
	double mmps;

	printf("# gnuplot:\n");
	printf("# set xlabel \"speed (mm/s)\"\n");
	printf("# set ylabel \"raw accel factor\"\n");
	printf("# set style data lines\n");
	printf("# plot \"gnuplot.data\" using 1:2 title 'accel factor'\n");
	printf("#\n");
	printf("# data: velocity(mm/s) factor velocity(units/us)\n");
	for (mmps = 0.0; mmps < 300.0; mmps += 1) {
		double units_per_us = mmps_to_upus(mmps, dpi);
		double result = profile(filter, NULL, units_per_us, 0 /* time */);
		printf("%.8f\t%.4f\t%.8f\n", mmps, result, units_per_us);
	}
}

static void
usage(void)
{
	printf("Usage: %s [options] [dx1] [dx2] [...] > gnuplot.data\n", program_invocation_short_name);
	printf("\n"
	       "Options:\n"
	       "--mode=<motion|accel|delta|sequence> \n"
	       "	motion   ... print motion to accelerated motion (default)\n"
	       "	delta    ... print delta to accelerated delta\n"
	       "	accel    ... print accel factor\n"
	       "	sequence ... print motion for custom delta sequence\n"
	       "--maxdx=<double>  ... in motion mode only. Stop increasing dx at maxdx\n"
	       "--steps=<double>  ... in motion and delta modes only. Increase dx by step each round\n"
	       "--speed=<double>  ... accel speed [-1, 1], default 0\n"
	       "--dpi=<int>	... device resolution in DPI (default: 1000)\n"
	       "--filter=<linear|low-dpi|touchpad|x230|trackpoint> \n"
	       "	linear	  ... the default motion filter\n"
	       "	low-dpi	  ... low-dpi filter, use --dpi with this argument\n"
	       "	touchpad  ... the touchpad motion filter\n"
	       "	x230  	  ... custom filter for the Lenovo x230 touchpad\n"
	       "	trackpoint... trackpoint motion filter\n"
	       "\n"
	       "If extra arguments are present and mode is not given, mode defaults to 'sequence'\n"
	       "and the arguments are interpreted as sequence of delta x coordinates\n"
	       "\n"
	       "If stdin is a pipe, mode defaults to 'sequence' and the pipe is read \n"
	       "for delta coordinates\n"
	       "\n"
	       "Delta coordinates passed into this tool must be in dpi as\n"
	       "specified by the --dpi argument\n"
	       "\n"
	       "Output best viewed with gnuplot. See output for gnuplot commands\n");
}

int
main(int argc, char **argv)
{
	struct motion_filter *filter;
	double step = 0.1,
	       max_dx = 10;
	int nevents = 0;
	bool print_accel = false,
	     print_motion = true,
	     print_delta = false,
	     print_sequence = false;
	double custom_deltas[1024];
	double speed = 0.0;
	int dpi = 1000;
	const char *filter_type = "linear";
	accel_profile_func_t profile = NULL;

	enum {
		OPT_HELP = 1,
		OPT_MODE,
		OPT_NEVENTS,
		OPT_MAXDX,
		OPT_STEP,
		OPT_SPEED,
		OPT_DPI,
		OPT_FILTER,
	};

	while (1) {
		int c;
		int option_index = 0;
		static struct option long_options[] = {
			{"help", 0, 0, OPT_HELP },
			{"mode", 1, 0, OPT_MODE },
			{"nevents", 1, 0, OPT_NEVENTS },
			{"maxdx", 1, 0, OPT_MAXDX },
			{"step", 1, 0, OPT_STEP },
			{"speed", 1, 0, OPT_SPEED },
			{"dpi", 1, 0, OPT_DPI },
			{"filter", 1, 0, OPT_FILTER },
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case OPT_HELP:
			usage();
			exit(0);
			break;
		case OPT_MODE:
			if (streq(optarg, "accel"))
				print_accel = true;
			else if (streq(optarg, "motion"))
				print_motion = true;
			else if (streq(optarg, "delta"))
				print_delta = true;
			else if (streq(optarg, "sequence"))
				print_sequence = true;
			else {
				usage();
				return 1;
			}
			break;
		case OPT_NEVENTS:
			nevents = atoi(optarg);
			if (nevents == 0) {
				usage();
				return 1;
			}
			break;
		case OPT_MAXDX:
			max_dx = strtod(optarg, NULL);
			if (max_dx == 0.0) {
				usage();
				return 1;
			}
			break;
		case OPT_STEP:
			step = strtod(optarg, NULL);
			if (step == 0.0) {
				usage();
				return 1;
			}
			break;
		case OPT_SPEED:
			speed = strtod(optarg, NULL);
			break;
		case OPT_DPI:
			dpi = strtod(optarg, NULL);
			break;
		case OPT_FILTER:
			filter_type = optarg;
			break;
		default:
			usage();
			exit(1);
			break;
		}
	}

	if (streq(filter_type, "linear")) {
		filter = create_pointer_accelerator_filter_linear(dpi);
		profile = pointer_accel_profile_linear;
	} else if (streq(filter_type, "low-dpi")) {
		filter = create_pointer_accelerator_filter_linear_low_dpi(dpi);
		profile = pointer_accel_profile_linear_low_dpi;
	} else if (streq(filter_type, "touchpad")) {
		filter = create_pointer_accelerator_filter_touchpad(dpi);
		profile = touchpad_accel_profile_linear;
	} else if (streq(filter_type, "x230")) {
		filter = create_pointer_accelerator_filter_lenovo_x230(dpi);
		profile = touchpad_lenovo_x230_accel_profile;
	} else if (streq(filter_type, "trackpoint")) {
		filter = create_pointer_accelerator_filter_trackpoint(dpi);
		profile = trackpoint_accel_profile;
	} else {
		fprintf(stderr, "Invalid filter type %s\n", filter_type);
		return 1;
	}

	assert(filter != NULL);
	filter_set_speed(filter, speed);

	if (!isatty(STDIN_FILENO)) {
		char buf[12];
		print_sequence = true;
		print_motion = false;
		nevents = 0;
		memset(custom_deltas, 0, sizeof(custom_deltas));

		while(fgets(buf, sizeof(buf), stdin) && nevents < 1024) {
			custom_deltas[nevents++] = strtod(buf, NULL);
		}
	} else if (optind < argc) {
		print_sequence = true;
		print_motion = false;
		nevents = 0;
		memset(custom_deltas, 0, sizeof(custom_deltas));
		while (optind < argc)
			custom_deltas[nevents++] = strtod(argv[optind++], NULL);
	}

	if (print_accel)
		print_accel_func(filter, profile, dpi);
	else if (print_delta)
		print_ptraccel_deltas(filter, step);
	else if (print_motion)
		print_ptraccel_movement(filter, nevents, max_dx, step);
	else if (print_sequence)
		print_ptraccel_sequence(filter, nevents, custom_deltas);

	filter_destroy(filter);

	return 0;
}
