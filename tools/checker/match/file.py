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

from collections                      import namedtuple
from common.immutables                import ImmutableDict
from common.logger                    import Logger
from file_format.c1visualizer.struct  import C1visualizerFile, C1visualizerPass
from file_format.checker.struct       import CheckerFile, TestCase, TestStatement
from match.line                       import MatchLines, EvaluateLine

MatchScope = namedtuple("MatchScope", ["start", "end"])
MatchInfo = namedtuple("MatchInfo", ["scope", "variables"])

class MatchFailedException(Exception):
  def __init__(self, statement, lineNo, variables):
    self.statement = statement
    self.lineNo = lineNo
    self.variables = variables

class BadStructureException(Exception):
  def __init__(self, msg, lineNo):
    self.msg = msg
    self.lineNo = lineNo

class IfStack:
  """
  The purpose of this class is to keep track of which branch the cursor is in.
  This will let us know if the line read by the cursor should be processed or not.
  Furthermore, this class contains the methods to handle the CHECK-[IF, ELIF, ELSE, FI]
  statements, and consequently update the stack with new information.

  The following elements can appear on the stack:
  - BranchTaken: a branch is taken if its condition evaluates to true and
    its parent branch was also previously taken.
  - BranchNotTakenYet: the branch's parent was taken, but this branch wasn't as its
    condition did not evaluate to true.
  - BranchNotTaken: a branch is not taken when its parent was either NotTaken or NotTakenYet.
    It doesn't matter if the condition would evaluate to true, that's not even checked.

  CHECK-IF is the only instruction that pushes a new element on the stack. CHECK-ELIF
  and CHECK-ELSE will update the top of the stack to keep track of what's been seen.
  That means that we can check if the line currently pointed to by the cursor should be
  processed just by looking at the top of the stack.
  CHECK-FI will pop the last element.

  `BranchTaken`, `BranchNotTaken`, `BranchNotTakenYet` are implemented as positive integers.
  Negated values of `BranchTaken` and `BranchNotTaken` may be appear; `-BranchTaken` and
  `-BranchNotTaken` have the same meaning as `BranchTaken` and `BranchNotTaken`
  (respectively), but they indicate that we went past the ELSE branch. Knowing that, we can
  output a precise error message if the user creates a malformed branching structure.
  """

  BranchTaken, BranchNotTaken, BranchNotTakenYet = range(1, 4)

  def __init__(self):
    self.stack = []

  def CanExecute(self):
    """
    Returns true if we're not in any branch, or the branch we're
    currently in was taken.
    """
    if self.__isEmpty():
      return True
    return abs(self.__peek()) == IfStack.BranchTaken

  def Handle(self, statement, variables):
    """
    This function is invoked if the cursor is pointing to a
    CHECK-[IF, ELIF, ELSE, FI] line.
    """
    variant = statement.variant
    if variant is TestStatement.Variant.If:
      self.__if(statement, variables)
    elif variant is TestStatement.Variant.Elif:
      self.__elif(statement, variables)
    elif variant is TestStatement.Variant.Else:
      self.__else(statement)
    else:
      assert variant is TestStatement.Variant.Fi
      self.__fi(statement)

  def Eof(self):
    """
    The last line the cursor points to is always EOF.
    """
    if not self.__isEmpty():
      raise BadStructureException("Missing CHECK-FI", -1)

  def __isEmpty(self):
    return len(self.stack) == 0

  def __if(self, statement, variables):
    if not self.__isEmpty() and abs(self.__peek()) in [ IfStack.BranchNotTaken,
                                                        IfStack.BranchNotTakenYet ]:
      self.__push(IfStack.BranchNotTaken)
    elif EvaluateLine(statement, variables):
      self.__push(IfStack.BranchTaken)
    else:
      self.__push(IfStack.BranchNotTakenYet)

  def __elif(self, statement, variables):
    if self.__isEmpty():
      raise BadStructureException("CHECK-ELIF must be after CHECK-IF or CHECK-ELIF",
                                  statement.lineNo)
    if self.__peek() < 0:
      raise BadStructureException("CHECK-ELIF cannot be after CHECK-ELSE", statement.lineNo)
    if self.__peek() == IfStack.BranchTaken:
      self.__setLast(IfStack.BranchNotTaken)
    elif self.__peek() == IfStack.BranchNotTakenYet:
      if EvaluateLine(statement, variables):
        self.__setLast(IfStack.BranchTaken)
      # else, the CHECK-ELIF condition is False, so do nothing: the last element on the stack is
      # already set to BranchNotTakenYet.
    else:
      assert self.__peek() == IfStack.BranchNotTaken

  def __else(self, statement):
    if self.__isEmpty():
      raise BadStructureException("CHECK-ELSE must be after CHECK-IF or CHECK-ELIF",
                                  statement.lineNo)
    if self.__peek() < 0:
      raise BadStructureException("Consecutive CHECK-ELSE statements", statement.lineNo)
    if self.__peek() in [ IfStack.BranchTaken, IfStack.BranchNotTaken ]:
      # Notice that we're setting -BranchNotTaken rather that BranchNotTaken as we went past the
      # ELSE branch.
      self.__setLast(-IfStack.BranchNotTaken)
    else:
      assert self.__peek() == IfStack.BranchNotTakenYet
      # Setting -BranchTaken rather BranchTaken for the same reason.
      self.__setLast(-IfStack.BranchTaken)

  def __fi(self, statement):
    if self.__isEmpty():
      raise BadStructureException("CHECK-FI does not have a matching CHECK-IF", statement.lineNo)
    self.stack.pop()

  def __peek(self):
    assert not self.__isEmpty()
    return self.stack[-1]

  def __push(self, element):
    self.stack.append(element)

  def __setLast(self, element):
    self.stack[-1] = element

