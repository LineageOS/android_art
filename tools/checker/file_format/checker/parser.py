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

from common.archs import archs_list
from common.logger import Logger
from file_format.common import split_stream
from file_format.checker.struct import CheckerFile, TestCase, TestStatement, TestExpression

import re


def _is_checker_line(line):
  return line.startswith("///") or line.startswith("##") or line.startswith(";;")


def _extract_line(prefix, line, arch=None, debuggable=False):
  """ Attempts to parse a check line. The regex searches for a comment symbol
      followed by the CHECK keyword, given attribute and a colon at the very
      beginning of the line. Whitespaces are ignored.
  """
  r_ignore_whitespace = r"\s*"
  r_comment_symbols = ["///", "##", ";;"]
  arch_specifier = "-{}".format(arch) if arch is not None else ""
  dbg_specifier = "-DEBUGGABLE" if debuggable else ""
  regex_prefix = (r_ignore_whitespace +
                  "(" + "|".join(r_comment_symbols) + ")"
                  + r_ignore_whitespace + prefix + arch_specifier + dbg_specifier + ":")

  # The 'match' function succeeds only if the pattern is matched at the
  # beginning of the line.
  match = re.match(regex_prefix, line)
  if match is not None:
    return line[match.end():].strip()
  else:
    return None


def _preprocess_line_for_start(prefix, line, target_arch):
  """ This function modifies a CHECK-START-{x,y,z} into a matching
      CHECK-START-y line for matching targetArch y. If no matching
      architecture is found, CHECK-START-x is returned arbitrarily
      to ensure all following check lines are put into a test that
      is skipped. Any other line is left unmodified.
  """
  if target_arch is not None:
    if prefix in line:
      # Find { } on the line and assume that defines the set.
      s = line.find("{")
      e = line.find("}")
      if 0 < s < e:
        archs = line[s + 1:e].split(",")
        # First verify that every archs is valid. Return the
        # full line on failure to prompt error back to user.
        for arch in archs:
          if arch not in archs_list:
            return line
        # Now accept matching arch or arbitrarily return first.
        if target_arch in archs:
          return line[:s] + target_arch + line[e + 1:]
        else:
          return line[:s] + archs[0] + line[e + 1:]
  return line


def _process_line(line, line_no, prefix, filename, target_arch):
  """ This function is invoked on each line of the check file and returns a triplet
      which instructs the parser how the line should be handled. If the line is
      to be included in the current check group, it is returned in the first
      value. If the line starts a new check group, the name of the group is
      returned in the second value. The third value indicates whether the line
      contained an architecture-specific suffix.
  """
  if not _is_checker_line(line):
    return None, None, None

  # Lines beginning with 'CHECK-START' start a new test case.
  # We currently only consider the architecture suffix(es) in "CHECK-START" lines.
  for debuggable in [True, False]:
    sline = _preprocess_line_for_start(prefix + "-START", line, target_arch)
    for arch in [None] + archs_list:
      start_line = _extract_line(prefix + "-START", sline, arch, debuggable)
      if start_line is not None:
        return None, start_line, (arch, debuggable)

  # Lines starting only with 'CHECK' are matched in order.
  plain_line = _extract_line(prefix, line)
  if plain_line is not None:
    return (plain_line, TestStatement.Variant.IN_ORDER, line_no), None, None

  # 'CHECK-NEXT' lines are in-order but must match the very next line.
  next_line = _extract_line(prefix + "-NEXT", line)
  if next_line is not None:
    return (next_line, TestStatement.Variant.NEXT_LINE, line_no), None, None

  # 'CHECK-DAG' lines are no-order statements.
  dag_line = _extract_line(prefix + "-DAG", line)
  if dag_line is not None:
    return (dag_line, TestStatement.Variant.DAG, line_no), None, None

  # 'CHECK-NOT' lines are no-order negative statements.
  not_line = _extract_line(prefix + "-NOT", line)
  if not_line is not None:
    return (not_line, TestStatement.Variant.NOT, line_no), None, None

  # 'CHECK-EVAL' lines evaluate a Python expression.
  eval_line = _extract_line(prefix + "-EVAL", line)
  if eval_line is not None:
    return (eval_line, TestStatement.Variant.EVAL, line_no), None, None

  # 'CHECK-IF' lines mark the beginning of a block that will be executed
  # only if the Python expression that follows evaluates to true.
  if_line = _extract_line(prefix + "-IF", line)
  if if_line is not None:
    return (if_line, TestStatement.Variant.IF, line_no), None, None

  # 'CHECK-ELIF' lines mark the beginning of an `else if` branch of a CHECK-IF block.
  elif_line = _extract_line(prefix + "-ELIF", line)
  if elif_line is not None:
    return (elif_line, TestStatement.Variant.ELIF, line_no), None, None

  # 'CHECK-ELSE' lines mark the beginning of the `else` branch of a CHECK-IF block.
  else_line = _extract_line(prefix + "-ELSE", line)
  if else_line is not None:
    return (else_line, TestStatement.Variant.ELSE, line_no), None, None

  # 'CHECK-FI' lines mark the end of a CHECK-IF block.
  fi_line = _extract_line(prefix + "-FI", line)
  if fi_line is not None:
    return (fi_line, TestStatement.Variant.FI, line_no), None, None

  Logger.fail("Checker statement could not be parsed: '" + line + "'", filename, line_no)


