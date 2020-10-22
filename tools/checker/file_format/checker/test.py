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

from common.archs import archs_list
from file_format.checker.parser import parse_checker_stream
from file_format.checker.struct import CheckerFile, TestCase, TestStatement, TestExpression

import io
import unittest

CheckerException = SystemExit


class CheckerParser_PrefixTest(unittest.TestCase):

  def try_parse(self, string):
    checker_text = "/// CHECK-START: pass\n" + string
    return parse_checker_stream("<test-file>", "CHECK", io.StringIO(checker_text))

  def assertParses(self, string):
    check_file = self.try_parse(string)
    self.assertEqual(len(check_file.test_cases), 1)
    self.assertNotEqual(len(check_file.test_cases[0].statements), 0)

  def assertIgnored(self, string):
    check_file = self.try_parse(string)
    self.assertEqual(len(check_file.test_cases), 1)
    self.assertEqual(len(check_file.test_cases[0].statements), 0)

  def assertInvalid(self, string):
    with self.assertRaises(CheckerException):
      self.try_parse(string)

  def test_ValidFormat(self):
    self.assertParses("///CHECK:foo")
    self.assertParses("##CHECK:bar")

  def test_InvalidFormat(self):
    self.assertIgnored("CHECK")
    self.assertIgnored(":CHECK")
    self.assertIgnored("CHECK:")
    self.assertIgnored("//CHECK")
    self.assertIgnored("#CHECK")
    self.assertInvalid("///CHECK")
    self.assertInvalid("##CHECK")

  def test_InvalidPrefix(self):
    self.assertInvalid("///ACHECK:foo")
    self.assertInvalid("##ACHECK:foo")

  def test_NotFirstOnTheLine(self):
    self.assertIgnored("A/// CHECK: foo")
    self.assertIgnored("A # CHECK: foo")
    self.assertInvalid("/// /// CHECK: foo")
    self.assertInvalid("## ## CHECK: foo")

  def test_WhitespaceAgnostic(self):
    self.assertParses("  ///CHECK: foo")
    self.assertParses("///  CHECK: foo")
    self.assertParses("    ///CHECK: foo")
    self.assertParses("///    CHECK: foo")


