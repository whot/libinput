#!/usr/bin/python

from __future__ import print_function

import os
import sys
import math
from pprint import pprint
import xml.etree.ElementTree
import itertools
import scipy.stats
import random

from collections import OrderedDict

mode = "normal"

question_code = {
		0 : "natural",
		1 : "precise",
		2 : "fast-movement",
		3 : "easy-to-hit",
		4 : "faster",
		5 : "slower",
		}


def print_normal(*args, **kwargs):
	if mode == "normal":
		print(*args, **kwargs)

def print_gnuplot(*args, **kwargs):
	if mode == "gnuplot":
		print(*args, **kwargs)

def vec_length(x, y):
	return math.sqrt(x * x + y * y)

def mean(data):
	if not data:
		return (0, 0)

	m = 1.0 * sum(data)/len(data)
	stddev = math.sqrt(sum((x-m) ** 2 for x in data) / len(data))
	return (m, stddev)

def median(data):
	if not data:
		return 0

	data = sorted(data)

	midpoint = data[len(data)/2]

	if len(data) % 2 == 0:
		midpoint = (midpoint + data[len(data)/2 - 1])/2
	return midpoint

class SetResults(object):
	"""
	Representation of results for a single set set. Matches another set
	for any properties that are set, or if that property is None on one
	of those.
	"""
	def __init__(self, method, target_size=None, data=None):
		self.method = method
		self.target_size = target_size
		if not data:
			data = []
		self.data = data

		self._mean = None
		self._stddev = None
		self._median = None

	@property
	def median(self):
		self._median = median(self.data)
		return self._median

	@property
	def mean(self):
		if self._mean == None:
			self._mean, self._stddev = mean(self.data)
		return self._mean

	@property
	def stddev(self):
		if self._mean == None:
			self._mean, self._stddev = mean(self.data)
		return self._stddev

	@property
	def nsamples(self):
		return len(self.data)

	def __ne__(self, other):
		return not self == other

	def __eq__(self, other):
		if type(other) == type(None):
			return True

		matches = True
		if matches and other.method != None:
			matches = (self.method == other.method)
		if matches and other.target_size != None:
			matches = matches and (self.target_size == other.target_size)
		return matches

	def __str__(self):
		if not self.data:
			return "method %d target size %d empty set" % (self.method, self.target_size)
		return "method %d target size %d: median %f mean %f stddev %f (samples: %d)" % (
				self.method,
				self.target_size,
				self.median,
				self.mean,
				self.stddev,
				self.nsamples)

class Results(object):
	"""
	Merged results objects, consists of a number of SetResults.
	"""
	def __init__(self, sets):
		self.sets = sets

	def filter(self, set):
		"""
		Return a new results, filtered by the set given
		"""
		return Results([s for s in self.sets if s == set])

	def median(self, set=None):
		data = [d for s in self.sets if s == set for d in s.data]
		return median(data)

	def mean(self, set=None):
		data = [d for s in self.sets if s == set for d in s.data]
		return mean(data)[0]

	def stddev(self, set=None):
		data = [d for s in self.sets if s == set for d in s.data]
		return mean(data)[1]

	def nsamples(self, set=None):
		data = [s.nsamples for s in self.sets if s == set]
		return sum(data)

	def methods(self, set=None):
		return [s.method for s in self.sets if s == set]

	def target_sizes(self, set=None):
		return [s.target_size for s in self.sets if s == set]

	def __str__(self):
		return "median: %f mean: %f stddev: %f (samples: %d)" % (self.median(),
									 self.mean(),
									 self.stddev(),
									 self.nsamples())
