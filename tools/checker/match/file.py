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

def splitIntoGroups(statements):
  """ Breaks up a list of statements, grouping instructions which should be
      tested in the same scope (consecutive DAG and NOT instructions).
   """
  splitStatements = []
  lastVariant = None
  for statement in statements:
    if (statement.variant == lastVariant and
        statement.variant in [TestStatement.Variant.DAG, TestStatement.Variant.Not]):
      splitStatements[-1].append(statement)
    else:
      splitStatements.append([statement])
      lastVariant = statement.variant
  return splitStatements

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

def matchDagGroup(statements, c1Pass, scope, variables):
  """ Attempts to find matching `c1Pass` lines for a group of DAG statements.

  Statements are matched in the list order and variable values propagated. Only
  lines in `scope` are scanned and each line can only match one statement.

  Returns the range of `c1Pass` lines covered by this group (min/max of matching
  line numbers) and the variable values after the match of the last statement.

  Raises MatchFailedException when an statement cannot be satisfied.
  """
  matchedLines = []
  for statement in statements:
    assert statement.variant == TestStatement.Variant.DAG
    match = findMatchingLine(statement, c1Pass, scope, variables, matchedLines)
    variables = match.variables
    assert match.scope.start == match.scope.end
    assert match.scope.start not in matchedLines
    matchedLines.append(match.scope.start)
  return MatchInfo(MatchScope(min(matchedLines), max(matchedLines)), variables)

def testNotGroup(statements, c1Pass, scope, variables):
  """ Verifies that none of the given NOT statements matches a line inside
      the given `scope` of `c1Pass` lines.

  Raises MatchFailedException if an statement matches a line in the scope.
  """
  for i in range(scope.start, scope.end):
    line = c1Pass.body[i]
    for statement in statements:
      assert statement.variant == TestStatement.Variant.Not
      if MatchLines(statement, line, variables) is not None:
        raise MatchFailedException(statement, i, variables)

def testEvalGroup(statements, scope, variables):
  for statement in statements:
    if not EvaluateLine(statement, variables):
      raise MatchFailedException(statement, scope.start, variables)

def MatchTestCase(testCase, c1Pass):
  """ Runs a test case against a C1visualizer graph dump.

  Raises MatchFailedException when an statement cannot be satisfied.
  """
  assert testCase.name == c1Pass.name

  matchFrom = 0
  variables = ImmutableDict()
  c1Length = len(c1Pass.body)

  # NOT statements are verified retrospectively, once the scope is known.
  pendingNotStatements = None

  # Prepare statements by grouping those that are verified in the same scope.
  # We also add None as an EOF statement that will set scope for NOTs.
  statementGroups = splitIntoGroups(testCase.statements)
  statementGroups.append(None)

  for statementGroup in statementGroups:
    if statementGroup is None:
      # EOF marker always matches the last+1 line of c1Pass.
      match = MatchInfo(MatchScope(c1Length, c1Length), None)
    elif statementGroup[0].variant == TestStatement.Variant.Not:
      # NOT statements will be tested together with the next group.
      assert not pendingNotStatements
      pendingNotStatements = statementGroup
      continue
    elif statementGroup[0].variant == TestStatement.Variant.InOrder:
      # Single in-order statement. Find the first line that matches.
      assert len(statementGroup) == 1
      scope = MatchScope(matchFrom, c1Length)
      match = findMatchingLine(statementGroup[0], c1Pass, scope, variables)
    elif statementGroup[0].variant == TestStatement.Variant.NextLine:
      # Single next-line statement. Test if the current line matches.
      assert len(statementGroup) == 1
      scope = MatchScope(matchFrom, matchFrom + 1)
      match = findMatchingLine(statementGroup[0], c1Pass, scope, variables)
    elif statementGroup[0].variant == TestStatement.Variant.DAG:
      # A group of DAG statements. Match them all starting from the same point.
      scope = MatchScope(matchFrom, c1Length)
      match = matchDagGroup(statementGroup, c1Pass, scope, variables)
    else:
      assert statementGroup[0].variant == TestStatement.Variant.Eval
      scope = MatchScope(matchFrom, c1Length)
      testEvalGroup(statementGroup, scope, variables)
      continue

    if pendingNotStatements:
      # Previous group were NOT statements. Make sure they don't match any lines
      # in the [matchFrom, match.start) scope.
      scope = MatchScope(matchFrom, match.scope.start)
      testNotGroup(pendingNotStatements, c1Pass, scope, variables)
      pendingNotStatements = None

    # Update state.
    assert matchFrom <= match.scope.end
    matchFrom = match.scope.end + 1
    variables = match.variables

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
