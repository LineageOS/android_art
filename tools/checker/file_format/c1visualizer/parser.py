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

from common.logger import Logger
from file_format.common import split_stream
from file_format.c1visualizer.struct import C1visualizerFile, C1visualizerPass

import re


class C1ParserState:
  OUTSIDE_BLOCK, INSIDE_COMPILATION_BLOCK, STARTING_CFG_BLOCK, INSIDE_CFG_BLOCK = range(4)

  def __init__(self):
    self.current_state = C1ParserState.OUTSIDE_BLOCK
    self.last_method_name = None


def _parse_c1_line(c1_file, line, line_no, state, filename):
  """ This function is invoked on each line of the output file and returns
      a triplet which instructs the parser how the line should be handled. If the
      line is to be included in the current group, it is returned in the first
      value. If the line starts a new output group, the name of the group is
      returned in the second value. The third value is only here to make the
      function prototype compatible with `SplitStream` and is always set to
      `None` here.
  """
  if state.current_state == C1ParserState.STARTING_CFG_BLOCK:
    # Previous line started a new 'cfg' block which means that this one must
    # contain the name of the pass (this is enforced by C1visualizer).
    if re.match(r'name\s+"[^"]+"', line):
      # Extract the pass name, prepend it with the name of the method and
      # return as the beginning of a new group.
      state.current_state = C1ParserState.INSIDE_CFG_BLOCK
      return None, state.last_method_name + " " + line.split('"')[1], None
    else:
      Logger.fail("Expected output group name", filename, line_no)

  elif state.current_state == C1ParserState.INSIDE_CFG_BLOCK:
    if line == "end_cfg":
      state.current_state = C1ParserState.OUTSIDE_BLOCK
      return None, None, None
    else:
      return line, None, None

  elif state.current_state == C1ParserState.INSIDE_COMPILATION_BLOCK:
    # Search for the method's name. Format: method "<name>"
    if re.match(r'method\s+"[^"]*"', line):
      method_name = line.split('"')[1].strip()
      if not method_name:
        Logger.fail("Empty method name in output", filename, line_no)

      match = re.search(r"isa_features:([\w,-]+)", method_name)
      if match:
        raw_features = match.group(1).split(",")
        # Create a map of features in the form {feature_name: is_enabled}.
        features = {}
        for rf in raw_features:
          feature_name = rf
          is_enabled = True
          # A '-' in front of the feature name indicates that the feature wasn't enabled at compile
          # time.
          if rf[0] == "-":
            feature_name = rf[1:]
            is_enabled = False
          features[feature_name] = is_enabled

        c1_file.set_isa_features(features)
      else:
        state.last_method_name = method_name
    elif line == "end_compilation":
      state.current_state = C1ParserState.OUTSIDE_BLOCK
    return None, None, None

  else:
    assert state.current_state == C1ParserState.OUTSIDE_BLOCK
    if line == "begin_cfg":
      # The line starts a new group but we'll wait until the next line from
      # which we can extract the name of the pass.
      if state.last_method_name is None:
        Logger.fail("Expected method header", filename, line_no)
      state.current_state = C1ParserState.STARTING_CFG_BLOCK
      return None, None, None
    elif line == "begin_compilation":
      state.current_state = C1ParserState.INSIDE_COMPILATION_BLOCK
      return None, None, None
    else:
      Logger.fail("C1visualizer line not inside a group", filename, line_no)


def parse_c1_visualizer_stream(filename, stream):
  c1_file = C1visualizerFile(filename)
  state = C1ParserState()

  def fn_process_line(line, line_no):
    return _parse_c1_line(c1_file, line, line_no, state, c1_file.base_file_name)

  def fn_line_outside_chunk(line, line_no):
    Logger.fail("C1visualizer line not inside a group", c1_file.base_file_name, line_no)

  for pass_name, pass_lines, start_line_no, test_arch in split_stream(stream, fn_process_line,
                                                                      fn_line_outside_chunk):
    C1visualizerPass(c1_file, pass_name, pass_lines, start_line_no + 1)
  return c1_file
