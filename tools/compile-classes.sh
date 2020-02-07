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
# This script compiles Java Language source files into arm64 odex files.
#
# Before running this script, do lunch and build for an arm64 device.
#
# Usage:
#     compile-classes.sh Scratch.java

set -e

SCRATCH=`mktemp -d`
DEX_FILE=classes.dex
ODEX_FILE=classes.odex

javac -d $SCRATCH $1
d8 $SCRATCH/*.class

$ANDROID_BUILD_TOP/art/tools/compile-jar.sh $DEX_FILE $ODEX_FILE arm64 \
    --generate-debug-info \
    --dump-cfg=classes.cfg

rm -rf $SCRATCH

echo
echo "OAT file is at $ODEX_FILE"
echo
echo "View it with one of the following commands:"
echo
echo "    oatdump --oat-file=$ODEX_FILE"
echo
echo "    aarch64-linux-android-objdump -d $ODEX_FILE"
echo
echo "The CFG is dumped to output.cfg for inspection of individual compiler passes."
echo
