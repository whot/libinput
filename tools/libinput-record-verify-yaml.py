#!/usr/bin/python3
# vim: set expandtab shiftwidth=4:
# -*- Mode: python; coding: utf-8; indent-tabs-mode: nil -*- */
#
# Copyright © 2018 Red Hat, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

import argparse
import os
import sys
import unittest
import yaml
import re

from pkg_resources import parse_version


class TestYaml(unittest.TestCase):
    filename = ''

    @classmethod
    def setUpClass(cls):
        with open(cls.filename) as f:
            cls.yaml = yaml.safe_load(f)

    def dict_key_crosscheck(self, d, keys):
        '''Check that each key in d is in keys, and that each key is in d'''
        self.assertEqual(sorted(d.keys()), sorted(keys))

    def libinput_events(self, filter=None):
        '''Returns all libinput events in the recording, regardless of the
        device'''
        devices = self.yaml['devices']
        for d in devices:
            events = d['events']
            for e in events:
                try:
                    libinput = e['libinput']
                except KeyError:
                    continue

                for ev in libinput:
                    if filter is None or ev['type'] == filter:
                        yield ev

    def test_sections_exist(self):
        sections = ['version', 'ndevices', 'libinput', 'system', 'devices']
        for section in sections:
            self.assertIn(section, self.yaml)

    def test_version(self):
        version = self.yaml['version']
        self.assertTrue(isinstance(version, int))
        self.assertEqual(version, 1)

    def test_ndevices(self):
        ndevices = self.yaml['ndevices']
        self.assertTrue(isinstance(ndevices, int))
        self.assertGreaterEqual(ndevices, 1)
        self.assertEqual(ndevices, len(self.yaml['devices']))

    def test_libinput(self):
        libinput = self.yaml['libinput']
        version = libinput['version']
        self.assertTrue(isinstance(version, str))
        self.assertGreaterEqual(parse_version(version), parse_version('1.10.0'))
        git = libinput['git']
        self.assertTrue(isinstance(git, str))
        self.assertNotEqual(git, 'unknown')

    def test_system(self):
        system = self.yaml['system']
        kernel = system['kernel']
        self.assertTrue(isinstance(kernel, str))
        self.assertEqual(kernel, os.uname().release)

        dmi = system['dmi']
        self.assertTrue(isinstance(dmi, str))
        with open('/sys/class/dmi/id/modalias') as f:
            sys_dmi = f.read()[:-1]  # trailing newline
            self.assertEqual(dmi, sys_dmi)

    def test_devices_sections_exist(self):
        devices = self.yaml['devices']
        for d in devices:
            self.assertIn('node', d)
            self.assertIn('evdev', d)
            self.assertIn('udev', d)

    def test_evdev_sections_exist(self):
        sections = ['name', 'id', 'codes', 'properties']
        devices = self.yaml['devices']
        for d in devices:
            evdev = d['evdev']
            for s in sections:
                self.assertIn(s, evdev)

    def test_evdev_name(self):
        devices = self.yaml['devices']
        for d in devices:
            evdev = d['evdev']
            name = evdev['name']
            self.assertTrue(isinstance(name, str))
            self.assertGreaterEqual(len(name), 5)

    def test_evdev_id(self):
        devices = self.yaml['devices']
        for d in devices:
            evdev = d['evdev']
            id = evdev['id']
            self.assertTrue(isinstance(id, list))
            self.assertEqual(len(id), 4)
            self.assertGreater(id[0], 0)
            self.assertGreater(id[1], 0)

    def test_evdev_properties(self):
        devices = self.yaml['devices']
        for d in devices:
            evdev = d['evdev']
            properties = evdev['properties']
            self.assertTrue(isinstance(properties, list))

    def test_udev_sections_exist(self):
        sections = ['properties']
        devices = self.yaml['devices']
        for d in devices:
            udev = d['udev']
            for s in sections:
                self.assertIn(s, udev)

    def test_udev_properties(self):
        devices = self.yaml['devices']
        for d in devices:
            udev = d['udev']
            properties = udev['properties']
            self.assertTrue(isinstance(properties, list))
            self.assertGreater(len(properties), 0)

            self.assertIn('ID_INPUT=1', properties)
            for p in properties:
                self.assertTrue(re.match('[A-Z0-9_]+=.+', p))

    def test_udev_id_inputs(self):
        devices = self.yaml['devices']
        for d in devices:
            udev = d['udev']
            properties = udev['properties']
            id_inputs = [p for p in properties if p.startswith('ID_INPUT')]
            # We expect ID_INPUT and ID_INPUT_something, but might get more
            # than one of the latter
            self.assertGreaterEqual(len(id_inputs), 2)

    def test_events_have_section(self):
        devices = self.yaml['devices']
        for d in devices:
            events = d['events']
            for e in events:
                self.assertTrue('evdev' in e or 'libinput' in e)

    def test_events_evdev(self):
        devices = self.yaml['devices']
        for d in devices:
            events = d['events']
            for e in events:
                try:
                    evdev = e['evdev']
                except KeyError:
                    continue

                for ev in evdev:
                    self.assertEqual(len(ev), 5)

                # Last event in each frame is SYN_REPORT
                ev_syn = evdev[-1]
                self.assertEqual(ev_syn[2], 0)
                self.assertEqual(ev_syn[3], 0)
                self.assertEqual(ev_syn[4], 0)

    def test_events_evdev_syn_report(self):
        devices = self.yaml['devices']
        for d in devices:
            events = d['events']
            for e in events:
                try:
                    evdev = e['evdev']
                except KeyError:
                    continue
                for ev in evdev[:-1]:
                    self.assertFalse(ev[2] == 0 and ev[3] == 0)

    def test_events_libinput(self):
        devices = self.yaml['devices']
        for d in devices:
            events = d['events']
            for e in events:
                try:
                    libinput = e['libinput']
                except KeyError:
                    continue

                self.assertTrue(isinstance(libinput, list))
                for ev in libinput:
                    self.assertTrue(isinstance(ev, dict))

    def test_events_libinput_type(self):
        types = ['POINTER_MOTION', 'POINTER_MOTION_ABSOLUTE',
                 'POINTER_BUTTON', 'DEVICE_ADDED', 'KEYBOARD_KEY',
                 'TOUCH_DOWN', 'TOUCH_MOTION', 'TOUCH_UP', 'TOUCH_FRAME']
        for e in self.libinput_events():
            self.assertIn('type', e)
            self.assertIn(e['type'], types)

    def test_events_libinput_time(self):
        for e in self.libinput_events():
            if e['type'] == 'DEVICE_ADDED':
                continue

            self.assertIn('time', e)
            self.assertGreater(e['time'], 0.0)
            self.assertLess(e['time'], 60.0)

    def test_events_libinput_device_added(self):
        keys = ['type', 'seat', 'logical_seat']
        for e in self.libinput_events('DEVICE_ADDED'):
            self.dict_key_crosscheck(e, keys)
            self.assertEqual(e['seat'], 'seat0')
            self.assertEqual(e['logical_seat'], 'default')

    def test_events_libinput_pointer_motion(self):
        keys = ['type', 'time', 'delta', 'unaccel']
        for e in self.libinput_events('POINTER_MOTION'):
            self.dict_key_crosscheck(e, keys)
            delta = e['delta']
            self.assertTrue(isinstance(delta, list))
            self.assertEqual(len(delta), 2)
            for d in delta:
                self.assertTrue(isinstance(d, float))
            unaccel = e['unaccel']
            self.assertTrue(isinstance(unaccel, list))
            self.assertEqual(len(unaccel), 2)
            for d in unaccel:
                self.assertTrue(isinstance(d, float))

    def test_events_libinput_pointer_button(self):
        keys = ['type', 'time', 'button', 'state', 'seat_count']
        for e in self.libinput_events('POINTER_BUTTON'):
            self.dict_key_crosscheck(e, keys)
            button = e['button']
            self.assertGreater(button, 0x100)  # BTN_0
            self.assertLess(button, 0x160)  # KEY_OK
            state = e['state']
            self.assertIn(state, ['pressed', 'released'])
            scount = e['seat_count']
            self.assertGreaterEqual(scount, 0)

    def test_events_libinput_pointer_absolute(self):
        keys = ['type', 'time', 'point', 'transformed']
        for e in self.libinput_events('POINTER_MOTION_ABSOLUTE'):
            self.dict_key_crosscheck(e, keys)
            point = e['point']
            self.assertTrue(isinstance(point, list))
            self.assertEqual(len(point), 2)
            for p in point:
                self.assertTrue(isinstance(p, float))
                self.assertGreater(p, 0.0)
                self.assertLess(p, 300.0)

            transformed = e['transformed']
            self.assertTrue(isinstance(transformed, list))
            self.assertEqual(len(transformed), 2)
            for t in transformed:
                self.assertTrue(isinstance(t, float))
                self.assertGreater(t, 0.0)
                self.assertLess(t, 100.0)

    def test_events_libinput_touch(self):
        keys = ['type', 'time', 'slot', 'seat_slot']
        for e in self.libinput_events():
            if (not e['type'].startswith('TOUCH_') or
                    e['type'] == 'TOUCH_FRAME'):
                continue

            for k in keys:
                self.assertIn(k, e.keys())
            slot = e['slot']
            seat_slot = e['seat_slot']

            self.assertGreaterEqual(slot, 0)
            self.assertGreaterEqual(seat_slot, 0)

    def test_events_libinput_touch_down(self):
        keys = ['type', 'time', 'slot', 'seat_slot', 'point', 'transformed']
        for e in self.libinput_events('TOUCH_DOWN'):
            self.dict_key_crosscheck(e, keys);
            point = e['point']
            self.assertTrue(isinstance(point, list))
            self.assertEqual(len(point), 2)
            for p in point:
                self.assertTrue(isinstance(p, float))
                self.assertGreater(p, 0.0)
                self.assertLess(p, 300.0)

            transformed = e['transformed']
            self.assertTrue(isinstance(transformed, list))
            self.assertEqual(len(transformed), 2)
            for t in transformed:
                self.assertTrue(isinstance(t, float))
                self.assertGreater(t, 0.0)
                self.assertLess(t, 100.0)

    def test_events_libinput_touch_motion(self):
        keys = ['type', 'time', 'slot', 'seat_slot', 'point', 'transformed']
        for e in self.libinput_events('TOUCH_MOTION'):
            self.dict_key_crosscheck(e, keys);
            point = e['point']
            self.assertTrue(isinstance(point, list))
            self.assertEqual(len(point), 2)
            for p in point:
                self.assertTrue(isinstance(p, float))
                self.assertGreater(p, 0.0)
                self.assertLess(p, 300.0)

            transformed = e['transformed']
            self.assertTrue(isinstance(transformed, list))
            self.assertEqual(len(transformed), 2)
            for t in transformed:
                self.assertTrue(isinstance(t, float))
                self.assertGreater(t, 0.0)
                self.assertLess(t, 100.0)

    def test_events_libinput_touch_frame(self):
        devices = self.yaml['devices']
        for d in devices:
            events = d['events']
            for e in events:
                try:
                    evdev = e['libinput']
                except KeyError:
                    continue

                need_frame = False
                for ev in evdev:
                    t = ev['type']
                    if not t.startswith('TOUCH_'):
                        self.assertFalse(need_frame)
                        continue

                        self.assertTrue(need_frame)
                        need_frame = False
                    else:
                        need_frame = True

                self.assertFalse(need_frame)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Verify a YAML recording')
    parser.add_argument('recording', metavar='recorded-file.yaml',
                        type=str, help='Path to device recording')
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args()
    TestYaml.filename = args.recording
    verbosity = 1
    if args.verbose:
        verbosity = 3
    del sys.argv[1:]
    unittest.main(verbosity=verbosity)
