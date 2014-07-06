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
#include <alloca.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <filter.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libinput-util.h>

static double
units_to_m_per_s(double units)
{
	units *= 125; /* units/s */
	units /= 400.0; /* assume 400 dpi -> in/s */
	units = units * 2.54 / 100; /* m/s */

	return units;
}

static void
print_gnuplot_header(const char *xlabel,
		     const char *ylabel)
{
	printf("#!/usr/bin/gnuplot\n"
	       "set style data lines\n"
	       "set xlabel '%s'\n"
	       "set ylabel '%s'\n", xlabel, ylabel);
}

static void
print_gnuplot_footer(void)
{
	printf("pause -1\n");
}

static void
print_ptraccel_speed(struct motion_filter *filter, double step)
{
	struct motion_params motion;
	const int nevents = 30;
	uint64_t time = 0;
	double dx;
	double *speed,
	       *gain; /* difference between input and output speed */
	int i;
	int idx;

	print_gnuplot_header("unaccel dx in m/s",
			     "accelerated dx in m/s");
	printf("set multiplot layout 1,2\n");
	printf("plot '-' using 1:2 title 'm/s',"
	       "     '-' using 1:2 title 'gain m/s'\n");

	speed = alloca(128/step * sizeof(double));
	gain = alloca(128/step * sizeof(double));

	/* for all deltas in 0..127, sent a set of events and total up the
	   pointer movements. Then use the avg movement of that total to
	   calculate speed in m/s, then map input speed to output speed.
	   127 is the max dx possible in a 7-bit report field */
	for (dx = 0; dx <= 127; dx += step) {
		double sum = 0;

		/* use 30 events to hide the tracker startup */
		for (i = 0; i < nevents; i++) {
			motion.dx = dx;
			motion.dy = 0;
			time += 8; /* ms */

			filter_dispatch(filter, &motion, NULL, time);

			sum += motion.dx;
		}

		idx = dx/step;

		speed[idx] = units_to_m_per_s(sum/nevents);
		gain[idx] = speed[idx] - units_to_m_per_s(dx);

		time += 1000; /* reset trackers with fake timeout */
	}

	for (i = 0; i <= idx; i++) {
		printf("\t%f %f\n",
		       units_to_m_per_s(i * step),
		       speed[i]);
	}
	printf("\te\n");

	for (i = 0; i <= idx; i++) {
		printf("\t%f %f\n",
		       units_to_m_per_s(i * step),
		       gain[i]);
	}

	printf("\te\n");

	printf("plot '-' using 1:2 title 'gain'\n");
	for (i = 0; i < idx; i++) {
		double unitless_gain = 0;
		if (speed[i] != 0.0)
			unitless_gain = gain[i]/speed[i];
		printf("\t%f %f\n",
		       units_to_m_per_s(i * step),
		       unitless_gain);
	}
	printf("\te\n");

	print_gnuplot_footer();
}

static void
print_ptraccel_deltas(struct motion_filter *filter, double step)
{
	struct motion_params motion;
	uint64_t time = 0;
	double i;

	print_gnuplot_header("dx unaccelerated",
			     "dx accelerated");
	printf("plot '-' using 1:2 title 'step %f'\n", step);

	/* Accel flattens out after 15 and becomes linear */
	for (i = 0.0; i < 15.0; i += step) {
		motion.dx = i;
		motion.dy = 0;
		time += 12; /* pretend 80Hz data */

		filter_dispatch(filter, &motion, NULL, time);

		printf("\t%f	%.3f\n", i, motion.dx);
	}

	printf("\te\n");
	print_gnuplot_footer();
}

