#!/usr/bin/python3
#
# Copyright 2019, The Android Open Source Project
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

"""
Usage: mkflame.py <jvmti_trace_file>
"""

import sys

table = {}

def add_definition_to_table(line):
  """
  Adds line to the list of definitions in table.
  """
  comma_pos = line.find(",")
  index = int(line[1:comma_pos])
  definition = line[comma_pos+1:]
  if line[0:1] == "=":
    # Skip the type/size prefix for flame graphs.
    semi_pos = definition.find(";")
    definition = definition[semi_pos + 1:]
    # Expand stack frame definitions to be a semicolon-separated list of stack
    # frame methods.
    expanded_definition = ""
    while definition != "":
      semi_pos = definition.find(";")
      if semi_pos == -1:
        method_index = int(definition)
        definition = ""
      else:
        method_index = int(definition[:semi_pos])
        definition = definition[semi_pos + 1:]
      # Replace semicolons by colons in the method entry signatures.
      method = lookup_definition(method_index).replace(";", ":")
      expanded_definition += ";" + method
    definition = expanded_definition
  table[index] = definition

def lookup_definition(index):
  """
  Returns the definition for "index" from table.
  """
  return table[index]

traces = {}
def record_stack_trace(string):
  """
  Remembers one stack trace index in the list of stack traces we have seen.
  Remembering a stack trace increments a count associated with the trace.
  """
  index = int(string)
  if index in traces:
    count = traces[index]
    traces[index] = count + 1
  else:
    traces[index] = 1

def main(argv):
  filename = argv[1]
  pagefile = open(filename, "r")
  current_allocation_trace = ""
  for line in pagefile:
    args = line.split()
    line = line.rstrip("\n")
    if line[0:1] == "=" or line[0:1] == "+":
      # definition.
      add_definition_to_table(line)
    else:
      # stack trace.
      record_stack_trace(line)
  # Dump all the traces, with count.
  for k, v in traces.items():
    print(lookup_definition(k) + " " + str(v))

if __name__ == '__main__':
  sys.exit(main(sys.argv))
