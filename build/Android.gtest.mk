#
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

# Build rules are excluded from Mac, since we can not run ART tests there in the first place.
ifneq ($(HOST_OS),darwin)

###################################################################################################
# Create module in testcases to hold all common data and tools needed for ART host tests.

# ART binary tools and libraries (automatic list of all art_cc_binary/art_cc_library modules).
my_files := $(ART_TESTCASES_CONTENT)

# Manually add system libraries that we need to run the host ART tools.
my_files += \
  $(foreach lib, libbacktrace libbase libc++ libicu_jni liblog libsigchain libunwindstack \
    libziparchive libjavacore libandroidio libopenjdkd, \
    $(call intermediates-dir-for,SHARED_LIBRARIES,$(lib),HOST)/$(lib).so:lib64/$(lib).so \
    $(call intermediates-dir-for,SHARED_LIBRARIES,$(lib),HOST,,2ND)/$(lib).so:lib/$(lib).so) \
  $(foreach lib, libcrypto libz libicuuc libicui18n libandroidicu libexpat, \
    $(call intermediates-dir-for,SHARED_LIBRARIES,$(lib),HOST)/$(lib).so:lib64/$(lib)-host.so \
    $(call intermediates-dir-for,SHARED_LIBRARIES,$(lib),HOST,,2ND)/$(lib).so:lib/$(lib)-host.so)

# Add apex directories for art, conscrypt and i18n.
icu_data_file := $(firstword $(wildcard external/icu/icu4c/source/stubdata/icu*.dat))
my_files += $(foreach infix,_ _VDEX_,$(foreach suffix,$(HOST_ARCH) $(HOST_2ND_ARCH), \
  $(DEXPREOPT_IMAGE$(infix)BUILT_INSTALLED_art_host_$(suffix))))
my_files += \
  $(foreach jar,$(CORE_IMG_JARS),\
    $(HOST_OUT_JAVA_LIBRARIES)/$(jar)-hostdex.jar:apex/com.android.art/javalib/$(jar).jar) \
  $(HOST_OUT_JAVA_LIBRARIES)/conscrypt-hostdex.jar:apex/com.android.conscrypt/javalib/conscrypt.jar\
  $(HOST_OUT_JAVA_LIBRARIES)/core-icu4j-hostdex.jar:apex/com.android.i18n/javalib/core-icu4j.jar \
  $(icu_data_file):com.android.i18n/etc/icu/$(notdir $(icu_data_file))

# Create dummy module that will copy all the data files into testcases directory.
# For now, this copies everything to "out/host/linux-x86/" subdirectory, since it
# is hard-coded in many places. TODO: Refactor tests to remove the need for this.
include $(CLEAR_VARS)
LOCAL_IS_HOST_MODULE := true
LOCAL_MODULE := art_common
LOCAL_MODULE_TAGS := tests
LOCAL_MODULE_CLASS := NATIVE_TESTS
LOCAL_MODULE_SUFFIX := .txt
LOCAL_COMPATIBILITY_SUITE := general-tests
LOCAL_COMPATIBILITY_SUPPORT_FILES := $(ART_TESTCASES_PREBUILT_CONTENT) \
	$(foreach f,$(my_files),$(call word-colon,1,$f):out/host/linux-x86/$(call word-colon,2,$f))
include $(BUILD_SYSTEM)/base_rules.mk

$(LOCAL_BUILT_MODULE):
	@mkdir -p $(dir $@)
	echo "This directory contains common data and tools needed for ART host tests" > $@

my_files :=
include $(CLEAR_VARS)
###################################################################################################

# The path for which all the dex files are relative, not actually the current directory.
LOCAL_PATH := art/test

include art/build/Android.common_test.mk
include art/build/Android.common_path.mk
include art/build/Android.common_build.mk

# Deprecated core.art dependencies.
HOST_CORE_IMAGE_DEFAULT_32 :=
HOST_CORE_IMAGE_DEFAULT_64 :=
TARGET_CORE_IMAGE_DEFAULT_32 :=
TARGET_CORE_IMAGE_DEFAULT_64 :=

# The elf writer test has dependencies on core.oat.
ART_GTEST_elf_writer_test_HOST_DEPS := $(HOST_CORE_IMAGE_DEFAULT_64) $(HOST_CORE_IMAGE_DEFAULT_32)
ART_GTEST_elf_writer_test_TARGET_DEPS := $(TARGET_CORE_IMAGE_DEFAULT_64) $(TARGET_CORE_IMAGE_DEFAULT_32)

