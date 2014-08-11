#!/usr/bin/python

import os
import sys
import math
from pprint import pprint
import xml.etree.ElementTree
import itertools

from collections import OrderedDict

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
		return "median: %f mean: %f stddev: %f (samples: %d)" % (self.mean(),
									 self.median(),
									 self.stddev(),
									 self.nsamples())

class QuestionaireResults(object):
	"""
	Answers to questionnaire
	"""

	def __init__(self, methods):
		self.methods = methods
		self.questions = 14 * [""] # number of questions
		self.answers = 14 * [0] # number of questions

	def set_userdata(self, age, gender, handed, experience, hours, device):
		self.age = age
		self.gender = gender
		self.handed = handed
		self.experience = experience
		self.hours = hours
		self.device = device

class UserStudyResultsFile(object):
	def __init__(self, path):
		self.tree = xml.etree.ElementTree.parse(path)
		self.root = self.tree.getroot()

		self.click_times = self._button_click_times()
		self.target_aquisition_times = self._target_aquisition_times()
		self.target_misses = self._target_misses()
		self.times_per_set = self._time_per_set()
		self.extradistance, self.overshoot = self._path_length_per_target()
		self.questionnaire = self._parse_questionnaire()

		self.methods = list(OrderedDict.fromkeys([r.method for r in self.click_times.sets]))
		self.target_sizes = list(OrderedDict.fromkeys([r.target_size for r in self.click_times.sets]))

	def _set_from_elem(self, elem):
		 return SetResults(target_size = int(elem.get("r")),
				   method = int(elem.get("method")))

	def _button_click_times(self):
		"""
		Searches for time between button press and release
		events.
		"""
		times = [0, 0]
		expected_state = 1
		cur_set = None
		sets = []

		for elem in self.root.iter():
			name = elem.tag
			if name == "set":
				cur_set = self._set_from_elem(elem)
				sets.append(cur_set)
				continue

			if name == "button":
				btn_state = int(elem.get("state"))

				# end of each set only records the press, not the
				# release event
				if (btn_state != expected_state):
					expected_state = 1
					continue

				times[btn_state] = int(elem.get("time"))
				expected_state = abs(expected_state - 1)

				if btn_state == 0:
					cur_set.data.append(times[0] - times[1])

		return Results(sets)


	def get_set_id(self, elem):
		return int(elem.get("method")) * 1000 + int(elem.get("id"))

	def _target_aquisition_times(self):
		"""
		Calculates the time between the target and the successful
		click onto the target in milliseconds per set
		"""
		sets = []
		times = [0, 0]
		cur_set = None
		for elem in self.root.iter():
			name = elem.tag
			if name == "set":
				cur_set = self._set_from_elem(elem)
				sets.append(cur_set)
				continue
			elif name == "target":
				times[0] = int(elem.get("time"))
				continue
			elif name != "button":
				continue

			btn_state = int(elem.get("state"))
			if btn_state != 1:
				continue
			hit = int(elem.get("hit"))
			if hit != 1:
				continue

			times[1] = int(elem.get("time"))
			cur_set.data.append(times[1] - times[0])

		return Results(sets)

	def _target_misses(self):
		"""
		Counts the number of clicks not on a target.
		"""
		sets = []
		for elem in self.root.iter():
			name = elem.tag
			if name == "set":
				cur_set = self._set_from_elem(elem);
				cur_set.data = [0]
				sets.append(cur_set)
				continue
			elif name != "button":
				continue

			state = int(elem.get("state"))
			if state == 1:
				hit = int(elem.get("hit"))
				if hit == 0:
					cur_set.data[0] += 1
		return Results(sets)

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

	def _path_length_per_target(self):
		paths = []
		x, y = None, None

		# for overshoot calculation
		vec = None
		target = None # target center
		B = None # some other point on the target line
		initial_side = None
		set_id = -1

		# in % of the original path, the path taken
		path_sets = []
		cur_path_set = None

		# in % of the original path
		overshoot_sets = []
		cur_overshoot_set = None

		cur_distance = None
		cur_path = 0
		cur_overshoot = 0

		for set_elem in self.root.iter("set"):
			cur_path_set = self._set_from_elem(set_elem);
			path_sets.append(cur_path_set)
			cur_overshoot_set = self._set_from_elem(set_elem);
			overshoot_sets.append(cur_overshoot_set)
			cur_distance = None

			for elem in set_elem.iter():
				name = elem.tag
				if name == "motion":
					x = float(elem.get("x"))
					y = float(elem.get("y"))
					dx = float(elem.get("dx"))
					dy = float(elem.get("dy"))
					cur_path += self.vec_length(dx, dy);

					if vec == None:
						vec, B = self.setup_vectors(target, (x, y))
						initial_side = self.side(target, B, (x, y))

					if self.side(target, B, (x, y)) != initial_side:
						d = self.distance(target, B, (x, y))
						if (d > cur_overshoot):
							cur_overshoot = d
					continue

				if name == "target":
					if cur_distance:
						cur_path_set.data.append(100.0 * cur_path / cur_distance)
						cur_overshoot_set.data.append(100.0 * cur_overshoot / cur_distance)

					target = (int(elem.get("xpos")), int(elem.get("ypos")))
					x = float(elem.get("x"))
					y = float(elem.get("y"))

					cur_distance = self.vec_length(target[0] - x,
								       target[1] - y)
					cur_path = 0
					cur_overshoot = 0

					vec, B = self.setup_vectors(target, (x, y))
					initial_side = self.side(target, B, (x, y))
					continue

			cur_path_set.data.append(100.0 * cur_path / cur_distance)
			cur_overshoot_set.data.append(100.0 * cur_overshoot / cur_distance)

		return (Results(path_sets), Results(overshoot_sets))

	def path_lengths(self):
		data = { "identifier" : "path_lengths" }
		data["delta_sums"] = []
		data["path_diffs"] = []

		for p in self.path_length_per_target():
			data["delta_sums"].append(p["delta_sum"])
			data["path_diffs"].append(p["path_diff"])

		data["diff_mean"] = mean(data["path_diffs"])
		return data

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

		qr = QuestionaireResults([int(questionnaire.get("first")),
					  int(questionnaire.get("second"))])

		userdata = questionnaire.find("userdata")
		device = questionnaire.find("device")
		qr.set_userdata(int(userdata.get("age")),
				userdata.get("gender"),
				userdata.get("handed"),
				int(userdata.get("experience")),
				int(userdata.get("hours_per_week")),
				device.get("type"))

		for q in questionnaire.findall("question"):
			qr.questions[int(q.get("question-id"))] = q.text
			qr.answers[int(q.get("question-id"))] = int(q.get("response"))

		return qr


