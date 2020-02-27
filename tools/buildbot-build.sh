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

set -e

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
common_targets="vogar core-tests apache-harmony-jdwp-tests-hostdex jsr166-tests libartpalette-system mockito-target"
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
  make_command+=" libnetd_client-target toybox toolbox sh"
  make_command+=" debuggerd su gdbserver"
  make_command+=" libstdc++ "
  make_command+=" ${ANDROID_PRODUCT_OUT#"${ANDROID_BUILD_TOP}/"}/system/etc/public.libraries.txt"
  if [[ -n "$ART_TEST_CHROOT" ]]; then
    # Targets required to generate a linker configuration on device within the
    # chroot environment.
    make_command+=" linkerconfig"
    # Additional targets needed for the chroot environment.
    make_command+=" crash_dump event-log-tags"
  fi
  # Build the Runtime (Bionic) APEX.
  make_command+=" com.android.runtime"
  # Build the Testing ART APEX (which is a superset of the Release and Debug ART APEXes).
  make_command+=" com.android.art.testing"
  # Build the bootstrap Bionic artifacts links (linker, libc, libdl, libm).
  # These targets create these symlinks:
  # - from /system/bin/linker(64) to /apex/com.android.runtime/bin/linker(64); and
  # - from /system/lib(64)/$lib to /apex/com.android.runtime/lib(64)/$lib.
  make_command+=" linker libc.bootstrap libdl.bootstrap libdl_android.bootstrap libm.bootstrap"
  # Build the Conscrypt APEX.
  make_command+=" com.android.conscrypt"
  # Build the i18n APEX.
  make_command+=" com.android.i18n"
  # Build the Time Zone Data APEX.
  make_command+=" com.android.tzdata"
fi

mode_specific_libraries="libjavacoretests libjdwp libwrapagentproperties libwrapagentpropertiesd"
for LIB in ${mode_specific_libraries} ; do
  make_command+=" $LIB${mode_suffix}"
done


echo "Executing $make_command"
# Disable path restrictions to enable luci builds using vpython.
eval "$make_command"

if [[ $mode == "target" ]]; then
  # Create canonical name -> file name symlink in the symbol directory for the
  # Testing ART APEX.
  #
  # This mimics the logic from `art/Android.mk`. We made the choice not to
  # implement this in `art/Android.mk`, as the Testing ART APEX is a test artifact
  # that should never ship with an actual product, and we try to keep it out of
  # standard build recipes
  #
  # TODO(b/141004137, b/129534335): Remove this, expose the Testing ART APEX in
  # the `art/Android.mk` build logic, and add absence checks (e.g. in
  # `build/make/core/main.mk`) to prevent the Testing ART APEX from ending up in a
  # system image.
  target_out_unstripped="$ANDROID_PRODUCT_OUT/symbols"
  link_name="$target_out_unstripped/apex/com.android.art"
  link_command="mkdir -p $(dirname "$link_name") && ln -sf com.android.art.testing \"$link_name\""
  echo "Executing $link_command"
  eval "$link_command"
  # Also provide access to symbols of binaries from the Runtime (Bionic) APEX,
  # e.g. to support debugging in GDB.
  find "$target_out_unstripped/apex/com.android.runtime/bin" -type f | while read target; do
    cmd="ln -sf $target $target_out_unstripped/system/bin/$(basename $target)"
    echo "Executing $cmd"
    eval "$cmd"
  done

  # Temporary fix for libjavacrypto.so dependencies in libcore and jvmti tests (b/147124225).
  conscrypt_apex="$ANDROID_PRODUCT_OUT/system/apex/com.android.conscrypt"
  conscrypt_libs="libjavacrypto.so libcrypto.so libssl.so"
  if [ ! -d "${conscrypt_apex}" ]; then
    echo -e "Missing conscrypt APEX in build output: ${conscrypt_apex}"
    exit 1
  fi
  for l in lib lib64; do
    if [ ! -d "${conscrypt_apex}/$l" ]; then
      continue
    fi
    for so in $conscrypt_libs; do
      src="${conscrypt_apex}/${l}/${so}"
      dst="$ANDROID_PRODUCT_OUT/system/${l}/${so}"
      if [ "${src}" -nt "${dst}" ]; then
        cmd="cp -p \"${src}\" \"${dst}\""
        echo "Executing $cmd"
        eval "$cmd"
      fi
    done
  done
fi
