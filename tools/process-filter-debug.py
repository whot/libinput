#!/usr/bin/python

from __future__ import print_function

import sys

class Point:
	def __init__(self, x, y):
		self.x = x
		self.y = y

class FilterData(object):
	def __init__(self):
		self.dict = {}
	def add(self, key, value):
		assert(not self.dict.has_key(key))
		self.dict[key] = value

	def finalize(self):
		for key, value in self.dict.iteritems():
			# convert -x keys into (x, y) tuples
			if key.endswith("-y"):
				continue
			if key.endswith("-x"):
				ykey = key[:-1] + "y"
				tpl = (value, self.dict[ykey])
				value = tpl
				key = key[:-2]

			if key.endswith("]"):
				key, index = key[:-1].split("[")
				index = int(index)

				if not hasattr(self, key):
					setattr(self, key, [ 0 ] * (index + 1))
				else:
					l = getattr(self, key)
					if index >= len(l):
						diff = index - len(l) + 1
						l.extend([0] * diff)
						setattr(self, key, l)

				l = getattr(self, key)
				l[index] = value
			else:
				assert(not hasattr(self, key))
				setattr(self, key, value)

		return self

def parse_debug_file(f):
	"""
	Parameters:
	-----------
	A file descriptor to a debug file. Parsing is done on all lines with
	a "filter: " prefix, all elements on that line are processed into
	key-value pairs separated by a ':', the value is converted into
	float/int where appropriate.

	Keys in the form "foo[0]:1" are interpreted as arrays, but must
	start with 0 and be in consecutive order.

	A "sync:1" line terminates each element.

	Returns:
	--------
	A list of FilterData objects with the various attributes set.
	"""
	PREFIX = "filter: "

	data = []
	current = FilterData()
	for line in f.readlines():
		if not line.startswith(PREFIX):
			continue

		line = line[len(PREFIX):]
		elements = line.split(" ")
		for e in elements:
			k, v = e.split(":")
			v = float(v)

			if k == "sync":
				data.append(current.finalize())
				current = FilterData()

			current.add(k, v)
	return data

def dump_gnuplot_dat_file(data):
	"""
	Parameters:
	-----------
	A list of FilterData objects with the various attributes set.

	Dumps those as a basic unsorted gnuplot data file.
	"""

	with open("filter-data-raw.dat", "w+") as f:
		f.write("# unaccel-x unaccel-y accel-x accel-y factor velocity velocity-offset\n")
		for d in data:
			f.write("{:f} {:f} ".format(*d.unaccelerated))
			f.write("{:f} {:f} ".format(*d.accelerated))
			f.write("{:f} {:f} {:f}".format(d.factor, d.velocity, d.velocity_offset))
			f.write("\n")

def dump_gnuplot_vectors(data):
	"""
	Parameters:
	------------
	"""

	with open("filter-data-vectors.gnuplot", "w+") as f:
		f.write((
			"file = 'filter-data-raw.dat\n"
			"set term wxt title 'vectors'\n"
			"set style data lines\n"
			"set xlabel 'x'\n"
			"set ylabel 'y'\n"
			"plot file using 0:1 title 'x', \\\n"
			"	file using 0:2 title 'y', 0\n"
			))

def dump_gnuplot_factor_vs_velocity(data):
	"""
	Produces a graph to show factor vs velocity over time. Ideally the
	curves should be roughly identical as higher velocities imply higher
	factors etc. Unless the factor is constant, if the curves diverge,
	it's likely that the finger movement cannot be predicted into
	pointer movements.
	"""
	with open("filter-data-factor-vs-velocity.gnuplot", "w+") as f:
		f.write((
			"file = 'filter-data-raw.dat\n"
			"set term wxt title 'factor vs velocity'\n"
			"set style data lines\n"
			"set xlabel 'event\n"
			"set ylabel 'factor\n"
			"set y2label 'velocity\n"
			"set autoscale y\n"
			"set autoscale y2\n"
			"set tics out\n"
			"set y2tics\n"
			"plot file using 0:5 title 'factor' axes x1y1, \\\n"
			"	file using 0:6 title 'velocity' axes x1y2\n"
			))

def dump_gnuplot_factor_velocity_ratio(data):
	"""
	Produces a graph to show the ratio of factor:velocity and also
	prints the velocity. Similar to dump_gnuplot_factor_vs_velocity()
	but just illustrates the ratio between the two better. Ideally, any
	spikes should mostly overlap. Any spikes in one but not in the other
	indicate that the pointer movement does not reflect the finger
	movement.
	"""
	with open("filter-data-factor-velocity-ratio.gnuplot", "w+") as f:
		f.write((
			"file = 'filter-data-raw.dat\n"
			"set term wxt title 'factor to velocity ratio'\n"
			"set style data lines\n"
			"set xlabel 'event\n"
			"set ylabel 'ratio factor:velocity\n"
			"set y2label 'velocity\n"
			"set autoscale y\n"
			"set autoscale y2\n"
			"set tics out\n"
			"set y2tics\n"
			"ratio(f, v) = v/f\n"
			"plot file using 0:(ratio($5, $6)) title 'factor:vel' axes x1y1, \\\n"
			"     file using 0:6 title 'velocity' axes x1y2\n"
			))


