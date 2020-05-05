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
#include "dex/type_reference.h"
#include "gc/space/image_space.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"

namespace art {

// A suitable address for loading the core images.
constexpr uint32_t kBaseAddress = ART_BASE_ADDRESS;

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
  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    // Disable implicit dex2oat invocations when loading image spaces.
    options->emplace_back("-Xnoimage-dex2oat", nullptr);
  }

  static void WriteLine(File* file, std::string line) {
    line += '\n';
    EXPECT_TRUE(file->WriteFully(&line[0], line.length()));
  }

  void AddRuntimeArg(std::vector<std::string>& args, const std::string& arg) {
    args.push_back("--runtime-arg");
    args.push_back(arg);
  }

  ImageSizes CompileImageAndGetSizes(ArrayRef<const std::string> dex_files,
                                     const std::vector<std::string>& extra_args) {
    ImageSizes ret;
    ScratchDir scratch;
    std::string filename_prefix = scratch.GetPath() + "boot";
    std::vector<std::string> local_extra_args = extra_args;
    local_extra_args.push_back(android::base::StringPrintf("--base=0x%08x", kBaseAddress));
    std::string error_msg;
    if (!CompileBootImage(local_extra_args, filename_prefix, dex_files, &error_msg)) {
      LOG(ERROR) << "Failed to compile image " << filename_prefix << error_msg;
    }
    std::string art_file = filename_prefix + ".art";
    std::string oat_file = filename_prefix + ".oat";
    std::string vdex_file = filename_prefix + ".vdex";
    int64_t art_size = OS::GetFileSizeBytes(art_file.c_str());
    int64_t oat_size = OS::GetFileSizeBytes(oat_file.c_str());
    int64_t vdex_size = OS::GetFileSizeBytes(vdex_file.c_str());
    CHECK_GT(art_size, 0u) << art_file;
    CHECK_GT(oat_size, 0u) << oat_file;
    CHECK_GT(vdex_size, 0u) << vdex_file;
    ret.art_size = art_size;
    ret.oat_size = oat_size;
    ret.vdex_size = vdex_size;
    return ret;
  }

  MemMap ReserveCoreImageAddressSpace(/*out*/std::string* error_msg) {
    constexpr size_t kReservationSize = 256 * MB;  // This should be enough for the compiled images.
    // Extend to both directions for maximum relocation difference.
    static_assert(ART_BASE_ADDRESS_MIN_DELTA < 0);
    static_assert(ART_BASE_ADDRESS_MAX_DELTA > 0);
    static_assert(IsAligned<kPageSize>(ART_BASE_ADDRESS_MIN_DELTA));
    static_assert(IsAligned<kPageSize>(ART_BASE_ADDRESS_MAX_DELTA));
    constexpr size_t kExtra = ART_BASE_ADDRESS_MAX_DELTA - ART_BASE_ADDRESS_MIN_DELTA;
    uint32_t min_relocated_address = kBaseAddress + ART_BASE_ADDRESS_MIN_DELTA;
    return MemMap::MapAnonymous("Reservation",
                                reinterpret_cast<uint8_t*>(min_relocated_address),
                                kReservationSize + kExtra,
                                PROT_NONE,
                                /*low_4gb=*/ true,
                                /*reuse=*/ false,
                                /*reservation=*/ nullptr,
                                error_msg);
  }

  void CopyDexFiles(const std::string& dir, /*inout*/std::vector<std::string>* dex_files) {
    CHECK(EndsWith(dir, "/"));
    for (std::string& dex_file : *dex_files) {
      size_t slash_pos = dex_file.rfind('/');
      CHECK(OS::FileExists(dex_file.c_str())) << dex_file;
      CHECK_NE(std::string::npos, slash_pos);
      std::string new_location = dir + dex_file.substr(slash_pos + 1u);
      std::ifstream src_stream(dex_file, std::ios::binary);
      std::ofstream dst_stream(new_location, std::ios::binary);
      dst_stream << src_stream.rdbuf();
      dex_file = new_location;
    }
  }

  bool CompareFiles(const std::string& filename1, const std::string& filename2) {
    std::unique_ptr<File> file1(OS::OpenFileForReading(filename1.c_str()));
    std::unique_ptr<File> file2(OS::OpenFileForReading(filename2.c_str()));
    // Did we open the files?
    if (file1 == nullptr || file2 == nullptr) {
      return false;
    }
    // Are they non-empty and the same length?
    if (file1->GetLength() <= 0 || file2->GetLength() != file1->GetLength()) {
      return false;
    }
    return file1->Compare(file2.get()) == 0;
  }

  void AddAndroidRootToImageCompilerOptions() {
    const char* android_root = getenv("ANDROID_ROOT");
    CHECK(android_root != nullptr);
    Runtime::Current()->image_compiler_options_.push_back(
        "--android-root=" + std::string(android_root));
  }

  void EnableImageDex2Oat() {
    Runtime::Current()->image_dex2oat_enabled_ = true;
  }

  void DisableImageDex2Oat() {
    Runtime::Current()->image_dex2oat_enabled_ = false;
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
  // Compile only a subset of the libcore dex files to make this test shorter.
  std::vector<std::string> libcore_dex_files = GetLibCoreDexFileNames();
  // The primary image must contain at least core-oj and core-libart to initialize the runtime
  // and we also need the core-icu4j if we want to compile these with full profile.
  ASSERT_NE(std::string::npos, libcore_dex_files[0].find("core-oj"));
  ASSERT_NE(std::string::npos, libcore_dex_files[1].find("core-libart"));
  ASSERT_NE(std::string::npos, libcore_dex_files[2].find("core-icu4j"));
  ArrayRef<const std::string> dex_files =
      ArrayRef<const std::string>(libcore_dex_files).SubArray(/*pos=*/ 0u, /*length=*/ 3u);

  ImageSizes base_sizes = CompileImageAndGetSizes(dex_files, {});
  ImageSizes everything_sizes;
  ImageSizes filter_sizes;
  std::cout << "Base compile sizes " << base_sizes << std::endl;
  // Compile all methods and classes
  std::vector<std::string> libcore_dexes = GetLibCoreDexFileNames();
  ArrayRef<const std::string> libcore_dexes_array(libcore_dexes);
  {
    ScratchFile profile_file;
    GenerateProfile(libcore_dexes_array,
                    profile_file.GetFile(),
                    /*method_frequency=*/ 1u,
                    /*type_frequency=*/ 1u);
    everything_sizes = CompileImageAndGetSizes(
        dex_files,
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
    GenerateProfile(libcore_dexes_array,
                    profile_file.GetFile(),
                    kMethodFrequency,
                    kTypeFrequency);
    filter_sizes = CompileImageAndGetSizes(
        dex_files,
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
    VisitDexes(libcore_dexes_array,
               VoidFunctor(),
               [&](TypeReference ref) {
      WriteLine(classes.GetFile(), ref.dex_file->PrettyType(ref.TypeIndex()));
    }, /*method_frequency=*/ 1u, /*class_frequency=*/ 1u);
    ImageSizes image_classes_sizes = CompileImageAndGetSizes(
        dex_files,
        {"--dirty-image-objects=" + classes.GetFilename()});
    classes.Close();
    std::cout << "Dirty image object sizes " << image_classes_sizes << std::endl;
  }
}

TEST_F(Dex2oatImageTest, TestExtension) {
  std::string error_msg;
  MemMap reservation = ReserveCoreImageAddressSpace(&error_msg);
  ASSERT_TRUE(reservation.IsValid()) << error_msg;

  ScratchDir scratch;
  const std::string& scratch_dir = scratch.GetPath();
  std::string image_dir = scratch_dir + GetInstructionSetString(kRuntimeISA);
  int mkdir_result = mkdir(image_dir.c_str(), 0700);
  ASSERT_EQ(0, mkdir_result);
  std::string filename_prefix = image_dir + "/core";

  // Copy the libcore dex files to a custom dir inside `scratch_dir` so that we do not
  // accidentally load pre-compiled core images from their original directory based on BCP paths.
  std::string jar_dir = scratch_dir + "jars";
  mkdir_result = mkdir(jar_dir.c_str(), 0700);
  ASSERT_EQ(0, mkdir_result);
  jar_dir += '/';
  std::vector<std::string> libcore_dex_files = GetLibCoreDexFileNames();
  CopyDexFiles(jar_dir, &libcore_dex_files);

  ArrayRef<const std::string> full_bcp(libcore_dex_files);
  size_t total_dex_files = full_bcp.size();
  ASSERT_GE(total_dex_files, 5u);  // 3 for "head", 1 for "tail", at least one for "mid", see below.

  // The primary image must contain at least core-oj and core-libart to initialize the runtime
  // and we also need the core-icu4j if we want to compile these with full profile.
  ASSERT_NE(std::string::npos, full_bcp[0].find("core-oj"));
  ASSERT_NE(std::string::npos, full_bcp[1].find("core-libart"));
  ASSERT_NE(std::string::npos, full_bcp[2].find("core-icu4j"));
  ArrayRef<const std::string> head_dex_files = full_bcp.SubArray(/*pos=*/ 0u, /*length=*/ 3u);
  // Middle part is everything else except for conscrypt.
  ASSERT_NE(std::string::npos, full_bcp[full_bcp.size() - 1u].find("conscrypt"));
  ArrayRef<const std::string> mid_bcp =
      full_bcp.SubArray(/*pos=*/ 0u, /*length=*/ total_dex_files - 1u);
  ArrayRef<const std::string> mid_dex_files = mid_bcp.SubArray(/*pos=*/ 3u);
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

  // Create profiles.
  ScratchFile head_profile_file;
  GenerateProfile(head_dex_files,
                  head_profile_file.GetFile(),
                  /*method_frequency=*/ 1u,
                  /*type_frequency=*/ 1u);
  const std::string& head_profile_filename = head_profile_file.GetFilename();
  ScratchFile mid_profile_file;
  GenerateProfile(mid_dex_files,
                  mid_profile_file.GetFile(),
                  /*method_frequency=*/ 5u,
                  /*type_frequency=*/ 4u);
  const std::string& mid_profile_filename = mid_profile_file.GetFilename();
  ScratchFile tail_profile_file;
  GenerateProfile(tail_dex_files,
                  tail_profile_file.GetFile(),
                  /*method_frequency=*/ 5u,
                  /*type_frequency=*/ 4u);
  const std::string& tail_profile_filename = tail_profile_file.GetFilename();

  // Compile the "head", i.e. the primary boot image.
  std::vector<std::string> extra_args;
  extra_args.push_back("--profile-file=" + head_profile_filename);
  extra_args.push_back(android::base::StringPrintf("--base=0x%08x", kBaseAddress));
  bool head_ok = CompileBootImage(extra_args, filename_prefix, head_dex_files, &error_msg);
  ASSERT_TRUE(head_ok) << error_msg;

  // Compile the "mid", i.e. the first extension.
  std::string mid_bcp_string = android::base::Join(mid_bcp, ':');
  extra_args.clear();
  extra_args.push_back("--profile-file=" + mid_profile_filename);
  AddRuntimeArg(extra_args, "-Xbootclasspath:" + mid_bcp_string);
  AddRuntimeArg(extra_args, "-Xbootclasspath-locations:" + mid_bcp_string);
  extra_args.push_back("--boot-image=" + base_location);
  bool mid_ok = CompileBootImage(extra_args, filename_prefix, mid_dex_files, &error_msg);
  ASSERT_TRUE(mid_ok) << error_msg;

  // Try to compile the "tail" without specifying the "mid" extension. This shall fail.
  extra_args.clear();
  extra_args.push_back("--profile-file=" + tail_profile_filename);
  std::string full_bcp_string = android::base::Join(full_bcp, ':');
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

  // Prepare directory for the single-image test that squashes the "mid" and "tail".
  std::string single_dir = scratch_dir + "single";
  mkdir_result = mkdir(single_dir.c_str(), 0700);
  ASSERT_EQ(0, mkdir_result);
  single_dir += '/';
  std::string single_image_dir = single_dir + GetInstructionSetString(kRuntimeISA);
  mkdir_result = mkdir(single_image_dir.c_str(), 0700);
  ASSERT_EQ(0, mkdir_result);
  std::string single_filename_prefix = single_image_dir + "/core";

  // The dex files for the single-image are everything not in the "head".
  ArrayRef<const std::string> single_dex_files = full_bcp.SubArray(/*pos=*/ head_dex_files.size());

  // Create a smaller profile for the single-image test that squashes the "mid" and "tail".
  ScratchFile single_profile_file;
  GenerateProfile(single_dex_files,
                  single_profile_file.GetFile(),
                  /*method_frequency=*/ 5u,
                  /*type_frequency=*/ 4u);
  const std::string& single_profile_filename = single_profile_file.GetFilename();

  // Prepare the single image name and location.
  CHECK_GE(single_dex_files.size(), 2u);
  std::string single_base_location = single_dir + base_name;
  std::vector<std::string> expanded_single = gc::space::ImageSpace::ExpandMultiImageLocations(
      single_dex_files.SubArray(/*pos=*/ 0u, /*length=*/ 1u),
      single_base_location,
      /*boot_image_extension=*/ true);
  CHECK_EQ(1u, expanded_single.size());
  std::string single_location = expanded_single[0];
  size_t single_slash_pos = single_location.rfind('/');
  ASSERT_NE(std::string::npos, single_slash_pos);
  std::string single_name = single_location.substr(single_slash_pos + 1u);
  CHECK_EQ(single_name, mid_name);

  // Compile the single-image against the primary boot image.
  extra_args.clear();
  extra_args.push_back("--profile-file=" + single_profile_filename);
  AddRuntimeArg(extra_args, "-Xbootclasspath:" + full_bcp_string);
  AddRuntimeArg(extra_args, "-Xbootclasspath-locations:" + full_bcp_string);
  extra_args.push_back("--boot-image=" + base_location);
  extra_args.push_back("--single-image");
  extra_args.push_back("--avoid-storing-invocation");  // For comparison below.
  error_msg.clear();
  bool single_ok =
      CompileBootImage(extra_args, single_filename_prefix, single_dex_files, &error_msg);
  ASSERT_TRUE(single_ok) << error_msg;

  reservation = MemMap::Invalid();  // Free the reserved memory for loading images.

  // Try to load the boot image with different image locations.
  std::vector<std::string> boot_class_path = libcore_dex_files;
  std::vector<std::unique_ptr<gc::space::ImageSpace>> boot_image_spaces;
  bool relocate = false;
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
                                                relocate,
                                                /*executable=*/ true,
                                                /*is_zygote=*/ false,
                                                /*extra_reservation_size=*/ 0u,
                                                &boot_image_spaces,
                                                &extra_reservation);
  };
  auto silent_load = [&](const std::string& image_location) {
    ScopedLogSeverity quiet(LogSeverity::FATAL);
    return load(image_location);
  };

  for (bool r : { false, true }) {
    relocate = r;

    // Load primary image with full path.
    bool load_ok = load(base_location);
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_FALSE(extra_reservation.IsValid());
    ASSERT_EQ(head_dex_files.size(), boot_image_spaces.size());

    // Fail to load primary image with just the name.
    load_ok = silent_load(base_name);
    ASSERT_FALSE(load_ok);

    // Fail to load primary image with a search path.
    load_ok = silent_load("*");
    ASSERT_FALSE(load_ok);
    load_ok = silent_load(scratch_dir + "*");
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
  }

  // Now copy the libcore dex files to the `scratch_dir` and retry loading the boot image
  // with BCP in the scratch_dir so that the images can be found based on BCP paths.
  CopyDexFiles(scratch_dir, &boot_class_path);

  for (bool r : { false, true }) {
    relocate = r;

    // Loading the primary image with just the name now succeeds.
    bool load_ok = load(base_name);
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_EQ(head_dex_files.size(), boot_image_spaces.size());

    // Loading the primary image with a search path still fails.
    load_ok = silent_load("*");
    ASSERT_FALSE(load_ok);
    load_ok = silent_load(scratch_dir + "*");
    ASSERT_FALSE(load_ok);

    // Load the primary and first extension without paths.
    load_ok = load(base_name + ':' + mid_name);
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_EQ(mid_bcp.size(), boot_image_spaces.size());

    // Load the primary without path and first extension with path.
    load_ok = load(base_name + ':' + mid_location);
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
    load_ok = silent_load(base_location + ":*:" + tail_location);
    ASSERT_FALSE(load_ok);
    load_ok = silent_load(base_location + ':' + scratch_dir + "*:" + tail_location);
    ASSERT_FALSE(load_ok);

    // Load the primary and single-image extension with full path.
    load_ok = load(base_location + ':' + single_location);
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_EQ(head_dex_files.size() + 1u, boot_image_spaces.size());

    // Load the primary with full path and single-image extension with a specified search path.
    load_ok = load(base_location + ':' + single_dir + '*');
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_EQ(head_dex_files.size() + 1u, boot_image_spaces.size());
  }

  // Recompile the single-image extension using file descriptors and compare contents.
  std::vector<std::string> expanded_single_filename_prefix =
      gc::space::ImageSpace::ExpandMultiImageLocations(
          single_dex_files.SubArray(/*pos=*/ 0u, /*length=*/ 1u),
          single_filename_prefix,
          /*boot_image_extension=*/ true);
  CHECK_EQ(1u, expanded_single_filename_prefix.size());
  std::string single_ext_prefix = expanded_single_filename_prefix[0];
  std::string single_ext_prefix2 = single_ext_prefix + "2";
  error_msg.clear();
  single_ok = CompileBootImage(extra_args,
                               single_filename_prefix,
                               single_dex_files,
                               &error_msg,
                               /*use_fd_prefix=*/ single_ext_prefix2);
  ASSERT_TRUE(single_ok) << error_msg;
  EXPECT_TRUE(CompareFiles(single_ext_prefix + ".art", single_ext_prefix2 + ".art"));
  EXPECT_TRUE(CompareFiles(single_ext_prefix + ".vdex", single_ext_prefix2 + ".vdex"));
  EXPECT_TRUE(CompareFiles(single_ext_prefix + ".oat", single_ext_prefix2 + ".oat"));

  // Test parsing profile specification and creating the boot image extension on-the-fly.
  // We must set --android-root in the image compiler options.
  AddAndroidRootToImageCompilerOptions();
  for (bool r : { false, true }) {
    relocate = r;

    // Try and fail to load everything as compiled extension.
    bool load_ok = silent_load(base_location + "!" + single_profile_filename);
    ASSERT_FALSE(load_ok);

    // Try and fail to load with invalid spec, two profile name separators.
    load_ok = silent_load(base_location + ":" + single_location + "!!arbitrary-profile-name");
    ASSERT_FALSE(load_ok);

    // Try and fail to load with invalid spec, missing profile name.
    load_ok = silent_load(base_location + ":" + single_location + "!");
    ASSERT_FALSE(load_ok);

    // Try and fail to load with invalid spec, missing component name.
    load_ok = silent_load(base_location + ":!" + single_profile_filename);
    ASSERT_FALSE(load_ok);

    // Load primary boot image, specifying invalid extension component and profile name.
    load_ok = load(base_location + ":/non-existent/" + single_name + "!non-existent-profile-name");
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_EQ(head_dex_files.size(), boot_image_spaces.size());

    // Load primary boot image and the single extension, specifying invalid profile name.
    // (Load extension from file.)
    load_ok = load(base_location + ":" + single_location + "!non-existent-profile-name");
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_EQ(head_dex_files.size() + 1u, boot_image_spaces.size());
    ASSERT_EQ(single_dex_files.size(),
              boot_image_spaces.back()->GetImageHeader().GetComponentCount());

    // Load primary boot image and fail to load the single extension, specifying
    // invalid extension component name but a valid profile file.
    // (Running dex2oat to compile extension is disabled.)
    ASSERT_FALSE(Runtime::Current()->IsImageDex2OatEnabled());
    load_ok = load(base_location + ":/non-existent/" + single_name + "!" + single_profile_filename);
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_EQ(head_dex_files.size(), boot_image_spaces.size());

    EnableImageDex2Oat();

    // Load primary boot image and the single extension, specifying invalid extension
    // component name but a valid profile file. (Compile extension by running dex2oat.)
    load_ok = load(base_location + ":/non-existent/" + single_name + "!" + single_profile_filename);
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_EQ(head_dex_files.size() + 1u, boot_image_spaces.size());
    ASSERT_EQ(single_dex_files.size(),
              boot_image_spaces.back()->GetImageHeader().GetComponentCount());

    // Load primary boot image and two extensions, specifying invalid extension component
    // names but valid profile files. (Compile extensions by running dex2oat.)
    load_ok = load(base_location + ":/non-existent/" + mid_name + "!" + mid_profile_filename
                                 + ":/non-existent/" + tail_name + "!" + tail_profile_filename);
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_EQ(head_dex_files.size() + 2u, boot_image_spaces.size());
    ASSERT_EQ(mid_dex_files.size(),
              boot_image_spaces[head_dex_files.size()]->GetImageHeader().GetComponentCount());
    ASSERT_EQ(tail_dex_files.size(),
              boot_image_spaces[head_dex_files.size() + 1u]->GetImageHeader().GetComponentCount());

    // Load primary boot image and fail to load extensions, specifying invalid component
    // names but valid profile file only for the second one. As we fail to load the first
    // extension, the second extension has a missing dependency and cannot be compiled.
    load_ok = load(base_location + ":/non-existent/" + mid_name
                                 + ":/non-existent/" + tail_name + "!" + tail_profile_filename);
    ASSERT_TRUE(load_ok) << error_msg;
    ASSERT_EQ(head_dex_files.size(), boot_image_spaces.size());

    DisableImageDex2Oat();
  }
}

}  // namespace art