class CheckerParser_TestExpressionTest(unittest.TestCase):
  def parse_statement(self, string, variant=""):
    checker_text = ("/// CHECK-START: pass\n" +
                    "/// CHECK" + variant + ": " + string)
    checker_file = parse_checker_stream("<test-file>", "CHECK", io.StringIO(checker_text))
    self.assertEqual(len(checker_file.test_cases), 1)
    test_case = checker_file.test_cases[0]
    self.assertEqual(len(test_case.statements), 1)
    return test_case.statements[0]

  def parse_expression(self, string):
    line = self.parse_statement(string)
    self.assertEqual(1, len(line.expressions))
    return line.expressions[0]

  def assertEqualsRegex(self, string, expected):
    self.assertEqual(expected, self.parse_statement(string).to_regex())

  def assertEqualsText(self, string, text):
    self.assertEqual(self.parse_expression(string),
                     TestExpression.create_pattern_from_plain_text(text))

  def assertEqualsPattern(self, string, pattern):
    self.assertEqual(self.parse_expression(string), TestExpression.create_pattern(pattern))

  def assertEqualsVarRef(self, string, name):
    self.assertEqual(self.parse_expression(string), TestExpression.create_variable_reference(name))

  def assertEqualsVarDef(self, string, name, pattern):
    self.assertEqual(self.parse_expression(string),
                     TestExpression.create_variable_definition(name, pattern))

  def assertVariantNotEqual(self, string, variant):
    self.assertNotEqual(variant, self.parse_expression(string).variant)

  # Test that individual parts of the line are recognized

  def test_TextOnly(self):
    self.assertEqualsText("foo", "foo")
    self.assertEqualsText("  foo  ", "foo")
    self.assertEqualsRegex("f$o^o", "(f\\$o\\^o)")

  def test_PatternOnly(self):
    self.assertEqualsPattern("{{a?b.c}}", "a?b.c")

  def test_VarRefOnly(self):
    self.assertEqualsVarRef("<<ABC>>", "ABC")

  def test_VarDefOnly(self):
    self.assertEqualsVarDef("<<ABC:a?b.c>>", "ABC", "a?b.c")

  def test_TextWithWhitespace(self):
    self.assertEqualsRegex("foo bar", "(foo), (bar)")
    self.assertEqualsRegex("foo   bar", "(foo), (bar)")

  def test_TextWithRegex(self):
    self.assertEqualsRegex("foo{{abc}}bar", "(foo)(abc)(bar)")

  def test_TextWithVar(self):
    self.assertEqualsRegex("foo<<ABC:abc>>bar", "(foo)(abc)(bar)")

  def test_PlainWithRegexAndWhitespaces(self):
    self.assertEqualsRegex("foo {{abc}}bar", "(foo), (abc)(bar)")
    self.assertEqualsRegex("foo{{abc}} bar", "(foo)(abc), (bar)")
    self.assertEqualsRegex("foo {{abc}} bar", "(foo), (abc), (bar)")

  def test_PlainWithVarAndWhitespaces(self):
    self.assertEqualsRegex("foo <<ABC:abc>>bar", "(foo), (abc)(bar)")
    self.assertEqualsRegex("foo<<ABC:abc>> bar", "(foo)(abc), (bar)")
    self.assertEqualsRegex("foo <<ABC:abc>> bar", "(foo), (abc), (bar)")

  def test_AllKinds(self):
    self.assertEqualsRegex("foo <<ABC:abc>>{{def}}bar", "(foo), (abc)(def)(bar)")
    self.assertEqualsRegex("foo<<ABC:abc>> {{def}}bar", "(foo)(abc), (def)(bar)")
    self.assertEqualsRegex("foo <<ABC:abc>> {{def}} bar", "(foo), (abc), (def), (bar)")

  # # Test that variables and patterns are parsed correctly

  def test_ValidPattern(self):
    self.assertEqualsPattern("{{abc}}", "abc")
    self.assertEqualsPattern("{{a[b]c}}", "a[b]c")
    self.assertEqualsPattern("{{(a{bc})}}", "(a{bc})")

  def test_ValidRef(self):
    self.assertEqualsVarRef("<<ABC>>", "ABC")
    self.assertEqualsVarRef("<<A1BC2>>", "A1BC2")

  def test_ValidDef(self):
    self.assertEqualsVarDef("<<ABC:abc>>", "ABC", "abc")
    self.assertEqualsVarDef("<<ABC:ab:c>>", "ABC", "ab:c")
    self.assertEqualsVarDef("<<ABC:a[b]c>>", "ABC", "a[b]c")
    self.assertEqualsVarDef("<<ABC:(a[bc])>>", "ABC", "(a[bc])")

  def test_Empty(self):
    self.assertEqualsText("{{}}", "{{}}")
    self.assertVariantNotEqual("<<>>", TestExpression.Variant.VAR_REF)
    self.assertVariantNotEqual("<<:>>", TestExpression.Variant.VAR_DEF)

  def test_InvalidVarName(self):
    self.assertVariantNotEqual("<<0ABC>>", TestExpression.Variant.VAR_REF)
    self.assertVariantNotEqual("<<AB=C>>", TestExpression.Variant.VAR_REF)
    self.assertVariantNotEqual("<<ABC=>>", TestExpression.Variant.VAR_REF)
    self.assertVariantNotEqual("<<0ABC:abc>>", TestExpression.Variant.VAR_DEF)
    self.assertVariantNotEqual("<<AB=C:abc>>", TestExpression.Variant.VAR_DEF)
    self.assertVariantNotEqual("<<ABC=:abc>>", TestExpression.Variant.VAR_DEF)

  def test_BodyMatchNotGreedy(self):
    self.assertEqualsRegex("{{abc}}{{def}}", "(abc)(def)")
    self.assertEqualsRegex("<<ABC:abc>><<DEF:def>>", "(abc)(def)")

  def test_NoVarDefsInNotChecks(self):
    with self.assertRaises(CheckerException):
      self.parse_statement("<<ABC:abc>>", "-NOT")