class TargetResults(object):
	"""
	Results for one specific target
	"""
	def __init__(self, filename, set_number, target_number, method,
			radius, pos, cursor_pos):
		self.radius = radius
		self.method = method
		self.time_to_click = 0
		self.actual_path = 0
		self.overshoot = 0

		self.motion_positions = []
		self.motion_deltas = []

		self.filename = filename
		self.set_number = set_number
		self.target_number = target_number
		self.pos = pos
		self.cursor = cursor_pos

		self.distance = vec_length(pos[0] - cursor_pos[0],
					   pos[1] - cursor_pos[1])
		self.difficulty = 1.0 * self.distance/(radius * 2)


	def _fileheader(self, f):
		f.write("# filename: %s\n" % self.filename)
		f.write("# set-ID: %d\n" % self.set_number)
		f.write("# target number: %d\n" % self.target_number)
		f.write("# method: %d\n" % self.method)
		f.write("# target radius: %d\n" % self.radius)
		f.write("# target position: %d %d\n" % (self.pos[0], self.pos[1]))
		f.write("# cursor position: %f %f\n" % (self.cursor[0], self.cursor[1]))
		f.write("# initial distance: %d\n" % self.distance)


	def _dump_vectors(self, path, suffix):

		f = open(os.path.join(path, "vectors.gnuplot"), "w+")
		f.write("""
		# Show the movement vectors for each target data file
		# call with gnuplot -e "data='datafile'" filename
		set style data vector
		set xrange [0:*]
		set yrange [1300:0]
		plot data using 1:2:3:4 title ''
		pause -1
		""")
		f = open(os.path.join(path, "vectors-%s" % suffix), "w+")
		self._fileheader(f)

		f.write("# cursor x, y, dx, dy\n")
		for pos, delta in zip(self.motion_positions, self.motion_deltas):
			f.write("%f %f %f %f\n" % (pos[0], pos[1], delta[0], delta[1]))

	@property
	def extra_path(self):
		return (1.0 * self.actual_path/self.distance - 1) * 100

	@property
	def extra_path_in_px(self):
		return self.actual_path - self.distance

	@property
	def overshoot_for_distance(self):
		return (1.0 * self.overshoot/self.distance) * 100

	@property
	def overshoot_for_path(self):
		return (1.0 * self.overshoot/self.actual_path) * 100

	def dump(self, path):
		suffix = "%s-set-%d-target-%d-method-%d" % (self.filename,
							    self.set_number,
							    self.target_number,
							    self.method)
		self._dump_vectors(path, suffix)

	def __str__(self):
		return "%d %d %d %f %d %f %f %s %d"  % (self.radius,
						self.method,
						self.distance,
						self.difficulty,
						self.time_to_click,
						self.actual_path,
						self.overshoot,
						self.filename,
						self.target_number)

class QuestionaireResults(object):
	"""
	Answers to questionnaire
	"""

	def __init__(self, methods):
		self.methods = methods
		self.questions =[]
		self.answers = [[] for _ in xrange(3)] # self.answers[method] = [a1, a2, ...]

		self.answer_difference = None
		self.answer_prefer = None

	def set_userdata(self, age, gender, handed, experience, hours, device):
		self.age = age
		self.gender = gender
		self.handed = handed
		self.experience = experience
		self.hours = hours
		self.device = device

	def get_answer_difference(self, m1, m2):
		if m1 in self.methods and m2 in self.methods:
			return self.answer_difference
		return None

	def get_answer_preferable(self, m1, m2):
		if m1 == self.methods[0] and m2 == self.methods[1]:
			return self.answer_preferable
		elif m1 == self.methods[1] and m2 == self.methods[0]:
			return -self.answer_preferable

		return None

