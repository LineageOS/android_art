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
# This script extracts the boot image profile from a previously configured device,
# which executed the critical user journyes.
#

if [[ "$#" -lt 1 ]]; then
  echo "Usage $0 <output-profile>"
  echo "Example: $0 android.prof"
  exit 1
fi

OUT_PROFILE="$1"

echo "Snapshoting platform profiles"
adb shell cmd package snapshot-profile android
adb pull /data/misc/profman/android.prof "$OUT_PROFILE"