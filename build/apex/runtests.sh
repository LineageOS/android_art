#!/bin/bash

# Copyright (C) 2018 The Android Open Source Project
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

# Run ART APEX tests.

SCRIPT_DIR=$(dirname $0)

# Status of whole test script.
exit_status=0
# Status of current test suite.
test_status=0

function say {
  echo "$0: $*"
}

function die {
  echo "$0: $*"
  exit 1
}

function setup_die {
  die "You need to source and lunch before you can use this script."
}

[[ -n "$ANDROID_BUILD_TOP" ]] || setup_die
[[ -n "$ANDROID_PRODUCT_OUT" ]] || setup_die
[[ -n "$ANDROID_HOST_OUT" ]] || setup_die

flattened_apex_p=$($ANDROID_BUILD_TOP/build/soong/soong_ui.bash --dumpvar-mode TARGET_FLATTEN_APEX)\
  || setup_die

# Switch the build system to unbundled mode in the reduced manifest branch.
# TODO(b/159109002): Clean this up.
if [ ! -d $ANDROID_BUILD_TOP/frameworks/base ]; then
  export TARGET_BUILD_UNBUNDLED=true
fi

have_debugfs_p=false
if $flattened_apex_p; then :; else
  if [ ! -e "$ANDROID_HOST_OUT/bin/debugfs" ] ; then
    say "Could not find debugfs, building now."
    build/soong/soong_ui.bash --make-mode debugfs-host || die "Cannot build debugfs"
  fi
  have_debugfs_p=true
fi

# Fail early.
set -e

build_apex_p=true
list_image_files_p=false
print_image_tree_p=false
print_file_sizes_p=false

function usage {
  cat <<EOF
Usage: $0 [OPTION]
Build (optional) and run tests on ART APEX package (on host).

  -B, --skip-build    skip the build step
  -l, --list-files    list the contents of the ext4 image (\`find\`-like style)
  -t, --print-tree    list the contents of the ext4 image (\`tree\`-like style)
  -s, --print-sizes   print the size in bytes of each file when listing contents
  -h, --help          display this help and exit

EOF
  exit
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    (-B|--skip-build)  build_apex_p=false;;
    (-l|--list-files)  list_image_files_p=true;;
    (-t|--print-tree)  print_image_tree_p=true;;
    (-s|--print-sizes) print_file_sizes_p=true;;
    (-h|--help) usage;;
    (*) die "Unknown option: '$1'
Try '$0 --help' for more information.";;
  esac
  shift
done

# build_apex APEX_MODULES
# -----------------------
# Build APEX packages APEX_MODULES.
function build_apex {
  if $build_apex_p; then
    say "Building $@" && build/soong/soong_ui.bash --make-mode "$@" || die "Cannot build $@"
  fi
}

# maybe_list_apex_contents_apex APEX TMPDIR [other]
function maybe_list_apex_contents_apex {
  local print_options=()
  if $print_file_sizes_p; then
    print_options+=(--size)
  fi

  # List the contents of the apex in list form.
  if $list_image_files_p; then
    say "Listing image files"
    $SCRIPT_DIR/art_apex_test.py --list ${print_options[@]} $@
  fi

  # List the contents of the apex in tree form.
  if $print_image_tree_p; then
    say "Printing image tree"
    $SCRIPT_DIR/art_apex_test.py --tree ${print_options[@]} $@
  fi
}

function fail_check {
  echo "$0: FAILED: $*"
  test_status=1
  exit_status=1
}

# Test all modules, if possible.

apex_modules=(
  "com.android.art.release"
  "com.android.art.debug"
  "com.android.art.testing"
)
if [[ "$HOST_PREFER_32_BIT" = true ]]; then
  say "Skipping com.android.art.host, as \`HOST_PREFER_32_BIT\` equals \`true\`"
else
  apex_modules+=("com.android.art.host")
fi

# Build the APEX packages (optional).
build_apex ${apex_modules[@]}

# Clean-up.
function cleanup {
  rm -rf "$work_dir"
}

# Garbage collection.
function finish {
  # Don't fail early during cleanup.
  set +e
  cleanup
}

for apex_module in ${apex_modules[@]}; do
  test_status=0
  say "Checking APEX package $apex_module"
  work_dir=$(mktemp -d)
  trap finish EXIT

  art_apex_test_args="--tmpdir $work_dir"
  test_only_args=""
  if [[ $apex_module = *.host ]]; then
    apex_path="$ANDROID_HOST_OUT/apex/${apex_module}.zipapex"
    art_apex_test_args="$art_apex_test_args --host"
    test_only_args="--flavor debug"
  else
    if $flattened_apex_p; then
      apex_path="$ANDROID_PRODUCT_OUT/system/apex/${apex_module}"
      art_apex_test_args="$art_apex_test_args --flattened"
    else
      apex_path="$ANDROID_PRODUCT_OUT/system/apex/${apex_module}.apex"
    fi
    if $have_debugfs_p; then
      art_apex_test_args="$art_apex_test_args --debugfs $ANDROID_HOST_OUT/bin/debugfs"
    fi
    case $apex_module in
      (*.release) test_only_args="--flavor release";;
      (*.debug)   test_only_args="--flavor debug";;
      (*.testing) test_only_args="--flavor testing";;
    esac
  fi
  say "APEX package path: $apex_path"

  # List the contents of the APEX image (optional).
  maybe_list_apex_contents_apex $art_apex_test_args $apex_path

  # Run tests on APEX package.
  $SCRIPT_DIR/art_apex_test.py $art_apex_test_args $test_only_args $apex_path \
    || fail_check "Checks failed on $apex_module"

  # Clean up.
  trap - EXIT
  cleanup

  [[ "$test_status" = 0 ]] && say "$apex_module tests passed"
  echo
done

[[ "$exit_status" = 0 ]] && say "All ART APEX tests passed"

exit $exit_status
