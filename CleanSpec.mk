# Copyright (C) 2014 The Android Open Source Project
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

# If you don't need to do a full clean build but would like to touch
# a file or delete some intermediate files, add a clean step to the end
# of the list.  These steps will only be run once, if they haven't been
# run before.
#
# E.g.:
#     $(call add-clean-step, touch -c external/sqlite/sqlite3.h)
#     $(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/STATIC_LIBRARIES/libz_intermediates)
#
# Always use "touch -c" and "rm -f" or "rm -rf" to gracefully deal with
# files that are missing or have been moved.
#
# Use $(PRODUCT_OUT) to get to the "out/target/product/blah/" directory.
# Use $(OUT_DIR) to refer to the "out" directory.
#
# If you need to re-do something that's already mentioned, just copy
# the command and add it to the bottom of the list.  E.g., if a change
# that you made last week required touching a file and a change you
# made today requires touching the same file, just copy the old
# touch step and add it to the end of the list.
#
# ************************************************
# NEWER CLEAN STEPS MUST BE AT THE END OF THE LIST
# ************************************************

# For example:
#$(call add-clean-step, rm -rf $(OUT_DIR)/target/common/obj/APPS/AndroidTests_intermediates)
#$(call add-clean-step, rm -rf $(OUT_DIR)/target/common/obj/JAVA_LIBRARIES/core_intermediates)
#$(call add-clean-step, find $(OUT_DIR) -type f -name "IGTalkSession*" -print0 | xargs -0 rm -f)
#$(call add-clean-step, rm -rf $(PRODUCT_OUT)/data/*)

# Switching to jemalloc requires deleting these files.
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/STATIC_LIBRARIES/libart_*)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/STATIC_LIBRARIES/libartd_*)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/SHARED_LIBRARIES/libart_*)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/SHARED_LIBRARIES/libartd_*)

# Old Android Runtime APEX package, before the introduction of "release" and "debug" packages.
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/apex/com.android.runtime.apex)

# Clean up ICU libraries moved to runtime apex
$(call add-clean-step, rm -f $(PRODUCT_OUT)/system/lib*/libandroidicu.so)
$(call add-clean-step, rm -f $(PRODUCT_OUT)/system/lib*/libpac.so)

$(call add-clean-step, rm -rf $(PRODUCT_OUT)/data/nativetest*/art_libdexfile_support_tests/dex_file_supp_test)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/data/nativetest*/)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/data/nativetest*/)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/data/nativetest*/)

# Clean up duplicate compiles between static and shared compiles of libart and libartd
$(call add-clean-step, rm -rf $(OUT_DIR)/soong/.intermediates/art/runtime/libart/*shared*/obj)
$(call add-clean-step, rm -rf $(OUT_DIR)/soong/.intermediates/art/runtime/libartd/*shared*/obj)

# Force regeneration of .apex files after removal of time zone data files from the runtime APEX
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/apex/com.android.runtime.*)

# Remove artifacts that used to be generated (as a workaround for
# improper Runtime APEX support) by tools/buildbot-build.sh via the
# `standalone-apex-files` Make rule.
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/bin)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/lib*)
# Remove artifacts that used to be generated (as a workaround for
# improper Runtime APEX support) by tools/buildbot-build.sh via the
# `icu-data-art-test` Make rule.
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/etc/icu)

# Remove ART test target artifacts.
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/data/nativetest*/)

# Remove all APEX artifacts after the change to use the Testing
# Runtime APEX in lieu of the Debug Runtime APEX for ART testing.
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/apex)

# Remove the icu .dat file from /apex/com.android.runtime and the host equivalent.
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/apex)
$(call add-clean-step, rm -rf $(HOST_OUT)/com.android.runtime/etc/icu/*)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/etc/icu)

# Remove all APEX artifacts for the Runtime/ART APEX split.
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/apex)
$(call add-clean-step, rm -rf $(HOST_OUT)/apex)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/apex)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/symbols/apex)

# Remove dex2oat artifacts for boot image extensions (workaround for broken dependencies).
$(call add-clean-step, find $(OUT_DIR) -name "*.oat" -o -name "*.odex" -o -name "*.art" -o -name '*.vdex' | xargs rm -f)
$(call add-clean-step, find $(OUT_DIR) -name "*.oat" -o -name "*.odex" -o -name "*.art" -o -name '*.vdex' | xargs rm -f)
$(call add-clean-step, find $(OUT_DIR) -name "*.oat" -o -name "*.odex" -o -name "*.art" -o -name '*.vdex' | xargs rm -f)

# Remove empty dir for art APEX because it will be created on demand while mounting release|debug
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/apex/com.android.art)

# ************************************************
# NEWER CLEAN STEPS MUST BE AT THE END OF THE LIST
# ************************************************
