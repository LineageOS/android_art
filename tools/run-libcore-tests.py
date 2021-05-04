#!/usr/bin/env python3
#
# [VPYTHON:BEGIN]
# python_version: "3.8"
# [VPYTHON:END]
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

import sys, os, argparse, subprocess, shlex, re, concurrent.futures, multiprocessing

def parse_args():
  parser = argparse.ArgumentParser(description="Run libcore tests using the vogar testing tool.")
  parser.add_argument('--mode', choices=['device', 'host', 'jvm'], required=True,
                      help='Specify where tests should be run.')
  parser.add_argument('--variant', choices=['X32', 'X64'],
                      help='Which dalvikvm variant to execute with.')
  parser.add_argument('-j', '--jobs', type=int,
                      help='Number of tests to run simultaneously.')
  parser.add_argument('--timeout', type=int,
                      help='How long to run the test before aborting (seconds).')
  parser.add_argument('--debug', action='store_true',
                      help='Use debug version of ART (device|host only).')
  parser.add_argument('--dry-run', action='store_true',
                      help='Print vogar command-line, but do not run.')
  parser.add_argument('--no-getrandom', action='store_false', dest='getrandom',
                      help='Ignore failures from getrandom() (for kernel < 3.17).')
  parser.add_argument('--no-jit', action='store_false', dest='jit',
                      help='Disable JIT (device|host only).')
  parser.add_argument('--gcstress', action='store_true',
                      help='Enable GC stress configuration (device|host only).')
  parser.add_argument('tests', nargs="*",
                      help='Name(s) of the test(s) to run')
  return parser.parse_args()

ART_TEST_ANDROID_ROOT = os.environ.get("ART_TEST_ANDROID_ROOT", "/system")
ART_TEST_CHROOT = os.environ.get("ART_TEST_CHROOT")
ANDROID_PRODUCT_OUT = os.environ.get("ANDROID_PRODUCT_OUT")

LIBCORE_TEST_NAMES = [
  # Naive critical path optimization: Run the longest tests first.
  "org.apache.harmony.tests.java.util",  # 90min under gcstress
  "libcore.java.lang",                   # 90min under gcstress
  "jsr166",                              # 60min under gcstress
  "libcore.java.util",                   # 60min under gcstress
  "libcore.java.math",                   # 50min under gcstress
  "org.apache.harmony.crypto",           # 30min under gcstress
  "org.apache.harmony.tests.java.io",    # 30min under gcstress
  "org.apache.harmony.tests.java.text",  # 30min under gcstress
  # Split highmemorytest to individual classes since it is too big.
  "libcore.highmemorytest.java.text.DateFormatTest",
  "libcore.highmemorytest.java.text.DecimalFormatTest",
  "libcore.highmemorytest.java.text.SimpleDateFormatTest",
  "libcore.highmemorytest.java.time.format.DateTimeFormatterTest",
  "libcore.highmemorytest.java.util.CalendarTest",
  "libcore.highmemorytest.java.util.CurrencyTest",
  "libcore.highmemorytest.libcore.icu.LocaleDataTest",
  # All other tests in alphabetical order.
  "libcore.android.system",
  "libcore.build",
  "libcore.dalvik.system",
  "libcore.java.awt",
  "libcore.java.text",
  "libcore.javax.crypto",
  "libcore.javax.net",
  "libcore.javax.security",
  "libcore.javax.sql",
  "libcore.javax.xml",
  "libcore.libcore.icu",
  "libcore.libcore.internal",
  "libcore.libcore.io",
  "libcore.libcore.net",
  "libcore.libcore.reflect",
  "libcore.libcore.util",
  "libcore.sun.invoke",
  "libcore.sun.misc",
  "libcore.sun.net",
  "libcore.sun.security",
  "libcore.sun.util",
  "libcore.xml",
  "org.apache.harmony.annotation",
  "org.apache.harmony.luni",
  "org.apache.harmony.nio",
  "org.apache.harmony.regex",
  "org.apache.harmony.testframework",
  "org.apache.harmony.tests.java.lang",
  "org.apache.harmony.tests.java.math",
  "org.apache.harmony.tests.javax.security",
  "tests.java.lang.String",
]
# "org.apache.harmony.security",  # We don't have rights to revert changes in case of failures.

