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

#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "common_runtime_test.h"

#include "base/array_ref.h"
#include "base/file_utils.h"
#include "base/macros.h"
#include "base/mem_map.h"
#include "base/string_view_cpp20.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "dex/art_dex_file_loader.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_file_loader.h"
#include "dex/method_reference.h"
#include "gc/space/image_space.h"
#include "profile/profile_compilation_info.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"

namespace art {

// A suitable address for loading the core images.
constexpr uint32_t kBaseAddress = 0x60000000;

struct ImageSizes {
  size_t art_size = 0;
  size_t oat_size = 0;
  size_t vdex_size = 0;
};

std::ostream& operator<<(std::ostream& os, const ImageSizes& sizes) {
  os << "art=" << sizes.art_size << " oat=" << sizes.oat_size << " vdex=" << sizes.vdex_size;
  return os;
}

class Dex2oatImageTest : public CommonRuntimeTest {
 public:
  void TearDown() override {}

 protected:
  // Visitors take method and type references
  template <typename MethodVisitor, typename ClassVisitor>
  void VisitLibcoreDexes(const MethodVisitor& method_visitor,
                         const ClassVisitor& class_visitor,
                         size_t method_frequency = 1,
                         size_t class_frequency = 1) {
    size_t method_counter = 0;
    size_t class_counter = 0;
    for (const std::string& dex : GetLibCoreDexFileNames()) {
      std::vector<std::unique_ptr<const DexFile>> dex_files;
      std::string error_msg;
      const ArtDexFileLoader dex_file_loader;
      CHECK(dex_file_loader.Open(dex.c_str(),
                                 dex,
                                 /*verify*/ true,
                                 /*verify_checksum*/ false,
                                 &error_msg,
                                 &dex_files))
          << error_msg;
      for (const std::unique_ptr<const DexFile>& dex_file : dex_files) {
        for (size_t i = 0; i < dex_file->NumMethodIds(); ++i) {
          if (++method_counter % method_frequency == 0) {
            method_visitor(MethodReference(dex_file.get(), i));
          }
        }
        for (size_t i = 0; i < dex_file->NumTypeIds(); ++i) {
          if (++class_counter % class_frequency == 0) {
            class_visitor(TypeReference(dex_file.get(), dex::TypeIndex(i)));
          }
        }
      }
    }
  }

  static void WriteLine(File* file, std::string line) {
    line += '\n';
    EXPECT_TRUE(file->WriteFully(&line[0], line.length()));
  }

  void GenerateProfile(File* out_file, size_t method_frequency,  size_t type_frequency) {
    ProfileCompilationInfo profile;
    VisitLibcoreDexes([&profile](MethodReference ref) {
      uint32_t flags = ProfileCompilationInfo::MethodHotness::kFlagHot |
          ProfileCompilationInfo::MethodHotness::kFlagStartup;
      EXPECT_TRUE(profile.AddMethod(
          ProfileMethodInfo(ref),
          static_cast<ProfileCompilationInfo::MethodHotness::Flag>(flags)));
    }, [&profile](TypeReference ref) {
      std::set<dex::TypeIndex> classes;
      classes.insert(ref.TypeIndex());
      EXPECT_TRUE(profile.AddClassesForDex(ref.dex_file, classes.begin(), classes.end()));
    }, method_frequency, type_frequency);
    ScratchFile profile_file;
    profile.Save(out_file->Fd());
    EXPECT_EQ(out_file->Flush(), 0);
  }

  void GenerateMethods(File* out_file, size_t frequency = 1) {
    VisitLibcoreDexes([out_file](MethodReference ref) {
      WriteLine(out_file, ref.PrettyMethod());
    }, VoidFunctor(), frequency, frequency);
    EXPECT_EQ(out_file->Flush(), 0);
  }

  void AddRuntimeArg(std::vector<std::string>& args, const std::string& arg) {
    args.push_back("--runtime-arg");
    args.push_back(arg);
  }