def findMatchingLine(statement, c1Pass, scope, variables, excludeLines=[]):
  """ Finds the first line in `c1Pass` which matches `statement`.

  Scan only lines numbered between `scope.start` and `scope.end` and not on the
  `excludeLines` list.

  Returns the index of the `c1Pass` line matching the statement and variables
  values after the match.

  Raises MatchFailedException if no such `c1Pass` line can be found.
  """
  for i in range(scope.start, scope.end):
    if i in excludeLines: continue
    newVariables = MatchLines(statement, c1Pass.body[i], variables)
    if newVariables is not None:
      return MatchInfo(MatchScope(i, i), newVariables)
  raise MatchFailedException(statement, scope.start, variables)

class ExecutionState(object):
  def __init__(self, c1Pass, variables={}):
    self.cursor = 0
    self.c1Pass = c1Pass
    self.c1Length = len(c1Pass.body)
    self.variables = ImmutableDict(variables)
    self.dagQueue = []
    self.notQueue = []
    self.ifStack = IfStack()
    self.lastVariant = None

  def moveCursor(self, match):
    assert self.cursor <= match.scope.end

    # Handle any pending NOT statements before moving the cursor
    self.handleNotQueue(MatchScope(self.cursor, match.scope.start))

    self.cursor = match.scope.end + 1
    self.variables = match.variables

  def handleDagQueue(self, scope):
    """ Attempts to find matching `c1Pass` lines for a group of DAG statements.

    Statements are matched in the list order and variable values propagated. Only
    lines in `scope` are scanned and each line can only match one statement.

    Returns the range of `c1Pass` lines covered by this group (min/max of matching
    line numbers) and the variable values after the match of the last statement.

    Raises MatchFailedException when a statement cannot be satisfied.
    """
    if not self.dagQueue:
      return

    matchedLines = []
    variables = self.variables

    for statement in self.dagQueue:
      assert statement.variant == TestStatement.Variant.DAG
      match = findMatchingLine(statement, self.c1Pass, scope, variables, matchedLines)
      variables = match.variables
      assert match.scope.start == match.scope.end
      assert match.scope.start not in matchedLines
      matchedLines.append(match.scope.start)

    match = MatchInfo(MatchScope(min(matchedLines), max(matchedLines)), variables)
    self.dagQueue = []
    self.moveCursor(match)

  def handleNotQueue(self, scope):
    """ Verifies that none of the given NOT statements matches a line inside
        the given `scope` of `c1Pass` lines.

    Raises MatchFailedException if a statement matches a line in the scope.
    """
    for statement in self.notQueue:
      assert statement.variant == TestStatement.Variant.Not
      for i in range(scope.start, scope.end):
        if MatchLines(statement, self.c1Pass.body[i], self.variables) is not None:
          raise MatchFailedException(statement, i, self.variables)
    self.notQueue = []

  def handleEOF(self):
    """ EOF marker always moves the cursor to the end of the file."""
    match = MatchInfo(MatchScope(self.c1Length, self.c1Length), None)
    self.moveCursor(match)

  def handleInOrder(self, statement):
    """ Single in-order statement. Find the first line that matches and move
        the cursor to the subsequent line.

    Raises MatchFailedException if no such line can be found.
    """
    scope = MatchScope(self.cursor, self.c1Length)
    match = findMatchingLine(statement, self.c1Pass, scope, self.variables)
    self.moveCursor(match)

  def handleNextLine(self, statement):
    """ Single next-line statement. Test if the current line matches and move
        the cursor to the next line if it does.

    Raises MatchFailedException if the current line does not match.
    """
    if self.lastVariant not in [ TestStatement.Variant.InOrder, TestStatement.Variant.NextLine ]:
      raise BadStructureException("A next-line statement can only be placed "
                  "after an in-order statement or another next-line statement.",
                  statement.lineNo)

    scope = MatchScope(self.cursor, self.cursor + 1)
    match = findMatchingLine(statement, self.c1Pass, scope, self.variables)
    self.moveCursor(match)

  def handleEval(self, statement):
    """ Evaluates the statement in the current context.

    Raises MatchFailedException if the expression evaluates to False.
    """
    if not EvaluateLine(statement, self.variables):
      raise MatchFailedException(statement, self.cursor, self.variables)

  def handle(self, statement):
    variant = None if statement is None else statement.variant

    if variant in [ TestStatement.Variant.If,
                    TestStatement.Variant.Elif,
                    TestStatement.Variant.Else,
                    TestStatement.Variant.Fi ]:
      self.ifStack.Handle(statement, self.variables)
      return

    if variant is None:
      self.ifStack.Eof()

    if not self.ifStack.CanExecute():
      return

    # First non-DAG statement always triggers execution of any preceding
    # DAG statements.
    if variant is not TestStatement.Variant.DAG:
      self.handleDagQueue(MatchScope(self.cursor, self.c1Length))

    if variant is None:
      self.handleEOF()
    elif variant is TestStatement.Variant.InOrder:
      self.handleInOrder(statement)
    elif variant is TestStatement.Variant.NextLine:
      self.handleNextLine(statement)
    elif variant is TestStatement.Variant.DAG:
      self.dagQueue.append(statement)
    elif variant is TestStatement.Variant.Not:
      self.notQueue.append(statement)
    else:
      assert variant is TestStatement.Variant.Eval
      self.handleEval(statement)

    self.lastVariant = variant