class UserStudyResults(object):
	def __init__(self, path):
		self.results = [r for r in self._parse_files(path)]
		if not self.results:
			raise Exception("No files found")

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

	def button_click_times(self):
		return Results([s for r in self.results for s in r.click_times.sets])

	def target_aquisition_times(self):
		return Results([s for r in self.results for s in r.target_aquisition_times.sets])

	def target_misses(self):
		return Results([s for r in self.results for s in r.target_misses.sets])

	def set_completion_times(self):
		return Results([s for r in self.results for s in r.times_per_set.sets])

	def extra_distances(self):
		return Results([s for r in self.results for s in r.extradistance.sets])

	def overshoot(self):
		return Results([s for r in self.results for s in r.overshoot.sets])

	def user_age(self):
		ages = [ r.questionnaire.age for r in self.results if r.questionnaire.age != 0]
		return mean(ages)

	def user_gender(self):
		genders = [ r.questionnaire.gender for r in self.results ]
		male = genders.count("male")
		female = genders.count("female")
		other = genders.count("other")
		none = genders.count("none")
		return (male, female, other, none)

	def user_handedness(self):
		handed = [ r.questionnaire.handed for r in self.results ]
		return (handed.count("right"), handed.count("left"))

	def user_experience(self):
		experience = [ r.questionnaire.experience for r in self.results if r.questionnaire.experience != 0]
		return mean(experience)

	def user_hours_per_week(self):
		hours = [ r.questionnaire.hours for r in self.results if r.questionnaire.experience != 0]
		return mean(hours)

