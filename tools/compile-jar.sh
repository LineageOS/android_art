#!/bin/bash
#
# Copyright (C) 2019 The Android Open Source Project
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
# This script runs dex2oat on the host to compile a provided JAR or APK.
#

if [[ "$#" -lt 1 ]]; then
  echo "Usage $0 <jar|apk> <output> <isa> [args]+"
  echo "Example $0 Maps.apk maps.odex arm64"
  exit 1
fi

FILE=$1
shift
OUTPUT=$1
shift
ISA=$1
shift

# Use services.odex as a quick and dirty way to get correct BCP.
dex2oatd \
    --runtime-arg -Xms64m --runtime-arg -Xmx512m \
    --boot-image=${OUT}/apex/com.android.art.debug/javalib/boot.art:${OUT}/system/framework/boot-framework.art \
    $(${ANDROID_BUILD_TOP}/art/tools/host_bcp.sh ${OUT}/system/framework/oat/${ISA}/services.odex --use-first-dir) \
    --dex-file=${FILE} --dex-location=/system/framework/${FILE} \
    --oat-file=${OUTPUT} \
    --android-root=${OUT}/system --instruction-set=$ISA \
    $@
