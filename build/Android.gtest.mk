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

# The path for which all the dex files are relative, not actually the current directory.
LOCAL_PATH := art/test

include art/build/Android.common_test.mk
include art/build/Android.common_path.mk
include art/build/Android.common_build.mk

# Subdirectories in art/test which contain dex files used as inputs for gtests.
GTEST_DEX_DIRECTORIES := \
  AbstractMethod \
  AllFields \
  DefaultMethods \
  DexToDexDecompiler \
  Dex2oatVdexTestDex \
  ErroneousA \
  ErroneousB \
  ErroneousInit \
  Extension1 \
  Extension2 \
  ForClassLoaderA \
  ForClassLoaderB \
  ForClassLoaderC \
  ForClassLoaderD \
  ExceptionHandle \
  GetMethodSignature \
  HiddenApi \
  HiddenApiSignatures \
  HiddenApiStubs \
  ImageLayoutA \
  ImageLayoutB \
  IMTA \
  IMTB \
  Instrumentation \
  Interfaces \
  Lookup \
  Main \
  ManyMethods \
  MethodTypes \
  MultiDex \
  MultiDexModifiedSecondary \
  MyClass \
  MyClassNatives \
  Nested \
  NonStaticLeafMethods \
  Packages \
  ProtoCompare \
  ProtoCompare2 \
  ProfileTestMultiDex \
  StaticLeafMethods \
  Statics \
  StaticsFromCode \
  StringLiterals \
  Transaction \
  XandY

# Create build rules for each dex file recording the dependency.
$(foreach dir,$(GTEST_DEX_DIRECTORIES), $(eval $(call build-art-test-dex,art-gtest,$(dir), \
  $(ART_TARGET_NATIVETEST_OUT),art/build/Android.gtest.mk,ART_TEST_TARGET_GTEST_$(dir)_DEX, \
  ART_TEST_HOST_GTEST_$(dir)_DEX)))

# Create rules for MainStripped, a copy of Main with the classes.dex stripped
# for the oat file assistant tests.
ART_TEST_HOST_GTEST_MainStripped_DEX := $(basename $(ART_TEST_HOST_GTEST_Main_DEX))Stripped$(suffix $(ART_TEST_HOST_GTEST_Main_DEX))
ART_TEST_TARGET_GTEST_MainStripped_DEX := $(basename $(ART_TEST_TARGET_GTEST_Main_DEX))Stripped$(suffix $(ART_TEST_TARGET_GTEST_Main_DEX))

# Create rules for MainUncompressedAligned, a copy of Main with the classes.dex uncompressed
# for the dex2oat tests.
ART_TEST_HOST_GTEST_MainUncompressedAligned_DEX := $(basename $(ART_TEST_HOST_GTEST_Main_DEX))UncompressedAligned$(suffix $(ART_TEST_HOST_GTEST_Main_DEX))
ART_TEST_TARGET_GTEST_MainUncompressedAligned_DEX := $(basename $(ART_TEST_TARGET_GTEST_Main_DEX))UncompressedAligned$(suffix $(ART_TEST_TARGET_GTEST_Main_DEX))

# Create rules for UncompressedEmpty, a classes.dex that is empty and uncompressed
# for the dex2oat tests.
ART_TEST_HOST_GTEST_EmptyUncompressed_DEX := $(basename $(ART_TEST_HOST_GTEST_Main_DEX))EmptyUncompressed$(suffix $(ART_TEST_HOST_GTEST_Main_DEX))
ART_TEST_TARGET_GTEST_EmptyUncompressed_DEX := $(basename $(ART_TEST_TARGET_GTEST_Main_DEX))EmptyUncompressed$(suffix $(ART_TEST_TARGET_GTEST_Main_DEX))