def print_results(msg, r, sets, target_sizes):
	methods = []
	print "%s: %s" % (msg, r)
	for s in sets:
		matching = r.filter(s)
		print "\tmethod: %d target size: %d: %s" % (s.method, s.target_size, matching)
		if not s.method in methods:
			methods.append(s.method)

	methods = sorted(methods)

	print "\tComparison: "
	for t in target_sizes:
		for (m1, m2) in itertools.combinations(methods, 2):
			r1 = r.filter(SetResults(m1, t))
			r2 = r.filter(SetResults(m2, t))

			print "\t\ttarget size %d: method %d - %d: %f" % (t, m1, m2, r1.mean() - r2.mean())


def print_user_info(results):
	print "User information"

	print "Average age %f (%f)" % results.user_age()
	print "Gender distribution: male %d female %d other %d none given %d" % results.user_gender()
	print "Right-handed: %d, left-handed %d" % results.user_handedness()
	print "Average experience in years: %f (%f)" % results.user_experience()
	print "Average usage in h per week: %f (%f)" % results.user_hours_per_week()

def print_questionnaire(results):
	print "Questionnaire results:"

	methods = sorted(results.methods)

	questions = results.results[0].questionnaire.questions

	for qidx, question in enumerate(questions[:6]):
		print question
		for m in methods:
			count = 5 * [ 0 ]
			data = []
			qrs = [ r.questionnaire for r in results.results ]
			for qr in qrs:
				if not m in qr.methods:
					continue

				if qr.methods[0] == m:
					answer = qr.answers[0]
					data.append(answer)
					count[answer + 2 ] += 1
				if qr.methods[1] == m:
					answer = qr.answers[6]
					data.append(answer)
					count[answer + 2 ] += 1

			stdmean, stddev = mean(data)
			print "For method %d: distribution: %s, mean %f stddev %f" % (m, count, stdmean, stddev)

	question = questions[12]
	print question
	for (m1, m2) in itertools.combinations(methods, 2):
		count = 5 * [ 0 ]
		data = []
		qrs = [ r.questionnaire for r in results.results ]
		for qr in qrs:
			if not m1 in qr.methods or not m2 in qr.methods:
				continue

			answer = qr.answers[12]
			data.append(answer)
			count[answer + 2 ] += 1

		stdmean, stddev = mean(data)
		print "For methods %d and %d: distribution: %s, mean %f stddev %f" % (m1, m2, count, stdmean, stddev)

	question = questions[13]
	print question
	for (m1, m2) in itertools.combinations(methods, 2):
		count = 5 * [ 0 ]
		data = []
		qrs = [ r.questionnaire for r in results.results ]
		for qr in qrs:
			if not m1 in qr.methods or not m2 in qr.methods:
				continue

			answer = qr.answers[13]
			data.append(answer)
			count[answer + 2 ] += 1

		stdmean, stddev = mean(data)
		print "For methods %d and %d: distribution: %s, mean %f stddev %f" % (m1, m2, count, stdmean, stddev)


def main(argv):
	fpath = argv[1];

	try:
		results = UserStudyResults(fpath)
	except:
		print "Error loading results files"
		return

	print_user_info(results)

	print "Target sizes: %s" % results.target_sizes
	print "Methods used: %s" % results.methods
	sets = [SetResults(m, s) for (m, s) in itertools.product(results.methods, results.target_sizes)]

	r = results.button_click_times()

	print "Button click time: %s" % r

	r = results.target_aquisition_times()
	print_results("Target time-to-aquisition times (in ms)", r, sets, results.target_sizes)

	r = results.target_misses()
	print_results("Target mis-clicks (in clicks)", r, sets, results.target_sizes)

	r = results.set_completion_times()
	print_results("Set completion times (in ms)", r, sets, results.target_sizes)

	r = results.extra_distances()
	print_results("Total distances (in % of minimum path)", r, sets, results.target_sizes)

	r = results.overshoot()
	print_results("Target overshoot (in % of minimum path)", r, sets, results.target_sizes)

	print_questionnaire(results)

if __name__ == "__main__":
	main(sys.argv)