# Note: This must start with the CORE_IMG_JARS in Android.common_path.mk
# because that's what we use for compiling the boot.art image.
# It may contain additional modules from TEST_CORE_JARS.
BOOT_CLASSPATH = [
  "/apex/com.android.art/javalib/core-oj.jar",
  "/apex/com.android.art/javalib/core-libart.jar",
  "/apex/com.android.art/javalib/okhttp.jar",
  "/apex/com.android.art/javalib/bouncycastle.jar",
  "/apex/com.android.art/javalib/apache-xml.jar",
  "/apex/com.android.i18n/javalib/core-icu4j.jar",
  "/apex/com.android.conscrypt/javalib/conscrypt.jar",
]

CLASSPATH = ["core-tests", "jsr166-tests", "mockito-target"]

def get_jar_filename(classpath):
  base_path = (ANDROID_PRODUCT_OUT + "/../..") if ANDROID_PRODUCT_OUT else "out/target"
  base_path = os.path.normpath(base_path)  # Normalize ".." components for readability.
  return f"{base_path}/common/obj/JAVA_LIBRARIES/{classpath}_intermediates/classes.jar"

def get_timeout_secs():
  default_timeout_secs = 600
  if args.mode == "device" and args.gcstress:
    default_timeout_secs = 1200
    if args.debug:
      default_timeout_secs = 1800
  return args.timeout or default_timeout_secs

def get_expected_failures():
  failures = ["art/tools/libcore_failures.txt"]
  if args.mode != "jvm":
    if args.gcstress:
      failures.append("art/tools/libcore_gcstress_failures.txt")
    if args.gcstress and args.debug:
      failures.append("art/tools/libcore_gcstress_debug_failures.txt")
    if args.debug and not args.gcstress and args.getrandom:
      failures.append("art/tools/libcore_debug_failures.txt")
    if not args.getrandom:
      failures.append("art/tools/libcore_fugu_failures.txt")
  return failures

def get_test_names():
  if args.tests:
    return args.tests
  test_names = list(LIBCORE_TEST_NAMES)
  # See b/78228743 and b/178351808.
  if args.gcstress or args.debug or args.mode == "jvm":
    test_names = list(t for t in test_names if not t.startswith("libcore.highmemorytest"))
  return test_names

def get_vogar_command(test_name):
  cmd = ["vogar"]
  if args.mode == "device":
    cmd.append("--mode=device --vm-arg -Ximage:/apex/com.android.art/javalib/boot.art")
    cmd.append("--vm-arg -Xbootclasspath:" + ":".join(BOOT_CLASSPATH))
  if args.mode == "host":
    # We explicitly give a wrong path for the image, to ensure vogar
    # will create a boot image with the default compiler. Note that
    # giving an existing image on host does not work because of
    # classpath/resources differences when compiling the boot image.
    cmd.append("--mode=host --vm-arg -Ximage:/non/existent/vogar.art")
  if args.mode == "jvm":
    cmd.append("--mode=jvm")
  if args.variant:
    cmd.append("--variant=" + args.variant)
  if args.gcstress:
    cmd.append("--vm-arg -Xgc:gcstress")
  if args.debug:
    cmd.append("--vm-arg -XXlib:libartd.so --vm-arg -XX:SlowDebug=true")

  if args.mode == "device":
    if ART_TEST_CHROOT:
      cmd.append(f"--chroot {ART_TEST_CHROOT} --device-dir=/tmp/vogar/test-{test_name}")
    else:
      cmd.append("--device-dir=/data/local/tmp/vogar/test-{test_name}")
    cmd.append(f"--vm-command={ART_TEST_ANDROID_ROOT}/bin/art")
  else:
    cmd.append(f"--device-dir=/tmp/vogar/test-{test_name}")

  if args.mode != "jvm":
    cmd.append("--timeout {}".format(get_timeout_secs()))

    # Suppress explicit gc logs that are triggered an absurd number of times by these tests.
    cmd.append("--vm-arg -XX:AlwaysLogExplicitGcs:false")
    cmd.append("--toolchain d8 --language CUR")
    if args.jit:
      cmd.append("--vm-arg -Xcompiler-option --vm-arg --compiler-filter=quicken")
    cmd.append("--vm-arg -Xusejit:{}".format(str(args.jit).lower()))

    if args.gcstress:
      # Bump pause threshold as long pauses cause explicit gc logging to occur irrespective
      # of -XX:AlwayLogExplicitGcs:false.
      cmd.append("--vm-arg -XX:LongPauseLogThreshold=15") # 15 ms (default: 5ms))

  # Suppress color codes if not attached to a terminal
  if not sys.stdout.isatty():
    cmd.append("--no-color")

  cmd.extend("--expectations " + f for f in get_expected_failures())
  cmd.extend("--classpath " + get_jar_filename(cp) for cp in CLASSPATH)
  cmd.append(test_name)
  return cmd