def dump_gnuplot_velocity_histogram(data):
	"""
	Produces a graph that shows the general distribution of velocities of a
	recording.
	"""
	buckets = 100
	counts = [0] * (buckets + 1)

	max_vel = max([d.velocity for d in data])
	for d in data:
		v = d.velocity
		index = int(v / max_vel * buckets)
		counts[index] += 1

	max_factor = max([d.factor for d in data])
	# can't reliably detect this because of deceleration
	# min_factor = min([d.factor for d in data])
	min_factor = 0.4 # min([d.factor for d in data])
	lowest_max_vel = min([d.velocity for d in data if d.factor == max_factor])
	highest_min_vel = max([0] + [d.velocity for d in data if d.factor == min_factor])

	with open("filter-data-velocity-histogram.gnuplot", "w+") as f:
		f.write((
			"set term wxt title 'velocity histogram'\n"
			"set xlabel 'velocity'\n"
			"set ylabel 'percent of events'\n"
			"set autoscale y\n"
			"set object 1 rect from {lmv}, graph 0 to graph 1, graph 1 fc rgb 'cyan'\n"
			"set object 2 rect from graph 0, graph 0 to {hmv}, graph 1 fc rgb 'magenta'\n"
			"plot '-' using 1:2 notitle\n").format(lmv=lowest_max_vel, hmv=highest_min_vel))
		for idx, c in enumerate(counts):
			v = max_vel * idx/buckets
			pc = 100.0 * c/len(data)
			f.write("{} {}\n".format(v, pc))
		f.write(("e\n"
			))

def dump_gnuplot_delta_to_velocity_ratio(data):
	"""
	Produces a graph that shows the ratio between the delta moved and the
	velocity, overlaid over the actual velocity.

	Unsure how useful this graph is as the delta does not include timestamps.
	"""
	with open("filter-data-delta-to-velocity-ratio.gnuplot", "w+") as f:
		f.write((
			"file = 'filter-data-raw.dat\n"
			"set term wxt title 'delta to velocity ratio'\n"
			"set style data lines\n"
			"set xlabel 'event'\n"
			"set ylabel 'ratio delta:velocity'\n"
			"set y2label 'velocity'\n"
			"set autoscale y\n"
			"set autoscale y2\n"
			"set tics out\n"
			"unset ytics\n"
			"unset y2tics\n"
			"ratio(dx, dy, v) = sqrt(dx * dx + dy * dy)/v\n"
			"plot file using 0:(ratio($0, $1, $6)) title 'delta:vel' axes x1y1, \\\n"
			"     file using 0:6 title 'velocity' axes x1y2\n"
			))

def dump_gnuplot_velocity_ratio_to_velocity_offset(data):
	"""
	Produces a graph that shows the delta-to-velocity ratio and the
	velocity offsets used in libinput to calculate the velocity, i.e. how
	far back we went to average this.

	Unsure how useful this graph is as the delta does not include timestamps.
	"""
	with open("filter-data-velocity-ratio-to-velocity-offset.gnuplot", "w+") as f:
		f.write((
			"file = 'filter-data-raw.dat\n"
			"set term wxt title 'velocity ratio to velocity offset'\n"
			"set style data lines\n"
			"set xlabel 'event'\n"
			"set ylabel 'ratio delta:velocity'\n"
			"set y2label 'velocity offset'\n"
			"set autoscale y\n"
			"set autoscale y2\n"
			"set tics out\n"
			"unset ytics\n"
			"set y2tics\n"
			"ratio(dx, dy, v) = sqrt(dx * dx + dy * dy)/v\n"
			"plot file using 0:(ratio($0, $1, $6)) title 'delta:vel' axes x1y1, \\\n"
			"     file using 0:7 title 'velocity-offset' axes x1y2\n"
			))

