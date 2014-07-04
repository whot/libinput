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

static void
print_ptraccel_deltas(struct motion_filter *filter, double step)
{
	struct motion_params motion;
	uint64_t time = 0;
	double i;

	printf("# gnuplot:\n");
	printf("# set xlabel dx unaccelerated\n");
	printf("# set ylabel dx accelerated\n");
	printf("# plot \"gnuplot.data\" using 1:2 title \"step %.2f\"\n", step);
	printf("#\n");

	/* Accel flattens out after 15 and becomes linear */
	for (i = 0.0; i < 15.0; i += step) {
		motion.dx = i;
		motion.dy = 0;
		time += 12; /* pretend 80Hz data */

		filter_dispatch(filter, &motion, NULL, time);

		printf("%.2f	%.3f\n", i, motion.dx);
	}
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

	printf("# gnuplot:\n");
	printf("# set xlabel \"event number\"\n");
	printf("# set ylabel \"delta motion\"\n");
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

	dx = min_dx;

	for (i = 0; i < nevents; i++) {
		motion.dx = dx;
		motion.dy = 0;
		time += 12; /* pretend 80Hz data */

		filter_dispatch(filter, &motion, NULL, time);

		printf("%d	%.3f	%.3f\n", i, motion.dx, dx);

		if (dx < max_dx)
			dx += step;
	}
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

	printf("# gnuplot:\n");
	printf("# set xlabel \"event number\"\n");
	printf("# set ylabel \"delta motion\"\n");
	printf("# plot \"gnuplot.data\" using 1:2 title \"dx out\", \\\n");
	printf("#      \"gnuplot.data\" using 1:3 title \"dx in\"\n");
	printf("#\n");

	dx = deltas;

	for (i = 0; i < nevents; i++, dx++) {
		motion.dx = *dx;
		motion.dy = 0;
		time += 12; /* pretend 80Hz data */

		filter_dispatch(filter, &motion, NULL, time);

		printf("%d	%.3f	%.3f\n", i, motion.dx, *dx);
	}
}

static void
print_accel_func(struct motion_filter *filter)
{
	double vel;

	printf("# gnuplot:\n");
	printf("# set xlabel \"velocity\"\n");
	printf("# set ylabel \"raw accel factor\"\n");
	printf("# plot \"gnuplot.data\"\n");
	for (vel = 0.0; vel < 3.0; vel += .0001) {
		double result = pointer_accel_profile_smooth_simple(filter,
								    NULL,
								    vel,
								    0 /* time */);
		printf("%.4f\t%.4f\n", vel, result);
	}
}

static void
usage(void)
{
	printf("Usage: %s [options] [dx1] [dx2] [...] > gnuplot.data\n", program_invocation_short_name);
	printf("\n"
	       "Options:\n"
	       "--mode=<motion|velocity|delta|sequence> \n"
	       "	motion   ... print motion to accelerated motion (default)\n"
	       "	delta    ... print delta to accelerated delta\n"
	       "	velocity ... print velocity to accel factor\n"
	       "	sequence ... print motion for custom delta sequence\n"
	       "--maxdx=<double>\n  ... in motion mode only. Stop increasing dx at maxdx\n"
	       "--mindx=<double>\n  ... in motion mode only. Start dx at mindx\n"
	       "--steps=<double>\n  ... in motion and delta modes only. Increase dx by step each round\n"
	       "\n"
	       "If extra arguments are present and mode is not given, mode is changed to 'sequence'\n"
	       "and the arguments are interpreted as sequence of delta x coordinates\n"
	       "\n"
	       "If stdin is a pipe, mode is changed to 'sequence' and the pipe is read \n"
	       "for delta coordinates\n"
	       "\n"
	       "Output best viewed with gnuplot. See output for gnuplot commands\n");
}

int
main(int argc, char **argv) {
	struct motion_filter *filter;
	double step = 0.1,
	       max_dx = 10,
	       min_dx = 0;
	int nevents = 0;
	bool print_velocity = false,
	     print_motion = true,
	     print_delta = false,
	     print_sequence = false;
	double custom_deltas[1024];

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
				print_velocity = true;
			else if (strcmp(optarg, "motion") == 0)
				print_motion = true;
			else if (strcmp(optarg, "delta") == 0)
				print_delta = true;
			else if (strcmp(optarg, "sequence") == 0)
				print_sequence = true;
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

	if (print_velocity)
		print_accel_func(filter);
	else if (print_delta)
		print_ptraccel_deltas(filter, step);
	else if (print_motion)
		print_ptraccel_movement(filter, nevents, min_dx, max_dx, step);
	else if (print_sequence)
		print_ptraccel_sequence(filter, nevents, custom_deltas);

	filter_destroy(filter);

	return 0;
}
