#!/bin/bash
#
# Copyright (C) 2014 The Android Open Source Project
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

# Exit on errors.
set -e

if [ ! -d libcore ]; then
  echo "Script needs to be run at the root of the android tree"
  exit 1
fi

# "Root" (actually "system") directory on device (in the case of
# target testing).
android_root=${ART_TEST_ANDROID_ROOT:-/system}

function classes_jar_path {
  local var="$1"
  local suffix="jar"
  if [ -z "$ANDROID_PRODUCT_OUT" ] ; then
    local java_libraries=out/target/common/obj/JAVA_LIBRARIES
  else
    local java_libraries=${ANDROID_PRODUCT_OUT}/../../common/obj/JAVA_LIBRARIES
  fi
  echo "${java_libraries}/${var}_intermediates/classes.${suffix}"
}

function cparg {
  for var
  do
    printf -- "--classpath $(classes_jar_path "$var") ";
  done
}

function boot_classpath_arg {
  local dir="$1"
  shift 1
  printf -- "--vm-arg -Xbootclasspath"
  for var
  do
    printf -- ":${dir}/${var}.jar";
  done
  printf -- ":/apex/com.android.i18n/javalib/core-icu4j.jar:/apex/com.android.conscrypt/javalib/conscrypt.jar";
}

function usage {
  local me=$(basename "${BASH_SOURCE[0]}")
  (
    cat << EOF
  Usage: ${me} --mode=<mode> [options] [-- <package_to_test> ...]

  Run libcore tests using the vogar testing tool.

  Required parameters:
    --mode=device|host|jvm Specify where tests should be run.

  Optional parameters:
    --debug                Use debug version of ART (device|host only).
    --dry-run              Print vogar command-line, but do not run.
    --no-getrandom         Ignore failures from getrandom() (for kernel < 3.17).
    --no-jit               Disable JIT (device|host only).
    --Xgc:gcstress         Enable GC stress configuration (device|host only).

  The script passes unrecognized options to the command-line created for vogar.

  The script runs a hardcoded list of libcore test packages by default. The user
  may run a subset of packages by appending '--' followed by a list of package
  names.

  Examples:

    1. Run full test suite on host:
      ${me} --mode=host

    2. Run full test suite on device:
      ${me} --mode=device

    3. Run tests only from the libcore.java.lang package on device:
      ${me} --mode=device -- libcore.java.lang
EOF
  ) | sed -e 's/^  //' >&2 # Strip leading whitespace from heredoc.
}

# Packages that currently work correctly with the expectation files.
working_packages=("libcore.android.system"
                  "libcore.build"
                  "libcore.dalvik.system"
                  "libcore.java.awt"
                  "libcore.java.lang"
                  "libcore.java.math"
                  "libcore.java.text"
                  "libcore.java.util"
                  "libcore.javax.crypto"
                  "libcore.javax.net"
                  "libcore.javax.security"
                  "libcore.javax.sql"
                  "libcore.javax.xml"
                  "libcore.libcore.internal"
                  "libcore.libcore.io"
                  "libcore.libcore.net"
                  "libcore.libcore.reflect"
                  "libcore.libcore.util"
                  "libcore.libcore.timezone"
                  "libcore.sun.invoke"
                  "libcore.sun.net"
                  "libcore.sun.misc"
                  "libcore.sun.security"
                  "libcore.sun.util"
                  "libcore.xml"
                  "org.apache.harmony.annotation"
                  "org.apache.harmony.crypto"
                  "org.apache.harmony.luni"
                  "org.apache.harmony.nio"
                  "org.apache.harmony.regex"
                  "org.apache.harmony.testframework"
                  "org.apache.harmony.tests.java.io"
                  "org.apache.harmony.tests.java.lang"
                  "org.apache.harmony.tests.java.math"
                  "org.apache.harmony.tests.java.util"
                  "org.apache.harmony.tests.java.text"
                  "org.apache.harmony.tests.javax.security"
                  "tests.java.lang.String"
                  "jsr166")

# List of packages we could run, but don't have rights to revert
# changes in case of failures.
# "org.apache.harmony.security"

#
# Setup environment for running tests.
#
source build/envsetup.sh >&/dev/null # for get_build_var, setpaths
setpaths # include platform prebuilt java, javac, etc in $PATH.

# Note: This must start with the CORE_IMG_JARS in Android.common_path.mk
# because that's what we use for compiling the core.art image.
# It may contain additional modules from TEST_CORE_JARS.
BOOT_CLASSPATH_JARS="core-oj core-libart okhttp bouncycastle apache-xml"

DEPS="core-tests jsr166-tests mockito-target"

for lib in $DEPS
do
  if [[ ! -f "$(classes_jar_path "$lib")" ]]; then
    echo "${lib} is missing. Before running, you must run art/tools/buildbot-build.sh"
    exit 1
  fi
done

#
# Defaults affected by command-line parsing
#