# The two_runtimes_test test has dependencies on core.oat.
ART_GTEST_two_runtimes_test_HOST_DEPS := $(HOST_CORE_IMAGE_DEFAULT_64) $(HOST_CORE_IMAGE_DEFAULT_32)
ART_GTEST_two_runtimes_test_TARGET_DEPS := $(TARGET_CORE_IMAGE_DEFAULT_64) $(TARGET_CORE_IMAGE_DEFAULT_32)

# The transaction test has dependencies on core.oat.
ART_GTEST_transaction_test_HOST_DEPS := $(HOST_CORE_IMAGE_DEFAULT_64) $(HOST_CORE_IMAGE_DEFAULT_32)
ART_GTEST_transaction_test_TARGET_DEPS := $(TARGET_CORE_IMAGE_DEFAULT_64) $(TARGET_CORE_IMAGE_DEFAULT_32)

# The path for which all the source files are relative, not actually the current directory.
LOCAL_PATH := art

ART_TEST_MODULES := \
    art_cmdline_tests \
    art_compiler_host_tests \
    art_compiler_tests \
    art_dex2oat_tests \
    art_dexanalyze_tests \
    art_dexdiag_tests \
    art_dexdump_tests \
    art_dexlayout_tests \
    art_dexlist_tests \
    art_dexoptanalyzer_tests \
    art_hiddenapi_tests \
    art_imgdiag_tests \
    art_libartbase_tests \
    art_libartpalette_tests \
    art_libdexfile_external_tests \
    art_libdexfile_support_static_tests \
    art_libdexfile_support_tests \
    art_libdexfile_tests \
    art_libprofile_tests \
    art_oatdump_tests \
    art_profman_tests \
    art_runtime_compiler_tests \
    art_runtime_tests \
    art_sigchain_tests \

ART_TARGET_GTEST_NAMES := $(foreach tm,$(ART_TEST_MODULES),\
  $(foreach path,$(ART_TEST_LIST_device_$(TARGET_ARCH)_$(tm)),\
    $(notdir $(path))\
   )\
)

ART_HOST_GTEST_FILES := $(foreach m,$(ART_TEST_MODULES),\
    $(ART_TEST_LIST_host_$(ART_HOST_ARCH)_$(m)))

ifneq ($(HOST_PREFER_32_BIT),true)
2ND_ART_HOST_GTEST_FILES += $(foreach m,$(ART_TEST_MODULES),\
    $(ART_TEST_LIST_host_$(2ND_ART_HOST_ARCH)_$(m)))
endif

# Variables holding collections of gtest pre-requisits used to run a number of gtests.
ART_TEST_HOST_GTEST$(ART_PHONY_TEST_HOST_SUFFIX)_RULES :=
ART_TEST_HOST_GTEST$(2ND_ART_PHONY_TEST_HOST_SUFFIX)_RULES :=
ART_TEST_HOST_GTEST_RULES :=
ART_TEST_TARGET_GTEST$(ART_PHONY_TEST_TARGET_SUFFIX)_RULES :=
ART_TEST_TARGET_GTEST$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)_RULES :=
ART_TEST_TARGET_GTEST_RULES :=
ART_TEST_HOST_GTEST_DEPENDENCIES :=
ART_TEST_TARGET_GTEST_DEPENDENCIES :=

ART_GTEST_TARGET_ANDROID_ROOT := '/system'
ifneq ($(ART_TEST_ANDROID_ROOT),)
  ART_GTEST_TARGET_ANDROID_ROOT := $(ART_TEST_ANDROID_ROOT)
endif

ART_GTEST_TARGET_ANDROID_I18N_ROOT := '/apex/com.android.i18n'
ifneq ($(ART_TEST_ANDROID_I18N_ROOT),)
  ART_GTEST_TARGET_ANDROID_I18N_ROOT := $(ART_TEST_ANDROID_I18N_ROOT)
endif

ART_GTEST_TARGET_ANDROID_ART_ROOT := '/apex/com.android.art'
ifneq ($(ART_TEST_ANDROID_ART_ROOT),)
  ART_GTEST_TARGET_ANDROID_ART_ROOT := $(ART_TEST_ANDROID_ART_ROOT)
endif