class UserStudyResultsFile(object):
	def __init__(self, path):
		self.path = path
		self.tree = xml.etree.ElementTree.parse(path)
		self.root = self.tree.getroot()

		self.times_per_set = self._time_per_set()
		self.questionnaire = self._parse_questionnaire()

		self.methods = list(OrderedDict.fromkeys([r.method for r in self.times_per_set.sets]))
		self.target_sizes = list(OrderedDict.fromkeys([r.target_size for r in self.times_per_set.sets]))


		self.target_results = self.parse_targets()

	def parse_targets(self):
		method = -1
		set_number = 0
		target = None
		start_time = 0

		targets = []

		target_pos = None # target center
		B = None # some other point on the target line
		vec = None
		initial_side = None

		for elem in self.root.iter():
			name = elem.tag
			if name == "set":
				method = int(elem.get("method"))
				set_number = int(elem.get("id"))
			elif name == "target":
				if target != None:
					targets.append(target)

				x, y = float(elem.get("x")), float(elem.get("y"))
				xpos, ypos = float(elem.get("xpos")), float(elem.get("ypos"))
				radius = int(elem.get("r"))
				start_time = int(elem.get("time"))

				target_pos = (xpos, ypos)
				cursor_pos = (x, y)

				target = TargetResults(os.path.basename(self.path),
							set_number,
							int(elem.get("number")),
							method,
							radius,
							target_pos,
							cursor_pos)
				# for overshoot
				vec, B = self.setup_vectors(target_pos, (x, y))
				initial_side = self.side(target_pos, B, (x, y))
			elif name == "button" and \
				elem.get("state") == "1" and \
				elem.get("hit") == "1":
				end_time = int(elem.get("time"))
				target.time_to_click = end_time - start_time
			elif name == "motion":
				dx = float(elem.get("dx"))
				dy = float(elem.get("dy"))
				target.actual_path += self.vec_length(dx, dy)

				x = float(elem.get("x"))
				y = float(elem.get("y"))

				target.motion_positions.append((x,y))
				target.motion_deltas.append((dx,dy))

				# overshoot
				if self.side(target_pos, B, (x, y)) != initial_side:
					d = self.distance(target_pos, B, (x, y))
					if (d > target.overshoot):
						target.overshoot = d

		targets.append(target)
		return targets

	def _set_from_elem(self, elem):
		 return SetResults(target_size = int(elem.get("r")),
				   method = int(elem.get("method")))

	def get_set_id(self, elem):
		return int(elem.get("method")) * 1000 + int(elem.get("id"))

	def _time_per_set(self):
		sets = []
		times = [0, 0]
		cur_set = None
		for elem in self.root.iter("set"):
			cur_set = self._set_from_elem(elem)
			sets.append(cur_set)
			times[0] = int(elem.get("time"))

			button = elem.findall("button[last()]")[0]
			times[1] = int(button.get("time"))
			cur_set.data.append(times[1] - times[0])

		return Results(sets)

	def setup_vectors(self, target, P):
		vec = (target[0] - P[0], target[1] - P[1]);
		vec_p = (-vec[1], vec[0]) # perpendicular
		B = (target[0] + vec_p[0], target[1] + vec_p[1])
		return vec, B

	def vec_length(self, x, y):
		return math.sqrt(x * x + y * y)

	def side(self, A, B, P):
		"""Side of a vector AP a point P is on
		   Return 1, 0, -1
		"""
		sign = (B[0] - A[0]) * (P[1] - A[1]) - (B[1] - A[1]) * (P[0] - A[0])
		if sign != 0:
			sign /= 1.0 * abs(sign)
		return sign

	def distance(self, A, B, P):
		""" Distance of point P from vector AB """
		v = (B[0] - A[0])*(B[1] - P[1]) - (A[0] - P[0])*(B[1] - A[1])
		return v/math.sqrt(math.pow(B[0] - A[0], 2) + math.pow(B[1] - A[1], 2))

	def _parse_questionnaire(self):
		# questionnaire tag is inside <set> for pre-trial results,
		# so root.find("questionnaire") won't work
		questionnaire = [e for e in self.root.iter("questionnaire")][0]

		first = int(questionnaire.get("first"))
		second = int(questionnaire.get("second"))

		qr = QuestionaireResults([first, second])

		userdata = questionnaire.find("userdata")
		device = questionnaire.find("device")
		qr.set_userdata(int(userdata.get("age")),
				userdata.get("gender"),
				userdata.get("handed"),
				int(userdata.get("experience")),
				int(userdata.get("hours_per_week")),
				device.get("type"))

		questions = []
		answers = []
		for q in questionnaire.findall("question"):
			questions.append(q.text)
			answers.append(int(q.get("response")))

		qr.questions = questions[0:6]
		qr.answers[first] = answers[0:6]
		qr.answers[second] = answers[6:12]

		qr.answer_difference = answers[12]
		qr.answer_preferable = answers[13]

		return qr


