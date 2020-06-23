#!/bin/bash
#
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

# This will build a target using linux_bionic. It can be called with normal make
# flags.
#
# TODO This runs a 'm clean' prior to building the targets in order to ensure
# that obsolete kati files don't mess up the build.

if [[ -z $ANDROID_BUILD_TOP ]]; then
  pushd .
else
  pushd $ANDROID_BUILD_TOP
fi

if [ ! -d art ]; then
  echo "Script needs to be run at the root of the android tree"
  exit 1
fi

soong_args=""

# Switch the build system to unbundled mode in the reduced manifest branch.
# TODO(b/159109002): Clean this up.
if [ ! -d frameworks/base ]; then
  soong_args="$soong_args TARGET_BUILD_UNBUNDLED=true"
fi

source build/envsetup.sh >&/dev/null # for get_build_var
# Soong needs a bunch of variables set and will not run if they are missing.
# The default values of these variables is only contained in make, so use
# nothing to create the variables then remove all the other artifacts.
# Lunch since it seems we cannot find the build-number otherwise.
lunch aosp_x86-eng
build/soong/soong_ui.bash --make-mode $soong_args nothing

if [ $? != 0 ]; then
  exit 1
fi

out_dir=$(get_build_var OUT_DIR)
host_out=$(get_build_var HOST_OUT)

# TODO(b/31559095) Figure out a better way to do this.
#
# There is no good way to force soong to generate host-bionic builds currently
# so this is a hacky workaround.
tmp_soong_var=$(mktemp --tmpdir soong.variables.bak.XXXXXX)
tmp_build_number=$(cat ${out_dir}/soong/build_number.txt)

cat $out_dir/soong/soong.variables > ${tmp_soong_var}

# See comment above about b/123645297 for why we cannot just do m clean. Clear
# out all files except for intermediates and installed files.
find $out_dir/ -maxdepth 1 -mindepth 1 \
               -not -name soong        \
               -not -name host         \
               -not -name target | xargs -I '{}' rm -rf '{}'
find $out_dir/soong/ -maxdepth 1 -mindepth 1   \
                     -not -name .intermediates \
                     -not -name host           \
                     -not -name target | xargs -I '{}' rm -rf '{}'

python3 <<END - ${tmp_soong_var} ${out_dir}/soong/soong.variables
import json
import sys
x = json.load(open(sys.argv[1]))
x['Allow_missing_dependencies'] = True
x['HostArch'] = 'x86_64'
x['CrossHost'] = 'linux_bionic'
x['CrossHostArch'] = 'x86_64'
if 'CrossHostSecondaryArch' in x:
  del x['CrossHostSecondaryArch']
if 'DexpreoptGlobalConfig' in x:
  del x['DexpreoptGlobalConfig']
json.dump(x, open(sys.argv[2], mode='w'))
END

rm $tmp_soong_var

# Write a new build-number
echo ${tmp_build_number}_SOONG_ONLY_BUILD > ${out_dir}/soong/build_number.txt

build/soong/soong_ui.bash --make-mode --skip-make $soong_args $@
