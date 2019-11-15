#!/bin/bash
#
# Copyright 2019 The Android Open Source Project
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

if [[ ${#@} != 1 ]] && [[ ${#@} != 2 ]]; then
  cat <<EOF
Usage
  host_bcp <image> [--use-first-dir] | xargs <art-host-tool> ...
Extracts boot class path locations from <image> and outputs the appropriate
  --runtime-arg -Xbootclasspath:...
  --runtime-arg -Xbootclasspath-locations:...
arguments for many ART host tools based on the \$ANDROID_PRODUCT_OUT variable
and existing \$ANDROID_PRODUCT_OUT/apex/com.android.art* paths.
If --use-first-dir is specified, the script will use the first apex dir instead
of resulting in an error.
EOF
  exit 1
fi

IMAGE=$1
USE_FIRST_DIR=false

if [[ $2 == "--use-first-dir" ]]; then
  USE_FIRST_DIR=true
fi

if [[ ! -e ${IMAGE} ]]; then
  IMAGE=${ANDROID_PRODUCT_OUT}/$1
  if [[ ! -e ${IMAGE} ]]; then
    echo "Neither $1 nor ${ANDROID_PRODUCT_OUT}/$1 exists."
    exit 1
  fi
fi
BCPL=`grep -az -A1 -E '^bootclasspath$' ${IMAGE} 2>/dev/null | \
      xargs -0 echo | gawk '{print $2}'`
if [[ "x${BCPL}" == "x" ]]; then
  echo "Failed to extract boot class path locations from $1."
  exit 1
fi

MANIFEST=/apex_manifest.pb
ART_APEX=/apex/com.android.art
ART_APEX_SELECTED=
for m in `ls -1 -d ${ANDROID_PRODUCT_OUT}{,/system}${ART_APEX}*${MANIFEST} 2>/dev/null`; do
  d=${m:0:-${#MANIFEST}}
  if [[ "x${ART_APEX_SELECTED}" != "x" ]]; then
    if [[ $USE_FIRST_DIR == true ]]; then
      break
    fi
    echo "Multiple ART APEX dirs: ${ART_APEX_SELECTED}, ${d}."
    exit 1
  fi
  ART_APEX_SELECTED=${d}
done
if [[ "x${ART_APEX_SELECTED}" == "x" ]]; then
  echo "No ART APEX dir."
  exit 1
fi

BCP=
OLD_IFS=${IFS}
IFS=:
for COMPONENT in ${BCPL}; do
  HEAD=${ANDROID_PRODUCT_OUT}
  TAIL=${COMPONENT}
  if [[ ${COMPONENT:0:${#ART_APEX}} = ${ART_APEX} ]]; then
    HEAD=${ART_APEX_SELECTED}
    TAIL=${COMPONENT:${#ART_APEX}}
  fi
  if [[ ! -e $HEAD$TAIL ]]; then
    echo "File does not exist: $HEAD$TAIL"
    exit 1
  fi
  BCP="${BCP}:${HEAD}${TAIL}"
done
IFS=${OLD_IFS}
BCP=${BCP:1}  # Strip leading ':'.

echo --runtime-arg -Xbootclasspath:${BCP} \
     --runtime-arg -Xbootclasspath-locations:${BCPL}