class UserStudyResults(object):
	def _normalize(self, results, max_values, what):
		count_methods = {}
		for m in self.methods:
			count_methods[m] = 0

		sets = []
		for r in results:
			for m in r.methods:
				if count_methods[m] >= max_values:
					continue
				t = getattr(r, what).filter(SetResults(m))
				sets += t.sets
				count_methods[m] += 1
		return Results(sets)

	def __init__(self, path):
		self.results = [r for r in self._parse_files(path)]
		if not self.results:
			raise Exception("No files found")

		# we don't have an equal number of datasets for each method
		# count how many we have. Then reduce the results to the
		# minimum of all methods
		count_methods = {}
		for r in self.results:
			for m in r.methods:
				count_methods[m] = count_methods.get(m, 0) + 1

		max_values = min(count_methods.itervalues())

		# files is a set of of UserStudyResultsFile. That's pretty
		# useless though, so we extract the actual data into lists
		# here
		self.questionnaires = [ r.questionnaire for r in self.results ]

	def _files(self, path):
		for root, dirs, files in os.walk(path):
			for file in files:
				if file.endswith(".xml"):
					yield os.path.join(root, file)

	def _parse_files(self, path):
		for file in self._files(path):
			yield UserStudyResultsFile(file)

	@property
	def target_sizes(self):
		return list(OrderedDict.fromkeys([t for r in self.results for t in r.target_sizes]))

	@property
	def methods(self):
		return list(OrderedDict.fromkeys([m for r in self.results for m in r.methods]))

	def user_age(self):
		ages = [ r.age for r in self.questionnaires if r.age != 0]
		return mean(ages)

	def user_gender(self):
		genders = [ r.gender for r in self.questionnaires ]
		male = genders.count("male")
		female = genders.count("female")
		other = genders.count("other")
		none = genders.count("none")
		return (male, female, other, none)

	def user_handedness(self):
		handed = [ r.handed for r in self.questionnaires ]
		return (handed.count("right"), handed.count("left"))

	def user_experience(self):
		experience = [ r.experience for r in self.questionnaires if r.experience != 0]
		return mean(experience)

	def user_hours_per_week(self):
		hours = [ r.hours for r in self.questionnaires if r.hours != 0]
		return mean(hours)

	@property
	def target_results(self):
		return [ t for r in self.results for t in r.target_results]


def print_user_info(results):
	print_normal("User information")

	print_normal("Average age %f (%f)" % results.user_age())
	print_normal("Gender distribution: male %d female %d other %d none given %d" % results.user_gender())
	print_normal("Right-handed: %d, left-handed %d" % results.user_handedness())
	print_normal("Average experience in years: %f (%f)" % results.user_experience())
	print_normal("Average usage in h per week: %f (%f)" % results.user_hours_per_week())


difficulties = [ 4.2, 8.4, 12.9, 16.8, 25, 0xffffff]

def difficulty_group(n):
	for idx, val in enumerate(difficulties):
		if n < val:
			return idx

def difficulty_name(n):
	names = [ str(x) for x in difficulties[:-1] ] + ["max"]
	return names[n]

def ngroups():
	return len(difficulties)

def sigstr(pvals):
	sig = []
	for p in pvals:
		sig.append('+') if p < 0.05 else sig.append('-')
	return "".join(sig)