class CheckerParser_FileLayoutTest(unittest.TestCase):

  # Creates an instance of CheckerFile from provided info.
  # Data format: [ ( <case-name>, [ ( <text>, <assert-variant> ), ... ] ), ... ]
  def create_file(self, case_list):
    test_file = CheckerFile("<test_file>")
    for caseEntry in case_list:
      case_name = caseEntry[0]
      test_case = TestCase(test_file, case_name, 0)
      statement_list = caseEntry[1]
      for statementEntry in statement_list:
        content = statementEntry[0]
        variant = statementEntry[1]
        statement = TestStatement(test_case, variant, content, 0)
        if statement.is_eval_content_statement():
          statement.add_expression(TestExpression.create_plain_text(content))
        elif statement.is_pattern_match_content_statement():
          statement.add_expression(TestExpression.create_pattern_from_plain_text(content))
    return test_file

  def assertParsesTo(self, checker_text, expected_data):
    expected_file = self.create_file(expected_data)
    actual_file = self.parse(checker_text)
    return self.assertEqual(expected_file, actual_file)

  def parse(self, checker_text):
    return parse_checker_stream("<test_file>", "CHECK", io.StringIO(checker_text))

  def test_EmptyFile(self):
    self.assertParsesTo("", [])

  def test_SingleGroup(self):
    self.assertParsesTo(
      """
        /// CHECK-START: Example Group
        /// CHECK:  foo
        /// CHECK:    bar
      """,
      [("Example Group", [("foo", TestStatement.Variant.IN_ORDER),
                          ("bar", TestStatement.Variant.IN_ORDER)])])

  def test_MultipleGroups(self):
    self.assertParsesTo(
      """
        /// CHECK-START: Example Group1
        /// CHECK: foo
        /// CHECK: bar
        /// CHECK-START: Example Group2
        /// CHECK: abc
        /// CHECK: def
      """,
      [("Example Group1", [("foo", TestStatement.Variant.IN_ORDER),
                           ("bar", TestStatement.Variant.IN_ORDER)]),
       ("Example Group2", [("abc", TestStatement.Variant.IN_ORDER),
                           ("def", TestStatement.Variant.IN_ORDER)])])

  def test_StatementVariants(self):
    self.assertParsesTo(
      """
        /// CHECK-START: Example Group
        /// CHECK:      foo1
        /// CHECK:      foo2
        /// CHECK-NEXT: foo3
        /// CHECK-NEXT: foo4
        /// CHECK-NOT:  bar
        /// CHECK-DAG:  abc
        /// CHECK-DAG:  def
        /// CHECK-EVAL: x > y
        /// CHECK-IF:   x < y
        /// CHECK-ELIF: x == y
        /// CHECK-ELSE:
        /// CHECK-FI:
      """,
      [("Example Group", [("foo1", TestStatement.Variant.IN_ORDER),
                          ("foo2", TestStatement.Variant.IN_ORDER),
                          ("foo3", TestStatement.Variant.NEXT_LINE),
                          ("foo4", TestStatement.Variant.NEXT_LINE),
                          ("bar", TestStatement.Variant.NOT),
                          ("abc", TestStatement.Variant.DAG),
                          ("def", TestStatement.Variant.DAG),
                          ("x > y", TestStatement.Variant.EVAL),
                          ("x < y", TestStatement.Variant.IF),
                          ("x == y", TestStatement.Variant.ELIF),
                          (None, TestStatement.Variant.ELSE),
                          (None, TestStatement.Variant.FI)])])

  def test_NoContentStatements(self):
    with self.assertRaises(CheckerException):
      self.parse(
        """
          /// CHECK-START: Example Group
          /// CHECK-ELSE:    foo
        """)
    with self.assertRaises(CheckerException):
      self.parse(
        """
          /// CHECK-START: Example Group
          /// CHECK-FI:      foo
        """)


class CheckerParser_SuffixTests(unittest.TestCase):
  NOARCH_BLOCK = """
                  /// CHECK-START: Group
                  /// CHECK:       foo
                  /// CHECK-NEXT:  bar
                  /// CHECK-NOT:   baz
                  /// CHECK-DAG:   yoyo
                  /// CHECK-EVAL: x > y
                  /// CHECK-IF:   x < y
                  /// CHECK-ELIF: x == y
                  /// CHECK-ELSE:
                  /// CHECK-FI:
                """

  ARCH_BLOCK = """
                  /// CHECK-START-{test_arch}: Group
                  /// CHECK:       foo
                  /// CHECK-NEXT:  bar
                  /// CHECK-NOT:   baz
                  /// CHECK-DAG:   yoyo
                  /// CHECK-EVAL: x > y
                  /// CHECK-IF:   x < y
                  /// CHECK-ELIF: x == y
                  /// CHECK-ELSE:
                  /// CHECK-FI:
                """

  def parse(self, checker_text):
    return parse_checker_stream("<test_file>", "CHECK", io.StringIO(checker_text))

  def test_NonArchTests(self):
    for arch in [None] + archs_list:
      checker_file = self.parse(self.NOARCH_BLOCK)
      self.assertEqual(len(checker_file.test_cases), 1)
      self.assertEqual(len(checker_file.test_cases[0].statements), 9)

  def test_IgnoreNonTargetArch(self):
    for target_arch in archs_list:
      for test_arch in [a for a in archs_list if a != target_arch]:
        checker_text = self.ARCH_BLOCK.format(test_arch=test_arch)
        checker_file = self.parse(checker_text)
        self.assertEqual(len(checker_file.test_cases), 1)
        self.assertEqual(len(checker_file.test_cases_for_arch(test_arch)), 1)
        self.assertEqual(len(checker_file.test_cases_for_arch(target_arch)), 0)

  def test_Arch(self):
    for arch in archs_list:
      checker_text = self.ARCH_BLOCK.format(test_arch=arch)
      checker_file = self.parse(checker_text)
      self.assertEqual(len(checker_file.test_cases), 1)
      self.assertEqual(len(checker_file.test_cases_for_arch(arch)), 1)
      self.assertEqual(len(checker_file.test_cases[0].statements), 9)

  def test_NoDebugAndArch(self):
    test_case = self.parse("""
        /// CHECK-START: Group
        /// CHECK: foo
        """).test_cases[0]
    self.assertFalse(test_case.for_debuggable)
    self.assertEqual(test_case.test_arch, None)

  def test_SetDebugNoArch(self):
    test_case = self.parse("""
        /// CHECK-START-DEBUGGABLE: Group
        /// CHECK: foo
        """).test_cases[0]
    self.assertTrue(test_case.for_debuggable)
    self.assertEqual(test_case.test_arch, None)

  def test_NoDebugSetArch(self):
    test_case = self.parse("""
        /// CHECK-START-ARM: Group
        /// CHECK: foo
        """).test_cases[0]
    self.assertFalse(test_case.for_debuggable)
    self.assertEqual(test_case.test_arch, "ARM")

  def test_SetDebugAndArch(self):
    test_case = self.parse("""
        /// CHECK-START-ARM-DEBUGGABLE: Group
        /// CHECK: foo
        """).test_cases[0]
    self.assertTrue(test_case.for_debuggable)
    self.assertEqual(test_case.test_arch, "ARM")