# Use JIT compiling by default.
use_jit=true

gcstress=false
debug=false
dry_run=false

# Run tests that use the getrandom() syscall? (Requires Linux 3.17+).
getrandom=true

# Execution mode specifies where to run tests (device|host|jvm).
execution_mode=""

# Default expectations file.
expectations="--expectations art/tools/libcore_failures.txt"

vogar_args=""
while [ -n "$1" ]; do
  case "$1" in
    --mode=device)
      vogar_args="$vogar_args --mode=device"
      vogar_args="$vogar_args --vm-arg -Ximage:/apex/com.android.art/javalib/boot.art"
      vogar_args="$vogar_args $(boot_classpath_arg /apex/com.android.art/javalib $BOOT_CLASSPATH_JARS)"
      execution_mode="device"
      ;;
    --mode=host)
      # We explicitly give a wrong path for the image, to ensure vogar
      # will create a boot image with the default compiler. Note that
      # giving an existing image on host does not work because of
      # classpath/resources differences when compiling the boot image.
      vogar_args="$vogar_args $1 --vm-arg -Ximage:/non/existent/vogar.art"
      execution_mode="host"
      ;;
    --mode=jvm)
      vogar_args="$vogar_args $1"
      execution_mode="jvm"
      ;;
    --no-getrandom)
      getrandom=false
      ;;
    --no-jit)
      use_jit=false
      ;;
    --debug)
      vogar_args="$vogar_args --vm-arg -XXlib:libartd.so --vm-arg -XX:SlowDebug=true"
      debug=true
      ;;
    -Xgc:gcstress)
      vogar_args="$vogar_args $1"
      gcstress=true
      ;;
    --dry-run)
      dry_run=true
      ;;
    --)
      shift
      # Assume remaining elements are packages to test.
      user_packages=("$@")
      break
      ;;
    --help)
      usage
      exit 1
      ;;
    *)
      vogar_args="$vogar_args $1"
      ;;
  esac
  shift
done

if [ -z "$execution_mode" ]; then
  usage
  exit 1
fi

# Default timeout, gets overridden on device under gcstress.
timeout_secs=480

if [ $execution_mode = "device" ]; then
  # Honor environment variable ART_TEST_CHROOT.
  if [[ -n "$ART_TEST_CHROOT" ]]; then
    # Set Vogar's `--chroot` option.
    vogar_args="$vogar_args --chroot $ART_TEST_CHROOT"
    vogar_args="$vogar_args --device-dir=/tmp"
  else
    # When not using a chroot on device, set Vogar's work directory to
    # /data/local/tmp.
    vogar_args="$vogar_args --device-dir=/data/local/tmp"
  fi
  vogar_args="$vogar_args --vm-command=$android_root/bin/art"

  # Increase the timeout, as vogar cannot set individual test
  # timeout when being asked to run packages, and some tests go above
  # the default timeout.
  if $gcstress; then
    if $debug; then
      timeout_secs=1440
    else
      timeout_secs=900
    fi
  fi
fi  # $execution_mode = "device"

if [ $execution_mode = "device" -o $execution_mode = "host" ]; then
  # Add timeout to vogar command-line.
  vogar_args="$vogar_args --timeout $timeout_secs"

  # set the toolchain to use.
  vogar_args="$vogar_args --toolchain d8 --language CUR"

  # JIT settings.
  if $use_jit; then
    vogar_args="$vogar_args --vm-arg -Xcompiler-option --vm-arg --compiler-filter=quicken"
  fi
  vogar_args="$vogar_args --vm-arg -Xusejit:$use_jit"

  # gcstress may lead to timeouts, so we need dedicated expectations files for it.
  if $gcstress; then
    expectations="$expectations --expectations art/tools/libcore_gcstress_failures.txt"
    if $debug; then
      expectations="$expectations --expectations art/tools/libcore_gcstress_debug_failures.txt"
    fi
  else
    # We only run this package when user has not specified packages
    # to run and not under gcstress as it can cause timeouts. See
    # b/78228743.
    working_packages+=("libcore.libcore.icu")
  fi

  if $getrandom; then :; else
    # Ignore failures in tests that use the system calls not supported
    # on fugu (Nexus Player, kernel version Linux 3.10).
    expectations="$expectations --expectations art/tools/libcore_fugu_failures.txt"
  fi
fi

if [ ! -t 1 ] ; then
  # Suppress color codes if not attached to a terminal
  vogar_args="$vogar_args --no-color"
fi

# Override working_packages if user provided specific packages to
# test.
if [[ ${#user_packages[@]} != 0 ]] ; then
  working_packages=("${user_packages[@]}")
fi

# Run the tests using vogar.
echo "Running tests for the following test packages:"
echo ${working_packages[@]} | tr " " "\n"

cmd="vogar $vogar_args $expectations $(cparg $DEPS) ${working_packages[@]}"
echo "Running $cmd"
$dry_run || eval $cmd