static void
print_ptraccel_movement(struct motion_filter *filter,
			int nevents,
			double min_dx,
			double max_dx,
			double step)
{
	struct motion_params motion;
	uint64_t time = 0;
	double dx;
	int i;
	double dx_out[nevents],
	       dx_in[nevents];

	print_gnuplot_header("event number", "delta motion");
	printf("plot '-' using 1:2 title 'dx out' with lines,"
	       "     '-' using 1:2 title 'dx in' with lines\n");

	if (nevents == 0) {
		if (step > 1.0)
			nevents = max_dx;
		else
			nevents = 1.0 * max_dx/step + 0.5;

		/* Print more events than needed so we see the curve
		 * flattening out */
		nevents *= 1.5;
	}

	dx = min_dx;

	for (i = 0; i < nevents; i++) {
		motion.dx = dx;
		motion.dy = 0;
		time += 12; /* pretend 80Hz data */

		filter_dispatch(filter, &motion, NULL, time);

		dx_in[i] = dx;
		dx_out[i] = motion.dx;

		if (dx < max_dx)
			dx += step;
	}

	for (i = 0; i < ARRAY_LENGTH(dx_in); i++) {
		printf("\t%d	%.3f\n", i, dx_out[i]);
	}

	printf("\te\n");

	for (i = 0; i < ARRAY_LENGTH(dx_in); i++) {
		printf("\t%d	%.3f\n", i, dx_in[i]);
	}

	printf("\te\n");
	print_gnuplot_footer();
}

static void
print_ptraccel_sequence(struct motion_filter *filter,
			int nevents,
			double *deltas)
{
	struct motion_params motion;
	uint64_t time = 0;
	double *dx;
	int i;

	print_gnuplot_header("event number", "delta motion");
	printf("plot '-' using 1:2 title 'dx out', "
	       "     '-' using 1:2 title 'dx in'\n");

	dx = deltas;

	for (i = 0; i < nevents; i++, dx++) {
		motion.dx = *dx;
		motion.dy = 0;
		time += 12; /* pretend 80Hz data */

		filter_dispatch(filter, &motion, NULL, time);

		printf("%d	%.3f\n", i, motion.dx);
	}

	dx = deltas;

	printf("\te\n");

	for (i = 0; i < nevents; i++, dx++)
		printf("%d	%.3f\n", i, *dx);

	printf("\te\n");
	print_gnuplot_footer();
}

static void
print_accel_func(struct motion_filter *filter,
		 double *sequence,
		 size_t sz)
{
	double *vel, last;

	print_gnuplot_header("velocity", "accel factor");
	printf("plot '-' using 1:2 title 'raw',"
	       "     '-' using 1:2 title 'Simpsons'\n");

	for (vel = sequence; vel < sequence + sz; vel ++) {
		double result = pointer_accel_profile_smooth_simple(filter,
								    NULL,
								    *vel,
								    0 /* time */);
		printf("\t%.4f\t%.4f\n", *vel, result);
	}
	printf("\te\n");

	for (vel = sequence, last = 0.0; vel < sequence + sz; last = *vel, vel++) {
		double result, mid;
		result = pointer_accel_profile_smooth_simple(filter, NULL,
							     *vel, 0 /* time */);
		result += pointer_accel_profile_smooth_simple(filter, NULL,
							      last, 0 /* time */);
		mid = (last + *vel)/2;
		result += 4 *
			  pointer_accel_profile_smooth_simple(filter, NULL,
							      mid, 0 /* time */);
		result /= 6.0;
		printf("\t%.4f\t%.4f\n", *vel, result);
	}
	printf("\te\n");

	print_gnuplot_footer();
}

static size_t
steps_to_sequence(double **out, double min, double max, double step)
{
	size_t sz = (max - min + 1)/step;
	double i;

	*out = malloc(sz * sizeof(double));

	for (i = min, sz = 0; i <= max; i += step, sz++)
		(*out)[sz] = i;
	return sz;
}

static size_t
doubles_from_stdin(double **out)
{
	char buf[12];
	const size_t MAXEVENTS = 1024;
	size_t sz = 0;

	*out = malloc(MAXEVENTS * sizeof(double));

	while(fgets(buf, sizeof(buf), stdin)) {
		assert(sz < MAXEVENTS);
		(*out)[sz++] = strtod(buf, NULL);
	}

	return sz;
}

