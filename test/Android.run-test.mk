# Copyright (C) 2011 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

include art/build/Android.common_test.mk

# Dependencies for actually running a run-test.
TEST_ART_RUN_TEST_DEPENDENCIES := \
  $(HOST_OUT_EXECUTABLES)/d8 \
  $(HOST_OUT_EXECUTABLES)/hiddenapi \
  $(HOST_OUT_EXECUTABLES)/jasmin \
  $(HOST_OUT_EXECUTABLES)/smali

# We need the ART Testing APEX (which is a superset of the Release
# and Debug APEXes) -- which contains dex2oat, dalvikvm, their
# dependencies and ART gtests -- on the target, as well as the core
# images (all images as we sync only once).
ART_TEST_TARGET_RUN_TEST_DEPENDENCIES := $(TESTING_ART_APEX) $(TARGET_CORE_IMG_OUTS)

# Also need libartagent.
ART_TEST_TARGET_RUN_TEST_DEPENDENCIES += libartagent-target libartagentd-target

# Also need libtiagent.
ART_TEST_TARGET_RUN_TEST_DEPENDENCIES += libtiagent-target libtiagentd-target

# Also need libtistress.
ART_TEST_TARGET_RUN_TEST_DEPENDENCIES += libtistress-target libtistressd-target

# Also need libarttest.
ART_TEST_TARGET_RUN_TEST_DEPENDENCIES += libarttest-target libarttestd-target

# Also need libnativebridgetest.
ART_TEST_TARGET_RUN_TEST_DEPENDENCIES += libnativebridgetest-target libnativebridgetestd-target

# Also need signal_dumper.
ART_TEST_TARGET_RUN_TEST_DEPENDENCIES += signal_dumper-target

# All tests require the host executables. The tests also depend on the core images, but on
# specific version depending on the compiler.
ART_TEST_HOST_RUN_TEST_DEPENDENCIES := \
  $(ART_HOST_EXECUTABLES) \
  $(HOST_OUT_EXECUTABLES)/hprof-conv \
  $(HOST_OUT_EXECUTABLES)/signal_dumper \
  $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_libtiagent) \
  $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_libtiagentd) \
  $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_libtistress) \
  $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_libtistressd) \
  $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_libartagent) \
  $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_libartagentd) \
  $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_libarttest) \
  $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_libarttestd) \
  $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_libnativebridgetest) \
  $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_libnativebridgetestd) \
  $(ART_HOST_OUT_SHARED_LIBRARIES)/libicu_jni$(ART_HOST_SHLIB_EXTENSION) \
  $(ART_HOST_OUT_SHARED_LIBRARIES)/libjavacore$(ART_HOST_SHLIB_EXTENSION) \
  $(ART_HOST_OUT_SHARED_LIBRARIES)/libopenjdk$(ART_HOST_SHLIB_EXTENSION) \
  $(ART_HOST_OUT_SHARED_LIBRARIES)/libopenjdkd$(ART_HOST_SHLIB_EXTENSION) \
  $(ART_HOST_OUT_SHARED_LIBRARIES)/libopenjdkjvmti$(ART_HOST_SHLIB_EXTENSION) \
  $(ART_HOST_OUT_SHARED_LIBRARIES)/libopenjdkjvmtid$(ART_HOST_SHLIB_EXTENSION) \
  $(ART_HOST_DEX_DEPENDENCIES) \
  $(HOST_I18N_DATA)

ifneq ($(HOST_PREFER_32_BIT),true)
ART_TEST_HOST_RUN_TEST_DEPENDENCIES += \
  $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_libtiagent) \
  $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_libtiagentd) \
  $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_libtistress) \
  $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_libtistressd) \
  $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_libartagent) \
  $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_libartagentd) \
  $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_libarttest) \
  $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_libarttestd) \
  $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_libnativebridgetest) \
  $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_libnativebridgetestd) \
  $(2ND_ART_HOST_OUT_SHARED_LIBRARIES)/libicu_jni$(ART_HOST_SHLIB_EXTENSION) \
  $(2ND_ART_HOST_OUT_SHARED_LIBRARIES)/libjavacore$(ART_HOST_SHLIB_EXTENSION) \
  $(2ND_ART_HOST_OUT_SHARED_LIBRARIES)/libopenjdk$(ART_HOST_SHLIB_EXTENSION) \
  $(2ND_ART_HOST_OUT_SHARED_LIBRARIES)/libopenjdkd$(ART_HOST_SHLIB_EXTENSION) \
  $(2ND_ART_HOST_OUT_SHARED_LIBRARIES)/libopenjdkjvmti$(ART_HOST_SHLIB_EXTENSION) \
  $(2ND_ART_HOST_OUT_SHARED_LIBRARIES)/libopenjdkjvmtid$(ART_HOST_SHLIB_EXTENSION) \

endif

test-art-host-run-test-dependencies : \
      $(ART_TEST_HOST_RUN_TEST_DEPENDENCIES) $(TEST_ART_RUN_TEST_DEPENDENCIES) \
      $(HOST_BOOT_IMAGE_JARS) $(HOST_BOOT_IMAGE) $(2ND_HOST_BOOT_IMAGE)
.PHONY: test-art-host-run-test-dependencies
test-art-run-test-dependencies : test-art-host-run-test-dependencies

test-art-target-run-test-dependencies :
.PHONY: test-art-target-run-test-dependencies
test-art-run-test-dependencies : test-art-target-run-test-dependencies
.PHONY: test-art-run-test-dependencies

# Create a rule to build and run a test group of the following form:
# test-art-{1: host target}-run-test
define define-test-art-host-or-target-run-test-group
  build_target := test-art-$(1)-run-test
  .PHONY: $$(build_target)

  $$(build_target) : args := --$(1) --verbose
  $$(build_target) : test-art-$(1)-run-test-dependencies
	./art/test/testrunner/testrunner.py $$(args)
  build_target :=
  args :=

  test-art-run-test : $(build_target)
endef  # define-test-art-host-or-target-run-test-group

$(eval $(call define-test-art-host-or-target-run-test-group,target))
$(eval $(call define-test-art-host-or-target-run-test-group,host))

define-test-art-host-or-target-run-test-group :=
LOCAL_PATH :=