ART_GTEST_TARGET_ANDROID_TZDATA_ROOT := '/apex/com.android.tzdata'
ifneq ($(ART_TEST_ANDROID_TZDATA_ROOT),)
  ART_GTEST_TARGET_ANDROID_TZDATA_ROOT := $(ART_TEST_ANDROID_TZDATA_ROOT)
endif

# Define make rules for a host gtests.
# $(1): gtest name - the name of the test we're building such as leb128_test.
# $(2): path relative to $OUT to the test binary
# $(3): 2ND_ or undefined - used to differentiate between the primary and secondary architecture.
define define-art-gtest-rule-host
  gtest_suffix := $(1)$$($(3)ART_PHONY_TEST_HOST_SUFFIX)
  gtest_rule := test-art-host-gtest-$$(gtest_suffix)
  gtest_output := $(call intermediates-dir-for,PACKAGING,art-host-gtest,HOST)/$$(gtest_suffix).xml
  $$(call dist-for-goals,$$(gtest_rule),$$(gtest_output):gtest/$$(gtest_suffix))
  gtest_exe := $(2)
  # Dependencies for all host gtests.
  gtest_deps := $$(ART_HOST_DEX_DEPENDENCIES) \
    $$(ART_TEST_HOST_GTEST_DEPENDENCIES) \
    $$(HOST_OUT)/$$(I18N_APEX)/timestamp \
    $$(HOST_BOOT_IMAGE_JARS) \
    $$($(3)ART_HOST_OUT_SHARED_LIBRARIES)/libicu_jni$$(ART_HOST_SHLIB_EXTENSION) \
    $$($(3)ART_HOST_OUT_SHARED_LIBRARIES)/libjavacore$$(ART_HOST_SHLIB_EXTENSION) \
    $$($(3)ART_HOST_OUT_SHARED_LIBRARIES)/libopenjdkd$$(ART_HOST_SHLIB_EXTENSION) \
    $$(gtest_exe) \
    $$(ART_GTEST_$(1)_HOST_DEPS) \
    $(foreach file,$(ART_GTEST_$(1)_DEX_DEPS),$(ART_TEST_HOST_GTEST_$(file)_DEX)) \
    $(HOST_OUT_EXECUTABLES)/signal_dumper

  # Note: The "host arch" Make variables defined in build/make/core/envsetup.mk
  # and art/build/Android.common.mk have different meanings:
  #
  #   * In build/make/core/envsetup.mk:
  #     * HOST_ARCH := x86_64
  #     * HOST_2ND_ARCH := x86
  #
  #   * In art/build/Android.common.mk:
  #     * When `HOST_PREFER_32_BIT` is `true`:
  #       * ART_HOST_ARCH := x86
  #       * 2ND_ART_HOST_ARCH :=
  #       * 2ND_HOST_ARCH :=
  #     * Otherwise:
  #       * ART_HOST_ARCH := x86_64
  #       * 2ND_ART_HOST_ARCH := x86
  #       * 2ND_HOST_ARCH := x86
  ifeq ($(HOST_PREFER_32_BIT),true)
    gtest_deps += $$(2ND_HOST_BOOT_IMAGE) # Depend on the 32-bit boot image.
  else
    gtest_deps += $$($(3)HOST_BOOT_IMAGE)
  endif

.PHONY: $$(gtest_rule)
$$(gtest_rule): $$(gtest_output)

# Re-run the tests, even if nothing changed. Until the build system has a dedicated "no cache"
# option, claim to write a file that is never produced.
$$(gtest_output): .KATI_IMPLICIT_OUTPUTS := $$(gtest_output)-nocache
# Limit concurrent runs. Each test itself is already highly parallel (and thus memory hungry).
$$(gtest_output): .KATI_NINJA_POOL := highmem_pool
$$(gtest_output): NAME := $$(gtest_rule)
ifeq (,$(SANITIZE_HOST))
$$(gtest_output): $$(gtest_exe) $$(gtest_deps)
	$(hide) ($$(call ART_TEST_SKIP,$$(NAME)) && \
		timeout --foreground -k 120s 2400s $(HOST_OUT_EXECUTABLES)/signal_dumper -s 15 \
			$$< --gtest_output=xml:$$@ && \
		$$(call ART_TEST_PASSED,$$(NAME))) || $$(call ART_TEST_FAILED,$$(NAME))