static void
usage(void)
{
	printf("Usage: %s [options] [dx1] [dx2] [...] > gnuplot.data\n", program_invocation_short_name);
	printf("\n"
	       "Options:\n"
	       "--mode=<motion|velocity|delta|sequence> \n"
	       "	motion   ... print motion to accelerated motion\n"
	       "	delta    ... print delta to accelerated delta\n"
	       "	velocity ... print velocity to accel factor\n"
	       "	sequence ... print motion for custom delta sequence\n"
	       "	speed    ... print speed to gain mapping (default)\n"
	       "--maxdx=<double>\n  ... in motion mode only. Stop increasing dx at maxdx\n"
	       "--mindx=<double>\n  ... in motion mode only. Start dx at mindx\n"
	       "--steps=<double>\n  ... in motion, delta, and speed modes only.\n"
	       "			Increase dx by step each round\n"
	       "\n"
	       "In sequence mode, extra arguments are a sequence of delta x coordinates.\n"
	       "In sequence mode, if stdin is a pipe, the pipe is read \n"
	       "for delta coordinates and extra arguments are ignored.\n"
	       "\n"
	       "In velocity mode, if stdin is a pipe, the pipe is read \n"
	       "for velocity data and step is ignored\n"
	       "\n"
	       "The output is a executable gnuplot command set.\n");
}

int
main(int argc, char **argv) {
	struct motion_filter *filter;
	double step = 0.1,
	       max_dx = 10,
	       min_dx = 0;
	int nevents = 0;
	enum mode {
		MODE_NONE,
		MODE_VELOCITY,
		MODE_MOTION,
		MODE_DELTA,
		MODE_SEQUENCE,
		MODE_SPEED,
	} mode = MODE_NONE;
	double *data = NULL;
	size_t data_sz;

	filter = create_pointer_accelator_filter(pointer_accel_profile_smooth_simple);
	assert(filter != NULL);


	while (1) {
		int c;
		int option_index = 0;
		static struct option long_options[] = {
			{"mode", 1, 0, 'm'},
			{"nevents", 1, 0, 'n'},
			{"mindx", 1, 0, '>'},
			{"maxdx", 1, 0, '<'},
			{"step", 1, 0, 's'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'm': /* --mode=? */
			if (strcmp(optarg, "velocity") == 0)
				mode= MODE_VELOCITY;
			else if (strcmp(optarg, "motion") == 0)
				mode = MODE_MOTION;
			else if (strcmp(optarg, "delta") == 0)
				mode = MODE_DELTA;
			else if (strcmp(optarg, "sequence") == 0)
				mode = MODE_SEQUENCE;
			else if (strcmp(optarg, "speed") == 0)
				mode = MODE_SPEED;
			else {
				usage();
				return 1;
			}
			break;
		case 'n':
			nevents = atoi(optarg);
			if (nevents == 0) {
				usage();
				return 1;
			}
			break;
		case '>': /* --mindx=? */
			min_dx = strtod(optarg, NULL);
			if (min_dx == 0.0) {
				usage();
				return 1;
			}
			break;
		case '<': /* --maxdx=? */
			max_dx = strtod(optarg, NULL);
			if (max_dx == 0.0) {
				usage();
				return 1;
			}
			break;
		case 's': /* --step=? */
			step = strtod(optarg, NULL);
			if (step == 0.0) {
				usage();
				return 1;
			}
			break;
		default:
			usage();
			exit(1);
			break;
		}

	}

	if (mode == MODE_SEQUENCE) {
		nevents = 0;
		if (!isatty(STDIN_FILENO)) {
			data_sz = doubles_from_stdin(&data);
		} else if (optind < argc) {
			data_sz = 0;
			while (optind < argc)
				data[data_sz++] = strtod(argv[optind++], NULL);
		} else {
			usage();
			return 1;
		}
	} else if (mode == MODE_VELOCITY) {
		if (!isatty(STDIN_FILENO))
			data_sz = doubles_from_stdin(&data);
		else
			data_sz = steps_to_sequence(&data, 0, 3, step);
	}

	switch (mode) {
	case MODE_VELOCITY:
		print_accel_func(filter, data, data_sz);
		break;
	case MODE_DELTA:
		print_ptraccel_deltas(filter, step);
		break;
	case MODE_MOTION:
		print_ptraccel_movement(filter, nevents, min_dx, max_dx, step);
		break;
	case MODE_SEQUENCE:
		print_ptraccel_sequence(filter, data_sz, data);
		break;
	case MODE_NONE:
	case MODE_SPEED:
		print_ptraccel_speed(filter, step);
		break;
	}

	filter_destroy(filter);

	free(data);

	return 0;
}
