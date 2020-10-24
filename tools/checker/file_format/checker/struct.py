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

import enum

from common.logger import Logger
from common.mixins import EqualityMixin, PrintableMixin

import re


class CheckerFile(PrintableMixin):

  def __init__(self, filename):
    self.file_name = filename
    self.test_cases = []

  def add_test_case(self, new_test_case):
    self.test_cases.append(new_test_case)

  def test_cases_for_arch(self, target_arch):
    return [t for t in self.test_cases if t.test_arch == target_arch]

  def __eq__(self, other):
    return isinstance(other, self.__class__) and self.test_cases == other.test_cases


class TestCase(PrintableMixin):

  def __init__(self, parent, name, start_line_no, test_arch=None, for_debuggable=False):
    assert isinstance(parent, CheckerFile)

    self.parent = parent
    self.name = name
    self.statements = []
    self.start_line_no = start_line_no
    self.test_arch = test_arch
    self.for_debuggable = for_debuggable

    if not self.name:
      Logger.fail("Test case does not have a name", self.filename, self.start_line_no)

    self.parent.add_test_case(self)

  @property
  def filename(self):
    return self.parent.file_name

  def add_statement(self, new_statement):
    self.statements.append(new_statement)

  def __eq__(self, other):
    return (isinstance(other, self.__class__)
            and self.name == other.name
            and self.statements == other.statements)


class TestStatement(PrintableMixin):
  class Variant(enum.IntEnum):
    """Supported types of statements."""
    IN_ORDER, NEXT_LINE, DAG, NOT, EVAL, IF, ELIF, ELSE, FI = range(9)

  def __init__(self, parent, variant, original_text, line_no):
    assert isinstance(parent, TestCase)

    self.parent = parent
    self.variant = variant
    self.expressions = []
    self.line_no = line_no
    self.original_text = original_text

    self.parent.add_statement(self)

  @property
  def filename(self):
    return self.parent.filename

  def is_pattern_match_content_statement(self):
    return self.variant in [TestStatement.Variant.IN_ORDER,
                            TestStatement.Variant.NEXT_LINE,
                            TestStatement.Variant.DAG,
                            TestStatement.Variant.NOT]

  def is_eval_content_statement(self):
    return self.variant in [TestStatement.Variant.EVAL,
                            TestStatement.Variant.IF,
                            TestStatement.Variant.ELIF]

  def is_no_content_statement(self):
    return self.variant in [TestStatement.Variant.ELSE,
                            TestStatement.Variant.FI]

  def add_expression(self, new_expression):
    assert isinstance(new_expression, TestExpression)
    if self.variant == TestStatement.Variant.NOT:
      if new_expression.variant == TestExpression.Variant.VAR_DEF:
        Logger.fail("CHECK-NOT lines cannot define variables", self.filename, self.line_no)
    self.expressions.append(new_expression)

  def to_regex(self):
    """ Returns a regex pattern for this entire statement. Only used in tests. """
    regex = ""
    for expression in self.expressions:
      if expression.variant == TestExpression.Variant.SEPARATOR:
        regex = regex + ", "
      else:
        regex = regex + "(" + expression.text + ")"
    return regex

  def __eq__(self, other):
    return (isinstance(other, self.__class__)
            and self.variant == other.variant
            and self.expressions == other.expressions)


class TestExpression(EqualityMixin, PrintableMixin):
  class Variant:
    """Supported language constructs."""
    PLAIN_TEXT, PATTERN, VAR_REF, VAR_DEF, SEPARATOR = range(5)

  class Regex:
    R_NAME = r"([a-zA-Z][a-zA-Z0-9]*)"
    R_REGEX = r"(.+?)"
    R_PATTERN_START_SYM = r"(\{\{)"
    R_PATTERN_END_SYM = r"(\}\})"
    R_VARIABLE_START_SYM = r"(<<)"
    R_VARIABLE_END_SYM = r"(>>)"
    R_VARIABLE_SEPARATOR = r"(:)"
    R_VARIABLE_DEFINITION_BODY = R_NAME + R_VARIABLE_SEPARATOR + R_REGEX

    REGEX_PATTERN = R_PATTERN_START_SYM + R_REGEX + R_PATTERN_END_SYM
    REGEX_VARIABLE_REFERENCE = R_VARIABLE_START_SYM + R_NAME + R_VARIABLE_END_SYM
    REGEX_VARIABLE_DEFINITION = (R_VARIABLE_START_SYM + R_VARIABLE_DEFINITION_BODY
                                 + R_VARIABLE_END_SYM)

  def __init__(self, variant, name, text):
    self.variant = variant
    self.name = name
    self.text = text

  def __eq__(self, other):
    return (isinstance(other, self.__class__)
            and self.variant == other.variant
            and self.name == other.name
            and self.text == other.text)

  @staticmethod
  def create_separator():
    return TestExpression(TestExpression.Variant.SEPARATOR, None, None)

  @staticmethod
  def create_plain_text(text):
    return TestExpression(TestExpression.Variant.PLAIN_TEXT, None, text)

  @staticmethod
  def create_pattern_from_plain_text(text):
    return TestExpression(TestExpression.Variant.PATTERN, None, re.escape(text))

  @staticmethod
  def create_pattern(pattern):
    return TestExpression(TestExpression.Variant.PATTERN, None, pattern)

  @staticmethod
  def create_variable_reference(name):
    assert re.match(TestExpression.Regex.R_NAME, name)
    return TestExpression(TestExpression.Variant.VAR_REF, name, None)

  @staticmethod
  def create_variable_definition(name, pattern):
    assert re.match(TestExpression.Regex.R_NAME, name)
    return TestExpression(TestExpression.Variant.VAR_DEF, name, pattern)
