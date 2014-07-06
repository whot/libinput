#!/usr/bin/python

import sys
import math
from pprint import pprint
import xml.etree.ElementTree

def mean(data):
	m = 1.0 * sum(data)/len(data)
	stddev = math.sqrt(sum((x-m) ** 2 for x in data) / len(data))
	return {"mean" : m, "stdev" : stddev}

class UserStudyParser(object):
	def __init__(self, path):
		self.tree = xml.etree.ElementTree.parse(path)
		self.root = self.tree.getroot()

	def button_click_times(self):
		"""
		Searches for time between button press and release
		events and returns a Data object with the raw times in
		milliseconds
		"""
		data = {"identifier" : "button_click_times",
			"click_durations" : [],
			}
		times = [0, 0]
		expected_state = 1
		for button in self.root.iter("button"):
			btn_state = int(button.get("state"))

			# end of each set only records the press, not the
			# release event
			if (btn_state != expected_state):
				expected_state = 1
				continue

			times[btn_state] = int(button.get("time"))
			expected_state = abs(expected_state - 1)

			if btn_state == 0:
				data["click_durations"].append(times[0] - times[1])

		data["mean"] = mean(data["click_durations"])
		return data

	def target_aquisition_times(self):
		"""
		Calculates the time between the target and the successful
		click onto the target in milliseconds
		"""

		data = {"identifier" : "target_aquisition_times",
			"times_to_hit" : [],
			}

		times = self.target_aquisition_times_per_set();
		for t in times:
			data["times_to_hit"] += t["times_to_hit"]

		data["mean"] = mean(data["times_to_hit"])
		return data


	def target_aquisition_times_per_set(self):
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
				cur_set = { "identifier" : "target_aquisition_times_per_set",
					    "set_id" : int(elem.get("id")),
					    "object_radius" : int(elem.get("r")),
					    }
				cur_set["times_to_hit"] = []
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

			times[1] = int(elem.get("time"))
			cur_set["times_to_hit"].append(times[1] - times[0])

		for s in sets:
			s["mean"] = mean(s["times_to_hit"])
		return sorted(sets, key=lambda data : data["object_radius"])

	def target_misses(self):
		data = {
			"identifier" : "target_misses_per_set",
			"misses" : 0
			}

		misses = self.target_misses_per_set()
		for m in misses:
			data["misses"] += m["misses"]
		return data

	def target_misses_per_set(self):
		"""
		Return: a list of dicts {
			"radius" : int # size of target
			"misses" : int # number of misses
		}
		"""
		sets = {}

		for elem in self.root.iter():
			name = elem.tag
			if name == "set":
				set_id = int(elem.get("id"))
				sets[set_id] = {
						"identifier" : "target_misses_per_set",
						"radius" : int(elem.get("r")),
						"misses" : 0
						}
				continue
			elif name != "button":
				continue

			state = int(elem.get("state"))
			if state == 1:
				hit = int(elem.get("hit"))
				if hit == 0:
					sets[set_id]["misses"] += 1
		return sorted(sets.values(), key=lambda data : data["radius"])

	def time_per_set(self):
		sets = []
		times = [0, 0]
		cur_set = None
		for elem in self.root.iter():
			name = elem.tag
			if name == "set":
				cur_set = { "identifier" : "time_per_set",
					    "set_id" : int(elem.get("id")),
					    "object_radius" : int(elem.get("r")),
					    }

				times[0] = int(elem.get("time"))
				sets.append(cur_set)
				continue
			elif name != "button":
				continue

			state = int(elem.get("state"))
			if state == 1:
				times[1] = int(elem.get("time"))
				cur_set["time"] = times[1] - times[0]

		return sets

	def setup_vectors(self, target, P):
		vec = (target[0] - P[0], target[1] - P[1]);
		vec_p = (-vec[1], vec[0]) # perpendicular
		B = (target[0] + vec_p[0], target[1] + vec_p[1])
		return vec, B

	def path_length_per_target(self):
		paths = []
		x, y = None, None

		# for overshoot calculation
		vec = None
		target = None # target center
		B = None # some other point on the target line
		initial_side = None

		for elem in self.root.iter():
			name = elem.tag

			# first delta is unaccounted for
			# FIXME: store current pointer pos in <target>
			if name == "motion":
				x = float(elem.get("x"))
				y = float(elem.get("y"))
				dx = float(elem.get("dx"))
				dy = float(elem.get("dy"))
				curr_path["delta_sum"] += math.sqrt(dx * dx + dy * dy)
				if not curr_path.has_key("distance"):
					xdist = abs(curr_path["position"][0] - x)
					ydist = abs(curr_path["position"][1] - y)
					curr_path["distance"] = math.sqrt(xdist * xdist + ydist + ydist)

				if vec == None:
					vec, B = self.setup_vectors(target, (x, y))
					initial_side = self.side(target, B, (x, y))

				if self.side(target, B, (x, y)) != initial_side:
					d = self.distance(target, B, (x, y))
					if (d > curr_path["overshoot"]):
						curr_path["overshoot"] = d
				continue

			if name == "target":
				curr_path = {"identifier" : "path_length_per_target",
					    "object_radius" : int(elem.get("r")),
					    "position" : (int(elem.get("xpos")), int(elem.get("ypos"))),
					    "delta_sum" : 0,
					    "overshoot" : 0,
					   }

				target = (int(elem.get("xpos")), int(elem.get("ypos")))
				x = float(elem.get("x"))
				y = float(elem.get("y"))

				xdist = abs(curr_path["position"][0] - x)
				ydist = abs(curr_path["position"][1] - y)
				curr_path["distance"] = math.sqrt(xdist * xdist + ydist + ydist)

				vec, B = self.setup_vectors(target, (x, y))
				initial_side = self.side(target, B, (x, y))

				paths.append(curr_path)
				continue

		# delta_sum is all deltas together
		# distance is the direct distance to the target
		# path_diff: is distance/delta_sum in percent overshot, i.e 100 == perfect line to target
		for p in paths:
			p["path_diff"] = p["delta_sum"]/p["distance"] * 100# in percent
			os = p["overshoot"]
			if (os != 0):
				os = p["overshoot"]/p["distance"] * 100

			p["overshoot_percent"] =  os # in percent
		return paths

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

	def measure_overshoot(self):
		paths = []
		x, y = None, None
		vec = None
		target = None # target center
		B = None # some other point on the target line
		initial_side = None

		for elem in self.root.iter():
			name = elem.tag

			if name == "motion":
				x = float(elem.get("x"))
				y = float(elem.get("y"))
				if vec == None:
					vec, B = self.setup_vectors(target, (x, y))
					initial_side = self.side(target, B, (x, y))

				if self.side(target, B, (x, y)) != initial_side:
					d = self.distance(target, B, (x, y))
					# FIXME: reduce d by object_radius, we're currently
					# measuring from the center
					if (d > curr_path["overshoot"]):
						curr_path["overshoot"] = d
				continue
			if name == "target":
				curr_path = {"identifier" : "overshoot_per_target",
					    "object_radius" : int(elem.get("r")),
					    "overshoot" : 0
					   }
				target = (int(elem.get("xpos")), int(elem.get("ypos")))
				x = float(elem.get("x"));
				y = float(elem.get("y"));

				vec, B = self.setup_vectors(target, (x, y))
				initial_side = self.side(target, B, (x, y))

				paths.append(curr_path)
				continue
		return paths

if __name__ == "__main__":
	fpath = sys.argv[1];
	parser = UserStudyParser(fpath)


	click_times = parser.button_click_times()
	target_aquisition_times = parser.target_aquisition_times();
	target_per_set_times = parser.target_aquisition_times_per_set();
	target_misses = parser.target_misses()

	print(":::::::Button click durations:")
	pprint(click_times)
	print("::::::: Target misses:")
	pprint(target_misses)
	print("::::::: Target misses per set")
	for m in parser.target_misses_per_set():
		pprint(m)

	print("Times per set:")
	for t in parser.time_per_set():
		pprint(t)
	print(":::::: Target aquisition times: ")
	pprint(target_aquisition_times)
	print(":::::: Target aquisition times per set")
	for data in target_per_set_times:
		pprint(data)

	print(":::::: Difference in distance to path taken:")
	pprint(parser.path_lengths())
	print(":::::: Difference in distance to path taken per target:")
	for p in parser.path_length_per_target():
		pprint(p)
