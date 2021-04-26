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

import argparse
import os

from common.archs import archs_list
from common.logger import Logger
from file_format.c1visualizer.parser import parse_c1_visualizer_stream
from file_format.checker.parser import parse_checker_stream
from match.file import match_files


def parse_arguments():
  parser = argparse.ArgumentParser()
  parser.add_argument("tested_file",
                      help="text file the checks should be verified against")
  parser.add_argument("source_path", nargs="?",
                      help="path to file/folder with checking annotations")
  parser.add_argument("--check-prefix", dest="check_prefix", default="CHECK", metavar="PREFIX",
                      help="prefix of checks in the test files (default: CHECK)")
  parser.add_argument("--list-passes", dest="list_passes", action="store_true",
                      help="print a list of all passes found in the tested file")
  parser.add_argument("--dump-pass", dest="dump_pass", metavar="PASS",
                      help="print a compiler pass dump")
  parser.add_argument("--arch", dest="arch", choices=archs_list,
                      help="Run tests for the specified target architecture.")
  parser.add_argument("--debuggable", action="store_true",
                      help="Run tests for debuggable code.")
  parser.add_argument("--print-cfg", action="store_true", default="True", dest="print_cfg",
                      help="Print the whole cfg file in case of test failure (default)")
  parser.add_argument("--no-print-cfg", action="store_false", default="True", dest="print_cfg",
                      help="Don't print the whole cfg file in case of test failure")
  parser.add_argument("-q", "--quiet", action="store_true",
                      help="print only errors")
  return parser.parse_args()


def list_passes(output_filename):
  c1_file = parse_c1_visualizer_stream(output_filename, open(output_filename, "r"))
  for compiler_pass in c1_file.passes:
    Logger.log(compiler_pass.name)


def dump_pass(output_filename, pass_name):
  c1_file = parse_c1_visualizer_stream(output_filename, open(output_filename, "r"))
  compiler_pass = c1_file.find_pass(pass_name)
  if compiler_pass:
    max_line_no = compiler_pass.start_line_no + len(compiler_pass.body)
    len_line_no = len(str(max_line_no)) + 2
    cur_line_no = compiler_pass.start_line_no
    for line in compiler_pass.body:
      Logger.log((str(cur_line_no) + ":").ljust(len_line_no) + line)
      cur_line_no += 1
  else:
    Logger.fail('Pass "{}" not found in the output'.format(pass_name))


def find_checker_files(path):
  """ Returns a list of files to scan for check annotations in the given path.
      Path to a file is returned as a single-element list, directories are
      recursively traversed and all '.java', '.j', and '.smali' files returned.
  """
  if not path:
    Logger.fail("No source path provided")
  elif os.path.isfile(path):
    return [path]
  elif os.path.isdir(path):
    found_files = []
    for root, dirs, files in os.walk(path):
      for file in files:
        extension = os.path.splitext(file)[1]
        if extension in [".java", ".smali", ".j"]:
          found_files.append(os.path.join(root, file))
    return found_files
  else:
    Logger.fail('Source path "{}" not found'.format(path))


def run_tests(check_prefix, check_path, output_filename, target_arch, debuggable_mode, print_cfg):
  c1_file = parse_c1_visualizer_stream(output_filename, open(output_filename, "r"))
  for check_filename in find_checker_files(check_path):
    checker_file = parse_checker_stream(os.path.basename(check_filename),
                                        check_prefix,
                                        open(check_filename, "r"),
                                        target_arch)
    match_files(checker_file, c1_file, target_arch, debuggable_mode, print_cfg)


if __name__ == "__main__":
  args = parse_arguments()

  if args.quiet:
    Logger.Verbosity = Logger.Level.ERROR

  if args.list_passes:
    list_passes(args.tested_file)
  elif args.dump_pass:
    dump_pass(args.tested_file, args.dump_pass)
  else:
    run_tests(args.check_prefix, args.source_path, args.tested_file, args.arch, args.debuggable,
              args.print_cfg)