else
# Note: envsetup currently exports ASAN_OPTIONS=detect_leaks=0 to suppress leak detection, as some
#       build tools (e.g., ninja) intentionally leak. We want leak checks when we run our tests, so
#       override ASAN_OPTIONS. b/37751350
# Note 2: Under sanitization, also capture the output, and run it through the stack tool on failure
# (with the x86-64 ABI, as this allows symbolization of both x86 and x86-64). We don't do this in
# general as it loses all the color output, and we have our own symbolization step when not running
# under ASAN.
$$(gtest_output): $$(gtest_exe) $$(gtest_deps)
	$(hide) ($$(call ART_TEST_SKIP,$$(NAME)) && set -o pipefail && \
		ASAN_OPTIONS=detect_leaks=1 timeout --foreground -k 120s 3600s \
			$(HOST_OUT_EXECUTABLES)/signal_dumper -s 15 \
				$$< --gtest_output=xml:$$@ 2>&1 | tee $$<.tmp.out >&2 && \
		{ $$(call ART_TEST_PASSED,$$(NAME)) ; rm $$<.tmp.out ; }) || \
		( grep -q AddressSanitizer $$<.tmp.out && export ANDROID_BUILD_TOP=`pwd` && \
			{ echo "ABI: 'x86_64'" | cat - $$<.tmp.out | development/scripts/stack | tail -n 3000 ; } ; \
		rm $$<.tmp.out ; $$(call ART_TEST_FAILED,$$(NAME)))
endif

  ART_TEST_HOST_GTEST$$($(3)ART_PHONY_TEST_HOST_SUFFIX)_RULES += $$(gtest_rule)
  ART_TEST_HOST_GTEST_RULES += $$(gtest_rule)
  ART_TEST_HOST_GTEST_$(1)_RULES += $$(gtest_rule)


  # Clear locally defined variables.
  gtest_deps :=
  gtest_exe :=
  gtest_output :=
  gtest_rule :=
  gtest_suffix :=
endef  # define-art-gtest-rule-host

ART_TEST_HOST_GTEST_DEPENDENCIES := $(host-i18n-data-timestamp)
ART_TEST_TARGET_GTEST_DEPENDENCIES := $(TESTING_ART_APEX)

# Add the additional dependencies for the specified test
# $(1): test name
define add-art-gtest-dependencies
  # Note that, both the primary and the secondary arches of the libs are built by depending
  # on the module name.
  gtest_deps := \
    $$(ART_GTEST_$(1)_TARGET_DEPS) \
    $(foreach file,$(ART_GTEST_$(1)_DEX_DEPS),$(ART_TEST_TARGET_GTEST_$(file)_DEX)) \

  ART_TEST_TARGET_GTEST_DEPENDENCIES += $$(gtest_deps)

  # Clear locally defined variables.
  gtest_deps :=
endef  # add-art-gtest-dependencies

# $(1): file name
# $(2): 2ND_ or undefined - used to differentiate between the primary and secondary architecture.
define define-art-gtest-host
  art_gtest_filename := $(1)

  include $$(CLEAR_VARS)
  art_gtest_name := $$(notdir $$(basename $$(art_gtest_filename)))
  ifndef ART_TEST_HOST_GTEST_$$(art_gtest_name)_RULES
    ART_TEST_HOST_GTEST_$$(art_gtest_name)_RULES :=
  endif
  $$(eval $$(call define-art-gtest-rule-host,$$(art_gtest_name),$$(art_gtest_filename),$(2)))

  # Clear locally defined variables.
  art_gtest_filename :=
  art_gtest_name :=
endef  # define-art-gtest-host

# Define the rules to build and run gtests for both archs on host.
# $(1): test name
define define-art-gtest-host-both
  art_gtest_name := $(1)

.PHONY: test-art-host-gtest-$$(art_gtest_name)
test-art-host-gtest-$$(art_gtest_name): $$(ART_TEST_HOST_GTEST_$$(art_gtest_name)_RULES)
	$$(hide) $$(call ART_TEST_PREREQ_FINISHED,$$@)

  # Clear now unused variables.
  ART_TEST_HOST_GTEST_$$(art_gtest_name)_RULES :=
  art_gtest_name :=
endef  # define-art-gtest-host-both

ifeq ($(ART_BUILD_TARGET),true)
  $(foreach name,$(ART_TARGET_GTEST_NAMES), $(eval $(call add-art-gtest-dependencies,$(name),)))
  ART_TEST_TARGET_GTEST_DEPENDENCIES += \
    com.android.i18n \
    libjavacore.com.android.art.testing \
    libopenjdkd.com.android.art.testing \
    com.android.art.testing \
    com.android.conscrypt
