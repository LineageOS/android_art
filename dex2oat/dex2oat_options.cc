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

#include "dex2oat_options.h"

#include <memory>

#include "cmdline_parser.h"
#include "driver/compiler_options_map-inl.h"

namespace art {

template<>
struct CmdlineType<InstructionSet> : CmdlineTypeParser<InstructionSet> {
  Result Parse(const std::string& option) {
    InstructionSet set = GetInstructionSetFromString(option.c_str());
    if (set == InstructionSet::kNone) {
      return Result::Failure(std::string("Not a valid instruction set: '") + option + "'");
    }
    return Result::Success(set);
  }

  static const char* Name() { return "InstructionSet"; }
  static const char* DescribeType() { return "arm|arm64|x86|x86_64|none"; }
};

#define COMPILER_OPTIONS_MAP_TYPE Dex2oatArgumentMap
#define COMPILER_OPTIONS_MAP_KEY_TYPE Dex2oatArgumentMapKey
#include "driver/compiler_options_map-storage.h"

// Specify storage for the Dex2oatOptions keys.

#define DEX2OAT_OPTIONS_KEY(Type, Name, ...) \
  const Dex2oatArgumentMap::Key<Type> Dex2oatArgumentMap::Name {__VA_ARGS__};
#include "dex2oat_options.def"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-larger-than="

using M = Dex2oatArgumentMap;
using Parser = CmdlineParser<Dex2oatArgumentMap, Dex2oatArgumentMap::Key>;
using Builder = Parser::Builder;

static void AddInputMappings(Builder& builder) {
  builder.
      Define("--dex-file=_")
          .WithType<std::vector<std::string>>().AppendValues()
          .WithHelp("Specifies a .dex, .jar, or .apk file to compile.\n"
                    "Eg: --dex-file=/system/framework/core.jar")
          .WithMetavar("<dex-file>")
          .IntoKey(M::DexFiles)
      .Define("--dex-location=_")
          .WithType<std::vector<std::string>>().AppendValues()
          .WithMetavar("<dex-location>")
          .WithHelp("specifies an alternative dex location to encode in the oat file for the\n"
                    "corresponding --dex-file argument. The first --dex-location corresponds to\n"
                    "the first --dex-file, the second to the second and so on.\n"
                    "Eg: --dex-file=/home/build/out/system/framework/core.jar\n"
                    "    --dex-location=/system/framework/core.jar")
          .IntoKey(M::DexLocations)
      .Define("--zip-fd=_")
          .WithType<int>()
          .WithHelp("specifies a file descriptor of a zip file containing a classes.dex file to\n"
                    "compile. Eg: --zip-fd=5")
          .IntoKey(M::ZipFd)
      .Define("--zip-location=_")
          .WithType<std::string>()
          .WithHelp("Specifies a symbolic name for the file corresponding to the FD given by\n"
                    "--zip-fd.")
          .IntoKey(M::ZipLocation)
      .Define("--boot-image=_")
          .WithType<std::string>()
          .WithHelp("provide the image file for the boot class path.\n"
                    "Do not include the arch as part of the name, it is added automatically.\n"
                    "Example: --boot-image=/system/framework/boot.art\n"
                    "         (specifies /system/framework/<arch>/boot.art as the image file)\n"
                    "Example: --boot-image=boot.art:boot-framework.art\n"
                    "         (specifies <bcp-path1>/<arch>/boot.art as the image file and\n"
                    "         <bcp-path2>/<arch>/boot-framework.art as the image extension file\n"
                    "         with paths taken from corresponding boot class path components)\n"
                    "Example: --boot-image=/apex/com.android.art/boot.art:/system/framework/*:*\n"
                    "         (specifies /apex/com.android.art/<arch>/boot.art as the image\n"
                    "         file and search for extensions in /framework/system and boot\n"
                    "         class path components' paths)\n"
                    "Default: $ANDROID_ROOT/system/framework/boot.art")
          .IntoKey(M::BootImage);
}

static void AddGeneratedArtifactMappings(Builder& builder) {
  builder.
      Define("--input-vdex-fd=_")
          .WithType<int>()
          .WithHelp("specifies the vdex input source via a file descriptor.")
          .IntoKey(M::InputVdexFd)
      .Define("--input-vdex=_")
          .WithType<std::string>()
          .WithHelp("specifies the vdex input source via a filename.")
          .IntoKey(M::InputVdex)
      .Define("--output-vdex-fd=_")
          .WithHelp("specifies the vdex output destination via a file descriptor.")
          .WithType<int>()
          .IntoKey(M::OutputVdexFd)
      .Define("--output-vdex=_")
          .WithType<std::string>()
          .WithHelp("specifies the vdex output destination via a filename.")
          .IntoKey(M::OutputVdex)
      .Define("--dm-fd=_")
          .WithType<int>()
          .WithHelp("specifies the dm output destination via a file descriptor.")
          .IntoKey(M::DmFd)
      .Define("--dm-file=_")
          .WithType<std::string>()
          .WithHelp("specifies the dm output destination via a filename.")
          .IntoKey(M::DmFile)
      .Define("--oat-file=_")
          .WithType<std::string>()
          .WithHelp(" Specifies an oat output destination via a filename.\n"
                    "Eg: --oat-file=/system/framework/boot.oat")
          .IntoKey(M::OatFile)
      .Define("--oat-symbols=_")
          .WithType<std::string>()
          .WithHelp("Specifies a symbolized oat output destination.\n"
                    "Eg: --oat-symbols=symbols/system/framework/boot.oat")
          .IntoKey(M::OatSymbols)
      .Define("--strip")
          .WithHelp("remove all debugging sections at the end (but keep mini-debug-info).\n"
                    "This is equivalent to the \"strip\" command as build post-processing step.\n"
                    "It is intended to be used with --oat-symbols and it happens after it.\n"
                    "Eg: --oat-symbols=/symbols/system/framework/boot.oat")
          .IntoKey(M::Strip)
      .Define("--oat-fd=_")
          .WithType<int>()
          .WithHelp("Specifies the oat output destination via a file descriptor. Eg: --oat-fd=5")
          .IntoKey(M::OatFd)
      .Define("--oat-location=_")
          .WithType<std::string>()
          .WithHelp("specifies a symbolic name for the file corresponding to the file descriptor\n"
                    "specified by --oat-fd.\n"
                    "Eg: --oat-location=/data/dalvik-cache/system@app@Calculator.apk.oat")
          .IntoKey(M::OatLocation);
}

static void AddImageMappings(Builder& builder) {
  builder.
      Define("--image=_")
          .WithType<std::string>()
          .WithHelp("specifies an output image filename. Eg: --image=/system/framework/boot.art")
          .IntoKey(M::ImageFilename)
      .Define("--image-fd=_")
          .WithType<int>()
          .WithHelp("specifies an output image file descriptor. Cannot be used with --image.\n"
                    "Eg: --image-fd=7")
          .IntoKey(M::ImageFd)
      .Define("--base=_")
          .WithType<std::string>()
          .WithHelp("Specifies the base address when creating a boot image. Eg: --base=0x50000000")
          .WithMetavar("{hex address}")
          .IntoKey(M::Base)
      .Define("--app-image-file=_")
          .WithType<std::string>()
          .WithHelp("Specify a file name for app image. Only used if a profile is passed in.")
          .IntoKey(M::AppImageFile)
      .Define("--app-image-fd=_")
          .WithType<int>()
          .WithHelp("Specify a file descriptor for app image. Only used if a profile is passed in.")
          .IntoKey(M::AppImageFileFd)
      .Define({"--multi-image", "--single-image"})
          .WithValues({true, false})
          .WithHelp("Specifies if separate oat and image files should be generated for each dex\n"
                    "file. --multi-image is default for boot image and --single-image for app\n"
                    "images.")
          .IntoKey(M::MultiImage)
      .Define("--dirty-image-objects=_")
          .WithType<std::string>()
          .WithHelp("list of known dirty objects in the image. The image writer will group them"
                    " together")
          .IntoKey(M::DirtyImageObjects)
      .Define("--updatable-bcp-packages-file=_")
          .WithType<std::string>()
          .WithHelp("file with a list of updatable boot class path packages. Classes in these\n"
                    "packages and sub-packages shall not be resolved during app compilation to\n"
                    "avoid AOT assumptions being invalidated after applying updates to these\n"
                    "components."
          )
          .IntoKey(M::UpdatableBcpPackagesFile)
      .Define("--image-format=_")
          .WithType<ImageHeader::StorageMode>()
          .WithValueMap({{"lz4", ImageHeader::kStorageModeLZ4},
                         {"lz4hc", ImageHeader::kStorageModeLZ4HC},
                         {"uncompressed", ImageHeader::kStorageModeUncompressed}})
          .WithHelp("Which format to store the image Defaults to uncompressed. Eg:"
                    " --image-format=lz4")
          .IntoKey(M::ImageFormat);
}

static void AddSwapMappings(Builder& builder) {
  builder.
      Define("--swap-file=_")
          .WithType<std::string>()
          .WithHelp("Specify a file to use for swap. Eg: --swap-file=/data/tmp/swap.001")
          .IntoKey(M::SwapFile)
      .Define("--swap-fd=_")
          .WithType<int>()
          .WithHelp("Specify a file to use for swap by file-descriptor. Eg: --swap-fd=3")
          .IntoKey(M::SwapFileFd)
      .Define("--swap-dex-size-threshold=_")
          .WithType<unsigned int>()
          .WithHelp("specifies the minimum total dex file size in bytes to allow the use of swap.")
          .IntoKey(M::SwapDexSizeThreshold)
      .Define("--swap-dex-count-threshold=_")
          .WithType<unsigned int>()
          .WithHelp("specifies the minimum number of dex file to allow the use of swap.")
          .IntoKey(M::SwapDexCountThreshold);
}

static void AddCompilerMappings(Builder& builder) {
  builder.
      Define("--run-passes=_")
          .WithType<std::string>()
          .IntoKey(M::Passes)
      .Define("--profile-file=_")
          .WithType<std::string>()
          .WithHelp("Specify profiler output file to use for compilation using a filename.")
          .IntoKey(M::Profile)
      .Define("--profile-file-fd=_")
          .WithType<int>()
          .WithHelp("Specify profiler output file to use for compilation using a file-descriptor.")
          .IntoKey(M::ProfileFd)
      .Define("--no-inline-from=_")
          .WithType<std::string>()
          .IntoKey(M::NoInlineFrom);
}

static void AddTargetMappings(Builder& builder) {
  builder.
      Define("--instruction-set=_")
          .WithType<InstructionSet>()
          .WithHelp("Compile for a particular instruction set.")
          .IntoKey(M::TargetInstructionSet)
      .Define("--instruction-set-variant=_")
          .WithType<std::string>()
          .WithHelp("Specify instruction set features using variant name.\n"
                    "Eg: --instruction-set-variant=silvermont")
          .WithMetavar("{Variant Name}")
          .IntoKey(M::TargetInstructionSetVariant)
      .Define("--instruction-set-features=_")
          .WithType<std::string>()
          .WithHelp("Specify instruction set features.\n"
                    "On target the value 'runtime' can be used to detect features at run time.\n"
                    "If target does not support run-time detection the value 'runtime'\n"
                    "has the same effect as the value 'default'.\n"
                    "Note: the value 'runtime' has no effect if it is used on host.\n"
                    "Example: --instruction-set-features=div\n"
                    "Default: default")
          .IntoKey(M::TargetInstructionSetFeatures);
}

Parser CreateDex2oatArgumentParser() {
  std::unique_ptr<Builder> parser_builder = std::make_unique<Builder>();

  AddInputMappings(*parser_builder);
  AddGeneratedArtifactMappings(*parser_builder);
  AddImageMappings(*parser_builder);
  AddSwapMappings(*parser_builder);
  AddCompilerMappings(*parser_builder);
  AddTargetMappings(*parser_builder);

  parser_builder->
      Define({"--watch-dog", "--no-watch-dog"})
          .WithHelp("Enable or disable the watchdog timer.")
          .WithValues({true, false})
          .IntoKey(M::Watchdog)
      .Define("--watchdog-timeout=_")
          .WithType<int>()
          .WithHelp("Set the watchdog timeout value in seconds.")
          .IntoKey(M::WatchdogTimeout)
      .Define("-j_")
          .WithType<unsigned int>()
          .WithHelp("specifies the number of threads used for compilation. Default is the number\n"
                    "of detected hardware threads available on the host system.")
          .IntoKey(M::Threads)
      .Define("--cpu-set=_")
          .WithType<std::vector<int32_t>>()
          .WithHelp("sets the cpu affinitiy to the given <set>. The <set> is a comma separated\n"
                    "list of cpus. Eg: --cpu-set=0,1,2,3")
          .WithMetavar("<set>")
          .IntoKey(M::CpuSet)
      .Define("--android-root=_")
          .WithType<std::string>()
          .WithHelp("Used to locate libraries for portable linking.\n"
                    "Eg: --android-root=out/host/linux-x86\n"
                    "Default: $ANDROID_ROOT")
          .IntoKey(M::AndroidRoot)
      .Define("--compiler-backend=_")
          .WithType<Compiler::Kind>()
          .WithValueMap({{"Quick", Compiler::Kind::kQuick},
                         {"Optimizing", Compiler::Kind::kOptimizing}})
          .WithHelp("Select a compiler backend set. Default: optimizing")
          .IntoKey(M::Backend)
      .Define("--host")
          .WithHelp("Run in host mode")
          .IntoKey(M::Host)
      .Define("--avoid-storing-invocation")
          .WithHelp("Avoid storing the invocation args in the key-value store. Used to test\n"
                    "determinism with different args.")
          .IntoKey(M::AvoidStoringInvocation)
      .Define("--very-large-app-threshold=_")
          .WithType<unsigned int>()
          .WithHelp("Specifies the minimum total dex file size in bytes to consider the input\n"
                    "\"very large\" and reduce compilation done.")
          .IntoKey(M::VeryLargeAppThreshold)
      .Define("--force-determinism")
          .WithHelp("Force the compiler to emit a deterministic output")
          .IntoKey(M::ForceDeterminism)
      .Define("--check-linkage-conditions")
          .IntoKey(M::CheckLinkageConditions)
      .Define("--crash-on-linkage-violation")
          .IntoKey(M::CrashOnLinkageViolation)
      .Define("--copy-dex-files=_")
          .WithType<linker::CopyOption>()
          .WithValueMap({{"true", linker::CopyOption::kOnlyIfCompressed},
                         {"false", linker::CopyOption::kNever},
                         {"always", linker::CopyOption::kAlways}})
          .WithHelp("enable|disable copying the dex files into the output vdex.")
          .IntoKey(M::CopyDexFiles)
      .Define("--force-allow-oj-inlines")
          .WithHelp("Disables automatic no-inline for core-oj on host. Has no effect on target."
                    " FOR TESTING USE ONLY! DO NOT DISTRIBUTE BINARIES BUILT WITH THIS OPTION!")
          .IntoKey(M::ForceAllowOjInlines)
      .Define("--write-invocation-to=_")
          .WithHelp("Write the invocation commandline to the given file for later use. Used to\n"
                    "test determinism with different args.")
          .WithType<std::string>()
          .IntoKey(M::InvocationFile)
      .Define("--classpath-dir=_")
          .WithType<std::string>()
          .WithHelp("Directory used to resolve relative class paths.")
          .IntoKey(M::ClasspathDir)
      .Define("--class-loader-context=_")
          .WithType<std::string>()
          .WithHelp("a string specifying the intended runtime loading context for the compiled\n"
                    "dex files.")
          .IntoKey(M::ClassLoaderContext)
      .Define("--class-loader-context-fds=_")
          .WithType<std::string>()
          .WithHelp("a colon-separated list of file descriptors for dex files in\n"
                    "--class-loader-context. Their order must be the same as dex files in a\n"
                    "flattened class loader context")
          .IntoKey(M::ClassLoaderContextFds)
      .Define("--stored-class-loader-context=_")
          .WithType<std::string>()
          .WithHelp("a string specifying the intended runtime loading context that is stored\n"
                    "in the oat file. Overrides --class-loader-context. Note that this ignores\n"
                    "the classpath_dir arg.\n"
                    "\n"
                    "It describes how the class loader chain should be built in order to ensure\n"
                    "classes are resolved during dex2aot as they would be resolved at runtime.\n"
                    "This spec will be encoded in the oat file. If at runtime the dex file is\n"
                    "loaded in a different context, the oat file will be rejected.\n"
                    "\n"
                    "The chain is interpreted in the natural 'parent order', meaning that class\n"
                    "loader 'i+1' will be the parent of class loader 'i'.\n"
                    "The compilation sources will be appended to the classpath of the first class\n"
                    "loader.\n"
                    "\n"
                    "E.g. if the context is 'PCL[lib1.dex];DLC[lib2.dex]' and \n"
                    "--dex-file=src.dex then dex2oat will setup a PathClassLoader with classpath \n"
                    "'lib1.dex:src.dex' and set its parent to a DelegateLastClassLoader with \n"
                    "classpath 'lib2.dex'.\n"
                    "\n"
                    "Note that the compiler will be tolerant if the source dex files specified\n"
                    "with --dex-file are found in the classpath. The source dex files will be\n"
                    "removed from any class loader's classpath possibly resulting in empty\n"
                    "class loaders.\n"
                    "\n"
                    "Example: --class-loader-context=PCL[lib1.dex:lib2.dex];DLC[lib3.dex]")
          .IntoKey(M::StoredClassLoaderContext)
      .Define("--compact-dex-level=_")
          .WithType<CompactDexLevel>()
          .WithValueMap({{"none", CompactDexLevel::kCompactDexLevelNone},
                         {"fast", CompactDexLevel::kCompactDexLevelFast}})
          .WithHelp("None avoids generating compact dex, fast generates compact dex with low\n"
                    "compile time. If speed-profile is specified as the compiler filter and the\n"
                    "profile is not empty, the default compact dex level is always used.")
          .IntoKey(M::CompactDexLevel)
      .Define("--runtime-arg _")
          .WithType<std::vector<std::string>>().AppendValues()
          .WithMetavar("{dalvikvm-arg}")
          .WithHelp("used to specify various arguments for the runtime, such as initial heap\n"
                    "size, maximum heap size, and verbose output. Use a separate --runtime-arg\n"
                    "switch for each argument.\n"
                    "Example: --runtime-arg -Xms256m")
          .IntoKey(M::RuntimeOptions)
      .Define("--compilation-reason=_")
          .WithType<std::string>()
          .WithHelp("optional metadata specifying the reason for compiling the apk. If specified,\n"
                    "the string will be embedded verbatim in the key value store of the oat file.\n"
                    "Example: --compilation-reason=install")
          .IntoKey(M::CompilationReason)
      .Define("--compile-individually")
          .WithHelp("Compiles dex files individually, unloading classes in between compiling each"
                    " file.")
          .IntoKey(M::CompileIndividually)
      .Define("--public-sdk=_")
          .WithType<std::string>()
          .IntoKey(M::PublicSdk)
      .Define("--apex-versions=_")
          .WithType<std::string>()
          .WithHelp("Versions of apexes in the boot classpath, separated by '/'")
          .IntoKey(M::ApexVersions);

  AddCompilerOptionsArgumentParserOptions<Dex2oatArgumentMap>(*parser_builder);

  parser_builder->IgnoreUnrecognized(false);

  return parser_builder->Build();
}

std::unique_ptr<Dex2oatArgumentMap> Dex2oatArgumentMap::Parse(int argc,
                                                              const char** argv,
                                                              std::string* error_msg) {
  Parser parser = CreateDex2oatArgumentParser();
  CmdlineResult parse_result = parser.Parse(argv, argc);
  if (!parse_result.IsSuccess()) {
    *error_msg = parse_result.GetMessage();
    return nullptr;
  }

  return std::make_unique<Dex2oatArgumentMap>(parser.ReleaseArgumentsMap());
}

#pragma GCC diagnostic pop
}  // namespace art
