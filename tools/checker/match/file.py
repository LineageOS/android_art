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

from collections import namedtuple

from common.immutables import ImmutableDict
from common.logger import Logger
from file_format.checker.struct import TestStatement
from match.line import match_lines, evaluate_line

MatchScope = namedtuple("MatchScope", ["start", "end"])
MatchInfo = namedtuple("MatchInfo", ["scope", "variables"])


class MatchFailedException(Exception):
  def __init__(self, statement, line_no, variables):
    self.statement = statement
    self.line_no = line_no
    self.variables = variables


class BadStructureException(Exception):
  def __init__(self, msg, line_no):
    self.msg = msg
    self.line_no = line_no


class IfStack:
  """
  The purpose of this class is to keep track of which branch the cursor is in.
  This will let us know if the line read by the cursor should be processed or not.
  Furthermore, this class contains the methods to handle the CHECK-[IF, ELIF, ELSE, FI]
  statements, and consequently update the stack with new information.

  The following elements can appear on the stack:
  - BRANCH_TAKEN: a branch is taken if its condition evaluates to true and
    its parent branch was also previously taken.
  - BRANCH_NOT_TAKEN_YET: the branch's parent was taken, but this branch wasn't as its
    condition did not evaluate to true.
  - BRANCH_NOT_TAKEN: a branch is not taken when its parent was either NotTaken or NotTakenYet.
    It doesn't matter if the condition would evaluate to true, that's not even checked.

  CHECK-IF is the only instruction that pushes a new element on the stack. CHECK-ELIF
  and CHECK-ELSE will update the top of the stack to keep track of what's been seen.
  That means that we can check if the line currently pointed to by the cursor should be
  processed just by looking at the top of the stack.
  CHECK-FI will pop the last element.

  `BRANCH_TAKEN`, `BRANCH_NOT_TAKEN`, `BRANCH_NOT_TAKEN_YET` are implemented as positive integers.
  Negated values of `BRANCH_TAKEN` and `BRANCH_NOT_TAKEN` may be appear; `-BRANCH_TAKEN` and
  `-BRANCH_NOT_TAKEN` have the same meaning as `BRANCH_TAKEN` and `BRANCH_NOT_TAKEN`
  (respectively), but they indicate that we went past the ELSE branch. Knowing that, we can
  output a precise error message if the user creates a malformed branching structure.
  """

  BRANCH_TAKEN, BRANCH_NOT_TAKEN, BRANCH_NOT_TAKEN_YET = range(1, 4)

  def __init__(self):
    self.stack = []

  def can_execute(self):
    """
    Returns true if we're not in any branch, or the branch we're
    currently in was taken.
    """
    if self._is_empty():
      return True
    return abs(self._peek()) == IfStack.BRANCH_TAKEN

  def handle(self, statement, variables):
    """
    This function is invoked if the cursor is pointing to a
    CHECK-[IF, ELIF, ELSE, FI] line.
    """
    variant = statement.variant
    if variant is TestStatement.Variant.IF:
      self._if(statement, variables)
    elif variant is TestStatement.Variant.ELIF:
      self._elif(statement, variables)
    elif variant is TestStatement.Variant.ELSE:
      self._else(statement)
    else:
      assert variant is TestStatement.Variant.FI
      self._fi(statement)

  def eof(self):
    """
    The last line the cursor points to is always EOF.
    """
    if not self._is_empty():
      raise BadStructureException("Missing CHECK-FI", -1)

  def _is_empty(self):
    return not self.stack

  def _if(self, statement, variables):
    if not self._is_empty() and abs(self._peek()) in [IfStack.BRANCH_NOT_TAKEN,
                                                      IfStack.BRANCH_NOT_TAKEN_YET]:
      self._push(IfStack.BRANCH_NOT_TAKEN)
    elif evaluate_line(statement, variables):
      self._push(IfStack.BRANCH_TAKEN)
    else:
      self._push(IfStack.BRANCH_NOT_TAKEN_YET)

  def _elif(self, statement, variables):
    if self._is_empty():
      raise BadStructureException("CHECK-ELIF must be after CHECK-IF or CHECK-ELIF",
                                  statement.line_no)
    if self._peek() < 0:
      raise BadStructureException("CHECK-ELIF cannot be after CHECK-ELSE", statement.line_no)
    if self._peek() == IfStack.BRANCH_TAKEN:
      self._set_last(IfStack.BRANCH_NOT_TAKEN)
    elif self._peek() == IfStack.BRANCH_NOT_TAKEN_YET:
      if evaluate_line(statement, variables):
        self._set_last(IfStack.BRANCH_TAKEN)
      # else, the CHECK-ELIF condition is False, so do nothing: the last element on the stack is
      # already set to BRANCH_NOT_TAKEN_YET.
    else:
      assert self._peek() == IfStack.BRANCH_NOT_TAKEN

  def _else(self, statement):
    if self._is_empty():
      raise BadStructureException("CHECK-ELSE must be after CHECK-IF or CHECK-ELIF",
                                  statement.line_no)
    if self._peek() < 0:
      raise BadStructureException("Consecutive CHECK-ELSE statements", statement.line_no)
    if self._peek() in [IfStack.BRANCH_TAKEN, IfStack.BRANCH_NOT_TAKEN]:
      # Notice that we're setting -BRANCH_NOT_TAKEN rather that BRANCH_NOT_TAKEN as we went past the
      # ELSE branch.
      self._set_last(-IfStack.BRANCH_NOT_TAKEN)
    else:
      assert self._peek() == IfStack.BRANCH_NOT_TAKEN_YET
      # Setting -BRANCH_TAKEN rather BRANCH_TAKEN for the same reason.
      self._set_last(-IfStack.BRANCH_TAKEN)

  def _fi(self, statement):
    if self._is_empty():
      raise BadStructureException("CHECK-FI does not have a matching CHECK-IF", statement.line_no)
    self.stack.pop()

  def _peek(self):
    assert not self._is_empty()
    return self.stack[-1]

  def _push(self, element):
    self.stack.append(element)

  def _set_last(self, element):
    self.stack[-1] = element


