#!/usr/bin/env python3
#
# Copyright (C) 2014 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from common.immutables import ImmutableDict
from file_format.c1visualizer.parser import parse_c1_visualizer_stream
from file_format.c1visualizer.struct import C1visualizerFile, C1visualizerPass

import io
import unittest


class C1visualizerParser_Test(unittest.TestCase):

  def create_file(self, data):
    """ Creates an instance of CheckerFile from provided info.

    Data format: ( [ <isa-feature>, ... ],
                   [ ( <case-name>, [ ( <text>, <assert-variant> ), ... ] ), ... ]
                 )
    """
    c1_file = C1visualizerFile("<c1_file>")
    c1_file.instruction_set_features = data[0]
    for pass_entry in data[1]:
      pass_name = pass_entry[0]
      pass_body = pass_entry[1]
      c1_pass = C1visualizerPass(c1_file, pass_name, pass_body, 0)
    return c1_file

  def assertParsesTo(self, c1_text, expected_data):
    expected_file = self.create_file(expected_data)
    actual_file = parse_c1_visualizer_stream("<c1_file>", io.StringIO(c1_text))
    return self.assertEqual(expected_file, actual_file)

  def test_EmptyFile(self):
    self.assertParsesTo("", (ImmutableDict(), []))

  def test_SingleGroup(self):
    self.assertParsesTo(
      """
        begin_compilation
          method "MyMethod"
        end_compilation
        begin_cfg
          name "pass1"
          foo
          bar
        end_cfg
      """,
      (ImmutableDict(), [
        ("MyMethod pass1", ["foo", "bar"])
      ]))

  def test_MultipleGroups(self):
    self.assertParsesTo(
      """
        begin_compilation
          name "xyz1"
          method "MyMethod1"
          date 1234
        end_compilation
        begin_cfg
          name "pass1"
          foo
          bar
        end_cfg
        begin_cfg
          name "pass2"
          abc
          def
        end_cfg
      """,
      (ImmutableDict(), [
        ("MyMethod1 pass1", ["foo", "bar"]),
        ("MyMethod1 pass2", ["abc", "def"])
      ]))
    self.assertParsesTo(
      """
        begin_compilation
          name "xyz1"
          method "MyMethod1"
          date 1234
        end_compilation
        begin_cfg
          name "pass1"
          foo
          bar
        end_cfg
        begin_compilation
          name "xyz2"
          method "MyMethod2"
          date 5678
        end_compilation
        begin_cfg
          name "pass2"
          abc
          def
        end_cfg
      """,
      (ImmutableDict(), [
        ("MyMethod1 pass1", ["foo", "bar"]),
        ("MyMethod2 pass2", ["abc", "def"])
      ]))

  def test_InstructionSetFeatures(self):
    self.assertParsesTo(
      """
        begin_compilation
          name "isa_features:feature1,-feature2"
          method "isa_features:feature1,-feature2"
          date 1234
        end_compilation
      """,
      (ImmutableDict({"feature1": True, "feature2": False}), []))
    self.assertParsesTo(
      """
        begin_compilation
          name "isa_features:feature1,-feature2"
          method "isa_features:feature1,-feature2"
          date 1234
        end_compilation
        begin_compilation
          name "xyz1"
          method "MyMethod1"
          date 1234
        end_compilation
        begin_cfg
          name "pass1"
          foo
          bar
        end_cfg
      """,
      (ImmutableDict({"feature1": True, "feature2": False}), [
        ("MyMethod1 pass1", ["foo", "bar"])
      ]))
    self.assertParsesTo(
      """
        begin_compilation
          name "isa:some_isa isa_features:feature1,-feature2"
          method "isa:some_isa isa_features:feature1,-feature2"
          date 1234
        end_compilation
      """,
      (ImmutableDict({"feature1": True, "feature2": False}), []))
    self.assertParsesTo(
      """
        begin_compilation
          name "isa:some_isa isa_features:feature1,-feature2"
          method "isa:some_isa isa_features:feature1,-feature2"
          date 1234
        end_compilation
        begin_compilation
          name "xyz1"
          method "MyMethod1"
          date 1234
        end_compilation
        begin_cfg
          name "pass1"
          foo
          bar
        end_cfg
      """,
      (ImmutableDict({"feature1": True, "feature2": False}), [
        ("MyMethod1 pass1", ["foo", "bar"])
      ]))
