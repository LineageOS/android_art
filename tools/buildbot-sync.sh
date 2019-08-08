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

red='\033[0;31m'
green='\033[0;32m'
yellow='\033[0;33m'
magenta='\033[0;35m'
nc='\033[0m'

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
    "\`TARGET_FLATTEN_APEX\` to \"true\" before starting the build and running this script.${nc}"
  exit 1
fi

# Linker configuration.
# ---------------------

# Adjust the chroot environment to have it use the system linker configuration
# of the built target ("guest system"), located in `/system/etc` under the
# chroot directory, even if the linker configuration flavor of the "guest
# system" (e.g. legacy configuration) does not match the one of the "host
# system" (e.g. full-VNDK configuration). This is done by renaming the
# configuration file provided by the "guest system" (created according to the
# build target configuration) within the chroot environment, using the name of
# the configuration file expected by the linker (governed by system properties
# of the "host system").

# Default linker configuration file name/stem.
ld_config_file_path="/system/etc/ld.config.txt";
# VNDK-lite linker configuration file name.
ld_config_vndk_lite_file_path="/system/etc/ld.config.vndk_lite.txt";

# Find linker configuration path name on the "host system".
#
# The logic here partly replicates (and simplifies) Bionic's linker logic around
# configuration file search (see `get_ld_config_file_path` in
# bionic/linker/linker.cpp).
get_ld_host_system_config_file_path() {
  # Check whether the "host device" uses a VNDK-lite linker configuration.
  local vndk_lite=$(adb shell getprop "ro.vndk.lite" false)
  if [[ "$vndk_lite" = true ]]; then
    if adb shell test -f "$ld_config_vndk_lite_file_path"; then
      echo "$ld_config_vndk_lite_file_path"
      return
    fi
  fi
  # Check the "host device"'s VNDK version, if any.
  local vndk_version=$(adb shell getprop "ro.vndk.version")
  if [[ -n "$vndk_version" ]] && [[ "$vndk_version" != current ]]; then
    # Insert the VNDK version after the last period (and add another period).
    local ld_config_file_vdnk_path=$(echo "$ld_config_file_path" \
      | sed -e "s/^\\(.*\\)\\.\\([^.]\\)/\\1.${vndk_version}.\\2/")
    if adb shell test -f "$ld_config_file_vdnk_path"; then
      echo "$ld_config_file_vdnk_path"
      return
    fi
  else
    if adb shell test -f "$ld_config_file_path"; then
      echo "$ld_config_file_path"
      return
    fi
  fi
  # If all else fails, return the default linker configuration name.
  echo -e "${yellow}Cannot find linker configuration; using default path name:" \
    "\`$ld_config_file_path\`${nc}" >&2
  echo "$ld_config_file_path"
  return
}

# Find linker configuration path name on the "guest system".
#
# The logic here tries to "guess" the name of the linker configuration file,
# based on the contents of the build directory.
get_ld_guest_system_config_file_path() {
  if [[ -z "$ANDROID_PRODUCT_OUT" ]]; then
    echo -e "${red}ANDROID_PRODUCT_OUT environment variable is empty;" \
      "did you forget to run \`lunch\`${nc}?" >&2
    exit 1
  fi
  local ld_config_file_location="$ANDROID_PRODUCT_OUT/system/etc"
  local ld_config_file_path_number=$(find "$ld_config_file_location" -name "ld.*.txt" | wc -l)
  if [[ "$ld_config_file_path_number" -eq 0 ]]; then
    echo -e "${red}No linker configuration file found in \`$ld_config_file_location\`${nc}" >&2
    exit 1
  fi
  if [[ "$ld_config_file_path_number" -gt 1 ]]; then
    echo -e \
      "${red}More than one linker configuration file found in \`$ld_config_file_location\`${nc}" >&2
    exit 1
  fi
  # Strip the build prefix to make the path name relative to the "guest root directory".
  find "$ld_config_file_location" -name "ld.*.txt" | sed -e "s|^$ANDROID_PRODUCT_OUT||"
}