  ImageSizes CompileImageAndGetSizes(const std::vector<std::string>& extra_args) {
    ImageSizes ret;
    ScratchFile scratch;
    std::string scratch_dir = scratch.GetFilename();
    while (!scratch_dir.empty() && scratch_dir.back() != '/') {
      scratch_dir.pop_back();
    }
    CHECK(!scratch_dir.empty()) << "No directory " << scratch.GetFilename();
    std::vector<std::string> libcore_dex_files = GetLibCoreDexFileNames();
    ArrayRef<const std::string> dex_files(libcore_dex_files);
    std::vector<std::string> local_extra_args = extra_args;
    local_extra_args.push_back(android::base::StringPrintf("--base=0x%08x", kBaseAddress));
    std::string error_msg;
    if (!CompileBootImage(local_extra_args, scratch.GetFilename(), dex_files, &error_msg)) {
      LOG(ERROR) << "Failed to compile image " << scratch.GetFilename() << error_msg;
    }
    std::string art_file = scratch.GetFilename() + ".art";
    std::string oat_file = scratch.GetFilename() + ".oat";
    std::string vdex_file = scratch.GetFilename() + ".vdex";
    int64_t art_size = OS::GetFileSizeBytes(art_file.c_str());
    int64_t oat_size = OS::GetFileSizeBytes(oat_file.c_str());
    int64_t vdex_size = OS::GetFileSizeBytes(vdex_file.c_str());
    CHECK_GT(art_size, 0u) << art_file;
    CHECK_GT(oat_size, 0u) << oat_file;
    CHECK_GT(vdex_size, 0u) << vdex_file;
    ret.art_size = art_size;
    ret.oat_size = oat_size;
    ret.vdex_size = vdex_size;
    scratch.Close();
    // Clear image files since we compile the image multiple times and don't want to leave any
    // artifacts behind.
    ClearDirectory(scratch_dir.c_str(), /*recursive=*/ false);
    return ret;
  }

  bool CompileBootImage(const std::vector<std::string>& extra_args,
                        const std::string& image_file_name_prefix,
                        ArrayRef<const std::string> dex_files,
                        std::string* error_msg) {
    Runtime* const runtime = Runtime::Current();
    std::vector<std::string> argv;
    argv.push_back(runtime->GetCompilerExecutable());
    AddRuntimeArg(argv, "-Xms64m");
    AddRuntimeArg(argv, "-Xmx64m");
    for (const std::string& dex_file : dex_files) {
      argv.push_back("--dex-file=" + dex_file);
      argv.push_back("--dex-location=" + dex_file);
    }
    if (runtime->IsJavaDebuggable()) {
      argv.push_back("--debuggable");
    }
    runtime->AddCurrentRuntimeFeaturesAsDex2OatArguments(&argv);

    AddRuntimeArg(argv, "-Xverify:softfail");

    if (!kIsTargetBuild) {
      argv.push_back("--host");
    }

    argv.push_back("--image=" + image_file_name_prefix + ".art");
    argv.push_back("--oat-file=" + image_file_name_prefix + ".oat");
    argv.push_back("--oat-location=" + image_file_name_prefix + ".oat");

    std::vector<std::string> compiler_options = runtime->GetCompilerOptions();
    argv.insert(argv.end(), compiler_options.begin(), compiler_options.end());

    // We must set --android-root.
    const char* android_root = getenv("ANDROID_ROOT");
    CHECK(android_root != nullptr);
    argv.push_back("--android-root=" + std::string(android_root));
    argv.insert(argv.end(), extra_args.begin(), extra_args.end());

    return RunDex2Oat(argv, error_msg);
  }

