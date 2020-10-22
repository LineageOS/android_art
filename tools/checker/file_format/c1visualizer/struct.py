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

import os

from common.immutables import ImmutableDict
from common.logger import Logger
from common.mixins import PrintableMixin


class C1visualizerFile(PrintableMixin):
  def __init__(self, filename):
    self.base_file_name = os.path.basename(filename)
    self.full_file_name = filename
    self.passes = []
    self.instruction_set_features = ImmutableDict()

  def set_isa_features(self, features):
    self.instruction_set_features = ImmutableDict(features)

  def add_pass(self, new_pass):
    self.passes.append(new_pass)

  def find_pass(self, name):
    for entry in self.passes:
      if entry.name == name:
        return entry
    return None

  def __eq__(self, other):
    return (isinstance(other, self.__class__)
            and self.passes == other.passes
            and self.instruction_set_features == other.instruction_set_features)


class C1visualizerPass(PrintableMixin):
  def __init__(self, parent, name, body, start_line_no):
    self.parent = parent
    self.name = name
    self.body = body
    self.start_line_no = start_line_no

    if not self.name:
      Logger.fail("C1visualizer pass does not have a name", self.filename, self.start_line_no)
    if not self.body:
      Logger.fail("C1visualizer pass does not have a body", self.filename, self.start_line_no)

    self.parent.add_pass(self)

  @property
  def filename(self):
    return self.parent.base_file_name

  def __eq__(self, other):
    return (isinstance(other, self.__class__)
            and self.name == other.name
            and self.body == other.body)
