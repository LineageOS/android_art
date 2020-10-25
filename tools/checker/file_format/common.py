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


def split_stream(stream, fn_process_line, fn_line_outside_chunk):
  """ Reads the given input stream and splits it into chunks based on
      information extracted from individual lines.

  Arguments:
   - fnProcessLine: Called on each line with the text and line number. Must
     return a triplet, composed of the name of the chunk started on this line,
     the data extracted, and the name of the architecture this test applies to
     (or None to indicate that all architectures should run this test).
   - fnLineOutsideChunk: Called on attempt to attach data prior to creating
     a chunk.
  """
  line_no = 0
  all_chunks = []
  current_chunk = None

  for line in stream:
    line_no += 1
    line = line.strip()
    if not line:
      continue

    # Let the child class process the line and return information about it.
    # The _processLine method can modify the content of the line (or delete it
    # entirely) and specify whether it starts a new group.
    processed_line, new_chunk_name, test_arch = fn_process_line(line, line_no)
    # Currently, only a full chunk can be specified as architecture-specific.
    assert test_arch is None or new_chunk_name is not None
    if new_chunk_name is not None:
      current_chunk = (new_chunk_name, [], line_no, test_arch)
      all_chunks.append(current_chunk)
    if processed_line is not None:
      if current_chunk is not None:
        current_chunk[1].append(processed_line)
      else:
        fn_line_outside_chunk(line, line_no)
  return all_chunks
