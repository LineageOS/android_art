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
from file_format.checker.struct import TestExpression, TestStatement

# Required for eval.
import os
import re


def head_and_tail(list):
  return list[0], list[1:]


def split_at_separators(expressions):
  """ Splits a list of TestExpressions at separators. """
  split_expressions = []
  word_start = 0
  for index, expression in enumerate(expressions):
    if expression.variant == TestExpression.Variant.SEPARATOR:
      split_expressions.append(expressions[word_start:index])
      word_start = index + 1
  split_expressions.append(expressions[word_start:])
  return split_expressions


def get_variable(name, variables, pos):
  if name in variables:
    return variables[name]
  else:
    Logger.test_failed('Missing definition of variable "{}"'.format(name), pos, variables)


def set_variable(name, value, variables, pos):
  if name not in variables:
    return variables.copy_with(name, value)
  else:
    Logger.test_failed('Multiple definitions of variable "{}"'.format(name), pos, variables)


def match_words(checker_word, string_word, variables, pos):
  """ Attempts to match a list of TestExpressions against a string.
      Returns updated variable dictionary if successful and None otherwise.
  """
  for expression in checker_word:
    # If `expression` is a variable reference, replace it with the value.
    if expression.variant == TestExpression.Variant.VAR_REF:
      pattern = re.escape(get_variable(expression.name, variables, pos))
    else:
      pattern = expression.text

    # Match the expression's regex pattern against the remainder of the word.
    # Note: re.match will succeed only if matched from the beginning.
    match = re.match(pattern, string_word)
    if not match:
      return None

    # If `expression` was a variable definition, set the variable's value.
    if expression.variant == TestExpression.Variant.VAR_DEF:
      variables = set_variable(expression.name, string_word[:match.end()], variables, pos)

    # Move cursor by deleting the matched characters.
    string_word = string_word[match.end():]

  # Make sure the entire word matched, i.e. `stringWord` is empty.
  if string_word:
    return None

  return variables


def match_lines(checker_line, string_line, variables):
  """ Attempts to match a CHECK line against a string. Returns variable state
      after the match if successful and None otherwise.
  """
  assert checker_line.variant != TestStatement.Variant.EVAL

  checker_words = split_at_separators(checker_line.expressions)
  string_words = string_line.split()

  while checker_words:
    # Get the next run of TestExpressions which must match one string word.
    checker_word, checker_words = head_and_tail(checker_words)

    # Keep reading words until a match is found.
    word_matched = False
    while string_words:
      string_word, string_words = head_and_tail(string_words)
      new_variables = match_words(checker_word, string_word, variables, checker_line)
      if new_variables is not None:
        word_matched = True
        variables = new_variables
        break
    if not word_matched:
      return None

  # All TestExpressions matched. Return new variable state.
  return variables


def get_eval_text(expression, variables, pos):
  if expression.variant == TestExpression.Variant.PLAIN_TEXT:
    return expression.text
  else:
    assert expression.variant == TestExpression.Variant.VAR_REF
    return get_variable(expression.name, variables, pos)


def evaluate_line(checker_line, variables):
  assert checker_line.is_eval_content_statement()
  # Required for eval.
  hasIsaFeature = lambda feature: variables["ISA_FEATURES"].get(feature, False)
  eval_string = "".join(get_eval_text(expr,
                                      variables,
                                      checker_line) for expr in checker_line.expressions)
  return eval(eval_string)