# Create rules for UncompressedEmptyAligned, a classes.dex that is empty, uncompressed,
# and 4 byte aligned for the dex2oat tests.
ART_TEST_HOST_GTEST_EmptyUncompressedAligned_DEX := $(basename $(ART_TEST_HOST_GTEST_Main_DEX))EmptyUncompressedAligned$(suffix $(ART_TEST_HOST_GTEST_Main_DEX))
ART_TEST_TARGET_GTEST_EmptyUncompressedAligned_DEX := $(basename $(ART_TEST_TARGET_GTEST_Main_DEX))EmptyUncompressedAligned$(suffix $(ART_TEST_TARGET_GTEST_Main_DEX))

# Create rules for MultiDexUncompressedAligned, a copy of MultiDex with the classes.dex uncompressed
# for the OatFile tests.
ART_TEST_HOST_GTEST_MultiDexUncompressedAligned_DEX := $(basename $(ART_TEST_HOST_GTEST_MultiDex_DEX))UncompressedAligned$(suffix $(ART_TEST_HOST_GTEST_MultiDex_DEX))
ART_TEST_TARGET_GTEST_MultiDexUncompressedAligned_DEX := $(basename $(ART_TEST_TARGET_GTEST_MultiDex_DEX))UncompressedAligned$(suffix $(ART_TEST_TARGET_GTEST_MultiDex_DEX))

ifdef ART_TEST_HOST_GTEST_Main_DEX
$(ART_TEST_HOST_GTEST_MainStripped_DEX): $(ART_TEST_HOST_GTEST_Main_DEX)
	cp $< $@
	$(call dexpreopt-remove-classes.dex,$@)
endif

ifdef ART_TEST_TARGET_GTEST_Main_DEX
$(ART_TEST_TARGET_GTEST_MainStripped_DEX): $(ART_TEST_TARGET_GTEST_Main_DEX)
	cp $< $@
	$(call dexpreopt-remove-classes.dex,$@)
endif

ifdef ART_TEST_HOST_GTEST_Main_DEX
$(ART_TEST_HOST_GTEST_MainUncompressedAligned_DEX): $(ART_TEST_HOST_GTEST_Main_DEX) $(ZIPALIGN)
	cp $< $@
	$(call uncompress-dexs, $@)
	$(call align-package, $@)
endif

ifdef ART_TEST_TARGET_GTEST_Main_DEX
$(ART_TEST_TARGET_GTEST_MainUncompressedAligned_DEX): $(ART_TEST_TARGET_GTEST_Main_DEX) $(ZIPALIGN)
	cp $< $@
	$(call uncompress-dexs, $@)
	$(call align-package, $@)
endif

ifdef ART_TEST_HOST_GTEST_Main_DEX
$(ART_TEST_HOST_GTEST_EmptyUncompressed_DEX):
	touch $@_classes.dex
	zip -j -qD -X -0 $@ $@_classes.dex
	rm $@_classes.dex
endif

ifdef ART_TEST_TARGET_GTEST_Main_DEX
$(ART_TEST_TARGET_GTEST_EmptyUncompressed_DEX):
	touch $@_classes.dex
	zip -j -qD -X -0 $@ $@_classes.dex
	rm $@_classes.dex
endif

ifdef ART_TEST_HOST_GTEST_Main_DEX
$(ART_TEST_HOST_GTEST_EmptyUncompressedAligned_DEX): $(ZIPALIGN)
	touch $@_classes.dex
	zip -j -0 $@_temp.zip $@_classes.dex
	$(ZIPALIGN) -f 4 $@_temp.zip $@
	rm $@_classes.dex
	rm $@_temp.zip
endif

ifdef ART_TEST_TARGET_GTEST_Main_DEX
$(ART_TEST_TARGET_GTEST_EmptyUncompressedAligned_DEX): $(ZIPALIGN)
	touch $@_classes.dex
	zip -j -0 $@_temp.zip $@_classes.dex
	$(ZIPALIGN) -f 4 $@_temp.zip $@
	rm $@_classes.dex
	rm $@_temp.zip
