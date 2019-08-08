#! /bin/bash
#
# Copyright (C) 2015 The Android Open Source Project
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

if [ ! -d art ]; then
  echo "Script needs to be run at the root of the android tree"
  exit 1
fi

source build/envsetup.sh >&/dev/null # for get_build_var

# Logic for setting out_dir from build/make/core/envsetup.mk:
if [[ -z $OUT_DIR ]]; then
  if [[ -z $OUT_DIR_COMMON_BASE ]]; then
    out_dir=out
  else
    out_dir=${OUT_DIR_COMMON_BASE}/${PWD##*/}
  fi
else
  out_dir=${OUT_DIR}
fi

java_libraries_dir=${out_dir}/target/common/obj/JAVA_LIBRARIES
common_targets="vogar core-tests apache-harmony-jdwp-tests-hostdex jsr166-tests mockito-target"
mode="target"
j_arg="-j$(nproc)"
showcommands=
make_command=

while true; do
  if [[ "$1" == "--host" ]]; then
    mode="host"
    shift
  elif [[ "$1" == "--target" ]]; then
    mode="target"
    shift
  elif [[ "$1" == -j* ]]; then
    j_arg=$1
    shift
  elif [[ "$1" == "--showcommands" ]]; then
    showcommands="showcommands"
    shift
  elif [[ "$1" == "" ]]; then
    break
  else
    echo "Unknown options $@"
    exit 1
  fi
done

# Allow to build successfully in master-art.
extra_args="SOONG_ALLOW_MISSING_DEPENDENCIES=true TEMPORARY_DISABLE_PATH_RESTRICTIONS=true"

if [[ $mode == "host" ]]; then
  make_command="build/soong/soong_ui.bash --make-mode $j_arg $extra_args $showcommands build-art-host-tests $common_targets"
  make_command+=" dx-tests junit-host"
  mode_suffix="-host"
elif [[ $mode == "target" ]]; then
  if [[ -z "${ANDROID_PRODUCT_OUT}" ]]; then
    echo 'ANDROID_PRODUCT_OUT environment variable is empty; did you forget to run `lunch`?'
    exit 1
  fi
  make_command="build/soong/soong_ui.bash --make-mode $j_arg $extra_args $showcommands build-art-target-tests $common_targets"
  make_command+=" libjavacrypto-target libnetd_client-target toybox toolbox sh"
  make_command+=" debuggerd su"
  make_command+=" libstdc++ "
  make_command+=" ${ANDROID_PRODUCT_OUT#"${ANDROID_BUILD_TOP}/"}/system/etc/public.libraries.txt"
  if [[ -n "$ART_TEST_CHROOT" ]]; then
    # These targets are needed for the chroot environment.
    make_command+=" crash_dump event-log-tags"
  fi
  # Build the Testing Runtime APEX (which is a superset of the Release and Debug Runtime APEXes).
  make_command+=" com.android.runtime.testing"
  # Build the system linker configuration, which is needed to use the
  # Runtime APEX's linker configuration.
  make_command+=" ld.config.txt "
  # Build the bootstrap Bionic artifacts links (linker, libc, libdl, libm).
  # These targets create these symlinks:
  # - from /system/bin/linker(64) to /apex/com.android.runtime/bin/linker(64); and
  # - from /system/lib(64)/$lib to /apex/com.android.runtime/lib(64)/$lib.
  make_command+=" linker libc.bootstrap libdl.bootstrap libm.bootstrap"
  # Build the i18n APEX.
  make_command+=" com.android.i18n"
  # Build the Time Zone Data APEX.
  make_command+=" com.android.tzdata"
  mode_suffix="-target"
fi

mode_specific_libraries="libjavacoretests libjdwp libwrapagentproperties libwrapagentpropertiesd"
for LIB in ${mode_specific_libraries} ; do
  make_command+=" $LIB${mode_suffix}"
done



echo "Executing $make_command"
# Disable path restrictions to enable luci builds using vpython.
bash -c "$make_command"