def anova(f, results, prop):
	# anova_base[difficulty-group][method]
	anova_base = [[[] for _ in xrange(3)] for _ in xrange(ngroups())]
	for target in results.target_results:
		dg = difficulty_group(target.difficulty)
		anova_base[dg][target.method].append(getattr(target, prop))

	f.write("# 1-difficulty-group 2-m0-mean 3-m1-mean 4-m2-mean 5-number-of-samples 6-pval-0vs1 7-pval-0vs2 8-pval-1vs2 9-stddev-m0 10-stddev-m1 11-stddev-m2 12-difficulty-val 13-significance\n")
	for dg in xrange(ngroups()):
		maxlen = min([len(d) for d in anova_base[dg]])
		data = [d[:maxlen] for d in anova_base[dg]]
		_, p01 = scipy.stats.f_oneway(data[0], data[1])
		_, p02 = scipy.stats.f_oneway(data[0], data[2])
		_, p12 = scipy.stats.f_oneway(data[1], data[2])
		m0,s0 = mean(data[0])
		m1,s1 = mean(data[1])
		m2,s2 = mean(data[2])

		sig = sigstr((p01, p02, p12))

		f.write("%d %f %f %f %d %f %f %f %f %f %f %s %s\n" % (dg, m0, m1, m2,
			maxlen, p01, p02, p12, s0, s1, s2,
			difficulty_name(dg), sig))