def find_matching_line(statement, c1_pass, scope, variables, exclude_lines=[]):
  """ Finds the first line in `c1_pass` which matches `statement`.

  Scan only lines numbered between `scope.start` and `scope.end` and not on the
  `excludeLines` list.

  Returns the index of the `c1Pass` line matching the statement and variables
  values after the match.

  Raises MatchFailedException if no such `c1Pass` line can be found.
  """
  for i in range(scope.start, scope.end):
    if i in exclude_lines:
      continue
    new_variables = match_lines(statement, c1_pass.body[i], variables)
    if new_variables is not None:
      return MatchInfo(MatchScope(i, i), new_variables)
  raise MatchFailedException(statement, scope.start, variables)


class ExecutionState(object):
  def __init__(self, c1_pass, variables={}):
    self.cursor = 0
    self.c1_pass = c1_pass
    self.c1_length = len(c1_pass.body)
    self.variables = ImmutableDict(variables)
    self.dag_queue = []
    self.not_queue = []
    self.if_stack = IfStack()
    self.last_variant = None

  def move_cursor(self, match):
    assert self.cursor <= match.scope.end

    # Handle any pending NOT statements before moving the cursor
    self.handle_not_queue(MatchScope(self.cursor, match.scope.start))

    self.cursor = match.scope.end + 1
    self.variables = match.variables

  def handle_dag_queue(self, scope):
    """ Attempts to find matching `c1Pass` lines for a group of DAG statements.

    Statements are matched in the list order and variable values propagated. Only
    lines in `scope` are scanned and each line can only match one statement.

    Returns the range of `c1Pass` lines covered by this group (min/max of matching
    line numbers) and the variable values after the match of the last statement.

    Raises MatchFailedException when a statement cannot be satisfied.
    """
    if not self.dag_queue:
      return

    matched_lines = []
    variables = self.variables

    for statement in self.dag_queue:
      assert statement.variant == TestStatement.Variant.DAG
      match = find_matching_line(statement, self.c1_pass, scope, variables, matched_lines)
      variables = match.variables
      assert match.scope.start == match.scope.end
      assert match.scope.start not in matched_lines
      matched_lines.append(match.scope.start)

    match = MatchInfo(MatchScope(min(matched_lines), max(matched_lines)), variables)
    self.dag_queue = []
    self.move_cursor(match)

  def handle_not_queue(self, scope):
    """ Verifies that none of the given NOT statements matches a line inside
        the given `scope` of `c1Pass` lines.

    Raises MatchFailedException if a statement matches a line in the scope.
    """
    for statement in self.not_queue:
      assert statement.variant == TestStatement.Variant.NOT
      for i in range(scope.start, scope.end):
        if match_lines(statement, self.c1_pass.body[i], self.variables) is not None:
          raise MatchFailedException(statement, i, self.variables)
    self.not_queue = []

  def handle_eof(self):
    """ EOF marker always moves the cursor to the end of the file."""
    match = MatchInfo(MatchScope(self.c1_length, self.c1_length), None)
    self.move_cursor(match)

  def handle_in_order(self, statement):
    """ Single in-order statement. Find the first line that matches and move
        the cursor to the subsequent line.

    Raises MatchFailedException if no such line can be found.
    """
    scope = MatchScope(self.cursor, self.c1_length)
    match = find_matching_line(statement, self.c1_pass, scope, self.variables)
    self.move_cursor(match)

  def handle_next_line(self, statement):
    """ Single next-line statement. Test if the current line matches and move
        the cursor to the next line if it does.

    Raises MatchFailedException if the current line does not match.
    """
    if self.last_variant not in [TestStatement.Variant.IN_ORDER, TestStatement.Variant.NEXT_LINE]:
      raise BadStructureException("A next-line statement can only be placed "
                                  "after an in-order statement or another next-line statement.",
                                  statement.line_no)

    scope = MatchScope(self.cursor, self.cursor + 1)
    match = find_matching_line(statement, self.c1_pass, scope, self.variables)
    self.move_cursor(match)

  def handle_eval(self, statement):
    """ Evaluates the statement in the current context.

    Raises MatchFailedException if the expression evaluates to False.
    """
    if not evaluate_line(statement, self.variables):
      raise MatchFailedException(statement, self.cursor, self.variables)

  def handle(self, statement):
    variant = None if statement is None else statement.variant

    if variant in [TestStatement.Variant.IF,
                   TestStatement.Variant.ELIF,
                   TestStatement.Variant.ELSE,
                   TestStatement.Variant.FI]:
      self.if_stack.handle(statement, self.variables)
      return

    if variant is None:
      self.if_stack.eof()

    if not self.if_stack.can_execute():
      return

    # First non-DAG statement always triggers execution of any preceding
    # DAG statements.
    if variant is not TestStatement.Variant.DAG:
      self.handle_dag_queue(MatchScope(self.cursor, self.c1_length))

    if variant is None:
      self.handle_eof()
    elif variant is TestStatement.Variant.IN_ORDER:
      self.handle_in_order(statement)
    elif variant is TestStatement.Variant.NEXT_LINE:
      self.handle_next_line(statement)
    elif variant is TestStatement.Variant.DAG:
      self.dag_queue.append(statement)
    elif variant is TestStatement.Variant.NOT:
      self.not_queue.append(statement)
    else:
      assert variant is TestStatement.Variant.EVAL
      self.handle_eval(statement)

    self.last_variant = variant


