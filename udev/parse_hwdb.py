#!/usr/bin/env python3
# vim: set expandtab shiftwidth=4:
# -*- Mode: python; coding: utf-8; indent-tabs-mode: nil -*- */
#
# ANY MODIFICATIONS TO THIS FILE SHOULD BE MERGED INTO THE SYSTEMD UPSTREAM
#
# This file is part of systemd. It is distributed under the MIT license, see
# below.
#
# Copyright 2016 Zbigniew Jędrzejewski-Szmek
#
# The MIT License (MIT)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import functools
import glob
import string
import sys
import os

try:
    from pyparsing import (Word, White, Literal, ParserElement, Regex,
                           LineStart, LineEnd,
                           ZeroOrMore, OneOrMore, Combine, Or, Optional, Suppress, Group,
                           nums, alphanums, printables,
                           stringEnd, pythonStyleComment,
                           ParseBaseException)
except ImportError:
    print('pyparsing is not available')
    sys.exit(77)

try:
    from evdev.ecodes import ecodes
except ImportError:
    ecodes = None
    print('WARNING: evdev is not available')

EOL = LineEnd().suppress()
EMPTYLINE = LineEnd()
COMMENTLINE = pythonStyleComment + EOL
INTEGER = Word(nums)
REAL = Combine((INTEGER + Optional('.' + Optional(INTEGER))) ^ ('.' + INTEGER))
UDEV_TAG = Word(string.ascii_uppercase, alphanums + '_')

TYPES = {
         'libinput': ('name', 'touchpad', 'mouse', 'keyboard'),
         }

@functools.lru_cache()
def hwdb_grammar():
    ParserElement.setDefaultWhitespaceChars('')

    prefix = Or(category + ':' + Or(conn) + ':'
                for category, conn in TYPES.items())
    matchline = Combine(prefix + Word(printables + ' ' + '®')) + EOL
    propertyline = (White(' ', exact=1).suppress() +
                    Combine(UDEV_TAG - '=' - Word(alphanums + '_=:@*.! ') - Optional(pythonStyleComment)) +
                    EOL)
    propertycomment = White(' ', exact=1) + pythonStyleComment + EOL

    group = (OneOrMore(matchline('MATCHES*') ^ COMMENTLINE.suppress()) -
             OneOrMore(propertyline('PROPERTIES*') ^ propertycomment.suppress()) -
             (EMPTYLINE ^ stringEnd()).suppress() )
    commentgroup = OneOrMore(COMMENTLINE).suppress() - EMPTYLINE.suppress()

    grammar = OneOrMore(group('GROUPS*') ^ commentgroup) + stringEnd()

    return grammar

@functools.lru_cache()
def property_grammar():
    ParserElement.setDefaultWhitespaceChars(' ')

    model_props = [Regex(r'LIBINPUT_MODEL_[_0-9A-Z]+')('NAME')
                   - Suppress('=') -
                   (Literal('1'))('VALUE')
                  ]

    dimension = INTEGER('X') + Suppress('x') + INTEGER('Y')
    sz_props = (
            ('LIBINPUT_ATTR_SIZE_HINT', Group(dimension('SETTINGS*'))),
            ('LIBINPUT_ATTR_RESOLUTION_HINT', Group(dimension('SETTINGS*'))),
            )

    size_props = [Literal(name)('NAME') - Suppress('=') - val('VALUE')
                   for name, val in sz_props]

    reliability_tags = Or(('reliable', 'write_open'))
    reliability = [Literal('LIBINPUT_ATTR_LID_SWITCH_RELIABILITY')('NAME') -
                         Suppress('=') -
                         reliability_tags('VALUE')]

    tpkbcombo_tags = Or(('below'))
    tpkbcombo = [Literal('LIBINPUT_ATTR_TPKBCOMBO_LAYOUT')('NAME') -
                         Suppress('=') -
                         tpkbcombo_tags('VALUE')]

    pressure_range = INTEGER('X') + Suppress(':') + INTEGER('Y')
    pressure_prop = [ Literal('LIBINPUT_ATTR_PRESSURE_RANGE')('NAME') -
                      Suppress('=') -
                      Group(pressure_range('SETTINGS*')) ]

    kbintegration_tags = Or(('internal', 'external'))
    kbintegration = [Literal('LIBINPUT_ATTR_KEYBOARD_INTEGRATION')('NAME') -
                         Suppress('=') -
                         kbintegration_tags('VALUE')]

    palm_prop = [Literal('LIBINPUT_ATTR_PALM_PRESSURE_THRESHOLD')('NAME') -
                        Suppress('=') -
                        INTEGER('X')]

    grammar = Or(model_props + size_props + reliability + tpkbcombo +
                 pressure_prop + kbintegration + palm_prop)

    return grammar

ERROR = False
def error(fmt, *args, **kwargs):
    global ERROR
    ERROR = True
    print(fmt.format(*args, **kwargs))

def convert_properties(group):
    matches = [m[0] for m in group.MATCHES]
    props = [p[0] for p in group.PROPERTIES]
    return matches, props

def parse(fname):
    grammar = hwdb_grammar()
    try:
        parsed = grammar.parseFile(fname)
    except ParseBaseException as e:
        error('Cannot parse {}: {}', fname, e)
        return []
    return [convert_properties(g) for g in parsed.GROUPS]

def check_match_uniqueness(groups):
    matches = sum((group[0] for group in groups), [])
    matches.sort()
    prev = None
    for match in matches:
        if match == prev:
            error('Match {!r} is duplicated', match)
        prev = match

def check_one_dimension(prop, value):
    if int(value[0]) <= 0 or int(value[1]) <= 0:
        error('Dimension {} invalid', value)

def check_properties(groups):
    grammar = property_grammar()
    for matches, props in groups:
        prop_names = set()
        for prop in props:
            # print('--', prop)
            prop = prop.partition('#')[0].rstrip()
            try:
                parsed = grammar.parseString(prop)
            except ParseBaseException as e:
                error('Failed to parse: {!r}', prop)
                continue
            # print('{!r}'.format(parsed))
            if parsed.NAME in prop_names:
                error('Property {} is duplicated', parsed.NAME)
            prop_names.add(parsed.NAME)
            if parsed.NAME == "LIBINPUT_ATTR_SIZE_HINT" or \
               parsed.NAME == "LIBINPUT_ATTR_RESOLUTION_HINT":
                check_one_dimension(prop, parsed.VALUE)

def print_summary(fname, groups):
    print('{}: {} match groups, {} matches, {} properties'
          .format(fname,
                  len(groups),
                  sum(len(matches) for matches, props in groups),
                  sum(len(props) for matches, props in groups),
          ))

if __name__ == '__main__':
    args = sys.argv[1:] or glob.glob(os.path.dirname(sys.argv[0]) + '/*.hwdb')

    for fname in args:
        groups = parse(fname)
        print_summary(fname, groups)
        check_match_uniqueness(groups)
        check_properties(groups)

    sys.exit(ERROR)