endif
ifeq ($(ART_BUILD_HOST),true)
  $(foreach file,$(ART_HOST_GTEST_FILES), $(eval $(call define-art-gtest-host,$(file),)))
  ifneq ($(HOST_PREFER_32_BIT),true)
    $(foreach file,$(2ND_ART_HOST_GTEST_FILES), $(eval $(call define-art-gtest-host,$(file),2ND_)))
  endif
  # Rules to run the different architecture versions of the gtest.
  $(foreach file,$(ART_HOST_GTEST_FILES), $(eval $(call define-art-gtest-host-both,$$(notdir $$(basename $$(file))))))
endif

# Used outside the art project to get a list of the current tests
RUNTIME_TARGET_GTEST_MAKE_TARGETS :=
art_target_gtest_files := $(foreach m,$(ART_TEST_MODULES),$(ART_TEST_LIST_device_$(TARGET_ARCH)_$(m)))
# If testdir == testfile, assume this is not a test_per_src module
$(foreach file,$(art_target_gtest_files),\
  $(eval testdir := $$(notdir $$(patsubst %/,%,$$(dir $$(file)))))\
  $(eval testfile := $$(notdir $$(basename $$(file))))\
  $(if $(call streq,$(testdir),$(testfile)),,\
    $(eval testfile := $(testdir)_$(testfile)))\
  $(eval RUNTIME_TARGET_GTEST_MAKE_TARGETS += $(testfile))\
)
testdir :=
testfile :=
art_target_gtest_files :=

# Define all the combinations of host/target and suffix such as:
# test-art-host-gtest or test-art-host-gtest64
# $(1): host or target
# $(2): HOST or TARGET
# $(3): undefined, 32 or 64
define define-test-art-gtest-combination
  ifeq ($(1),host)
    ifneq ($(2),HOST)
      $$(error argument mismatch $(1) and ($2))
    endif
  else
    ifneq ($(1),target)
      $$(error found $(1) expected host or target)
    endif
    ifneq ($(2),TARGET)
      $$(error argument mismatch $(1) and ($2))
    endif
  endif

  rule_name := test-art-$(1)-gtest$(3)
  dependencies := $$(ART_TEST_$(2)_GTEST$(3)_RULES)

.PHONY: $$(rule_name)
$$(rule_name): $$(dependencies) d8
	$(hide) $$(call ART_TEST_PREREQ_FINISHED,$$@)

  # Clear locally defined variables.
  rule_name :=
  dependencies :=
endef  # define-test-art-gtest-combination

$(eval $(call define-test-art-gtest-combination,target,TARGET,))
$(eval $(call define-test-art-gtest-combination,target,TARGET,$(ART_PHONY_TEST_TARGET_SUFFIX)))
ifdef 2ND_ART_PHONY_TEST_TARGET_SUFFIX
$(eval $(call define-test-art-gtest-combination,target,TARGET,$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)))
endif
$(eval $(call define-test-art-gtest-combination,host,HOST,))
$(eval $(call define-test-art-gtest-combination,host,HOST,$(ART_PHONY_TEST_HOST_SUFFIX)))
ifneq ($(HOST_PREFER_32_BIT),true)
$(eval $(call define-test-art-gtest-combination,host,HOST,$(2ND_ART_PHONY_TEST_HOST_SUFFIX)))
endif