endif

ifdef ART_TEST_HOST_GTEST_MultiDex_DEX
$(ART_TEST_HOST_GTEST_MultiDexUncompressedAligned_DEX): $(ART_TEST_HOST_GTEST_MultiDex_DEX) $(ZIPALIGN)
	cp $< $@
	$(call uncompress-dexs, $@)
	$(call align-package, $@)
endif

ifdef ART_TEST_TARGET_GTEST_MultiDex_DEX
$(ART_TEST_TARGET_GTEST_MultiDexUncompressedAligned_DEX): $(ART_TEST_TARGET_GTEST_MultiDex_DEX) $(ZIPALIGN)
	cp $< $@
	$(call uncompress-dexs, $@)
	$(call align-package, $@)
endif

ART_TEST_GTEST_VerifierDeps_SRC := $(abspath $(wildcard $(LOCAL_PATH)/VerifierDeps/*.smali))
ART_TEST_GTEST_VerifierDepsMulti_SRC := $(abspath $(wildcard $(LOCAL_PATH)/VerifierDepsMulti/*.smali))
ART_TEST_HOST_GTEST_VerifierDeps_DEX := $(dir $(ART_TEST_HOST_GTEST_Main_DEX))$(subst Main,VerifierDeps,$(basename $(notdir $(ART_TEST_HOST_GTEST_Main_DEX))))$(suffix $(ART_TEST_HOST_GTEST_Main_DEX))
ART_TEST_TARGET_GTEST_VerifierDeps_DEX := $(dir $(ART_TEST_TARGET_GTEST_Main_DEX))$(subst Main,VerifierDeps,$(basename $(notdir $(ART_TEST_TARGET_GTEST_Main_DEX))))$(suffix $(ART_TEST_TARGET_GTEST_Main_DEX))
ART_TEST_HOST_GTEST_VerifierDepsMulti_DEX := $(dir $(ART_TEST_HOST_GTEST_Main_DEX))$(subst Main,VerifierDepsMulti,$(basename $(notdir $(ART_TEST_HOST_GTEST_Main_DEX))))$(suffix $(ART_TEST_HOST_GTEST_Main_DEX))
ART_TEST_TARGET_GTEST_VerifierDepsMulti_DEX := $(dir $(ART_TEST_TARGET_GTEST_Main_DEX))$(subst Main,VerifierDepsMulti,$(basename $(notdir $(ART_TEST_TARGET_GTEST_Main_DEX))))$(suffix $(ART_TEST_TARGET_GTEST_Main_DEX))

$(ART_TEST_HOST_GTEST_VerifierDeps_DEX): $(ART_TEST_GTEST_VerifierDeps_SRC) $(HOST_OUT_EXECUTABLES)/smali
	 $(HOST_OUT_EXECUTABLES)/smali assemble --output $@ $(filter %.smali,$^)

$(ART_TEST_TARGET_GTEST_VerifierDeps_DEX): $(ART_TEST_GTEST_VerifierDeps_SRC) $(HOST_OUT_EXECUTABLES)/smali
	 $(HOST_OUT_EXECUTABLES)/smali assemble --output $@ $(filter %.smali,$^)

$(ART_TEST_HOST_GTEST_VerifierDepsMulti_DEX): $(ART_TEST_GTEST_VerifierDepsMulti_SRC) $(HOST_OUT_EXECUTABLES)/smali
	 $(HOST_OUT_EXECUTABLES)/smali assemble --output $@ $(filter %.smali,$^)

$(ART_TEST_TARGET_GTEST_VerifierDepsMulti_DEX): $(ART_TEST_GTEST_VerifierDepsMulti_SRC) $(HOST_OUT_EXECUTABLES)/smali
	 $(HOST_OUT_EXECUTABLES)/smali assemble --output $@ $(filter %.smali,$^)

ART_TEST_GTEST_VerifySoftFailDuringClinit_SRC := $(abspath $(wildcard $(LOCAL_PATH)/VerifySoftFailDuringClinit/*.smali))
ART_TEST_HOST_GTEST_VerifySoftFailDuringClinit_DEX := $(dir $(ART_TEST_HOST_GTEST_Main_DEX))$(subst Main,VerifySoftFailDuringClinit,$(basename $(notdir $(ART_TEST_HOST_GTEST_Main_DEX))))$(suffix $(ART_TEST_HOST_GTEST_Main_DEX))
ART_TEST_TARGET_GTEST_VerifySoftFailDuringClinit_DEX := $(dir $(ART_TEST_TARGET_GTEST_Main_DEX))$(subst Main,VerifySoftFailDuringClinit,$(basename $(notdir $(ART_TEST_TARGET_GTEST_Main_DEX))))$(suffix $(ART_TEST_TARGET_GTEST_Main_DEX))

$(ART_TEST_HOST_GTEST_VerifySoftFailDuringClinit_DEX): $(ART_TEST_GTEST_VerifySoftFailDuringClinit_SRC) $(HOST_OUT_EXECUTABLES)/smali
	 $(HOST_OUT_EXECUTABLES)/smali assemble --output $@ $(filter %.smali,$^)

$(ART_TEST_TARGET_GTEST_VerifySoftFailDuringClinit_DEX): $(ART_TEST_GTEST_VerifySoftFailDuringClinit_SRC) $(HOST_OUT_EXECUTABLES)/smali
	 $(HOST_OUT_EXECUTABLES)/smali assemble --output $@ $(filter %.smali,$^)

# Dex file dependencies for each gtest.
ART_GTEST_art_dex_file_loader_test_DEX_DEPS := GetMethodSignature Main Nested MultiDex
ART_GTEST_dex2oat_environment_tests_DEX_DEPS := Main MainStripped MultiDex MultiDexModifiedSecondary MyClassNatives Nested VerifierDeps VerifierDepsMulti

ART_GTEST_atomic_dex_ref_map_test_DEX_DEPS := Interfaces
ART_GTEST_class_linker_test_DEX_DEPS := AllFields ErroneousA ErroneousB ErroneousInit ForClassLoaderA ForClassLoaderB ForClassLoaderC ForClassLoaderD Interfaces MethodTypes MultiDex MyClass Nested Statics StaticsFromCode
ART_GTEST_class_loader_context_test_DEX_DEPS := Main MultiDex MyClass ForClassLoaderA ForClassLoaderB ForClassLoaderC ForClassLoaderD
ART_GTEST_class_table_test_DEX_DEPS := XandY
ART_GTEST_compiler_driver_test_DEX_DEPS := AbstractMethod StaticLeafMethods ProfileTestMultiDex
ART_GTEST_dex_cache_test_DEX_DEPS := Main Packages MethodTypes
ART_GTEST_dexanalyze_test_DEX_DEPS := MultiDex
ART_GTEST_dexlayout_test_DEX_DEPS := ManyMethods
ART_GTEST_dex2oat_test_DEX_DEPS := $(ART_GTEST_dex2oat_environment_tests_DEX_DEPS) Dex2oatVdexTestDex ManyMethods Statics VerifierDeps MainUncompressedAligned EmptyUncompressed EmptyUncompressedAligned StringLiterals
ART_GTEST_dex2oat_image_test_DEX_DEPS := $(ART_GTEST_dex2oat_environment_tests_DEX_DEPS) Statics VerifierDeps
ART_GTEST_exception_test_DEX_DEPS := ExceptionHandle
ART_GTEST_hiddenapi_test_DEX_DEPS := HiddenApi HiddenApiStubs
ART_GTEST_hidden_api_test_DEX_DEPS := HiddenApiSignatures Main MultiDex
ART_GTEST_image_test_DEX_DEPS := ImageLayoutA ImageLayoutB DefaultMethods VerifySoftFailDuringClinit
ART_GTEST_imtable_test_DEX_DEPS := IMTA IMTB
ART_GTEST_instrumentation_test_DEX_DEPS := Instrumentation
ART_GTEST_jni_compiler_test_DEX_DEPS := MyClassNatives
ART_GTEST_jni_internal_test_DEX_DEPS := AllFields StaticLeafMethods MyClassNatives
ART_GTEST_oat_file_assistant_test_DEX_DEPS := $(ART_GTEST_dex2oat_environment_tests_DEX_DEPS)
ART_GTEST_dexoptanalyzer_test_DEX_DEPS := $(ART_GTEST_dex2oat_environment_tests_DEX_DEPS)
ART_GTEST_image_space_test_DEX_DEPS := $(ART_GTEST_dex2oat_environment_tests_DEX_DEPS) Extension1 Extension2
ART_GTEST_oat_file_test_DEX_DEPS := Main MultiDex MainUncompressedAligned MultiDexUncompressedAligned MainStripped Nested MultiDexModifiedSecondary
ART_GTEST_oat_test_DEX_DEPS := Main
ART_GTEST_oat_writer_test_DEX_DEPS := Main
# two_runtimes_test build off dex2oat_environment_test, which does sanity checks on the following dex files.
ART_GTEST_two_runtimes_test_DEX_DEPS := Main MainStripped Nested MultiDex MultiDexModifiedSecondary
ART_GTEST_object_test_DEX_DEPS := ProtoCompare ProtoCompare2 StaticsFromCode XandY
ART_GTEST_proxy_test_DEX_DEPS := Interfaces
ART_GTEST_reflection_test_DEX_DEPS := Main NonStaticLeafMethods StaticLeafMethods
ART_GTEST_profile_assistant_test_DEX_DEPS := ProfileTestMultiDex
ART_GTEST_profile_compilation_info_test_DEX_DEPS := ManyMethods ProfileTestMultiDex
ART_GTEST_profile_boot_info_test_DEX_DEPS := ManyMethods ProfileTestMultiDex MultiDex
ART_GTEST_profiling_info_test_DEX_DEPS := ProfileTestMultiDex
ART_GTEST_runtime_callbacks_test_DEX_DEPS := XandY
ART_GTEST_stub_test_DEX_DEPS := AllFields
ART_GTEST_transaction_test_DEX_DEPS := Transaction
ART_GTEST_type_lookup_table_test_DEX_DEPS := Lookup
ART_GTEST_unstarted_runtime_test_DEX_DEPS := Nested
ART_GTEST_heap_verification_test_DEX_DEPS := ProtoCompare ProtoCompare2 StaticsFromCode XandY
ART_GTEST_verifier_deps_test_DEX_DEPS := VerifierDeps VerifierDepsMulti MultiDex
ART_GTEST_dex_to_dex_decompiler_test_DEX_DEPS := VerifierDeps DexToDexDecompiler
ART_GTEST_oatdump_app_test_DEX_DEPS := ProfileTestMultiDex
ART_GTEST_oatdump_test_DEX_DEPS := ProfileTestMultiDex
ART_GTEST_reg_type_test_DEX_DEPS := Interfaces

# The elf writer test has dependencies on core.oat.
ART_GTEST_elf_writer_test_HOST_DEPS := $(HOST_CORE_IMAGE_DEFAULT_64) $(HOST_CORE_IMAGE_DEFAULT_32)
ART_GTEST_elf_writer_test_TARGET_DEPS := $(TARGET_CORE_IMAGE_DEFAULT_64) $(TARGET_CORE_IMAGE_DEFAULT_32)

# The two_runtimes_test test has dependencies on core.oat.
ART_GTEST_two_runtimes_test_HOST_DEPS := $(HOST_CORE_IMAGE_DEFAULT_64) $(HOST_CORE_IMAGE_DEFAULT_32)
ART_GTEST_two_runtimes_test_TARGET_DEPS := $(TARGET_CORE_IMAGE_DEFAULT_64) $(TARGET_CORE_IMAGE_DEFAULT_32)

# The transaction test has dependencies on core.oat.
ART_GTEST_transaction_test_HOST_DEPS := $(HOST_CORE_IMAGE_DEFAULT_64) $(HOST_CORE_IMAGE_DEFAULT_32)
ART_GTEST_transaction_test_TARGET_DEPS := $(TARGET_CORE_IMAGE_DEFAULT_64) $(TARGET_CORE_IMAGE_DEFAULT_32)

# The dex2oat_vdex_test test has dependencies on core.oat.
ART_GTEST_dex2oat_vdex_test_HOST_DEPS := $(HOST_CORE_IMAGE_DEFAULT_64) $(HOST_CORE_IMAGE_DEFAULT_32)
ART_GTEST_dex2oat_vdex_test_TARGET_DEPS := $(TARGET_CORE_IMAGE_DEFAULT_64) $(TARGET_CORE_IMAGE_DEFAULT_32)

ART_GTEST_dex2oat_environment_tests_HOST_DEPS := \
  $(HOST_CORE_IMAGE_optimizing_64) \
  $(HOST_CORE_IMAGE_optimizing_32) \
  $(HOST_CORE_IMAGE_interpreter_64) \
  $(HOST_CORE_IMAGE_interpreter_32)
ART_GTEST_dex2oat_environment_tests_TARGET_DEPS := \
  $(TARGET_CORE_IMAGE_optimizing_64) \
  $(TARGET_CORE_IMAGE_optimizing_32) \
  $(TARGET_CORE_IMAGE_interpreter_64) \
  $(TARGET_CORE_IMAGE_interpreter_32)

ART_GTEST_oat_file_test_HOST_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_HOST_DEPS) \
  $(HOST_OUT_EXECUTABLES)/dex2oatd
ART_GTEST_oat_file_test_TARGET_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_TARGET_DEPS) \
  dex2oatd.com.android.art.debug

ART_GTEST_oat_file_assistant_test_HOST_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_HOST_DEPS)
ART_GTEST_oat_file_assistant_test_TARGET_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_TARGET_DEPS)

ART_GTEST_dexoptanalyzer_test_HOST_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_HOST_DEPS) \
  $(HOST_OUT_EXECUTABLES)/dexoptanalyzerd
ART_GTEST_dexoptanalyzer_test_TARGET_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_TARGET_DEPS) \
  $(TESTING_ART_APEX)  # For dexoptanalyzerd.

ART_GTEST_image_space_test_HOST_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_HOST_DEPS)
ART_GTEST_image_space_test_TARGET_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_TARGET_DEPS)

ART_GTEST_dex2oat_test_HOST_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_HOST_DEPS) \
  $(HOST_OUT_EXECUTABLES)/dex2oatd
ART_GTEST_dex2oat_test_TARGET_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_TARGET_DEPS) \
  $(TESTING_ART_APEX)  # For dex2oatd.

ART_GTEST_dex2oat_image_test_HOST_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_HOST_DEPS) \
  $(HOST_OUT_EXECUTABLES)/dex2oatd
ART_GTEST_dex2oat_image_test_TARGET_DEPS := \
  $(ART_GTEST_dex2oat_environment_tests_TARGET_DEPS) \
  $(TESTING_ART_APEX)  # For dex2oatd.

ART_GTEST_module_exclusion_test_HOST_DEPS := \
  $(ART_GTEST_dex2oat_image_test_HOST_DEPS)
ART_GTEST_module_exclusion_test_TARGET_DEPS := \
  $(ART_GTEST_dex2oat_image_test_TARGET_DEPS)

# TODO: document why this is needed.
ART_GTEST_proxy_test_HOST_DEPS := $(HOST_CORE_IMAGE_DEFAULT_64) $(HOST_CORE_IMAGE_DEFAULT_32)

# The dexdiag test requires the dexdiag utility.
ART_GTEST_dexdiag_test_HOST_DEPS := $(HOST_OUT_EXECUTABLES)/dexdiag
ART_GTEST_dexdiag_test_TARGET_DEPS := $(TESTING_ART_APEX)  # For dexdiag.

# The dexdump test requires an image and the dexdump utility.
# TODO: rename into dexdump when migration completes
ART_GTEST_dexdump_test_HOST_DEPS := \
  $(HOST_CORE_IMAGE_DEFAULT_64) \
  $(HOST_CORE_IMAGE_DEFAULT_32) \
  $(HOST_OUT_EXECUTABLES)/dexdump
ART_GTEST_dexdump_test_TARGET_DEPS := \
  $(TARGET_CORE_IMAGE_DEFAULT_64) \
  $(TARGET_CORE_IMAGE_DEFAULT_32) \
  dexdump.com.android.art.debug

# The dexanalyze test requires an image and the dexanalyze utility.
ART_GTEST_dexanalyze_test_HOST_DEPS := \
  $(HOST_CORE_IMAGE_DEFAULT_64) \
  $(HOST_CORE_IMAGE_DEFAULT_32) \
  $(HOST_OUT_EXECUTABLES)/dexanalyze
ART_GTEST_dexanalyze_test_TARGET_DEPS := \
  $(TARGET_CORE_IMAGE_DEFAULT_64) \
  $(TARGET_CORE_IMAGE_DEFAULT_32) \
  dexanalyze.com.android.art.debug

# The dexlayout test requires an image and the dexlayout utility.
# TODO: rename into dexdump when migration completes
ART_GTEST_dexlayout_test_HOST_DEPS := \
  $(HOST_CORE_IMAGE_DEFAULT_64) \
  $(HOST_CORE_IMAGE_DEFAULT_32) \
  $(HOST_OUT_EXECUTABLES)/dexlayoutd \
  $(HOST_OUT_EXECUTABLES)/dexdump
ART_GTEST_dexlayout_test_TARGET_DEPS := \
  $(TARGET_CORE_IMAGE_DEFAULT_64) \
  $(TARGET_CORE_IMAGE_DEFAULT_32) \
  dexlayoutd.com.android.art.debug \
  dexdump.com.android.art.debug

# The dexlist test requires an image and the dexlist utility.
ART_GTEST_dexlist_test_HOST_DEPS := \
  $(HOST_CORE_IMAGE_DEFAULT_64) \
  $(HOST_CORE_IMAGE_DEFAULT_32) \
  $(HOST_OUT_EXECUTABLES)/dexlist
ART_GTEST_dexlist_test_TARGET_DEPS := \
  $(TARGET_CORE_IMAGE_DEFAULT_64) \
  $(TARGET_CORE_IMAGE_DEFAULT_32) \
  $(TESTING_ART_APEX)   # For dexlist.

# The imgdiag test has dependencies on core.oat since it needs to load it during the test.
# For the host, also add the installed tool (in the base size, that should suffice). For the
# target, just the module is fine, the sync will happen late enough.
ART_GTEST_imgdiag_test_HOST_DEPS := \
  $(HOST_CORE_IMAGE_DEFAULT_64) \
  $(HOST_CORE_IMAGE_DEFAULT_32) \
  $(HOST_OUT_EXECUTABLES)/imgdiagd
ART_GTEST_imgdiag_test_TARGET_DEPS := \
  $(TARGET_CORE_IMAGE_DEFAULT_64) \
  $(TARGET_CORE_IMAGE_DEFAULT_32) \
  imgdiagd.com.android.art.debug

# Dex analyze test requires dexanalyze.
ART_GTEST_dexanalyze_test_HOST_DEPS := \
  $(HOST_OUT_EXECUTABLES)/dexanalyze
ART_GTEST_dexanalyze_test_TARGET_DEPS := \
  dexanalyze.com.android.art.debug

# Oatdump test requires an image and oatfile to dump.
ART_GTEST_oatdump_test_HOST_DEPS := \
  $(HOST_CORE_IMAGE_DEFAULT_64) \
  $(HOST_CORE_IMAGE_DEFAULT_32) \
  $(HOST_OUT_EXECUTABLES)/oatdumpd \
  $(HOST_OUT_EXECUTABLES)/oatdumpds \
  $(HOST_OUT_EXECUTABLES)/dexdump \
  $(HOST_OUT_EXECUTABLES)/dex2oatd \
  $(HOST_OUT_EXECUTABLES)/dex2oatds
ART_GTEST_oatdump_test_TARGET_DEPS := \
  $(TARGET_CORE_IMAGE_DEFAULT_64) \
  $(TARGET_CORE_IMAGE_DEFAULT_32) \
  $(TESTING_ART_APEX)    # For oatdumpd, dexdump, dex2oatd.
ART_GTEST_oatdump_image_test_HOST_DEPS := $(ART_GTEST_oatdump_test_HOST_DEPS)
ART_GTEST_oatdump_image_test_TARGET_DEPS := $(ART_GTEST_oatdump_test_TARGET_DEPS)
ART_GTEST_oatdump_app_test_HOST_DEPS := $(ART_GTEST_oatdump_test_HOST_DEPS)
ART_GTEST_oatdump_app_test_TARGET_DEPS := $(ART_GTEST_oatdump_test_TARGET_DEPS)

# Profile assistant tests requires profman utility.
ART_GTEST_profile_assistant_test_HOST_DEPS := $(HOST_OUT_EXECUTABLES)/profmand
ART_GTEST_profile_assistant_test_TARGET_DEPS := $(TESTING_ART_APEX)  # For profmand.

ART_GTEST_hiddenapi_test_HOST_DEPS := \
  $(HOST_CORE_IMAGE_DEFAULT_64) \
  $(HOST_CORE_IMAGE_DEFAULT_32) \
  $(HOST_OUT_EXECUTABLES)/hiddenapid

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
  gtest_deps := $$(HOST_CORE_DEX_LOCATIONS) \
    $$($(3)ART_HOST_OUT_SHARED_LIBRARIES)/libicu_jni$$(ART_HOST_SHLIB_EXTENSION) \
    $$($(3)ART_HOST_OUT_SHARED_LIBRARIES)/libjavacore$$(ART_HOST_SHLIB_EXTENSION) \
    $$($(3)ART_HOST_OUT_SHARED_LIBRARIES)/libopenjdkd$$(ART_HOST_SHLIB_EXTENSION) \
    $$(gtest_exe) \
    $$(ART_GTEST_$(1)_HOST_DEPS) \
    $(foreach file,$(ART_GTEST_$(1)_DEX_DEPS),$(ART_TEST_HOST_GTEST_$(file)_DEX)) \
    $(HOST_OUT_EXECUTABLES)/signal_dumper

  ART_TEST_HOST_GTEST_DEPENDENCIES += $$(gtest_deps)

.PHONY: $$(gtest_rule)
$$(gtest_rule): $$(gtest_output)

# Re-run the tests, even if nothing changed. Until the build system has a dedicated "no cache"
# option, claim to write a file that is never produced.
$$(gtest_output): .KATI_IMPLICIT_OUTPUTS := $$(gtest_output)-nocache
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
    libicu_jni.com.android.art.debug \
    libjavacore.com.android.art.debug \
    libopenjdkd.com.android.art.debug \
    $(foreach jar,$(TARGET_TEST_CORE_JARS),$(TARGET_OUT_JAVA_LIBRARIES)/$(jar).jar)
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
