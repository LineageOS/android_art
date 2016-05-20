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

from common.immutables               import ImmutableDict
from common.testing                  import ToUnicode
from file_format.c1visualizer.parser import ParseC1visualizerStream
from file_format.c1visualizer.struct import C1visualizerFile, C1visualizerPass
from file_format.checker.parser      import ParseCheckerStream, ParseCheckerStatement
from file_format.checker.struct      import CheckerFile, TestCase, TestStatement
from match.file                      import MatchTestCase, MatchFailedException, \
                                            BadStructureException
from match.line                      import MatchLines

import io
import unittest

CheckerException = SystemExit

class MatchLines_Test(unittest.TestCase):

  def createTestStatement(self, checkerString):
    checkerFile = CheckerFile("<checker-file>")
    testCase = TestCase(checkerFile, "TestMethod TestPass", 0)
    return ParseCheckerStatement(testCase, checkerString, TestStatement.Variant.InOrder, 0)

  def tryMatch(self, checkerString, c1String, varState={}):
    return MatchLines(self.createTestStatement(checkerString),
                      ToUnicode(c1String),
                      ImmutableDict(varState))

  def assertMatches(self, checkerString, c1String, varState={}):
    self.assertIsNotNone(self.tryMatch(checkerString, c1String, varState))

  def assertDoesNotMatch(self, checkerString, c1String, varState={}):
    self.assertIsNone(self.tryMatch(checkerString, c1String, varState))

  def test_TextAndWhitespace(self):
    self.assertMatches("foo", "foo")
    self.assertMatches("foo", "  foo  ")
    self.assertMatches("foo", "foo bar")
    self.assertDoesNotMatch("foo", "XfooX")
    self.assertDoesNotMatch("foo", "zoo")

    self.assertMatches("foo bar", "foo   bar")
    self.assertMatches("foo bar", "abc foo bar def")
    self.assertMatches("foo bar", "foo foo bar bar")

    self.assertMatches("foo bar", "foo X bar")
    self.assertDoesNotMatch("foo bar", "foo Xbar")

  def test_Pattern(self):
    self.assertMatches("foo{{A|B}}bar", "fooAbar")
    self.assertMatches("foo{{A|B}}bar", "fooBbar")
    self.assertDoesNotMatch("foo{{A|B}}bar", "fooCbar")

  def test_VariableReference(self):
    self.assertMatches("foo<<X>>bar", "foobar", {"X": ""})
    self.assertMatches("foo<<X>>bar", "fooAbar", {"X": "A"})
    self.assertMatches("foo<<X>>bar", "fooBbar", {"X": "B"})
    self.assertDoesNotMatch("foo<<X>>bar", "foobar", {"X": "A"})
    self.assertDoesNotMatch("foo<<X>>bar", "foo bar", {"X": "A"})
    with self.assertRaises(CheckerException):
      self.tryMatch("foo<<X>>bar", "foobar", {})

  def test_VariableDefinition(self):
    self.assertMatches("foo<<X:A|B>>bar", "fooAbar")
    self.assertMatches("foo<<X:A|B>>bar", "fooBbar")
    self.assertDoesNotMatch("foo<<X:A|B>>bar", "fooCbar")

    env = self.tryMatch("foo<<X:A.*B>>bar", "fooABbar", {})
    self.assertEqual(env, {"X": "AB"})
    env = self.tryMatch("foo<<X:A.*B>>bar", "fooAxxBbar", {})
    self.assertEqual(env, {"X": "AxxB"})

    self.assertMatches("foo<<X:A|B>>bar<<X>>baz", "fooAbarAbaz")
    self.assertMatches("foo<<X:A|B>>bar<<X>>baz", "fooBbarBbaz")
    self.assertDoesNotMatch("foo<<X:A|B>>bar<<X>>baz", "fooAbarBbaz")

  def test_NoVariableRedefinition(self):
    with self.assertRaises(CheckerException):
      self.tryMatch("<<X:...>><<X>><<X:...>><<X>>", "foofoobarbar")

  def test_EnvNotChangedOnPartialMatch(self):
    env = {"Y": "foo"}
    self.assertDoesNotMatch("<<X:A>>bar", "Abaz", env)
    self.assertFalse("X" in env.keys())

  def test_VariableContentEscaped(self):
    self.assertMatches("<<X:..>>foo<<X>>", ".*foo.*")
    self.assertDoesNotMatch("<<X:..>>foo<<X>>", ".*fooAAAA")


