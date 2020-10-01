/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The main purpose of this test is to ensure this C header compiles in C, so
 * that no C++ features inadvertently leak into the C ABI. */
#include "art_api/dex_file_external.h"

static const char gtest_output_arg[] = "--gtest_output=xml:";
static const char gtest_output_xml[] = "\
<?xml version=\"1.0\"?>\n\
<testsuites tests=\"1\" failures=\"0\" disabled=\"0\" errors=\"0\" name=\"AllTests\">\n\
  <testsuite tests=\"1\" failures=\"0\" disabled=\"0\" errors=\"0\" name=\"NopTest\">\n\
    <testcase name=\"nop\" status=\"run\" />\n\
  </testsuite>\n\
</testsuites>";

static const char canned_stdout[] = "\
[==========] Running 1 test from 1 test suite.\n\
[----------] 1 test from NopTest\n\
[ RUN      ] NopTest.nop\n\
[       OK ] NopTest.nop (0 ms)\n\
[----------] 1 test from NopTest (0 ms total)\n\
\n\
[==========] 1 test from 1 test suite ran. (0 ms total)\n\
[  PASSED  ] 1 test.\n\
";

/* Writes a fake gtest xml report to the given path. */
static int write_gtest_output_xml(char* gtest_output_path) {
  FILE* output_fd = fopen(gtest_output_path, "w");
  if (output_fd == NULL) {
    fprintf(stderr, "Failed to open %s: %s\n", gtest_output_path, strerror(errno));
    return 1;
  }
  if (fprintf(output_fd, gtest_output_xml) != sizeof(gtest_output_xml) - 1) {
    fprintf(stderr, "Failed to write %s: %s\n", gtest_output_path, strerror(errno));
    fclose(output_fd);
    return 1;
  }
  if (fclose(output_fd) != 0) {
    fprintf(stderr, "Failed to close %s: %s\n", gtest_output_path, strerror(errno));
    return 1;
  }
  return 0;
}

int main(int argc, char** argv) {
  int i;
  for (i = 1; i < argc; ++i) {
    if (strncmp(argv[i], gtest_output_arg, sizeof(gtest_output_arg) - 1) == 0) {
      /* The ART gtest framework expects all tests to understand --gtest_output. */
      if (write_gtest_output_xml(argv[i] + sizeof(gtest_output_arg) - 1)) {
        return 1;
      }
    }
  }
  /* Tradefed parses the output, so send something passable there. */
  if (fputs(canned_stdout, stdout) < 0) {
    return 1;
  }
  return 0;
}
