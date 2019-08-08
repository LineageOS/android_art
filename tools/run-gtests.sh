#! /bin/bash
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

# Script to run all gtests located in the (Testing) Runtime APEX.

if [[ -z "$ART_TEST_CHROOT" ]]; then
  echo 'ART_TEST_CHROOT environment variable is empty; please set it before running this script.'
  exit 1
fi

adb="${ADB:-adb}"

android_i18n_root=/apex/com.android.i18n
android_runtime_root=/apex/com.android.runtime
android_tzdata_root=/apex/com.android.tzdata

# Search for executables under the `bin/art` directory of the Runtime APEX.
tests=$("$adb" shell chroot "$ART_TEST_CHROOT" \
  find "$android_runtime_root/bin/art" -type f -perm /ugo+x | sort)

failing_tests=()

for t in $tests; do
  echo "$t"
  "$adb" shell chroot "$ART_TEST_CHROOT" \
    env ANDROID_I18N_ROOT="$android_i18n_root" ANDROID_RUNTIME_ROOT="$android_runtime_root" ANDROID_TZDATA_ROOT="$android_tzdata_root" $t \
    || failing_tests+=("$t")
done

if [ -n "$failing_tests" ]; then
  for t in "${failing_tests[@]}"; do
    echo "Failed test: $t"
  done
  exit 1
fi