# Clear locally defined variables.
define-art-gtest-rule-target :=
define-art-gtest-rule-host :=
define-art-gtest :=
define-test-art-gtest-combination :=
RUNTIME_GTEST_COMMON_SRC_FILES :=
COMPILER_GTEST_COMMON_SRC_FILES :=
RUNTIME_GTEST_TARGET_SRC_FILES :=
RUNTIME_GTEST_HOST_SRC_FILES :=
COMPILER_GTEST_TARGET_SRC_FILES :=
COMPILER_GTEST_HOST_SRC_FILES :=
ART_TEST_HOST_GTEST$(ART_PHONY_TEST_HOST_SUFFIX)_RULES :=
ART_TEST_HOST_GTEST$(2ND_ART_PHONY_TEST_HOST_SUFFIX)_RULES :=
ART_TEST_HOST_GTEST_RULES :=
ART_TEST_TARGET_GTEST$(ART_PHONY_TEST_TARGET_SUFFIX)_RULES :=
ART_TEST_TARGET_GTEST$(2ND_ART_PHONY_TEST_TARGET_SUFFIX)_RULES :=
ART_TEST_TARGET_GTEST_RULES :=
ART_GTEST_TARGET_ANDROID_ROOT :=
ART_GTEST_TARGET_ANDROID_I18N_ROOT :=
ART_GTEST_TARGET_ANDROID_ART_ROOT :=
ART_GTEST_TARGET_ANDROID_TZDATA_ROOT :=
ART_GTEST_class_linker_test_DEX_DEPS :=
ART_GTEST_class_table_test_DEX_DEPS :=
ART_GTEST_compiler_driver_test_DEX_DEPS :=
ART_GTEST_dex_file_test_DEX_DEPS :=
ART_GTEST_exception_test_DEX_DEPS :=
ART_GTEST_elf_writer_test_HOST_DEPS :=
ART_GTEST_elf_writer_test_TARGET_DEPS :=
ART_GTEST_imtable_test_DEX_DEPS :=
ART_GTEST_jni_compiler_test_DEX_DEPS :=
ART_GTEST_jni_internal_test_DEX_DEPS :=
ART_GTEST_oat_file_assistant_test_DEX_DEPS :=
ART_GTEST_oat_file_assistant_test_HOST_DEPS :=
ART_GTEST_oat_file_assistant_test_TARGET_DEPS :=
ART_GTEST_dexanalyze_test_DEX_DEPS :=
ART_GTEST_dexoptanalyzer_test_DEX_DEPS :=
ART_GTEST_dexoptanalyzer_test_HOST_DEPS :=
ART_GTEST_dexoptanalyzer_test_TARGET_DEPS :=
ART_GTEST_image_space_test_DEX_DEPS :=
ART_GTEST_image_space_test_HOST_DEPS :=
ART_GTEST_image_space_test_TARGET_DEPS :=
ART_GTEST_dex2oat_test_DEX_DEPS :=
ART_GTEST_dex2oat_test_HOST_DEPS :=
ART_GTEST_dex2oat_test_TARGET_DEPS :=
ART_GTEST_dex2oat_image_test_DEX_DEPS :=
ART_GTEST_dex2oat_image_test_HOST_DEPS :=
ART_GTEST_dex2oat_image_test_TARGET_DEPS :=
ART_GTEST_module_exclusion_test_HOST_DEPS :=
ART_GTEST_module_exclusion_test_TARGET_DEPS :=
ART_GTEST_object_test_DEX_DEPS :=
ART_GTEST_proxy_test_DEX_DEPS :=
ART_GTEST_reflection_test_DEX_DEPS :=
ART_GTEST_stub_test_DEX_DEPS :=
ART_GTEST_transaction_test_DEX_DEPS :=
ART_GTEST_dex2oat_environment_tests_DEX_DEPS :=
ART_GTEST_heap_verification_test_DEX_DEPS :=
ART_GTEST_verifier_deps_test_DEX_DEPS :=
$(foreach dir,$(GTEST_DEX_DIRECTORIES), $(eval ART_TEST_TARGET_GTEST_$(dir)_DEX :=))
$(foreach dir,$(GTEST_DEX_DIRECTORIES), $(eval ART_TEST_HOST_GTEST_$(dir)_DEX :=))
ART_TEST_HOST_GTEST_MainStripped_DEX :=
ART_TEST_TARGET_GTEST_MainStripped_DEX :=
ART_TEST_HOST_GTEST_MainUncompressedAligned_DEX :=
ART_TEST_TARGET_GTEST_MainUncompressedAligned_DEX :=
ART_TEST_HOST_GTEST_EmptyUncompressed_DEX :=
ART_TEST_TARGET_GTEST_EmptyUncompressed_DEX :=
ART_TEST_GTEST_VerifierDeps_SRC :=
ART_TEST_HOST_GTEST_VerifierDeps_DEX :=
ART_TEST_TARGET_GTEST_VerifierDeps_DEX :=
ART_TEST_GTEST_VerifySoftFailDuringClinit_SRC :=
ART_TEST_HOST_GTEST_VerifySoftFailDuringClinit_DEX :=
ART_TEST_TARGET_GTEST_VerifySoftFailDuringClinit_DEX :=
GTEST_DEX_DIRECTORIES :=
LOCAL_PATH :=

endif # ifneq ($(HOST_OS),darwin)