def get_target_cpu_count():
  adb_command = 'adb shell cat /sys/devices/system/cpu/present'
  with subprocess.Popen(adb_command.split(),
                        stderr=subprocess.STDOUT,
                        stdout=subprocess.PIPE,
                        universal_newlines=True) as proc:
    assert(proc.wait() == 0)  # Check the exit code.
    match = re.match(r'\d*-(\d*)', proc.stdout.read())
    assert(match)
    return int(match.group(1)) + 1  # Add one to convert from "last-index" to "count"

def main():
  global args
  args = parse_args()

  if not os.path.exists('build/envsetup.sh'):
    raise AssertionError("Script needs to be run at the root of the android tree")
  for jar in map(get_jar_filename, CLASSPATH):
    if not os.path.exists(jar):
      raise AssertionError(f"Missing {jar}. Run buildbot-build.sh first.")

  if not args.jobs:
    args.jobs = get_target_cpu_count() if args.mode == "device" else multiprocessing.cpu_count()

  def run_test(test_name):
    cmd = " ".join(get_vogar_command(test_name))
    if args.dry_run:
      return test_name, cmd, "Dry-run: skipping execution", 0
    with subprocess.Popen(shlex.split(cmd),
                          stderr=subprocess.STDOUT,
                          stdout=subprocess.PIPE,
                          universal_newlines=True) as proc:
      return test_name, cmd, proc.communicate()[0], proc.wait()

  failed_regex = re.compile(r"^.* FAIL \((?:EXEC_FAILED|ERROR)\)$", re.MULTILINE)
  failed_tests, max_exit_code = [], 0
  with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
    futures = [pool.submit(run_test, test_name) for test_name in get_test_names()]
    print(f"Running {len(futures)} tasks on {args.jobs} core(s)...\n")
    for i, future in enumerate(concurrent.futures.as_completed(futures)):
      test_name, cmd, stdout, exit_code = future.result()
      if exit_code != 0 or args.dry_run:
        print(cmd)
        print(stdout.strip())
      else:
        print(stdout.strip().split("\n")[-1])  # Vogar final summary line.
      failed_match = failed_regex.findall(stdout)
      failed_tests.extend(failed_match)
      max_exit_code = max(max_exit_code, exit_code)
      result = "PASSED" if exit_code == 0 else f"FAILED ({len(failed_match)} test(s) failed)"
      print(f"[{i+1}/{len(futures)}] Test set {test_name} {result}\n")
  print(f"Overall, {len(failed_tests)} test(s) failed:")
  print("\n".join(failed_tests))
  sys.exit(max_exit_code)

if __name__ == '__main__':
  main()
