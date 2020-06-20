#!/bin/bash
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

#
# This script configures a device for boot image profile
#

if [[ -z "$ANDROID_BUILD_TOP" ]]; then
  echo "You must run on this after running envsetup.sh and launch target"
  exit 1
fi

if [[ "$#" -lt 1 ]]; then
  echo "Usage $0 <output-for-boot-zip>"
  echo "Example: $0 boot.zip"
  exit 1
fi

OUT_BOOT_ZIP="$1"

echo "Changing dirs to the build top"
cd "$ANDROID_BUILD_TOP"

# Make dist in order to easily get the boot and system server dex files
# This will be stored in $ANDROID_PRODUCT_OUT/boot.zip
echo "Make dist"
m dist
echo "Copy boot.zip to $OUT_BOOT_ZIP"
cp "$ANDROID_PRODUCT_OUT"/boot.zip $OUT_BOOT_ZIP

echo "Setting properties and clearing existing profiles"
# If the device needs to be rebooted, it is better to set the properties
# via a local.prop file:
#  1) create a local.prop file with the content
#      dalvik.vm.profilebootclasspath=true
#      dalvik.vm.profilesystemserver=true
#  2) adb push local.prop /data/
#     adb shell chmod 0750 /data/local.prop
#     adb reboot

adb root
adb shell stop
adb shell setprop dalvik.vm.profilebootclasspath true
adb shell setprop dalvik.vm.profilesystemserver true
adb shell find "/data/misc/profiles -name *.prof -exec truncate -s 0 {} \;"
adb shell start