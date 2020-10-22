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

import collections
import enum
import sys


class Logger:
  class Level(enum.IntEnum):
    NO_OUTPUT, ERROR, INFO = range(3)

  class Color(enum.Enum):
    DEFAULT, BLUE, GRAY, PURPLE, RED, GREEN = range(6)

    @staticmethod
    def terminal_code(color, out=sys.stdout):
      if not out.isatty():
        return ""
      elif color == Logger.Color.BLUE:
        return "\033[94m"
      elif color == Logger.Color.GRAY:
        return "\033[37m"
      elif color == Logger.Color.PURPLE:
        return "\033[95m"
      elif color == Logger.Color.RED:
        return "\033[91m"
      elif color == Logger.Color.GREEN:
        return "\033[32m"
      else:
        return "\033[0m"

  Verbosity = Level.INFO

  @staticmethod
  def log(content, level=Level.INFO, color=Color.DEFAULT, new_line=True, out=sys.stdout):
    if level <= Logger.Verbosity:
      content = "{}{}{}".format(Logger.Color.terminal_code(color, out), content,
                                Logger.Color.terminal_code(Logger.Color.DEFAULT, out))
      if new_line:
        print(content, file=out)
      else:
        print(content, end="", file=out)
      out.flush()

  @staticmethod
  def fail(msg, file=None, line=-1, line_text=None, variables=None):
    Logger.log("error: ", Logger.Level.ERROR, color=Logger.Color.RED, new_line=False,
               out=sys.stderr)
    Logger.log(msg, Logger.Level.ERROR, out=sys.stderr)

    if line_text:
      loc = ""
      if file:
        loc += file + ":"
      if line > 0:
        loc += str(line) + ":"
      if loc:
        loc += " "
      Logger.log(loc, Logger.Level.ERROR, color=Logger.Color.GRAY, new_line=False,
                 out=sys.stderr)
      Logger.log(line_text, Logger.Level.ERROR, out=sys.stderr)

    if variables:
      longest_name = max(len(var) for var in variables)

      for var in collections.OrderedDict(sorted(variables.items())):
        padding = " " * (longest_name - len(var))
        Logger.log(var, Logger.Level.ERROR, color=Logger.Color.GREEN, new_line=False,
                   out=sys.stderr)
        Logger.log(padding, Logger.Level.ERROR, new_line=False, out=sys.stderr)
        Logger.log(" = ", Logger.Level.ERROR, new_line=False, out=sys.stderr)
        Logger.log(variables[var], Logger.Level.ERROR, out=sys.stderr)

    sys.exit(1)

  @staticmethod
  def start_test(name):
    Logger.log("TEST ", color=Logger.Color.PURPLE, new_line=False)
    Logger.log(name + "... ", new_line=False)

  @staticmethod
  def test_passed():
    Logger.log("PASS", color=Logger.Color.BLUE)

  @staticmethod
  def test_failed(msg, statement, variables):
    Logger.log("FAIL", color=Logger.Color.RED)
    Logger.fail(msg, statement.filename, statement.line_no, statement.original_text, variables)
