/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_COMPILER_DRIVER_COMPILER_OPTIONS_MAP_INL_H_
#define ART_COMPILER_DRIVER_COMPILER_OPTIONS_MAP_INL_H_

#include "compiler_options_map.h"

#include <memory>

#include "android-base/logging.h"
#include "android-base/macros.h"
#include "android-base/stringprintf.h"

#include "base/macros.h"
#include "cmdline_parser.h"
#include "compiler_options.h"

namespace art {

template <>
struct CmdlineType<CompilerFilter::Filter> : CmdlineTypeParser<CompilerFilter::Filter> {
  Result Parse(const std::string& option) {
    CompilerFilter::Filter compiler_filter;
    if (!CompilerFilter::ParseCompilerFilter(option.c_str(), &compiler_filter)) {
      return Result::Failure(
          android::base::StringPrintf("Unknown --compiler-filter value %s", option.c_str()));
    }
    return Result::Success(compiler_filter);
  }

  static const char* Name() {
    return "CompilerFilter";
  }
  static const char* DescribeType() {
    return CompilerFilter::DescribeOptions();
  }
};

template <class Base>
inline bool ReadCompilerOptions(Base& map, CompilerOptions* options, std::string* error_msg) {
  if (map.Exists(Base::CompilerFilter)) {
    options->SetCompilerFilter(*map.Get(Base::CompilerFilter));
  }
  map.AssignIfExists(Base::CompileArtTest, &options->compile_art_test_);
  map.AssignIfExists(Base::HugeMethodMaxThreshold, &options->huge_method_threshold_);
  map.AssignIfExists(Base::LargeMethodMaxThreshold, &options->large_method_threshold_);
  map.AssignIfExists(Base::NumDexMethodsThreshold, &options->num_dex_methods_threshold_);
  map.AssignIfExists(Base::InlineMaxCodeUnitsThreshold, &options->inline_max_code_units_);
  map.AssignIfExists(Base::GenerateDebugInfo, &options->generate_debug_info_);
  map.AssignIfExists(Base::GenerateMiniDebugInfo, &options->generate_mini_debug_info_);
  map.AssignIfExists(Base::GenerateBuildID, &options->generate_build_id_);
  if (map.Exists(Base::Debuggable)) {
    options->debuggable_ = true;
  }
  if (map.Exists(Base::Baseline)) {
    options->baseline_ = true;
  }
  map.AssignIfExists(Base::TopKProfileThreshold, &options->top_k_profile_threshold_);
  map.AssignIfExists(Base::AbortOnHardVerifierFailure, &options->abort_on_hard_verifier_failure_);
  map.AssignIfExists(Base::AbortOnSoftVerifierFailure, &options->abort_on_soft_verifier_failure_);
  if (map.Exists(Base::DumpInitFailures)) {
    if (!options->ParseDumpInitFailures(*map.Get(Base::DumpInitFailures), error_msg)) {
      return false;
    }
  }
  map.AssignIfExists(Base::DumpCFG, &options->dump_cfg_file_name_);
  if (map.Exists(Base::DumpCFGAppend)) {
    options->dump_cfg_append_ = true;
  }
  if (map.Exists(Base::RegisterAllocationStrategy)) {
    if (!options->ParseRegisterAllocationStrategy(*map.Get(Base::DumpInitFailures), error_msg)) {
      return false;
    }
  }
  map.AssignIfExists(Base::VerboseMethods, &options->verbose_methods_);
  options->deduplicate_code_ = map.GetOrDefault(Base::DeduplicateCode);
  if (map.Exists(Base::CountHotnessInCompiledCode)) {
    options->count_hotness_in_compiled_code_ = true;
  }
  map.AssignIfExists(Base::ResolveStartupConstStrings, &options->resolve_startup_const_strings_);
  map.AssignIfExists(Base::InitializeAppImageClasses, &options->initialize_app_image_classes_);
  if (map.Exists(Base::CheckProfiledMethods)) {
    options->check_profiled_methods_ = *map.Get(Base::CheckProfiledMethods);
  }
  map.AssignIfExists(Base::MaxImageBlockSize, &options->max_image_block_size_);

  if (map.Exists(Base::DumpTimings)) {
    options->dump_timings_ = true;
  }

  if (map.Exists(Base::DumpPassTimings)) {
    options->dump_pass_timings_ = true;
  }

  if (map.Exists(Base::DumpStats)) {
    options->dump_stats_ = true;
  }

  return true;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-larger-than="

template <typename Map, typename Builder>
inline void AddCompilerOptionsArgumentParserOptions(Builder& b) {
  b.
      Define("--compiler-filter=_")
          .template WithType<CompilerFilter::Filter>()
          .WithHelp("Select compiler filter\n"
                    "Default: speed-profile if profile provided, speed otherwise")
          .IntoKey(Map::CompilerFilter)

      .Define({"--compile-art-test", "--no-compile-art-test"})
          .WithValues({true, false})
          .IntoKey(Map::CompileArtTest)
      .Define("--huge-method-max=_")
          .template WithType<unsigned int>()
          .WithHelp("threshold size for a huge method for compiler filter tuning.")
          .IntoKey(Map::HugeMethodMaxThreshold)
      .Define("--large-method-max=_")
          .template WithType<unsigned int>()
          .WithHelp("threshold size for a large method for compiler filter tuning.")
          .IntoKey(Map::LargeMethodMaxThreshold)
      .Define("--num-dex-methods=_")
          .template WithType<unsigned int>()
          .WithHelp("threshold size for a small dex file for compiler filter tuning. If the input\n"
                    "has fewer than this many methods and the filter is not interpret-only or\n"
                    "verify-none or verify-at-runtime, overrides the filter to use speed")
          .IntoKey(Map::NumDexMethodsThreshold)
      .Define("--inline-max-code-units=_")
          .template WithType<unsigned int>()
          .WithHelp("the maximum code units that a methodcan have to be considered for inlining.\n"
                    "A zero value will disable inlining. Honored only by Optimizing. Has priority\n"
                    "over the --compiler-filter option. Intended for development/experimental use.")
          .IntoKey(Map::InlineMaxCodeUnitsThreshold)

      .Define({"--generate-debug-info", "-g", "--no-generate-debug-info"})
          .WithValues({true, true, false})
          .WithHelp("Generate (or don't generate) debug information for native debugging, such as\n"
                    "stack unwinding information, ELF symbols and dwarf sections. If used without\n"
                    "--debuggable it will be best effort only. Does not affect the generated\n"
                    "code. Disabled by default.")
          .IntoKey(Map::GenerateDebugInfo)
      .Define({"--generate-mini-debug-info", "--no-generate-mini-debug-info"})
          .WithValues({true, false})
          .WithHelp("Whether or not to generate minimal amount of LZMA-compressed debug\n"
                    "information necessary to print backtraces (disabled by default).")
          .IntoKey(Map::GenerateMiniDebugInfo)

      .Define({"--generate-build-id", "--no-generate-build-id"})
          .WithValues({true, false})
          .WithHelp("Generate GNU-compatible linker build ID ELF section with SHA-1 of the file\n"
                    "content (and thus stable across identical builds)")
          .IntoKey(Map::GenerateBuildID)

      .Define({"--deduplicate-code=_"})
          .template WithType<bool>()
          .WithValueMap({{"false", false}, {"true", true}})
          .WithHelp("enable|disable code deduplication. Deduplicated code will have an arbitrary\n"
                    "symbol tagged with [DEDUPED].")
          .IntoKey(Map::DeduplicateCode)

      .Define({"--count-hotness-in-compiled-code"})
          .IntoKey(Map::CountHotnessInCompiledCode)

      .Define({"--check-profiled-methods=_"})
          .template WithType<ProfileMethodsCheck>()
          .WithValueMap({{"log", ProfileMethodsCheck::kLog},
                         {"abort", ProfileMethodsCheck::kAbort}})
          .IntoKey(Map::CheckProfiledMethods)

      .Define({"--dump-timings"})
          .WithHelp("Display a breakdown of where time was spent.")
          .IntoKey(Map::DumpTimings)

      .Define({"--dump-pass-timings"})
          .WithHelp("Display a breakdown time spent in optimization passes for each compiled"
                    " method.")
          .IntoKey(Map::DumpPassTimings)

      .Define({"--dump-stats"})
          .WithHelp("Display overall compilation statistics.")
          .IntoKey(Map::DumpStats)

      .Define("--debuggable")
          .WithHelp("Produce code debuggable with a java-debugger.")
          .IntoKey(Map::Debuggable)

      .Define("--baseline")
          .WithHelp("Produce code using the baseline compilation")
          .IntoKey(Map::Baseline)

      .Define("--top-k-profile-threshold=_")
          .template WithType<double>().WithRange(0.0, 100.0)
          .IntoKey(Map::TopKProfileThreshold)

      .Define({"--abort-on-hard-verifier-error", "--no-abort-on-hard-verifier-error"})
          .WithValues({true, false})
          .IntoKey(Map::AbortOnHardVerifierFailure)
      .Define({"--abort-on-soft-verifier-error", "--no-abort-on-soft-verifier-error"})
          .WithValues({true, false})
          .IntoKey(Map::AbortOnSoftVerifierFailure)

      .Define("--dump-init-failures=_")
          .template WithType<std::string>()
          .IntoKey(Map::DumpInitFailures)

      .Define("--dump-cfg=_")
          .template WithType<std::string>()
          .WithHelp("Dump control-flow graphs (CFGs) to specified file.")
          .IntoKey(Map::DumpCFG)
      .Define("--dump-cfg-append")
          .WithHelp("when dumping CFGs to an existing file, append new CFG data to existing data\n"
                    "(instead of overwriting existing data with new data, which is the default\n"
                    "behavior). This option is only meaningful when used with --dump-cfg.")
          .IntoKey(Map::DumpCFGAppend)

      .Define("--register-allocation-strategy=_")
          .template WithType<std::string>()
          .IntoKey(Map::RegisterAllocationStrategy)

      .Define("--resolve-startup-const-strings=_")
          .template WithType<bool>()
          .WithValueMap({{"false", false}, {"true", true}})
          .WithHelp("If true, the compiler eagerly resolves strings referenced from const-string\n"
                    "of startup methods.")
          .IntoKey(Map::ResolveStartupConstStrings)

      .Define("--initialize-app-image-classes=_")
          .template WithType<bool>()
          .WithValueMap({{"false", false}, {"true", true}})
          .IntoKey(Map::InitializeAppImageClasses)

      .Define("--verbose-methods=_")
          .template WithType<ParseStringList<','>>()
          .WithHelp("Restrict the dumped CFG data to methods whose name is listed.\n"
                    "Eg: --verbose-methods=toString,hashCode")
          .IntoKey(Map::VerboseMethods)

      .Define("--max-image-block-size=_")
          .template WithType<unsigned int>()
          .WithHelp("Maximum solid block size for compressed images.")
          .IntoKey(Map::MaxImageBlockSize);
}

#pragma GCC diagnostic pop

}  // namespace art

#endif  // ART_COMPILER_DRIVER_COMPILER_OPTIONS_MAP_INL_H_
