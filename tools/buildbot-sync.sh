#! /bin/bash
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

# Push ART artifacts and its dependencies to a chroot directory for on-device testing.

if [ -t 1 ]; then
  # Color sequences if terminal is a tty.
  red='\033[0;31m'
  green='\033[0;32m'
  yellow='\033[0;33m'
  magenta='\033[0;35m'
  nc='\033[0m'
fi

# Setup as root, as some actions performed here require it.
adb root
adb wait-for-device

if [[ -z "$ANDROID_BUILD_TOP" ]]; then
  echo 'ANDROID_BUILD_TOP environment variable is empty; did you forget to run `lunch`?'
  exit 1
fi

if [[ -z "$ANDROID_PRODUCT_OUT" ]]; then
  echo 'ANDROID_PRODUCT_OUT environment variable is empty; did you forget to run `lunch`?'
  exit 1
fi

if [[ -z "$ART_TEST_CHROOT" ]]; then
  echo 'ART_TEST_CHROOT environment variable is empty; please set it before running this script.'
  exit 1
fi

if [[ "$(build/soong/soong_ui.bash --dumpvar-mode TARGET_FLATTEN_APEX)" != "true" ]]; then
  echo -e "${red}This script only works when  APEX packages are flattened, but the build" \
    "configuration is set up to use non-flattened APEX packages.${nc}"
  echo -e "${magenta}You can force APEX flattening by setting the environment variable" \
    "\`OVERRIDE_TARGET_FLATTEN_APEX\` to \"true\" before starting the build and running this" \
    "script.${nc}"
  exit 1
fi


# `/system` "partition" synchronization.
# --------------------------------------

# Sync the system directory to the chroot.
echo -e "${green}Syncing system directory...${nc}"
adb shell mkdir -p "$ART_TEST_CHROOT/system"
adb push "$ANDROID_PRODUCT_OUT/system" "$ART_TEST_CHROOT/"
# Overwrite the default public.libraries.txt file with a smaller one that
# contains only the public libraries pushed to the chroot directory.
adb push "$ANDROID_BUILD_TOP/art/tools/public.libraries.buildbot.txt" \
  "$ART_TEST_CHROOT/system/etc/public.libraries.txt"


# APEX packages activation.
# -------------------------

# Manually "activate" the flattened APEX $1 by syncing it to /apex/$2 in the
# chroot. $2 defaults to $1.
#
# TODO: Handle the case of build targets using non-flatted APEX packages.
# As a workaround, one can run `export OVERRIDE_TARGET_FLATTEN_APEX=true` before building
# a target to have its APEX packages flattened.
activate_apex() {
  local src_apex=${1}
  local dst_apex=${2:-${src_apex}}
  echo -e "${green}Activating APEX ${src_apex} as ${dst_apex}...${nc}"
  # We move the files from `/system/apex/${src_apex}` to `/apex/${dst_apex}` in
  # the chroot directory, instead of simply using a symlink, as Bionic's linker
  # relies on the real path name of a binary (e.g.
  # `/apex/com.android.art/bin/dex2oat`) to select the linker configuration.
  adb shell mkdir -p "$ART_TEST_CHROOT/apex"
  adb shell rm -rf "$ART_TEST_CHROOT/apex/${dst_apex}"
  # Use use mv instead of cp, as cp has a bug on fugu NRD90R where symbolic
  # links get copied with odd names, eg: libcrypto.so -> /system/lib/libcrypto.soe.sort.so
  adb shell mv "$ART_TEST_CHROOT/system/apex/${src_apex}" "$ART_TEST_CHROOT/apex/${dst_apex}" \
    || exit 1
}

# "Activate" the required APEX modules.
activate_apex com.android.art.testing com.android.art
activate_apex com.android.i18n
activate_apex com.android.runtime
activate_apex com.android.tzdata
activate_apex com.android.conscrypt


# Linker configuration.
# ---------------------

# Statically linked `linkerconfig` binary.
linkerconfig_binary="/system/bin/linkerconfig"
# Generated linker configuration file path (since Android R).
ld_generated_config_file_path="/linkerconfig/ld.config.txt"
# Location of the generated linker configuration file.
ld_generated_config_file_location=$(dirname "$ld_generated_config_file_path")

# Generate linker configuration files on device.
echo -e "${green}Generating linker configuration files on device in" \
  "\`$ld_generated_config_file_path\`${nc}..."
adb shell chroot "$ART_TEST_CHROOT" \
  "$linkerconfig_binary" --target "$ld_generated_config_file_location" || exit 1
ld_generated_config_files=$(adb shell find $ART_TEST_CHROOT/linkerconfig ! -type d | sed 's/^/  /')
echo -e "${green}Generated linker configuration files on device:${nc}"
echo -e "${green}$ld_generated_config_files${nc}"


# `/data` "partition" synchronization.
# ------------------------------------

# Sync the data directory to the chroot.
echo -e "${green}Syncing data directory...${nc}"
adb shell mkdir -p "$ART_TEST_CHROOT/data"
adb push "$ANDROID_PRODUCT_OUT/data" "$ART_TEST_CHROOT/"