class CheckerParser_EvalTests(unittest.TestCase):
  def parse_test_case(self, string):
    checker_text = "/// CHECK-START: pass\n" + string
    checker_file = parse_checker_stream("<test-file>", "CHECK", io.StringIO(checker_text))
    self.assertEqual(len(checker_file.test_cases), 1)
    return checker_file.test_cases[0]

  def parse_expressions(self, string):
    test_case = self.parse_test_case("/// CHECK-EVAL: " + string)
    self.assertEqual(len(test_case.statements), 1)
    statement = test_case.statements[0]
    self.assertEqual(statement.variant, TestStatement.Variant.EVAL)
    self.assertEqual(statement.original_text, string)
    return statement.expressions

  def assertParsesToPlainText(self, text):
    test_case = self.parse_test_case("/// CHECK-EVAL: " + text)
    self.assertEqual(len(test_case.statements), 1)
    statement = test_case.statements[0]
    self.assertEqual(statement.variant, TestStatement.Variant.EVAL)
    self.assertEqual(statement.original_text, text)
    self.assertEqual(len(statement.expressions), 1)
    expression = statement.expressions[0]
    self.assertEqual(expression.variant, TestExpression.Variant.PLAIN_TEXT)
    self.assertEqual(expression.text, text)

  def test_PlainText(self):
    self.assertParsesToPlainText("XYZ")
    self.assertParsesToPlainText("True")
    self.assertParsesToPlainText("{{abc}}")
    self.assertParsesToPlainText("<<ABC:abc>>")
    self.assertParsesToPlainText("<<ABC=>>")

  def test_VariableReference(self):
    self.assertEqual(self.parse_expressions("<<ABC>>"),
                     [TestExpression.create_variable_reference("ABC")])
    self.assertEqual(self.parse_expressions("123<<ABC>>"),
                     [TestExpression.create_plain_text("123"),
                      TestExpression.create_variable_reference("ABC")])
    self.assertEqual(self.parse_expressions("123  <<ABC>>"),
                     [TestExpression.create_plain_text("123  "),
                      TestExpression.create_variable_reference("ABC")])
    self.assertEqual(self.parse_expressions("<<ABC>>XYZ"),
                     [TestExpression.create_variable_reference("ABC"),
                      TestExpression.create_plain_text("XYZ")])
    self.assertEqual(self.parse_expressions("<<ABC>>   XYZ"),
                     [TestExpression.create_variable_reference("ABC"),
                      TestExpression.create_plain_text("   XYZ")])
    self.assertEqual(self.parse_expressions("123<<ABC>>XYZ"),
                     [TestExpression.create_plain_text("123"),
                      TestExpression.create_variable_reference("ABC"),
                      TestExpression.create_plain_text("XYZ")])
    self.assertEqual(self.parse_expressions("123 <<ABC>>  XYZ"),
                     [TestExpression.create_plain_text("123 "),
                      TestExpression.create_variable_reference("ABC"),
                      TestExpression.create_plain_text("  XYZ")])