def MatchTestCase(testCase, c1Pass):
  """ Runs a test case against a C1visualizer graph dump.

  Raises MatchFailedException when a statement cannot be satisfied.
  """
  assert testCase.name == c1Pass.name

  state = ExecutionState(c1Pass)
  testStatements = testCase.statements + [ None ]
  for statement in testStatements:
    state.handle(statement)

def MatchFiles(checkerFile, c1File, targetArch, debuggableMode):
  for testCase in checkerFile.testCases:
    if testCase.testArch not in [None, targetArch]:
      continue
    if testCase.forDebuggable != debuggableMode:
      continue

    # TODO: Currently does not handle multiple occurrences of the same group
    # name, e.g. when a pass is run multiple times. It will always try to
    # match a check group against the first output group of the same name.
    c1Pass = c1File.findPass(testCase.name)
    if c1Pass is None:
      with file(c1File.fileName) as cfgFile:
        Logger.log(''.join(cfgFile), Logger.Level.Error)
      Logger.fail("Test case not found in the CFG file",
                  testCase.fileName, testCase.startLineNo, testCase.name)

    Logger.startTest(testCase.name)
    try:
      MatchTestCase(testCase, c1Pass)
      Logger.testPassed()
    except MatchFailedException as e:
      lineNo = c1Pass.startLineNo + e.lineNo
      if e.statement.variant == TestStatement.Variant.Not:
        msg = "NOT statement matched line {}"
      else:
        msg = "Statement could not be matched starting from line {}"
      msg = msg.format(lineNo)
      with file(c1File.fileName) as cfgFile:
        Logger.log(''.join(cfgFile), Logger.Level.Error)
      Logger.testFailed(msg, e.statement, e.variables)