def main(argv):
	if argv[1] == "--gnuplot":
		global mode
		mode = "gnuplot"
		argv = argv[1:]
	if argv[1] == "-o":
		output_path = argv[2]
		argv = argv[2:]

	fpath = argv[1];

	results = UserStudyResults(fpath)

	f = open(os.path.join(output_path, "target-analysis.dat"), "w+")
	f.write("# 1-radius 2-method 3-distance 4-difficulty 5-time-to-click 6-path 7-overshoot 8-filename 9-target_number 10-set_number 11-difficulty-group\n")
	random.seed(123456789)
	random.shuffle(results.target_results)
	random.seed(123456789)
	random.shuffle(results.results)

	for target in results.target_results:
		target.dump(os.path.join(output_path, "vectors"))
		f.write("%d %d %d %f %d %f %f %s %d %d %d\n" % (target.radius,
						target.method,
						target.distance,
						target.difficulty,
						target.time_to_click,
						target.actual_path,
						target.overshoot,
						target.filename,
						target.target_number,
						target.set_number,
						difficulty_group(target.difficulty)))

	count_groups = [[0] * 3 for _ in  xrange(ngroups())]
	for target in results.target_results:
		dg = difficulty_group(target.difficulty)
		count_groups[dg][target.method] += 1

	f = open(os.path.join(output_path, "hist-target-counts-grouped-by-ID.dat"), "w+")
	f.write("# 1-difficulty-group 2-m1-count 3-m2-count 4-m3-count 5-difficulty-name\n")
	for ID, methods in enumerate(count_groups):
		f.write("%d %d %d %d %s\n" % (ID, methods[0], methods[1], methods[2], difficulty_name(ID)))

	# run ANOVA on the various tasks
	f = open(os.path.join(output_path, "hist-time-to-click-mean-grouped-by-ID.dat"), "w+")
	anova(f, results, "time_to_click")

	f = open(os.path.join(output_path, "hist-path-mean-grouped-by-ID.dat"), "w+")
	anova(f, results, "extra_path")

	f = open(os.path.join(output_path, "hist-path-absolute-mean-grouped-by-ID.dat"), "w+")
	anova(f, results, "extra_path_in_px")

	f = open(os.path.join(output_path, "hist-overshoot-mean-grouped-by-ID.dat"), "w+")
	anova(f, results, "overshoot")

	f = open(os.path.join(output_path, "hist-overshoot-for-distance-mean-grouped-by-ID.dat"), "w+")
	anova(f, results, "overshoot_for_distance")

	f = open(os.path.join(output_path, "hist-overshoot-for-path-mean-grouped-by-ID.dat"), "w+")
	anova(f, results, "overshoot_for_path")

	f = open(os.path.join(output_path, "user-info.dat"), "w+")
	f.write("1-age 2-gender 3-right-handed 4-years-experience 5-h-per-week 6-device\n")
	for r in results.results:
		q = r.questionnaire
		f.write("%d %s %s %d %d %s" % (q.age, q.gender, q.handed,
			q.experience, q.hours, q.device))

	# All question answers dumped into a file
	f = open(os.path.join(output_path, "questionnaire-answers.dat"), "w+")
	f.write("1-method")
	for key in sorted(question_code.keys()):
		f.write(" %d-%s" % (2 + key, question_code[key]))
	f.write("\n");
	for q in [r.questionnaire for r in results.results]:
		for m in q.methods:
			answers = q.answers[m]
			if answers == []:
				continue
			f.write("%d" % m)
			for a in answers:
				f.write(" %d" % a)
			f.write("\n")

	# histogram of all answers by question IDX
	f = open(os.path.join(output_path, "hist-questionnaire.dat"), "w+")
	f.write("# 1-question-idx 2-m0-mean 3-m1-mean 4-m2-mean 5-number-of-samples 6-pval-0vs1 7-pval-0vs2 8-pval-1vs2 9-stddev-m0 10-stddev-m1 11-stddev-m2 12-question-code 13-significance\n")

	# all_answers[question][method] = [a1, a2, a3, ...]
	all_answers = [[[] for _ in xrange(3)] for _ in xrange(len(question_code))]
	for q in [r.questionnaire for r in results.results]:
		for m in q.methods:
			answers = q.answers[m]
			if answers == []:
				continue
			for idx, a in enumerate(answers):
				all_answers[idx][m].append(a)

	# normalize to equal number of answers for each question
	maxlen = min([len(d) for d in all_answers[0]])
	for idx, vals in enumerate(all_answers):
		for m in xrange(3):
			all_answers[idx][m] = all_answers[idx][m][:maxlen]

	for idx, data in enumerate(all_answers):
		qcode = question_code[idx]
		_, p01 = scipy.stats.f_oneway(data[0], data[1])
		_, p02 = scipy.stats.f_oneway(data[0], data[2])
		_, p12 = scipy.stats.f_oneway(data[1], data[2])
		m0,s0 = mean(data[0])
		m1,s1 = mean(data[1])
		m2,s2 = mean(data[2])

		sig = sigstr((p01, p02, p12))

		f.write("%d %f %f %f %d %f %f %f %f %f %f %s %s\n" % (idx, m0, m1, m2,
			maxlen, p01, p02, p12, s0, s1, s2,
			qcode, sig))

	# likert counts for each question
	f = open(os.path.join(output_path, "hist-questionnaire-likert-counts.dat"), "w+")
	f.write("#1-question 2-qcode 3-m0-likert-2 4-m0-likert-1 5-m0-likert0 6-m0-likert1 7-m0-likert2 8-m1-likert-2...\n")
	# likert_counts[method][question][likert-scale-val] = count
	likert_counts = [[[0 for _ in xrange(5)] for _ in xrange(6) ] for _ in xrange(3)]
	for idx, data in enumerate(all_answers):
		qcode = question_code[idx]
		for m in xrange(3):
			answers = data[m]
			if answers == []:
				continue
			for a in answers:
				likert_counts[m][idx][a + 2] += 1

	for idx in xrange(6):
		f.write("%d %s" % (idx, question_code[idx]))
		for m in xrange(3):
			counts = likert_counts[m][idx]
			for count in counts:
				f.write(" %d" % count)
		f.write("\n")

	# cross-comparison of the "do they feel different" question
	f = open(os.path.join(output_path, "hist-questionnaire-accel-different.dat"), "w+")
	f.write("# 1-unused 2-m01-mean 3-m02-mean 4-m12-mean 5-number-of-samples 6-pval-01vs02 7-pval-02vs12 8-pval-12vs02 9-stddev-m0 10-stddev-m1 11-stddev-m2 12-unused 13-significance\n")

	# all_answers[(m1, m2)] = [a1, a2, ...]
	all_answers = {}
	for (m1, m2) in itertools.combinations(xrange(3), 2):
		all_answers[(m1, m2)] = []

	for q in [r.questionnaire for r in results.results]:
		for (m1, m2) in itertools.combinations(xrange(3), 2):
			ad = q.get_answer_difference(m1, m2)
			if ad == None:
				continue
			all_answers[(m1, m2)].append(ad)

	maxlen = min([len(d) for d in all_answers.values()])
	for key in all_answers.keys():
		all_answers[key] = all_answers[key][:maxlen]

	_, p0102 = scipy.stats.f_oneway(all_answers[(0, 1)], all_answers[(0, 2)])
	_, p0112 = scipy.stats.f_oneway(all_answers[(0, 1)], all_answers[(1, 2)])
	_, p0212 = scipy.stats.f_oneway(all_answers[(0, 2)], all_answers[(1, 2)])

	m01, s01 = mean(all_answers[(0, 1)])
	m02, s02 = mean(all_answers[(0, 2)])
	m12, s12 = mean(all_answers[(1, 2)])

	sig = sigstr((p0102, p0112, p0212))

	f.write("x %f %f %f %d %f %f %f %f %f %f x %s\n" % (
		m01, m02, m12, maxlen, p0102, p0112, p0212, s01, s02, s12,
		sig))

	# cross-comparison of the "first is preferable" question
	f = open(os.path.join(output_path, "hist-questionnaire-preferable.dat"), "w+")
	f.write("# 1-unused 2-m01-mean 3-m02-mean 4-m12-mean 5-number-of-samples 6-pval-01vs02 7-pval-01vs12 8-pval-02vs12 9-stddev-m01 10-stddev-m02 11-stddev-m12 12-unused 13-significance\n")

	# all_answers[(m1, m2)] = [a1, a2, ...]
	all_answers = {}
	for (m1, m2) in itertools.combinations(xrange(3), 2):
		all_answers[(m1, m2)] = []

	for q in [r.questionnaire for r in results.results]:
		for (m1, m2) in itertools.combinations(xrange(3), 2):
			ad = q.get_answer_preferable(m1, m2)
			if ad == None:
				continue
			all_answers[(m1, m2)].append(ad)

	maxlen = min([len(d) for d in all_answers.values()])
	for key in all_answers.keys():
		all_answers[key] = all_answers[key][:maxlen]

	_, p0102 = scipy.stats.f_oneway(all_answers[(0, 1)], all_answers[(0, 2)])
	_, p0112 = scipy.stats.f_oneway(all_answers[(0, 1)], all_answers[(1, 2)])
	_, p0212 = scipy.stats.f_oneway(all_answers[(0, 2)], all_answers[(1, 2)])

	m01, s01 = mean(all_answers[(0, 1)])
	m02, s02 = mean(all_answers[(0, 2)])
	m12, s12 = mean(all_answers[(1, 2)])

	sig = sigstr((p0102, p0112, p0212))

	f.write("x %f %f %f %d %f %f %f %f %f %f x %s\n" % (
		m01, m02, m12, maxlen, p0102, p0112, p0212, s01, s02, s12,
		sig))

	f = open(os.path.join(output_path, "hist-questionnaire-preferable-likert-count.dat"), "w+")
	f.write("# 1-question 2-qcode 3-likert 4-m01 5-m01-count 6-m02 7-m02-count 8-m12 9-count\n")
	for likert in xrange(-2, 3):
		f.write("13 preferable %d" % likert)
		for (m1, m2) in all_answers.keys():
			m12count = all_answers[(m1, m2)].count(likert)
			m21count = all_answers[(m1, m2)].count(-likert)

			f.write(" m%d%d %d m%d%d %d" % (m1, m2, m12count, m2, m1, m21count))
		f.write("\n");


	return

if __name__ == "__main__":
	main(sys.argv)
