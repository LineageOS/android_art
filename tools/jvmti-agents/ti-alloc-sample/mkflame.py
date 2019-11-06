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
weights = {}

def get_size(thread_type_size):
  SIZE_STRING = "size["
  SIZE_STRING_LEN = len(SIZE_STRING)
  size_string = thread_type_size[thread_type_size.find(SIZE_STRING) + SIZE_STRING_LEN:]
  size_string = size_string[:size_string.find(",")]
  return int(size_string)

def add_definition_to_table(line):
  """
  Adds line to the list of definitions in table.
  """
  comma_pos = line.find(",")
  index = int(line[1:comma_pos])
  definition = line[comma_pos+1:]
  expanded_definition = ""
  weights[index] = 1
  if line[0:1] == "=":
    tokens = definition.split(";")
    # Pick the thread/type/size off the front (base) of the stack trace.
    thread_type_size = lookup_definition(int(tokens[0])).replace(";", ":")
    weights[index] = get_size(thread_type_size)
    del tokens[0]
    # Build the stack trace list.
    for token in tokens:
      # Replace semicolons by colons in the method entry signatures.
      method = lookup_definition(int(token)).replace(";", ":")
      if len(expanded_definition) > 0:
        expanded_definition += ";"
      expanded_definition += method
    # Add the thread/type/size as the top-most stack frame.
    if len(expanded_definition) > 0:
      expanded_definition += ";"
    expanded_definition += thread_type_size
    definition = expanded_definition
  table[index] = definition

def lookup_definition(index):
  """
  Returns the definition for "index" from table.
  """
  return table[index]

traces = {}
def record_stack_trace(string, count_or_size):
  """
  Remembers one stack trace index in the list of stack traces we have seen.
  Remembering a stack trace increments a count associated with the trace.
  """
  index = int(string)
  if count_or_size == "size":
    weight = weights[index]
  else:
    weight = 1
  if index in traces:
    count = traces[index]
    traces[index] = count + weight
  else:
    traces[index] = weight

def main(argv):
  count_or_size = argv[1]
  filename = argv[2]
  pagefile = open(filename, "r")
  current_allocation_trace = ""
  for line in pagefile:
    line = line.rstrip("\n")
    if line[0:1] == "=" or line[0:1] == "+":
      # definition.
      add_definition_to_table(line)
    else:
      # stack trace.
      record_stack_trace(line, count_or_size)
  # Dump all the traces, with count.
  for k, v in traces.items():
    definition = lookup_definition(k)
    if len(definition) == 0:
      # Zero length samples are discarded.
      return
    print(definition + " " + str(v))

if __name__ == '__main__':
  sys.exit(main(sys.argv))