def match_test_case(test_case, c1_pass, instruction_set_features):
  """ Runs a test case against a C1visualizer graph dump.

  Raises MatchFailedException when a statement cannot be satisfied.
  """
  assert test_case.name == c1_pass.name

  initial_variables = {"ISA_FEATURES": instruction_set_features}
  state = ExecutionState(c1_pass, initial_variables)
  test_statements = test_case.statements + [None]
  for statement in test_statements:
    state.handle(statement)


def match_files(checker_file, c1_file, target_arch, debuggable_mode, print_cfg):
  for test_case in checker_file.test_cases:
    if test_case.test_arch not in [None, target_arch]:
      continue
    if test_case.for_debuggable != debuggable_mode:
      continue

    # TODO: Currently does not handle multiple occurrences of the same group
    # name, e.g. when a pass is run multiple times. It will always try to
    # match a check group against the first output group of the same name.
    c1_pass = c1_file.find_pass(test_case.name)
    if c1_pass is None:
      with open(c1_file.full_file_name) as cfg_file:
        Logger.log("".join(cfg_file), Logger.Level.ERROR)
      Logger.fail("Test case not found in the CFG file",
                  test_case.full_file_name, test_case.start_line_no, test_case.name)

    Logger.start_test(test_case.name)
    try:
      match_test_case(test_case, c1_pass, c1_file.instruction_set_features)
      Logger.test_passed()
    except MatchFailedException as e:
      line_no = c1_pass.start_line_no + e.line_no
      if e.statement.variant == TestStatement.Variant.NOT:
        msg = "NOT statement matched line {}"
      else:
        msg = "Statement could not be matched starting from line {}"
      msg = msg.format(line_no)
      if print_cfg:
        with open(c1_file.full_file_name) as cfg_file:
          Logger.log("".join(cfg_file), Logger.Level.ERROR)
      Logger.test_failed(msg, e.statement, e.variables)
