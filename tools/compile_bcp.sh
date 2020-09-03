#!/system/bin/sh
#
# Copyright (C) 2020 The Android Open Source Project
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

# Script to recompile the non-APEX jars in the boot class path and
# system server jars for on-device signing following an ART Module
# update.
#
# For testing purposes, prepare Android device using:
#
#   $ adb root
#   $ adb shell setenforce 0
#
# TODO: this is script is currently for proof-of-concept. It does not
# respect many of the available dalvik.vm properties, e.g. affinity,
# threads, etc nor dalvik.vm.dex2oat-resolve-startup-strings.
#
# TODO: some of the system server jars seem to be installed on device
# for 32-bit and 64-bit, but services.jar artifacts are just one arch.
# Not sure why both flavors are present. Does this script need to generate
# both?
#
# TODO: Logging failure / error handling.

# Output directory for generated files. Real location is TBD.
output=$PWD/out

function mkdir_clean() {
  # mkdir_clean <dir_path>
  local dir_path=$1
  rm -rf "${dir_path}"
  mkdir -p "${dir_path}"
}

# Determine candidate architectures for this device.
case `getprop ro.product.cpu.abi` in
  arm*)
    arch32=arm
    arch64=arm64
    ;;
  x86*)
    arch32=x86
    arch64=x86_64
    ;;
  *)
    echo "Unknown abi"
    exit -1
esac

# Determine architectures to use for Zygote. system_server runs under the primary
# architecture. Prefer dex2oat64 if device supports 64-bits.
case $(getprop ro.zygote) in
  zygote32)
    # The primary architecture is 32-bits.
    archs="$arch32"
    systemserver_arch="$arch32"
    dex2oat=/apex/com.android.art/bin/dex2oat32
    ;;
  zygote32_64)
    # The primary architecture is 32-bits and the secondary is 64-bits.
    archs="$arch32 $arch64"
    systemserver_arch="$arch32"
    dex2oat=/apex/com.android.art/bin/dex2oat64
    ;;
  zygote64_32)
    # The primary architecture is 64-bits and the secondary is 32-bits.
    archs="$arch32 $arch64"
    systemserver_arch="$arch64"
    dex2oat=/apex/com.android.art/bin/dex2oat64
    ;;
  zygote64)
    # Primary architecture is 64-bits.
    archs="$arch64"
    systemserver_arch="$arch64"
    dex2oat=/apex/com.android.art/bin/dex2oat64
    ;;
  *)
    echo "Unknown ro.zygote value"
    exit -1
    ;;
esac

# Determine which boot class path jars to compile.
device_bcp_list=""
device_bcp_dex_files=""
for jar in ${DEX2OATBOOTCLASSPATH//:/ }; do
  if [[ ${jar} = *com.android.art* ]]; then
    continue
  fi
  device_bcp_list="${device_bcp_list}${device_bcp_list:+:}${jar}"
  device_bcp_dex_files="$device_bcp_dex_files --dex-file=${jar}"
done

# Compile the boot class path elements that are present on device.
for arch in ${archs}; do
  arch_output="${output}/$arch"
  mkdir_clean "${arch_output}"

  invocation_dir="${output}/$arch"
  mkdir_clean "${invocation_dir}"

  echo "Compiling ${device_bcp_list} ($arch)"
  ${dex2oat} --avoid-storing-invocation \
    --compiler-filter=speed-profile \
    --profile-file=/system/etc/boot-image.prof \
    --dirty-image-objects=/system/etc/dirty-image-objects \
    --runtime-arg -Xbootclasspath:${DEX2OATBOOTCLASSPATH} \
    --boot-image=/apex/com.android.art/javalib/boot.art \
    ${device_bcp_dex_files} \
    --generate-debug-info \
    --image-format=lz4hc \
    --strip \
    --oat-file=${arch_output}/boot.oat \
    --image=${arch_output}/boot.art \
    --android-root=out/empty \
    --abort-on-hard-verifier-error \
    --instruction-set=$arch \
    --generate-mini-debug-info
done

# Compile system_server and related jars.
classloader_context=""
for jar in ${SYSTEMSERVERCLASSPATH//:/ }; do
  # Skip class path components in APEXes
  if [[ ${jar} = "/apex"* ]]; then
    continue
  fi

  # Add profile if it exists. Only services.jar has a profile in AOSP.
  stem=$(basename $jar .jar)
  profile_file=/system/framework/${stem}.jar.prof
  if [ -f "${profile_file}" ] ; then
    profile_arg=--profile-file=${profile_file}
    filter=speed-profile
  else
    profile_arg=""
    filter=speed
  fi

  # Add updatable boot class path packages file if there is a property for it.
  updatable_bcp_file=$(getprop dalvik.vm.dex2oat-updatable-bcp-packages-file)
  if [ "${updatable_bcp_file}" -a -f "${updatable_bcp_file}" ] ; then
    updatable_bcp_file_arg="--updatable-bcp-packages-file=${updatable_bcp_file}"
  fi

  echo "Compiling ${jar} (${systemserver_arch} ${filter} PCL[${classloader_context}])"
  $dex2oat --avoid-storing-invocation \
           --runtime-arg -Xbootclasspath:${DEX2OATBOOTCLASSPATH} \
           --class-loader-context=PCL[${classloader_context}] \
           --boot-image=/apex/com.android.art/javalib/boot.art:${output}/boot-framework.art \
           --dex-file=${jar} \
           --oat-file=${arch_output}/${stem}.odex \
           --app-image-file=${oat_file_dir}/${stem}.art \
           --android-root=out/empty \
           --instruction-set=${systemserver_arch} \
           --abort-on-hard-verifier-error \
           --compiler-filter=$filter \
           --generate-mini-debug-info \
           --compilation-reason=prebuilt \
           --image-format=lz4 \
           --resolve-startup-const-strings=true \
           ${profile_arg} \
           ${updatable_bcp_file_arg}

  classloader_context=${classloader_context}${classloader_context:+:}${jar}
done
