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
extra_args="SOONG_ALLOW_MISSING_DEPENDENCIES=true"

# Switch the build system to unbundled mode in the reduced manifest branch.
# TODO(b/159109002): Clean this up.
if [ ! -d frameworks/base ]; then
  extra_args="$extra_args TARGET_BUILD_UNBUNDLED=true"
fi

apexes=(
  "com.android.art.testing"
  "com.android.conscrypt"
  "com.android.i18n"
  "com.android.runtime"
  "com.android.tzdata"
)

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
  make_command+=" libnetd_client-target toybox sh"
  make_command+=" debuggerd su gdbserver"
  # vogar requires the class files for conscrypt.
  make_command+=" conscrypt"
  make_command+=" ${ANDROID_PRODUCT_OUT#"${ANDROID_BUILD_TOP}/"}/system/etc/public.libraries.txt"
  # Targets required to generate a linker configuration for device within the
  # chroot environment. The *.libraries.txt targets are required by
  # linkerconfig but not included in host_linkerconfig_all_targets. We cannot
  # use linkerconfig, because building the device binary statically might not
  # work in an unbundled tree.
  make_command+=" host_linkerconfig_all_targets sanitizer.libraries.txt vndkcorevariant.libraries.txt"
  # Additional targets needed for the chroot environment.
  make_command+=" event-log-tags"
  # Needed to extract prebuilt APEXes.
  make_command+=" deapexer"
  # Build/install the required APEXes.
  make_command+=" ${apexes[*]}"
fi

mode_specific_libraries="libjavacoretests libjdwp libwrapagentproperties libwrapagentpropertiesd"
for LIB in ${mode_specific_libraries} ; do
  make_command+=" $LIB${mode_suffix}"
done


echo "Executing $make_command"
# Disable path restrictions to enable luci builds using vpython.
eval "$make_command"

if [[ $mode == "target" ]]; then
  if [[ -z "${ANDROID_HOST_OUT}" ]]; then
    echo "ANDROID_HOST_OUT environment variable is empty; using $out_dir/host/linux-x86"
    ANDROID_HOST_OUT=$out_dir/host/linux-x86
  fi

  # Extract prebuilt APEXes.
  debugfs=$ANDROID_HOST_OUT/bin/debugfs_static
  for apex in ${apexes[@]}; do
    dir="$ANDROID_PRODUCT_OUT/system/apex/${apex}"
    file="$ANDROID_PRODUCT_OUT/system/apex/${apex}.apex"
    if [ -f "${file}" ]; then
      echo "Extracting APEX file: ${apex}"
      rm -rf $dir
      mkdir -p $dir
      $ANDROID_HOST_OUT/bin/deapexer --debugfs_path $debugfs extract $file $dir
    fi
  done

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
  conscrypt_dir="$ANDROID_PRODUCT_OUT/system/apex/com.android.conscrypt"
  conscrypt_libs="libjavacrypto.so libcrypto.so libssl.so"
  if [ ! -d "${conscrypt_dir}" ]; then
    echo -e "Missing conscrypt APEX in build output: ${conscrypt_dir}"
    exit 1
  fi
  if [ ! -f "${conscrypt_dir}/javalib/conscrypt.jar" ]; then
    echo -e "Missing conscrypt jar in build output: ${conscrypt_dir}"
    exit 1
  fi
  for l in lib lib64; do
    if [ ! -d "$ANDROID_PRODUCT_OUT/system/$l" ]; then
      continue
    fi
    for so in $conscrypt_libs; do
      src="${conscrypt_dir}/${l}/${so}"
      dst="$ANDROID_PRODUCT_OUT/system/${l}/${so}"
      if [ "${src}" -nt "${dst}" ]; then
        cmd="cp -p \"${src}\" \"${dst}\""
        echo "Executing $cmd"
        eval "$cmd"
      fi
    done
  done

  # TODO(b/159355595): Ensure there is a tzdata in system to avoid warnings on
  # stderr from Bionic.
  if [ ! -f $ANDROID_PRODUCT_OUT/system/usr/share/zoneinfo/tzdata ]; then
    mkdir -p $ANDROID_PRODUCT_OUT/system/usr/share/zoneinfo
    cp $ANDROID_PRODUCT_OUT/system/apex/com.android.tzdata/etc/tz/tzdata \
      $ANDROID_PRODUCT_OUT/system/usr/share/zoneinfo/tzdata
  fi

  # Create system symlinks for the Runtime APEX. Normally handled by
  # installSymlinkToRuntimeApex in soong/cc/binary.go, but we have to replicate
  # it here since we don't run the install rules for the Runtime APEX.
  for b in linker{,_asan}{,64}; do
    echo "Symlinking /apex/com.android.runtime/bin/$b to /system/bin"
    ln -sf /apex/com.android.runtime/bin/$b $ANDROID_PRODUCT_OUT/system/bin/$b
  done
  for p in $ANDROID_PRODUCT_OUT/system/apex/com.android.runtime/lib{,64}/bionic/*; do
    lib_dir=$(expr $p : '.*/\(lib[0-9]*\)/.*')
    lib_file=$(basename $p)
    src=/apex/com.android.runtime/${lib_dir}/bionic/${lib_file}
    dst=$ANDROID_PRODUCT_OUT/system/${lib_dir}/${lib_file}
    echo "Symlinking $src into /system/${lib_dir}"
    mkdir -p $(dirname $dst)
    ln -sf $src $dst
  done

  # Create linker config files. We run linkerconfig on host to avoid problems
  # building it statically for device in an unbundled tree.

  # temporary root for linkerconfig
  linkerconfig_root=$ANDROID_PRODUCT_OUT/art_linkerconfig_root

  rm -rf $linkerconfig_root

  # Linkerconfig reads files from /system/etc
  mkdir -p $linkerconfig_root/system
  cp -r $ANDROID_PRODUCT_OUT/system/etc $linkerconfig_root/system

  # For linkerconfig to pick up the APEXes correctly we need to make them
  # available in $linkerconfig_root/apex.
  mkdir -p $linkerconfig_root/apex
  for apex in ${apexes[@]}; do
    src="$ANDROID_PRODUCT_OUT/system/apex/${apex}"
    if [[ $apex == com.android.art.* ]]; then
      dst="$linkerconfig_root/apex/com.android.art"
    else
      dst="$linkerconfig_root/apex/${apex}"
    fi
    echo "Copying APEX directory from $src to $dst"
    rm -rf $dst
    cp -r $src $dst
  done

  # To avoid warnings from linkerconfig when it checks following two partitions
  mkdir -p $linkerconfig_root/product
  mkdir -p $linkerconfig_root/system_ext

  platform_version=$(build/soong/soong_ui.bash --dumpvar-mode PLATFORM_VERSION)
  linkerconfig_out=$ANDROID_PRODUCT_OUT/linkerconfig
  echo "Generating linkerconfig in $linkerconfig_out"
  rm -rf $linkerconfig_out
  mkdir -p $linkerconfig_out
  $ANDROID_HOST_OUT/bin/linkerconfig --target $linkerconfig_out --root $linkerconfig_root --vndk $platform_version
fi
