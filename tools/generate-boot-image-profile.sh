#!/bin/bash
#
# Copyright (C) 2017 The Android Open Source Project
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
# This script creates a boot image profile based on input profiles.
#

if [[ "$#" -lt 2 ]]; then
  echo "Usage $0 <output> <profman args> <profiles>+"
  echo "Also outputs <output>.txt and <output>.preloaded-classes"
  echo 'Example: generate-boot-image-profile.sh boot.prof --profman-arg --boot-image-sampled-method-threshold=1 profiles/cur/0/*/primary.prof'
  exit 1
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
TOP="$DIR/../.."
source "${TOP}/build/envsetup.sh" >&/dev/null # import get_build_var

OUT_PROFILE=$1
shift

# Read the profman args.
profman_args=()
while [[ "$#" -ge 2 ]] && [[ "$1" = '--profman-arg' ]]; do
  profman_args+=("$2")
  shift 2
done

# Remaining args are all the profiles.
for file in "$@"; do
  if [[ -s $file ]]; then
    profman_args+=("--profile-file=$file")
  fi
done

# b/139391334: the stem of framework-minus-apex is framework
real_jar_name() {
  if [[ "$1" == "framework-minus-apex" ]]; then
    echo "framework"
  else
    echo "$1"
  fi
}

# Boot jars have hidden API access flags which do not pass dex file
# verification. Skip it.
jar_args=()
boot_jars=$("$ANDROID_BUILD_TOP"/art/tools/bootjars.sh --target)
product_out=$ANDROID_BUILD_TOP/$(get_build_var PRODUCT_OUT)
for pair in $boot_jars; do
  words=( $(echo $pair | tr ':' ' ') )
  if [[ ${#words[@]} -eq 2 ]]; then
    # format in Android > R: <apex>:<jar>
    apex="${words[0]}"
    name="$(real_jar_name ${words[1]})"
    case "$apex" in
        platform*)   subdir=system/framework ;;
        system_ext*) subdir=system_ext/framework ;;
        *)           subdir=apex/$apex/javalib ;;
    esac
    filename="$product_out/$subdir/$name.jar"
  else
    # format in Android <= R: <jar>, have to infer location with `find`
    name="$(real_jar_name ${words[0]})"
    filename="$(find $product_out -name $name.jar 2>/dev/null)"
    if [[ $(echo "$filename" | wc -w ) -ne 1 ]]; then
      echo "expected to find $name.jar, got '$filename'"
      exit 1
    fi
  fi
  jar_args+=("--apk=$filename")
  jar_args+=("--dex-location=$filename")
done
profman_args+=("${jar_args[@]}")

# Generate the profile.
"$ANDROID_HOST_OUT/bin/profman" --generate-boot-image-profile "--reference-profile-file=$OUT_PROFILE" "${profman_args[@]}"

# Convert it to text.
echo Dumping profile to $OUT_PROFILE.txt
"$ANDROID_HOST_OUT/bin/profman" --dump-classes-and-methods "--profile-file=$OUT_PROFILE" "${jar_args[@]}" > "$OUT_PROFILE.txt"

# Generate preloaded classes
# Filter only classes by using grep -v
# Remove first and last characters L and ;
# Replace / with . to make dot format
grep -v "\\->" "$OUT_PROFILE.txt" | sed 's/.\(.*\)./\1/g' | tr "/" "." > "$OUT_PROFILE.preloaded-classes"

# You may need to filter some classes out since creating threads is not allowed in the zygote.
# i.e. using: grep -v -E '(android.net.ConnectivityThread\$Singleton)'