def _is_match_at_start(match):
  """ Tests if the given Match occurred at the beginning of the line. """
  return (match is not None) and (match.start() == 0)


def _first_match(matches, string):
  """ Takes in a list of Match objects and returns the minimal start point among
      them. If there aren't any successful matches it returns the length of
      the searched string.
  """
  return min(len(string) if m is None else m.start() for m in matches)


def parse_checker_statement(parent, line, variant, line_no):
  """ This method parses the content of a check line stripped of the initial
      comment symbol and the CHECK-* keyword.
  """
  statement = TestStatement(parent, variant, line, line_no)

  if statement.is_no_content_statement() and line:
    Logger.fail("Expected empty statement: '{}'".format(line), statement.filename,
                statement.line_no)

  # Loop as long as there is something to parse.
  while line:
    # Search for the nearest occurrence of the special markers.
    if statement.is_eval_content_statement():
      # The following constructs are not supported in CHECK-EVAL, -IF and -ELIF lines
      match_whitespace = None
      match_pattern = None
      match_variable_definition = None
    else:
      match_whitespace = re.search(r"\s+", line)
      match_pattern = re.search(TestExpression.Regex.REGEX_PATTERN, line)
      match_variable_definition = re.search(TestExpression.Regex.REGEX_VARIABLE_DEFINITION, line)
    match_variable_reference = re.search(TestExpression.Regex.REGEX_VARIABLE_REFERENCE, line)

    # If one of the above was identified at the current position, extract them
    # from the line, parse them and add to the list of line parts.
    if _is_match_at_start(match_whitespace):
      # A whitespace in the check line creates a new separator of line parts.
      # This allows for ignored output between the previous and next parts.
      line = line[match_whitespace.end():]
      statement.add_expression(TestExpression.create_separator())
    elif _is_match_at_start(match_pattern):
      pattern = line[0:match_pattern.end()]
      pattern = pattern[2:-2]
      line = line[match_pattern.end():]
      statement.add_expression(TestExpression.create_pattern(pattern))
    elif _is_match_at_start(match_variable_reference):
      var = line[0:match_variable_reference.end()]
      line = line[match_variable_reference.end():]
      name = var[2:-2]
      statement.add_expression(TestExpression.create_variable_reference(name))
    elif _is_match_at_start(match_variable_definition):
      var = line[0:match_variable_definition.end()]
      line = line[match_variable_definition.end():]
      colon_pos = var.find(":")
      name = var[2:colon_pos]
      body = var[colon_pos + 1:-2]
      statement.add_expression(TestExpression.create_variable_definition(name, body))
    else:
      # If we're not currently looking at a special marker, this is a plain
      # text match all the way until the first special marker (or the end
      # of the line).
      first_match = _first_match([match_whitespace,
                                  match_pattern,
                                  match_variable_reference,
                                  match_variable_definition],
                                 line)
      text = line[0:first_match]
      line = line[first_match:]
      if statement.is_eval_content_statement():
        statement.add_expression(TestExpression.create_plain_text(text))
      else:
        statement.add_expression(TestExpression.create_pattern_from_plain_text(text))
  return statement


def parse_checker_stream(file_name, prefix, stream, target_arch=None):
  checker_file = CheckerFile(file_name)

  def fn_process_line(line, line_no):
    return _process_line(line, line_no, prefix, file_name, target_arch)

  def fn_line_outside_chunk(line, line_no):
    Logger.fail("Checker line not inside a group", file_name, line_no)

  for case_name, case_lines, start_line_no, test_data in split_stream(stream, fn_process_line,
                                                                      fn_line_outside_chunk):
    test_arch = test_data[0]
    for_debuggable = test_data[1]
    test_case = TestCase(checker_file, case_name, start_line_no, test_arch, for_debuggable)
    for case_line in case_lines:
      parse_checker_statement(test_case, case_line[0], case_line[1], case_line[2])
  return checker_file
