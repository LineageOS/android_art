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

import argparse
import sys

class TraceCollection:
  def __init__(self, args):
    self.args = args
    # A table indexed by number and containing the definition for that number.
    self.definitions = {}
    # The "weight" of a stack trace, either 1 for counting or the size of the allocation.
    self.weights = {}
    # The count for each individual allocation.
    self.allocation_count = {}

  def definition(self, index):
    """
    Returns the definition for "index".
    """
    return self.definitions[index]

  def set_definition(self, index, definition):
    """
    Sets the definition for "index".
    """
    self.definitions[index] = definition

  def weight(self, index):
    """
    Returns the weight for "index".
    """
    return self.weights[index]

  def set_weight(self, index, weight):
    """
    Sets the weight for "index".
    """
    self.weights[index] = weight

  def read_file(self, filename):
    """
    Reads a file into a DefinitionTable.
    """
    def process_definition(line):
      """
      Adds line to the list of definitions in table.
      """
      def expand_stack_trace(definition):
        """
        Converts a semicolon-separated list of numbers into the text stack trace.
        """
        def get_allocation_thread(thread_type_size):
          """
          Returns the thread of an allocation from the thread/type/size record.
          """
          THREAD_STRING = "thread["
          THREAD_STRING_LEN = len(THREAD_STRING)
          thread_string = thread_type_size[thread_type_size.find(THREAD_STRING) +
                                           THREAD_STRING_LEN:]
          return thread_string[:thread_string.find("]")]

        def get_allocation_type(thread_type_size):
          """
          Returns the type of an allocation from the thread/type/size record.
          """
          TYPE_STRING = "jclass["
          TYPE_STRING_LEN = len(TYPE_STRING)
          type_string = thread_type_size[thread_type_size.find(TYPE_STRING) + TYPE_STRING_LEN:]
          return type_string[:type_string.find(" ")]

        def get_allocation_size(thread_type_size):
          """
          Returns the size of an allocation from the thread/type/size record.
          """
          SIZE_STRING = "size["
          SIZE_STRING_LEN = len(SIZE_STRING)
          size_string = thread_type_size[thread_type_size.find(SIZE_STRING) + SIZE_STRING_LEN:]
          size_string = size_string[:size_string.find(",")]
          return int(size_string)

        def get_top_and_weight(index):
          thread_type_size = self.definition(int(tokens[0]))
          size = get_allocation_size(thread_type_size)
          if self.args.type_only:
            thread_type_size = get_allocation_type(thread_type_size)
          elif self.args.thread_only:
            thread_type_size = get_allocation_thread(thread_type_size)
          return (thread_type_size, size)

        tokens = definition.split(";")
        # The first element (base) of the stack trace is the thread/type/size.
        # Get the weight (either 1 or the number of bytes allocated).
        (thread_type_size, weight) = get_top_and_weight(int(tokens[0]))
        self.set_weight(index, weight)
        # Remove the thread/type/size from the base of the stack trace.
        del tokens[0]
        # Build the stack trace list.
        expanded_definition = ""
        for i in range(len(tokens)):
          if self.args.depth_limit > 0 and i >= self.args.depth_limit:
            break
          token = tokens[i]
          # Replace semicolons by colons in the method entry signatures.
          method = self.definition(int(token)).replace(";", ":")
          if len(expanded_definition) > 0:
            expanded_definition += ";"
          expanded_definition += method
        if not self.args.ignore_type:
          # Add the thread/type/size as the top-most stack frame.
          if len(expanded_definition) > 0:
            expanded_definition += ";"
          expanded_definition += thread_type_size.replace(";", ":")
        if self.args.reverse_stack:
          def_list = expanded_definition.split(";")
          expanded_definition = ";".join(def_list[::-1])
        return expanded_definition

      # If the line contains a comma, it is of the form [+=]index,definition,
      # where index is a string containing an integer, and definition is the
      # value represented by the integer whenever it is used later.
      # * Lines starting with + are either a thread/type/size record or a single
      #   stack frame.  These are simply interned in the table.
      # * Those starting with = are stack traces, and contain a sequence of
      #   numbers separated by semicolon.  These are "expanded" and then interned.
      comma_pos = line.find(",")
      index = int(line[1:comma_pos])
      definition = line[comma_pos+1:]
      if line[0:1] == "=":
        definition = expand_stack_trace(definition)
      # Intern the definition in the table.
      #if len(definition) == 0:
        # Zero length samples are errors and are discarded.
        #print("ERROR: definition for " + str(index) + " is empty")
        #return
      self.set_definition(index, definition)

    def process_trace(index):
      """
      Remembers one stack trace in the list of stack traces we have seen.
      Remembering a stack trace increments a count associated with the trace.
      """
      trace = self.definition(index)
      if self.args.use_size:
        weight = self.weight(index)
      else:
        weight = 1
      if trace in self.allocation_count:
        self.allocation_count[trace] = self.allocation_count[trace] + weight
      else:
        self.allocation_count[trace] = weight

    # Read the file, processing each line as a definition or stack trace.
    tracefile = open(filename, "r")
    current_allocation_trace = ""
    for line in tracefile:
      line = line.rstrip("\n")
      if line[0:1] == "=" or line[0:1] == "+":
        # definition.
        process_definition(line)
      else:
        # stack trace.
        process_trace(int(line))

  def dump_flame_graph(self):
    """
    Prints out a stack trace format compatible with flame graph creation utilities.
    """
    for definition, weight in self.allocation_count.items():
      print(definition + " " + str(weight))

def parse_options():
  parser = argparse.ArgumentParser(description="Convert a trace to a form usable for flame graphs.")
  parser.add_argument("filename", help="The trace file as input", type=str)
  parser.add_argument("--use_size", help="Count by allocation size", action="store_true",
                      default=False)
  parser.add_argument("--ignore_type", help="Ignore type of allocation", action="store_true",
                      default=False)
  parser.add_argument("--reverse_stack", help="Reverse root and top of stacks", action="store_true",
                      default=False)
  parser.add_argument("--type_only", help="Only consider allocation type", action="store_true",
                      default=False)
  parser.add_argument("--thread_only", help="Only consider allocation thread", action="store_true",
                      default=False)
  parser.add_argument("--depth_limit", help="Limit the length of a trace", type=int, default=0)
  args = parser.parse_args()
  return args

def main(argv):
  args = parse_options()
  trace_collection = TraceCollection(args)
  trace_collection.read_file(args.filename)
  trace_collection.dump_flame_graph()

if __name__ == '__main__':
  sys.exit(main(sys.argv))