def dump_gnuplot_velocity_diffs(data):
	"""
	Produces two graphs, each with the velocity drawn in and all the velocity diffs.
	The first graph has each diff (i.e. diff between current velocity and
	velocity X events ago) on a separate line.

	The second graph has each diff (i.e. diff between current velocity and
	velocity X events ago) on a separate line, but only if that diff
	reaches the max velocity we need to say it's out of range.

	This graph should be useful, it shows whether we're hitting the max at
	the right type of speed changes.
	"""
	maxdiffs = max([len(d.velocity_diff) for d in data if hasattr(d, "velocity_diff")])

	with open("filter-data-velocity-diffs.dat", "w+") as f:
		f.write("# velocity velocity-diff[0] ...")
		for d in data:
			f.write("{:f} ".format(d.velocity))
			for i in range(0, maxdiffs):
				diff = 0.0
				try:
					diff = d.velocity_diff[i]
				except AttributeError:
					pass
				except IndexError:
					pass
				f.write("{:f} ".format(diff))
			f.write("\n")

	with open("filter-data-velocity-diffs.gnuplot", "w") as f:
		# Two plots, one with the each diff as separate line
		# we use a custom linestyle (ls) for gnuplot because ls 2 is dotted by deafult
		f.write((
			"file = 'filter-data-velocity-diffs.dat'\n"
			"set term wxt title 'velocity diffs'\n"
			"set linetype 1 lc rgb \"dark-violet\" lw 1\n"
			"set linetype 2 lc rgb \"#009e73\" lw 1\n"
			"set linetype 3 lc rgb \"#56b4e9\" lw 1\n"
			"set linetype 4 lc rgb \"#e69f00\" lw 1\n"
			"set linetype 5 lc rgb \"#f0e442\" lw 1\n"
			"set linetype 6 lc rgb \"#0072b2\" lw 1\n"
			"set linetype 7 lc rgb \"#e51e10\" lw 1\n"
			"set linetype 8 lc rgb \"black\"   lw 1\n"
			"set linetype 9 lc rgb \"gray50\"  lw 1\n"
			"set key out vert font \",5\"\n"
			"set multiplot layout 2, 1\n"
			"set style data lines\n"
			"set xlabel 'event'\n"
			"set ylabel 'velocity diff'\n"
			"set y2label 'velocity'\n"
			"set autoscale y\n"
			"set autoscale x\n"
			"set tics out\n"
			"set y2tics\n"
			"unset xtics\n"
			"plot file using 0:1 title 'vel',\\\n"))
		for i in range(0, maxdiffs):
			ls = i + 1
			f.write(" file using 0:{} title 'diff[{}]' ls {},\\\n".format(i+2, i, ls))
		f.write("   0.001 title '' ls 0 \n")

		# and another one with each diff on 0 unless it hits the MAX, this illustratate
		# we use a custom linestyle (ls) for gnuplot because ls 2 is dotted by deafult
		f.write("plot file using 0:1 title 'vel',\\\n")
		for i in range(0, maxdiffs):
			ls = i + 1
			f.write(" file using 0:(${}>0.001?${}:0.0) title 'diff[{}]' ls {},\\\n".format(i+2, i+2, i, ls))
		f.write(("   0.001 title '' ls 0\n"
			 "unset multiplot\n"
			 ))

def dump_gnuplot_max_factor_parts(data):
	"""
	Produces a graph that shows which parts of a sequence had the maximum factor applied.
	"""

	max_factor = max([d.factor for d in data])
	# can't reliably detect this because of deceleration
	# min_factor = min([d.factor for d in data])
	min_factor = 0.4
	max_vel = max([d.velocity for d in data])

	with open("filter-data-max-factor-parts.gnuplot", "w+") as f:
		f.write((
			"file = 'filter-data-raw.dat\n"
			"set term wxt title 'max factor'\n"
			"set style data lines\n"
			"set xlabel 'event'\n"
			"set ylabel 'velocity'\n"
			"set autoscale y\n"
			"set tics out\n"
			"set key out\n"
			"set ytics\n"
			"set yrange [0:{max_vel}]\n"
			"set style fill solid 1.0\n"
			"plot file using 0:($5 >= {max_factor} ? 1 : 0) with boxes ls 3 title 'max', \\\n"
			"     file using 0:($5 > {min_factor} && $5 < {max_factor} ? $6 : 0) with boxes ls 4 title 'adaptive', \\\n"
			"     file using 0:($5 < {min_factor} ? $6 : 0) with boxes ls 5 title 'decel', \\\n"
			"     file using 0:6 title 'velocity' ls 1\n"
			).format(max_factor=max_factor, min_factor=min_factor, max_vel=max_vel))
def process_data(data):
	"""
	Parameters:
	-----------
	A list of FilterData objects with the various attributes set.
	"""

	dump_gnuplot_dat_file(data)
	dump_gnuplot_vectors(data)
	dump_gnuplot_factor_vs_velocity(data)
	dump_gnuplot_factor_velocity_ratio(data)
	dump_gnuplot_delta_to_velocity_ratio(data)
	dump_gnuplot_velocity_ratio_to_velocity_offset(data)
	dump_gnuplot_velocity_diffs(data)
	dump_gnuplot_max_factor_parts(data)
	dump_gnuplot_velocity_histogram(data)

def main(argv):
	debug_file = argv[1]
	with open(debug_file) as f:
		data = parse_debug_file(f)
		process_data(data)

if __name__ == "__main__":
	main(sys.argv)
