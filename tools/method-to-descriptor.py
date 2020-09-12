#!/usr/bin/python3
#
# Copyright 2020, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Converts a method to a descriptor or vice-versa.

eg:

% echo 'void myclass.foobar(long, java.lang.Object)' | method-to-descriptor.py
Lmyclass;->foobar(jLjaga/lang/Object;)V
% echo 'Lmyclass;->foobar(j)V' | method2descriptor.py -r
void myclass.foobar(long)
"""

import argparse
import sys


def GetStdinLineIter():
  """reads from stdin"""
  return map(str.strip, sys.stdin)


def readDescriptor(s):
  """Reads a single descriptor and returns the string starting at the point after the descriptor"""
  if s[0] == "[":
    inner, rest = readDescriptor(s[1:])
    return "[" + inner, rest
  elif s[0] == "L":
    type_end = s.index(";")
    return s[:type_end + 1], s[type_end + 1:]
  else:
    assert s[0] in {"B", "C", "D", "F", "I", "J", "S", "Z", "V"}, s[0]
    return s[0], s[1:]


# Descriptor to name for basic types
TYPE_MAP = {
    "V": "void",
    "B": "byte",
    "C": "char",
    "D": "double",
    "F": "float",
    "I": "int",
    "J": "long",
    "S": "short",
    "Z": "boolean"
}

# Name to descriptor
DESC_MAP = dict((y, x) for x, y in TYPE_MAP.items())

def TypeDescriptorToName(desc):
  """Turn a single type descirptor into a name"""
  if desc[0] == "[":
    inner = TypeDescriptorToName(desc[1:])
    return inner + "[]"
  elif desc[0] == "L":
    assert desc[-1] == ";", desc
    return desc[1:-1].replace("/", ".")
  else:
    return TYPE_MAP[desc]

def DescriptorToName(desc):
  """Turn a method descriptor into a name"""
  class_name, rest = readDescriptor(desc)
  assert rest[0:2] == "->", desc
  rest = rest[2:]
  args_start = rest.index("(")
  func_name = rest[:args_start]
  rest = rest[args_start + 1:]
  args = []
  while rest[0] != ")":
    cur_arg, rest = readDescriptor(rest)
    args.append(cur_arg)
  rest = rest[1:]
  return_type, rest = readDescriptor(rest)
  assert rest.strip() == "", desc
  return "{} {}.{}({})".format(
      TypeDescriptorToName(return_type), TypeDescriptorToName(class_name),
      func_name, ",".join(map(TypeDescriptorToName, args)))

def SingleNameToDescriptor(name):
  if name in DESC_MAP:
    return DESC_MAP[name]
  elif name.endswith("[]"):
    return "[" + SingleNameToDescriptor(name[:-2])
  elif name == "":
    return ""
  else:
    return "L" + name.replace(".", "/") + ";"


def NameToDescriptor(desc):
  return_name = desc.split()[0]
  name_and_args = desc.split()[1]
  args_start = name_and_args.index("(")
  names = name_and_args[0:args_start]
  meth_split = names.rfind(".")
  class_name = names[:meth_split]
  meth_name = names[meth_split + 1:]
  args = map(str.strip, name_and_args[args_start + 1:-1].split(","))
  return "{}->{}({}){}".format(
      SingleNameToDescriptor(class_name), meth_name,
      "".join(map(SingleNameToDescriptor, args)),
      SingleNameToDescriptor(return_name))


def main():
  parser = argparse.ArgumentParser(
      "method-to-descriptor.py",
      description="Convert a java method-name/stream into it's descriptor or vice-versa."
  )
  parser.add_argument(
      "-r",
      "--reverse",
      dest="reverse",
      action="store_true",
      default=False,
      help="reverse. Go from descriptor to method-declaration")
  parser.add_argument("method", help="what to change", nargs="*")
  args = parser.parse_args()
  if args.method != []:
    inputs = iter(args.method)
  else:
    inputs = GetStdinLineIter()
  for name in inputs:
    if args.reverse:
      print(DescriptorToName(name))
    else:
      print(NameToDescriptor(name))


if __name__ == "__main__":
  main()