  bool RunDex2Oat(const std::vector<std::string>& args, std::string* error_msg) {
    // We only want fatal logging for the error message.
    auto post_fork_fn = []() { return setenv("ANDROID_LOG_TAGS", "*:f", 1) == 0; };
    ForkAndExecResult res = ForkAndExec(args, post_fork_fn, error_msg);
    if (res.stage != ForkAndExecResult::kFinished) {
      *error_msg = strerror(errno);
      return false;
    }
    return res.StandardSuccess();
  }
};

TEST_F(Dex2oatImageTest, TestModesAndFilters) {
  // This test crashes on the gtest-heap-poisoning configuration
  // (AddressSanitizer + CMS/RosAlloc + heap-poisoning); see b/111061592.
  // Temporarily disable this test on this configuration to keep
  // our automated build/testing green while we work on a fix.
  TEST_DISABLED_FOR_MEMORY_TOOL_WITH_HEAP_POISONING_WITHOUT_READ_BARRIERS();
  if (kIsTargetBuild) {
    // This test is too slow for target builds.
    return;
  }
  ImageSizes base_sizes = CompileImageAndGetSizes({});
  ImageSizes everything_sizes;
  ImageSizes filter_sizes;
  std::cout << "Base compile sizes " << base_sizes << std::endl;
  // Compile all methods and classes
  {
    ScratchFile profile_file;
    GenerateProfile(profile_file.GetFile(), /*method_frequency=*/ 1u, /*type_frequency=*/ 1u);
    everything_sizes = CompileImageAndGetSizes(
        {"--profile-file=" + profile_file.GetFilename(),
         "--compiler-filter=speed-profile"});
    profile_file.Close();
    std::cout << "All methods and classes sizes " << everything_sizes << std::endl;
    // Putting all classes as image classes should increase art size
    EXPECT_GE(everything_sizes.art_size, base_sizes.art_size);
    // Sanity check that dex is the same size.
    EXPECT_EQ(everything_sizes.vdex_size, base_sizes.vdex_size);
  }
  static size_t kMethodFrequency = 3;
  static size_t kTypeFrequency = 4;
  // Test compiling fewer methods and classes.
  {
    ScratchFile profile_file;
    GenerateProfile(profile_file.GetFile(), kMethodFrequency, kTypeFrequency);
    filter_sizes = CompileImageAndGetSizes(
        {"--profile-file=" + profile_file.GetFilename(),
         "--compiler-filter=speed-profile"});
    profile_file.Close();
    std::cout << "Fewer methods and classes sizes " << filter_sizes << std::endl;
    EXPECT_LE(filter_sizes.art_size, everything_sizes.art_size);
    EXPECT_LE(filter_sizes.oat_size, everything_sizes.oat_size);
    EXPECT_LE(filter_sizes.vdex_size, everything_sizes.vdex_size);
  }
  // Test dirty image objects.
  {
    ScratchFile classes;
    VisitLibcoreDexes(VoidFunctor(),
                      [&](TypeReference ref) {
      WriteLine(classes.GetFile(), ref.dex_file->PrettyType(ref.TypeIndex()));
    }, /*method_frequency=*/ 1u, /*class_frequency=*/ 1u);
    ImageSizes image_classes_sizes = CompileImageAndGetSizes(
        {"--dirty-image-objects=" + classes.GetFilename()});
    classes.Close();
    std::cout << "Dirty image object sizes " << image_classes_sizes << std::endl;
  }
}

TEST_F(Dex2oatImageTest, TestExtension) {
  constexpr size_t kReservationSize = 256 * MB;  // This should be enough for the compiled images.
  std::string error_msg;
  MemMap reservation = MemMap::MapAnonymous("Reservation",
                                            reinterpret_cast<uint8_t*>(kBaseAddress),
                                            kReservationSize,
                                            PROT_NONE,
                                            /*low_4gb=*/ true,
                                            /*reuse=*/ false,
                                            /*reservation=*/ nullptr,
                                            &error_msg);
  ASSERT_TRUE(reservation.IsValid());

  ScratchFile scratch;
  std::string scratch_dir = scratch.GetFilename() + "-d";
  int mkdir_result = mkdir(scratch_dir.c_str(), 0700);
  ASSERT_EQ(0, mkdir_result);
  scratch_dir += '/';
  std::string image_dir = scratch_dir + GetInstructionSetString(kRuntimeISA);
  mkdir_result = mkdir(image_dir.c_str(), 0700);
  ASSERT_EQ(0, mkdir_result);
  std::string filename_prefix = image_dir + "/core";

  // Copy the libcore dex files to a custom dir inside `scratch_dir` so that we do not
  // accidentally load pre-compiled core images from their original directory based on BCP paths.
  std::string jar_dir = scratch_dir + "jars";
  mkdir_result = mkdir(jar_dir.c_str(), 0700);
  ASSERT_EQ(0, mkdir_result);
  jar_dir += '/';
  std::vector<std::string> libcore_dex_files = GetLibCoreDexFileNames();
  for (std::string& dex_file : libcore_dex_files) {
    size_t slash_pos = dex_file.rfind('/');
    ASSERT_NE(std::string::npos, slash_pos);
    std::string new_location = jar_dir + dex_file.substr(slash_pos + 1u);
    std::ifstream src_stream(dex_file, std::ios::binary);
    std::ofstream dst_stream(new_location, std::ios::binary);
    dst_stream << src_stream.rdbuf();
    dex_file = new_location;
  }

  ArrayRef<const std::string> full_bcp(libcore_dex_files);
  size_t total_dex_files = full_bcp.size();
  ASSERT_GE(total_dex_files, 4u);  // 2 for "head", 1 for "tail", at least one for "mid", see below.

  // The primary image must contain at least core-oj and core-libart to initialize the runtime.
  ASSERT_NE(std::string::npos, full_bcp[0].find("core-oj"));
  ASSERT_NE(std::string::npos, full_bcp[1].find("core-libart"));
  ArrayRef<const std::string> head_dex_files = full_bcp.SubArray(/*pos=*/ 0u, /*length=*/ 2u);
  // Middle part is everything else except for conscrypt.
  ASSERT_NE(std::string::npos, full_bcp[full_bcp.size() - 1u].find("conscrypt"));
  ArrayRef<const std::string> mid_bcp =
      full_bcp.SubArray(/*pos=*/ 0u, /*length=*/ total_dex_files - 1u);
  ArrayRef<const std::string> mid_dex_files = mid_bcp.SubArray(/*pos=*/ 2u);
  // Tail is just the conscrypt.
  ArrayRef<const std::string> tail_dex_files =
      full_bcp.SubArray(/*pos=*/ total_dex_files - 1u, /*length=*/ 1u);

  // Prepare the "head", "mid" and "tail" names and locations.
  std::string base_name = "core.art";
  std::string base_location = scratch_dir + base_name;
  std::vector<std::string> expanded_mid = gc::space::ImageSpace::ExpandMultiImageLocations(
      mid_dex_files.SubArray(/*pos=*/ 0u, /*length=*/ 1u),
      base_location,
      /*boot_image_extension=*/ true);
  CHECK_EQ(1u, expanded_mid.size());
  std::string mid_location = expanded_mid[0];
  size_t mid_slash_pos = mid_location.rfind('/');
  ASSERT_NE(std::string::npos, mid_slash_pos);
  std::string mid_name = mid_location.substr(mid_slash_pos + 1u);
  CHECK_EQ(1u, tail_dex_files.size());
  std::vector<std::string> expanded_tail = gc::space::ImageSpace::ExpandMultiImageLocations(
      tail_dex_files, base_location, /*boot_image_extension=*/ true);
  CHECK_EQ(1u, expanded_tail.size());
  std::string tail_location = expanded_tail[0];
  size_t tail_slash_pos = tail_location.rfind('/');
  ASSERT_NE(std::string::npos, tail_slash_pos);
  std::string tail_name = tail_location.substr(tail_slash_pos + 1u);

  // Compile the "head", i.e. the primary boot image.
  std::string base = android::base::StringPrintf("--base=0x%08x", kBaseAddress);
  bool head_ok = CompileBootImage({base}, filename_prefix, head_dex_files, &error_msg);
  ASSERT_TRUE(head_ok) << error_msg;

  // Compile the "mid", i.e. the first extension.
  std::string mid_bcp_string = android::base::Join(mid_bcp, ':');
  std::vector<std::string> extra_args;
  AddRuntimeArg(extra_args, "-Xbootclasspath:" + mid_bcp_string);
  AddRuntimeArg(extra_args, "-Xbootclasspath-locations:" + mid_bcp_string);
  extra_args.push_back("--boot-image=" + base_location);
  bool mid_ok = CompileBootImage(extra_args, filename_prefix, mid_dex_files, &error_msg);
  ASSERT_TRUE(mid_ok) << error_msg;

  // Try to compile the "tail" without specifying the "mid" extension. This shall fail.
  std::string full_bcp_string = android::base::Join(full_bcp, ':');
  extra_args.clear();
  AddRuntimeArg(extra_args, "-Xbootclasspath:" + full_bcp_string);
  AddRuntimeArg(extra_args, "-Xbootclasspath-locations:" + full_bcp_string);
  extra_args.push_back("--boot-image=" + base_location);
  bool tail_ok = CompileBootImage(extra_args, filename_prefix, tail_dex_files, &error_msg);
  ASSERT_FALSE(tail_ok) << error_msg;

  // Now compile the tail against both "head" and "mid".
  CHECK(StartsWith(extra_args.back(), "--boot-image="));
  extra_args.back() = "--boot-image=" + base_location + ':' + mid_location;
  tail_ok = CompileBootImage(extra_args, filename_prefix, tail_dex_files, &error_msg);
  ASSERT_TRUE(tail_ok) << error_msg;

  reservation = MemMap::Invalid();  // Free the reserved memory for loading images.

  // Try to load the boot image with different image locations.
  std::vector<std::string> boot_class_path = libcore_dex_files;
  std::vector<std::unique_ptr<gc::space::ImageSpace>> boot_image_spaces;
  MemMap extra_reservation;
  auto load = [&](const std::string& image_location) {
    boot_image_spaces.clear();
    extra_reservation = MemMap::Invalid();
    ScopedObjectAccess soa(Thread::Current());
    return gc::space::ImageSpace::LoadBootImage(/*boot_class_path=*/ boot_class_path,
                                                /*boot_class_path_locations=*/ libcore_dex_files,
                                                image_location,
                                                kRuntimeISA,
                                                gc::space::ImageSpaceLoadingOrder::kSystemFirst,
                                                /*relocate=*/ false,
                                                /*executable=*/ true,
                                                /*is_zygote=*/ false,
                                                /*extra_reservation_size=*/ 0u,
                                                &boot_image_spaces,
                                                &extra_reservation);
  };

  // Load primary image with full path.
  bool load_ok = load(base_location);
  ASSERT_TRUE(load_ok) << error_msg;
  ASSERT_FALSE(extra_reservation.IsValid());
  ASSERT_EQ(head_dex_files.size(), boot_image_spaces.size());

  // Fail to load primary image with just the name.
  load_ok = load(base_name);
  ASSERT_FALSE(load_ok);

  // Fail to load primary image with a search path.
  load_ok = load("*");
  ASSERT_FALSE(load_ok);
  load_ok = load(scratch_dir + "*");
  ASSERT_FALSE(load_ok);

  // Load the primary and first extension with full path.
  load_ok = load(base_location + ':' + mid_location);
  ASSERT_TRUE(load_ok) << error_msg;
  ASSERT_EQ(mid_bcp.size(), boot_image_spaces.size());

  // Load the primary with full path and fail to load first extension without full path.
  load_ok = load(base_location + ':' + mid_name);
  ASSERT_TRUE(load_ok) << error_msg;  // Primary image loaded successfully.
  ASSERT_EQ(head_dex_files.size(), boot_image_spaces.size());  // But only the primary image.

  // Load all the libcore images with full paths.
  load_ok = load(base_location + ':' + mid_location + ':' + tail_location);
  ASSERT_TRUE(load_ok) << error_msg;
  ASSERT_EQ(full_bcp.size(), boot_image_spaces.size());

  // Load the primary and first extension with full paths, fail to load second extension by name.
  load_ok = load(base_location + ':' + mid_location + ':' + tail_name);
  ASSERT_TRUE(load_ok) << error_msg;
  ASSERT_EQ(mid_bcp.size(), boot_image_spaces.size());

  // Load the primary with full path and fail to load first extension without full path,
  // fail to load second extension because it depends on the first.
  load_ok = load(base_location + ':' + mid_name + ':' + tail_location);
  ASSERT_TRUE(load_ok) << error_msg;  // Primary image loaded successfully.
  ASSERT_EQ(head_dex_files.size(), boot_image_spaces.size());  // But only the primary image.

  // Load the primary with full path and extensions with a specified search path.
  load_ok = load(base_location + ':' + scratch_dir + '*');
  ASSERT_TRUE(load_ok) << error_msg;
  ASSERT_EQ(full_bcp.size(), boot_image_spaces.size());

  // Load the primary with full path and fail to find extensions in BCP path.
  load_ok = load(base_location + ":*");
  ASSERT_TRUE(load_ok) << error_msg;
  ASSERT_EQ(head_dex_files.size(), boot_image_spaces.size());

  // Now copy the libcore dex files to the `scratch_dir` and retry loading the boot image
  // with BCP in the scratch_dir so that the images can be found based on BCP paths.
  for (std::string& bcp_component : boot_class_path) {
    size_t slash_pos = bcp_component.rfind('/');
    ASSERT_NE(std::string::npos, slash_pos);
    std::string new_location = scratch_dir + bcp_component.substr(slash_pos + 1u);
    std::ifstream src_stream(bcp_component, std::ios::binary);
    std::ofstream dst_stream(new_location, std::ios::binary);
    dst_stream << src_stream.rdbuf();
    bcp_component = new_location;
  }

  // Loading the primary image with just the name now succeeds.
  load_ok = load(base_name);
  ASSERT_TRUE(load_ok) << error_msg;

  // Loading the primary image with a search path still fails.
  load_ok = load("*");
  ASSERT_FALSE(load_ok);
  load_ok = load(scratch_dir + "*");
  ASSERT_FALSE(load_ok);

  // Load the primary and first extension without paths.
  load_ok = load(base_name + ':' + mid_name);
  ASSERT_TRUE(load_ok) << error_msg;
  ASSERT_EQ(mid_bcp.size(), boot_image_spaces.size());

  // Load the primary with full path and the first extension without full path.
  load_ok = load(base_location + ':' + mid_name);
  ASSERT_TRUE(load_ok) << error_msg;  // Loaded successfully.
  ASSERT_EQ(mid_bcp.size(), boot_image_spaces.size());  // Including the extension.

  // Load all the libcore images without paths.
  load_ok = load(base_name + ':' + mid_name + ':' + tail_name);
  ASSERT_TRUE(load_ok) << error_msg;
  ASSERT_EQ(full_bcp.size(), boot_image_spaces.size());

  // Load the primary and first extension with full paths and second extension by name.
  load_ok = load(base_location + ':' + mid_location + ':' + tail_name);
  ASSERT_TRUE(load_ok) << error_msg;
  ASSERT_EQ(full_bcp.size(), boot_image_spaces.size());

  // Load the primary with full path, first extension without path,
  // and second extension with full path.
  load_ok = load(base_location + ':' + mid_name + ':' + tail_location);
  ASSERT_TRUE(load_ok) << error_msg;  // Loaded successfully.
  ASSERT_EQ(full_bcp.size(), boot_image_spaces.size());  // Including both extensions.

  // Load the primary with full path and find both extensions in BCP path.
  load_ok = load(base_location + ":*");
  ASSERT_TRUE(load_ok) << error_msg;
  ASSERT_EQ(full_bcp.size(), boot_image_spaces.size());

  // Fail to load any images with invalid image locations (named component after search paths).
  load_ok = load(base_location + ":*:" + tail_location);
  ASSERT_FALSE(load_ok);
  load_ok = load(base_location + ':' + scratch_dir + "*:" + tail_location);
  ASSERT_FALSE(load_ok);

  ClearDirectory(scratch_dir.c_str());
  int rmdir_result = rmdir(scratch_dir.c_str());
  ASSERT_EQ(0, rmdir_result);
}

}  // namespace art