class MatchFiles_Test(unittest.TestCase):

  def assertMatches(self, checkerString, c1String):
    checkerString = \
      """
        /// CHECK-START: MyMethod MyPass
      """ + checkerString
    c1String = \
      """
        begin_compilation
          name "MyMethod"
          method "MyMethod"
          date 1234
        end_compilation
        begin_cfg
          name "MyPass"
      """ + c1String + \
      """
        end_cfg
      """
    checkerFile = ParseCheckerStream("<test-file>", "CHECK", io.StringIO(ToUnicode(checkerString)))
    c1File = ParseC1visualizerStream("<c1-file>", io.StringIO(ToUnicode(c1String)))
    assert len(checkerFile.testCases) == 1
    assert len(c1File.passes) == 1
    MatchTestCase(checkerFile.testCases[0], c1File.passes[0])

  def assertDoesNotMatch(self, checkerString, c1String):
    with self.assertRaises(MatchFailedException):
      self.assertMatches(checkerString, c1String)

  def assertBadStructure(self, checkerString, c1String):
    with self.assertRaises(BadStructureException):
      self.assertMatches(checkerString, c1String)

  def test_Text(self):
    self.assertMatches("/// CHECK: foo bar", "foo bar")
    self.assertDoesNotMatch("/// CHECK: foo bar", "abc def")

  def test_Pattern(self):
    self.assertMatches("/// CHECK: abc {{de.}}", "abc de#")
    self.assertDoesNotMatch("/// CHECK: abc {{de.}}", "abc d#f")

  def test_Variables(self):
    self.assertMatches(
    """
      /// CHECK: foo<<X:.>>bar
      /// CHECK: abc<<X>>def
    """,
    """
      foo0bar
      abc0def
    """)
    self.assertMatches(
    """
      /// CHECK: foo<<X:([0-9]+)>>bar
      /// CHECK: abc<<X>>def
      /// CHECK: ### <<X>> ###
    """,
    """
      foo1234bar
      abc1234def
      ### 1234 ###
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK: foo<<X:([0-9]+)>>bar
      /// CHECK: abc<<X>>def
    """,
    """
      foo1234bar
      abc1235def
    """)

  def test_WholeWordMustMatch(self):
    self.assertMatches("/// CHECK: b{{.}}r", "abc bar def")
    self.assertDoesNotMatch("/// CHECK: b{{.}}r", "abc Xbar def")
    self.assertDoesNotMatch("/// CHECK: b{{.}}r", "abc barX def")
    self.assertDoesNotMatch("/// CHECK: b{{.}}r", "abc b r def")

  def test_InOrderStatements(self):
    self.assertMatches(
    """
      /// CHECK: foo
      /// CHECK: bar
    """,
    """
      foo
      bar
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK: foo
      /// CHECK: bar
    """,
    """
      bar
      foo
    """)

  def test_NextLineStatements(self):
    self.assertMatches(
    """
      /// CHECK:      foo
      /// CHECK-NEXT: bar
      /// CHECK-NEXT: abc
      /// CHECK:      def
    """,
    """
      foo
      bar
      abc
      def
    """)
    self.assertMatches(
    """
      /// CHECK:      foo
      /// CHECK-NEXT: bar
      /// CHECK:      def
    """,
    """
      foo
      bar
      abc
      def
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK:      foo
      /// CHECK-NEXT: bar
    """,
    """
      foo
      abc
      bar
    """)

    self.assertDoesNotMatch(
    """
      /// CHECK:      foo
      /// CHECK-NEXT: bar
    """,
    """
      bar
      foo
      abc
    """)

  def test_DagStatements(self):
    self.assertMatches(
    """
      /// CHECK-DAG: foo
      /// CHECK-DAG: bar
    """,
    """
      foo
      bar
    """)
    self.assertMatches(
    """
      /// CHECK-DAG: foo
      /// CHECK-DAG: bar
    """,
    """
      bar
      foo
    """)

  def test_DagStatementsScope(self):
    self.assertMatches(
    """
      /// CHECK:     foo
      /// CHECK-DAG: abc
      /// CHECK-DAG: def
      /// CHECK:     bar
    """,
    """
      foo
      def
      abc
      bar
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK:     foo
      /// CHECK-DAG: abc
      /// CHECK-DAG: def
      /// CHECK:     bar
    """,
    """
      foo
      abc
      bar
      def
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK:     foo
      /// CHECK-DAG: abc
      /// CHECK-DAG: def
      /// CHECK:     bar
    """,
    """
      foo
      def
      bar
      abc
    """)

  def test_NotStatements(self):
    self.assertMatches(
    """
      /// CHECK-NOT: foo
    """,
    """
      abc
      def
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK-NOT: foo
    """,
    """
      abc foo
      def
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK-NOT: foo
      /// CHECK-NOT: bar
    """,
    """
      abc
      def bar
    """)

  def test_NotStatementsScope(self):
    self.assertMatches(
    """
      /// CHECK:     abc
      /// CHECK-NOT: foo
      /// CHECK:     def
    """,
    """
      abc
      def
    """)
    self.assertMatches(
    """
      /// CHECK:     abc
      /// CHECK-NOT: foo
      /// CHECK:     def
    """,
    """
      abc
      def
      foo
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK:     abc
      /// CHECK-NOT: foo
      /// CHECK:     def
    """,
    """
      abc
      foo
      def
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK-NOT:  foo
      /// CHECK-EVAL: 1 + 1 == 2
      /// CHECK:      bar
    """,
    """
      foo
      abc
      bar
    """);
    self.assertMatches(
    """
      /// CHECK-DAG:  bar
      /// CHECK-DAG:  abc
      /// CHECK-NOT:  foo
    """,
    """
      foo
      abc
      bar
    """);
    self.assertDoesNotMatch(
    """
      /// CHECK-DAG:  abc
      /// CHECK-DAG:  foo
      /// CHECK-NOT:  bar
    """,
    """
      foo
      abc
      bar
    """);

  def test_LineOnlyMatchesOnce(self):
    self.assertMatches(
    """
      /// CHECK-DAG: foo
      /// CHECK-DAG: foo
    """,
    """
      foo
      abc
      foo
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK-DAG: foo
      /// CHECK-DAG: foo
    """,
    """
      foo
      abc
      bar
    """)

  def test_EvalStatements(self):
    self.assertMatches("/// CHECK-EVAL: True", "foo")
    self.assertDoesNotMatch("/// CHECK-EVAL: False", "foo")

    self.assertMatches("/// CHECK-EVAL: 1 + 2 == 3", "foo")
    self.assertDoesNotMatch("/// CHECK-EVAL: 1 + 2 == 4", "foo")

    twoVarTestCase = """
                       /// CHECK-DAG: <<X:\d+>> <<Y:\d+>>
                       /// CHECK-EVAL: <<X>> > <<Y>>
                     """
    self.assertMatches(twoVarTestCase, "42 41");
    self.assertDoesNotMatch(twoVarTestCase, "42 43")

  def test_MisplacedNext(self):
    self.assertBadStructure(
      """
        /// CHECK-DAG:  foo
        /// CHECK-NEXT: bar
      """,
      """
      foo
      bar
      """)
    self.assertBadStructure(
      """
        /// CHECK-NOT:  foo
        /// CHECK-NEXT: bar
      """,
      """
      foo
      bar
      """)
    self.assertBadStructure(
      """
        /// CHECK-EVAL: True
        /// CHECK-NEXT: bar
      """,
      """
      foo
      bar
      """)
    self.assertBadStructure(
      """
        /// CHECK-NEXT: bar
      """,
      """
      foo
      bar
      """)

  def test_EnvVariableEval(self):
    self.assertMatches(
    """
      /// CHECK-IF: os.environ.get('MARTY_MCFLY') != '89mph!'
      /// CHECK-FI:
    """,
    """
    foo
    """
    )
    self.assertMatches(
    """
      /// CHECK-EVAL: os.environ.get('MARTY_MCFLY') != '89mph!'
    """,
    """
    foo
    """
    )

  def test_IfStatements(self):
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: True
      ///   CHECK-NEXT: foo2
      /// CHECK-FI:
      /// CHECK-NEXT: foo3
      /// CHECK-NEXT: bar
    """,
    """
    foo1
    foo2
    foo3
    bar
    """)
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: False
      ///   CHECK-NEXT:    foo2
      /// CHECK-FI:
      /// CHECK-NEXT:    bar
    """,
    """
    foo1
    bar
    """)
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: True
      ///   CHECK-DAG:    foo2
      /// CHECK-FI:
      /// CHECK-DAG:    bar
      /// CHECK: foo3
    """,
    """
    foo1
    bar
    foo2
    foo3
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK: foo1
      /// CHECK-IF: False
      ///   CHECK-NEXT: foo2
      /// CHECK-FI:
      /// CHECK-NEXT: foo3
    """,
    """
    foo1
    foo2
    foo3
    """)

  def test_IfElseStatements(self):
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: True
      ///   CHECK-NEXT:    foo2
      /// CHECK-ELSE:
      ///   CHECK-NEXT:    foo3
      /// CHECK-FI:
      /// CHECK-NEXT:    bar
    """,
    """
    foo1
    foo2
    bar
    """)
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: False
      ///   CHECK-NEXT:    foo2
      /// CHECK-ELSE:
      ///   CHECK-NEXT:    foo3
      /// CHECK-FI:
      /// CHECK-NEXT:    bar
    """,
    """
    foo1
    foo3
    bar
    """)
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: False
      ///   CHECK-NEXT:    foo2
      /// CHECK-ELSE:
      ///   CHECK-DAG:    bar
      /// CHECK-FI:
      /// CHECK-DAG:    foo3
      /// CHECK: foo4
    """,
    """
    foo1
    foo3
    bar
    foo4
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK: foo1
      /// CHECK-IF: False
      ///   CHECK-NEXT:    foo2
      /// CHECK-ELSE:
      ///   CHECK-NEXT:    foo3
      /// CHECK-FI:
      /// CHECK-NEXT:    bar
    """,
    """
    foo1
    foo2
    bar
    """)

  def test_IfElifElseStatements(self):
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: True
      ///   CHECK-NEXT:    foo2
      /// CHECK-ELIF: True
      ///   CHECK-NEXT:    foo3
      /// CHECK-ELIF: True
      ///   CHECK-NEXT:    foo4
      /// CHECK-FI:
      /// CHECK-NEXT:    bar
    """,
    """
    foo1
    foo2
    bar
    """)
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: False
      ///   CHECK-NEXT:    foo2
      /// CHECK-ELIF: False
      ///   CHECK-NEXT:    foo3
      /// CHECK-ELIF: True
      ///   CHECK-NEXT:    foo4
      /// CHECK-FI:
      /// CHECK-NEXT:    bar
    """,
    """
    foo1
    foo4
    bar
    """)
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: False
      ///   CHECK-NEXT:    foo2
      /// CHECK-ELIF: True
      ///   CHECK-NEXT:    foo3
      /// CHECK-ELIF: True
      ///   CHECK-NEXT:    foo4
      /// CHECK-FI:
      /// CHECK-NEXT:    bar
    """,
    """
    foo1
    foo3
    bar
    """)
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: False
      ///   CHECK-NEXT:    foo2
      /// CHECK-ELIF: False
      ///   CHECK-NEXT:    foo3
      /// CHECK-ELIF: False
      ///   CHECK-NEXT:    foo4
      /// CHECK-FI:
      /// CHECK-NEXT:    bar
    """,
    """
    foo1
    bar
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK: foo1
      /// CHECK-IF: False
      ///   CHECK-NEXT:    foo2
      /// CHECK-ELIF: True
      ///   CHECK-NEXT:    foo3
      /// CHECK-ELSE:
      ///   CHECK-NEXT:    foo4
      /// CHECK-FI:
      /// CHECK-NEXT:    bar
    """,
    """
    foo1
    foo2
    bar
    """)

  def test_NestedBranching(self):
    self.assertMatches(
    """
      /// CHECK: foo1
      /// CHECK-IF: True
      ///   CHECK-IF: True
      ///     CHECK-NEXT:    foo2
      ///   CHECK-ELSE:
      ///     CHECK-NEXT:    foo3
      ///   CHECK-FI:
      /// CHECK-ELSE:
      ///   CHECK-IF: True
      ///     CHECK-NEXT:    foo4
      ///   CHECK-ELSE:
      ///     CHECK-NEXT:    foo5
      ///   CHECK-FI:
      /// CHECK-FI:
      /// CHECK-NEXT: foo6
    """,
    """
    foo1
    foo2
    foo6
    """)
    self.assertMatches(
    """
      /// CHECK-IF: True
      ///   CHECK-IF: False
      ///     CHECK:    foo1
      ///   CHECK-ELSE:
      ///     CHECK:    foo2
      ///   CHECK-FI:
      /// CHECK-ELSE:
      ///   CHECK-IF: True
      ///     CHECK:    foo3
      ///   CHECK-ELSE:
      ///     CHECK:    foo4
      ///   CHECK-FI:
      /// CHECK-FI:
    """,
    """
    foo2
    """)
    self.assertMatches(
    """
      /// CHECK-IF: False
      ///   CHECK-IF: True
      ///     CHECK:    foo1
      ///   CHECK-ELSE:
      ///     CHECK:    foo2
      ///   CHECK-FI:
      /// CHECK-ELSE:
      ///   CHECK-IF: False
      ///     CHECK:    foo3
      ///   CHECK-ELSE:
      ///     CHECK-IF: False
      ///       CHECK:    foo4
      ///     CHECK-ELSE:
      ///       CHECK: foo5
      ///     CHECK-FI:
      ///   CHECK-FI:
      /// CHECK-FI:
    """,
    """
    foo5
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK: foo1
      /// CHECK-IF: True
      ///   CHECK-IF: False
      ///     CHECK-NEXT:    foo2
      ///   CHECK-ELSE:
      ///     CHECK-NEXT:    foo3
      ///   CHECK-FI:
      /// CHECK-NEXT: foo6
    """,
    """
    foo1
    foo2
    foo6
    """)

  def test_VariablesInBranches(self):
    self.assertMatches(
    """
      /// CHECK-IF: True
      ///   CHECK: foo<<VarA:\d+>>
      /// CHECK-FI:
      /// CHECK-EVAL: <<VarA>> == 12
    """,
    """
    foo12
    """)
    self.assertDoesNotMatch(
    """
      /// CHECK-IF: True
      ///   CHECK: foo<<VarA:\d+>>
      /// CHECK-FI:
      /// CHECK-EVAL: <<VarA>> == 99
    """,
    """
    foo12
    """)
    self.assertMatches(
    """
      /// CHECK-IF: True
      ///   CHECK: foo<<VarA:\d+>>
      ///   CHECK-IF: <<VarA>> == 12
      ///     CHECK: bar<<VarB:M|N>>
      ///   CHECK-FI:
      /// CHECK-FI:
      /// CHECK-EVAL: "<<VarB>>" == "M"
    """,
    """
    foo12
    barM
    """)
    self.assertMatches(
    """
      /// CHECK-IF: False
      ///   CHECK: foo<<VarA:\d+>>
      /// CHECK-ELIF: True
      ///   CHECK: foo<<VarA:M|N>>
      /// CHECK-FI:
      /// CHECK-EVAL: "<<VarA>>" == "M"
    """,
    """
    fooM
    """)
    self.assertMatches(
    """
      /// CHECK-IF: False
      ///   CHECK: foo<<VarA:A|B>>
      /// CHECK-ELIF: False
      ///   CHECK: foo<<VarA:A|B>>
      /// CHECK-ELSE:
      ///   CHECK-IF: False
      ///     CHECK: foo<<VarA:A|B>>
      ///   CHECK-ELSE:
      ///     CHECK: foo<<VarA:M|N>>
      ///   CHECK-FI:
      /// CHECK-FI:
      /// CHECK-EVAL: "<<VarA>>" == "N"
    """,
    """
    fooN
    """)

  def test_MalformedBranching(self):
    self.assertBadStructure(
      """
        /// CHECK-IF: True
        /// CHECK: foo
      """,
      """
      foo
      """)
    self.assertBadStructure(
      """
        /// CHECK-ELSE:
        /// CHECK: foo
      """,
      """
      foo
      """)
    self.assertBadStructure(
      """
        /// CHECK-IF: True
        /// CHECK: foo
        /// CHECK-ELSE:
      """,
      """
      foo
      """)
    self.assertBadStructure(
      """
        /// CHECK-IF: True
        ///   CHECK: foo
        /// CHECK-ELIF:
        ///   CHECK: foo
        ///   CHECK-IF: True
        ///     CHECK: foo
        /// CHECK-FI:
      """,
      """
      foo
      """)
    self.assertBadStructure(
      """
        /// CHECK-IF: True
        ///   CHECK: foo
        /// CHECK-ELSE:
        ///   CHECK: foo
        /// CHECK-ELIF:
        ///   CHECK: foo
        /// CHECK-FI:
      """,
      """
      foo
      """)
    self.assertBadStructure(
      """
        /// CHECK-IF: True
        ///   CHECK: foo
        /// CHECK-ELSE:
        ///   CHECK: foo
        /// CHECK-ELSE:
        ///   CHECK: foo
        /// CHECK-FI:
      """,
      """
      foo
      """)