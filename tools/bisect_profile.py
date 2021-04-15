#!/usr/bin/python3
#
# Copyright (C) 2021 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# Generates profiles from the set of all methods in a given set of dex/jars and
# bisects to find minimal repro sets.
#

import shlex
import argparse
import pylibdexfile
import math
import subprocess
from collections import namedtuple
import sys
import random
import os

ApkEntry = namedtuple("ApkEntry", ["file", "location"])


def get_parser():
  parser = argparse.ArgumentParser(
      description="Bisect profile contents. We will wait while the user runs test"
  )

  class ApkAction(argparse.Action):

    def __init__(self, option_strings, dest, **kwargs):
      super(ApkAction, self).__init__(option_strings, dest, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
      lst = getattr(namespace, self.dest)
      if lst is None:
        setattr(namespace, self.dest, [])
        lst = getattr(namespace, self.dest)
      if len(values) == 1:
        values = (values[0], values[0])
      assert len(values) == 2, values
      lst.append(ApkEntry(*values))

  apks = parser.add_argument_group(title="APK selection")
  apks.add_argument(
      "--apk",
      action=ApkAction,
      dest="apks",
      nargs=1,
      default=[],
      help="an apk/dex/jar to get methods from. Uses same path as location. " +
           "Use --apk-and-location if this isn't desired."
  )
  apks.add_argument(
      "--apk-and-location",
      action=ApkAction,
      nargs=2,
      dest="apks",
      help="an apk/dex/jar + location to get methods from."
  )
  profiles = parser.add_argument_group(
      title="Profile selection").add_mutually_exclusive_group()
  profiles.add_argument(
      "--input-text-profile", help="a text profile to use for bisect")
  profiles.add_argument("--input-profile", help="a profile to use for bisect")
  parser.add_argument(
      "--output-source", help="human readable file create the profile from")
  parser.add_argument("--test-exec", help="file to exec (without arguments) to test a" +
                                           " candidate. Test should exit 0 if the issue" +
                                           " is not present and non-zero if the issue is" +
                                           " present.")
  parser.add_argument("output_file", help="file we will write the profiles to")
  return parser


def dump_files(meths, args, output):
  for m in meths:
    print("HS{}".format(m), file=output)
  output.flush()
  profman_args = [
      "profmand", "--reference-profile-file={}".format(args.output_file),
      "--create-profile-from={}".format(args.output_source)
  ]
  print(" ".join(map(shlex.quote, profman_args)))
  for apk in args.apks:
    profman_args += [
        "--apk={}".format(apk.file), "--dex-location={}".format(apk.location)
    ]
  profman = subprocess.run(profman_args)
  profman.check_returncode()


def get_answer(args):
  if args.test_exec is None:
    while True:
      answer = input("Does the file at {} cause the issue (y/n):".format(
          args.output_file))
      if len(answer) >= 1 and answer[0].lower() == "y":
        return "y"
      elif len(answer) >= 1 and answer[0].lower() == "n":
        return "n"
      else:
        print("Please enter 'y' or 'n' only!")
  else:
    test_args = shlex.split(args.test_exec)
    print(" ".join(map(shlex.quote, test_args)))
    answer = subprocess.run(test_args)
    if answer.returncode == 0:
      return "n"
    else:
      return "y"

def run_test(meths, args):
  with open(args.output_source, "wt") as output:
    dump_files(meths, args, output)
    print("Currently testing {} methods. ~{} rounds to go.".format(
        len(meths), 1 + math.floor(math.log2(len(meths)))))
  return get_answer(args)

def main():
  parser = get_parser()
  args = parser.parse_args()
  if args.output_source is None:
    fdnum = os.memfd_create("tempfile_profile")
    args.output_source = "/proc/{}/fd/{}".format(os.getpid(), fdnum)
  all_dexs = list()
  for f in args.apks:
    try:
      all_dexs.append(pylibdexfile.FileDexFile(f.file, f.location))
    except Exception as e1:
      try:
        all_dexs += pylibdexfile.OpenJar(f.file)
      except Exception as e2:
        parser.error("Failed to open file: {}. errors were {} and {}".format(
            f.file, e1, e2))
  if args.input_profile is not None:
    profman_args = [
        "profmand", "--dump-classes-and-methods",
        "--profile-file={}".format(args.input_profile)
    ]
    for apk in args.apks:
      profman_args.append("--apk={}".format(apk.file))
    print(" ".join(map(shlex.quote, profman_args)))
    res = subprocess.run(
        profman_args, capture_output=True, universal_newlines=True)
    res.check_returncode()
    meth_list = list(filter(lambda a: a != "", res.stdout.split()))
  elif args.input_text_profile is not None:
    with open(args.input_text_profile, "rt") as inp:
      meth_list = list(filter(lambda a: a != "", inp.readlines()))
  else:
    all_methods = set()
    for d in all_dexs:
      for m in d.methods:
        all_methods.add(m.descriptor)
    meth_list = list(all_methods)
  print("Found {} methods. Will take ~{} iterations".format(
      len(meth_list), 1 + math.floor(math.log2(len(meth_list)))))
  print(
      "type 'yes' if the behavior you are looking for is present (i.e. the compiled code crashes " +
      "or something)"
  )
  print("Performing single check with all methods")
  result = run_test(meth_list, args)
  if result[0].lower() != "y":
    cont = input(
        "The behavior you were looking for did not occur when run against all methods. Continue " +
        "(yes/no)? "
    )
    if cont[0].lower() != "y":
      print("Aborting!")
      sys.exit(1)
  needs_dump = False
  while len(meth_list) > 1:
    test_methods = list(meth_list[0:len(meth_list) // 2])
    result = run_test(test_methods, args)
    if result[0].lower() == "y":
      meth_list = test_methods
      needs_dump = False
    else:
      meth_list = meth_list[len(meth_list) // 2:]
      needs_dump = True
  if needs_dump:
    with open(args.output_source, "wt") as output:
      dump_files(meth_list, args, output)
  print("Found result!")
  print("{}".format(meth_list[0]))
  print("Leaving profile at {} and text profile at {}".format(
      args.output_file, args.output_source))


if __name__ == "__main__":
  main()