# Synchronization recipe.
# -----------------------

# Sync the system directory to the chroot.
echo -e "${green}Syncing system directory...${nc}"
adb push "$ANDROID_PRODUCT_OUT/system" "$ART_TEST_CHROOT/"
# Overwrite the default public.libraries.txt file with a smaller one that
# contains only the public libraries pushed to the chroot directory.
adb push "$ANDROID_BUILD_TOP/art/tools/public.libraries.buildbot.txt" \
  "$ART_TEST_CHROOT/system/etc/public.libraries.txt"

echo -e "${green}Activating Runtime APEX...${nc}"
# Manually "activate" the flattened Testing Runtime APEX by syncing it to the
# /apex directory in the chroot.
#
# We copy the files from `/system/apex/com.android.runtime.testing` to
# `/apex/com.android.runtime` in the chroot directory, instead of simply using a
# symlink, as Bionic's linker relies on the real path name of a binary
# (e.g. `/apex/com.android.runtime/bin/dex2oat`) to select the linker
# configuration.
#
# TODO: Handle the case of build targets using non-flatted APEX packages.
# As a workaround, one can run `export TARGET_FLATTEN_APEX=true` before building
# a target to have its APEX packages flattened.
adb shell rm -rf "$ART_TEST_CHROOT/apex/com.android.runtime"
adb shell cp -a "$ART_TEST_CHROOT/system/apex/com.android.runtime.testing" \
  "$ART_TEST_CHROOT/apex/com.android.runtime"

echo -e "${green}Activating i18n APEX...${nc}"
# Manually "activate" the flattened i18n APEX by syncing it to the
# /apex directory in the chroot.
#
# TODO: Likewise, handle the case of build targets using non-flatted APEX packages.
adb shell rm -rf "$ART_TEST_CHROOT/apex/com.android.i18n"
adb shell cp -a "$ART_TEST_CHROOT/system/apex/com.android.i18n" "$ART_TEST_CHROOT/apex/"

echo -e "${green}Activating Time Zone Data APEX...${nc}"
# Manually "activate" the flattened Time Zone Data APEX by syncing it to the
# /apex directory in the chroot.
#
# TODO: Likewise, handle the case of build targets using non-flatted APEX
# packages.
adb shell rm -rf "$ART_TEST_CHROOT/apex/com.android.tzdata"
adb shell cp -a "$ART_TEST_CHROOT/system/apex/com.android.tzdata" "$ART_TEST_CHROOT/apex/"

# Adjust the linker configuration file (if needed).
#
# Check the linker configurations files on the "host system" and the "guest
# system". If these file names are different, rename the "guest system" linker
# configuration file within the chroot environment using the "host system"
# linker configuration file name.
ld_host_system_config_file_path=$(get_ld_host_system_config_file_path)
echo -e "${green}Determining host system linker configuration:" \
  "\`$ld_host_system_config_file_path\`${nc}"
ld_guest_system_config_file_path=$(get_ld_guest_system_config_file_path)
echo -e "${green}Determining guest system linker configuration:" \
  "\`$ld_guest_system_config_file_path\`${nc}"
if [[ "$ld_host_system_config_file_path" != "$ld_guest_system_config_file_path" ]]; then
  echo -e "${green}Renaming linker configuration file in chroot environment:" \
    "\`$ART_TEST_CHROOT$ld_guest_system_config_file_path\`" \
    "-> \`$ART_TEST_CHROOT$ld_host_system_config_file_path\`${nc}"
  adb shell mv -f "$ART_TEST_CHROOT$ld_guest_system_config_file_path" \
      "$ART_TEST_CHROOT$ld_host_system_config_file_path"
fi

# Sync the data directory to the chroot.
echo -e "${green}Syncing data directory...${nc}"
adb push "$ANDROID_PRODUCT_OUT/data" "$ART_TEST_CHROOT/"
