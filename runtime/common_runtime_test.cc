/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include "common_runtime_test.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cstdio>
#include "nativehelper/scoped_local_ref.h"

#include "android-base/stringprintf.h"

#include "art_field-inl.h"
#include "base/file_utils.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/mem_map.h"
#include "base/mutex.h"
#include "base/os.h"
#include "base/runtime_debug.h"
#include "base/stl_util.h"
#include "base/unix_file/fd_file.h"
#include "class_linker.h"
#include "class_loader_utils.h"
#include "compiler_callbacks.h"
#include "dex/art_dex_file_loader.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_file_loader.h"
#include "dex/method_reference.h"
#include "dex/primitive.h"
#include "dex/type_reference.h"
#include "gc/heap.h"
#include "gc/space/image_space.h"
#include "gc_root-inl.h"
#include "gtest/gtest.h"
#include "handle_scope-inl.h"
#include "interpreter/unstarted_runtime.h"
#include "jni/java_vm_ext.h"
#include "jni/jni_internal.h"
#include "mirror/class-alloc-inl.h"
#include "mirror/class-inl.h"
#include "mirror/class_loader-inl.h"
#include "mirror/object_array-alloc-inl.h"
#include "native/dalvik_system_DexFile.h"
#include "noop_compiler_callbacks.h"
#include "profile/profile_compilation_info.h"
#include "runtime-inl.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"
#include "well_known_classes.h"

namespace art {

using android::base::StringPrintf;

static bool unstarted_initialized_ = false;

CommonRuntimeTestImpl::CommonRuntimeTestImpl()
    : class_linker_(nullptr), java_lang_dex_file_(nullptr) {
}

CommonRuntimeTestImpl::~CommonRuntimeTestImpl() {
  // Ensure the dex files are cleaned up before the runtime.
  loaded_dex_files_.clear();
  runtime_.reset();
}

std::string CommonRuntimeTestImpl::GetAndroidTargetToolsDir(InstructionSet isa) {
  switch (isa) {
    case InstructionSet::kArm:
    case InstructionSet::kThumb2:
      return GetAndroidToolsDir("prebuilts/gcc/linux-x86/arm",
                                "arm-linux-androideabi",
                                "arm-linux-androideabi");
    case InstructionSet::kArm64:
      return GetAndroidToolsDir("prebuilts/gcc/linux-x86/aarch64",
                                "aarch64-linux-android",
                                "aarch64-linux-android");
    case InstructionSet::kX86:
    case InstructionSet::kX86_64:
      return GetAndroidToolsDir("prebuilts/gcc/linux-x86/x86",
                                "x86_64-linux-android",
                                "x86_64-linux-android");
    default:
      break;
  }
  ADD_FAILURE() << "Invalid isa " << isa;
  return "";
}

void CommonRuntimeTestImpl::SetUp() {
  CommonArtTestImpl::SetUp();

  std::string min_heap_string(StringPrintf("-Xms%zdm", gc::Heap::kDefaultInitialSize / MB));
  std::string max_heap_string(StringPrintf("-Xmx%zdm", gc::Heap::kDefaultMaximumSize / MB));

  RuntimeOptions options;
  std::string boot_class_path_string =
      GetClassPathOption("-Xbootclasspath:", GetLibCoreDexFileNames());
  std::string boot_class_path_locations_string =
      GetClassPathOption("-Xbootclasspath-locations:", GetLibCoreDexLocations());

  options.push_back(std::make_pair(boot_class_path_string, nullptr));
  options.push_back(std::make_pair(boot_class_path_locations_string, nullptr));
  options.push_back(std::make_pair("-Xcheck:jni", nullptr));
  options.push_back(std::make_pair(min_heap_string, nullptr));
  options.push_back(std::make_pair(max_heap_string, nullptr));

  // Technically this is redundant w/ common_art_test, but still check.
  options.push_back(std::make_pair("-XX:SlowDebug=true", nullptr));
  static bool gSlowDebugTestFlag = false;
  RegisterRuntimeDebugFlag(&gSlowDebugTestFlag);

  callbacks_.reset(new NoopCompilerCallbacks());

  SetUpRuntimeOptions(&options);

  // Install compiler-callbacks if SetupRuntimeOptions hasn't deleted them.
  if (callbacks_.get() != nullptr) {
    options.push_back(std::make_pair("compilercallbacks", callbacks_.get()));
  }

  PreRuntimeCreate();
  if (!Runtime::Create(options, false)) {
    LOG(FATAL) << "Failed to create runtime";
    UNREACHABLE();
  }
  PostRuntimeCreate();
  runtime_.reset(Runtime::Current());
  class_linker_ = runtime_->GetClassLinker();

  // Runtime::Create acquired the mutator_lock_ that is normally given away when we
  // Runtime::Start, give it away now and then switch to a more managable ScopedObjectAccess.
  Thread::Current()->TransitionFromRunnableToSuspended(kNative);

  // Get the boot class path from the runtime so it can be used in tests.
  boot_class_path_ = class_linker_->GetBootClassPath();
  ASSERT_FALSE(boot_class_path_.empty());
  java_lang_dex_file_ = boot_class_path_[0];

  FinalizeSetup();

  // Ensure that we're really running with debug checks enabled.
  CHECK(gSlowDebugTestFlag);
}

void CommonRuntimeTestImpl::FinalizeSetup() {
  // Initialize maps for unstarted runtime. This needs to be here, as running clinits needs this
  // set up.
  if (!unstarted_initialized_) {
    interpreter::UnstartedRuntime::Initialize();
    unstarted_initialized_ = true;
  }

  {
    ScopedObjectAccess soa(Thread::Current());
    runtime_->RunRootClinits(soa.Self());
  }

  // We're back in native, take the opportunity to initialize well known classes.
  WellKnownClasses::Init(Thread::Current()->GetJniEnv());

  // Create the heap thread pool so that the GC runs in parallel for tests. Normally, the thread
  // pool is created by the runtime.
  runtime_->GetHeap()->CreateThreadPool();
  runtime_->GetHeap()->VerifyHeap();  // Check for heap corruption before the test
  // Reduce timinig-dependent flakiness in OOME behavior (eg StubTest.AllocObject).
  runtime_->GetHeap()->SetMinIntervalHomogeneousSpaceCompactionByOom(0U);
}

void CommonRuntimeTestImpl::TearDown() {
  CommonArtTestImpl::TearDown();
  if (runtime_ != nullptr) {
    runtime_->GetHeap()->VerifyHeap();  // Check for heap corruption after the test
  }
}

// Check that for target builds we have ART_TARGET_NATIVETEST_DIR set.
#ifdef ART_TARGET
#ifndef ART_TARGET_NATIVETEST_DIR
#error "ART_TARGET_NATIVETEST_DIR not set."
#endif
// Wrap it as a string literal.
#define ART_TARGET_NATIVETEST_DIR_STRING STRINGIFY(ART_TARGET_NATIVETEST_DIR) "/"
#else
#define ART_TARGET_NATIVETEST_DIR_STRING ""
#endif

std::vector<const DexFile*> CommonRuntimeTestImpl::GetDexFiles(jobject jclass_loader) {
  ScopedObjectAccess soa(Thread::Current());

  StackHandleScope<1> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader = hs.NewHandle(
      soa.Decode<mirror::ClassLoader>(jclass_loader));
  return GetDexFiles(soa, class_loader);
}

std::vector<const DexFile*> CommonRuntimeTestImpl::GetDexFiles(
    ScopedObjectAccess& soa,
    Handle<mirror::ClassLoader> class_loader) {
  DCHECK(
      (class_loader->GetClass() ==
          soa.Decode<mirror::Class>(WellKnownClasses::dalvik_system_PathClassLoader)) ||
      (class_loader->GetClass() ==
          soa.Decode<mirror::Class>(WellKnownClasses::dalvik_system_DelegateLastClassLoader)));

  std::vector<const DexFile*> ret;
  VisitClassLoaderDexFiles(soa,
                           class_loader,
                           [&](const DexFile* cp_dex_file) {
                             if (cp_dex_file == nullptr) {
                               LOG(WARNING) << "Null DexFile";
                             } else {
                               ret.push_back(cp_dex_file);
                             }
                             return true;
                           });
  return ret;
}

const DexFile* CommonRuntimeTestImpl::GetFirstDexFile(jobject jclass_loader) {
  std::vector<const DexFile*> tmp(GetDexFiles(jclass_loader));
  DCHECK(!tmp.empty());
  const DexFile* ret = tmp[0];
  DCHECK(ret != nullptr);
  return ret;
}

jobject CommonRuntimeTestImpl::LoadMultiDex(const char* first_dex_name,
                                            const char* second_dex_name) {
  std::vector<std::unique_ptr<const DexFile>> first_dex_files = OpenTestDexFiles(first_dex_name);
  std::vector<std::unique_ptr<const DexFile>> second_dex_files = OpenTestDexFiles(second_dex_name);
  std::vector<const DexFile*> class_path;
  CHECK_NE(0U, first_dex_files.size());
  CHECK_NE(0U, second_dex_files.size());
  for (auto& dex_file : first_dex_files) {
    class_path.push_back(dex_file.get());
    loaded_dex_files_.push_back(std::move(dex_file));
  }
  for (auto& dex_file : second_dex_files) {
    class_path.push_back(dex_file.get());
    loaded_dex_files_.push_back(std::move(dex_file));
  }

  Thread* self = Thread::Current();
  jobject class_loader = Runtime::Current()->GetClassLinker()->CreatePathClassLoader(self,
                                                                                     class_path);
  self->SetClassLoaderOverride(class_loader);
  return class_loader;
}

jobject CommonRuntimeTestImpl::LoadDex(const char* dex_name) {
  jobject class_loader = LoadDexInPathClassLoader(dex_name, nullptr);
  Thread::Current()->SetClassLoaderOverride(class_loader);
  return class_loader;
}

jobject
CommonRuntimeTestImpl::LoadDexInWellKnownClassLoader(const std::vector<std::string>& dex_names,
                                                     jclass loader_class,
                                                     jobject parent_loader,
                                                     jobject shared_libraries) {
  std::vector<const DexFile*> class_path;
  for (const std::string& dex_name : dex_names) {
    std::vector<std::unique_ptr<const DexFile>> dex_files = OpenTestDexFiles(dex_name.c_str());
    CHECK_NE(0U, dex_files.size());
    for (auto& dex_file : dex_files) {
      class_path.push_back(dex_file.get());
      loaded_dex_files_.push_back(std::move(dex_file));
    }
  }
  Thread* self = Thread::Current();
  ScopedObjectAccess soa(self);

  jobject result = Runtime::Current()->GetClassLinker()->CreateWellKnownClassLoader(
      self,
      class_path,
      loader_class,
      parent_loader,
      shared_libraries);

  {
    // Verify we build the correct chain.

    ObjPtr<mirror::ClassLoader> actual_class_loader = soa.Decode<mirror::ClassLoader>(result);
    // Verify that the result has the correct class.
    CHECK_EQ(soa.Decode<mirror::Class>(loader_class), actual_class_loader->GetClass());
    // Verify that the parent is not null. The boot class loader will be set up as a
    // proper object.
    ObjPtr<mirror::ClassLoader> actual_parent(actual_class_loader->GetParent());
    CHECK(actual_parent != nullptr);

    if (parent_loader != nullptr) {
      // We were given a parent. Verify that it's what we expect.
      ObjPtr<mirror::ClassLoader> expected_parent = soa.Decode<mirror::ClassLoader>(parent_loader);
      CHECK_EQ(expected_parent, actual_parent);
    } else {
      // No parent given. The parent must be the BootClassLoader.
      CHECK(Runtime::Current()->GetClassLinker()->IsBootClassLoader(soa, actual_parent));
    }
  }

  return result;
}

jobject CommonRuntimeTestImpl::LoadDexInPathClassLoader(const std::string& dex_name,
                                                        jobject parent_loader,
                                                        jobject shared_libraries) {
  return LoadDexInPathClassLoader(std::vector<std::string>{ dex_name },
                                  parent_loader,
                                  shared_libraries);
}

jobject CommonRuntimeTestImpl::LoadDexInPathClassLoader(const std::vector<std::string>& names,
                                                        jobject parent_loader,
                                                        jobject shared_libraries) {
  return LoadDexInWellKnownClassLoader(names,
                                       WellKnownClasses::dalvik_system_PathClassLoader,
                                       parent_loader,
                                       shared_libraries);
}

jobject CommonRuntimeTestImpl::LoadDexInDelegateLastClassLoader(const std::string& dex_name,
                                                                jobject parent_loader) {
  return LoadDexInWellKnownClassLoader({ dex_name },
                                       WellKnownClasses::dalvik_system_DelegateLastClassLoader,
                                       parent_loader);
}

jobject CommonRuntimeTestImpl::LoadDexInInMemoryDexClassLoader(const std::string& dex_name,
                                                               jobject parent_loader) {
  return LoadDexInWellKnownClassLoader({ dex_name },
                                       WellKnownClasses::dalvik_system_InMemoryDexClassLoader,
                                       parent_loader);
}

void CommonRuntimeTestImpl::FillHeap(Thread* self,
                                     ClassLinker* class_linker,
                                     VariableSizedHandleScope* handle_scope) {
  DCHECK(handle_scope != nullptr);

  Runtime::Current()->GetHeap()->SetIdealFootprint(1 * GB);

  // Class java.lang.Object.
  Handle<mirror::Class> c(handle_scope->NewHandle(
      class_linker->FindSystemClass(self, "Ljava/lang/Object;")));
  // Array helps to fill memory faster.
  Handle<mirror::Class> ca(handle_scope->NewHandle(
      class_linker->FindSystemClass(self, "[Ljava/lang/Object;")));

  // Start allocating with ~128K
  size_t length = 128 * KB;
  while (length > 40) {
    const int32_t array_length = length / 4;  // Object[] has elements of size 4.
    MutableHandle<mirror::Object> h(handle_scope->NewHandle<mirror::Object>(
        mirror::ObjectArray<mirror::Object>::Alloc(self, ca.Get(), array_length)));
    if (self->IsExceptionPending() || h == nullptr) {
      self->ClearException();

      // Try a smaller length
      length = length / 2;
      // Use at most a quarter the reported free space.
      size_t mem = Runtime::Current()->GetHeap()->GetFreeMemory();
      if (length * 4 > mem) {
        length = mem / 4;
      }
    }
  }

  // Allocate simple objects till it fails.
  while (!self->IsExceptionPending()) {
    handle_scope->NewHandle<mirror::Object>(c->AllocObject(self));
  }
  self->ClearException();
}

void CommonRuntimeTestImpl::SetUpRuntimeOptionsForFillHeap(RuntimeOptions *options) {
  // Use a smaller heap
  bool found = false;
  for (std::pair<std::string, const void*>& pair : *options) {
    if (pair.first.find("-Xmx") == 0) {
      pair.first = "-Xmx4M";  // Smallest we can go.
      found = true;
    }
  }
  if (!found) {
    options->emplace_back("-Xmx4M", nullptr);
  }
}

void CommonRuntimeTestImpl::MakeInterpreted(ObjPtr<mirror::Class> klass) {
  PointerSize pointer_size = class_linker_->GetImagePointerSize();
  for (ArtMethod& method : klass->GetMethods(pointer_size)) {
    class_linker_->SetEntryPointsToInterpreter(&method);
  }
}

bool CommonRuntimeTestImpl::StartDex2OatCommandLine(/*out*/std::vector<std::string>* argv,
                                                    /*out*/std::string* error_msg,
                                                    bool use_runtime_bcp_and_image) {
  DCHECK(argv != nullptr);
  DCHECK(argv->empty());

  Runtime* runtime = Runtime::Current();
  if (use_runtime_bcp_and_image && runtime->GetHeap()->GetBootImageSpaces().empty()) {
    *error_msg = "No image location found for Dex2Oat.";
    return false;
  }

  argv->push_back(runtime->GetCompilerExecutable());
  if (runtime->IsJavaDebuggable()) {
    argv->push_back("--debuggable");
  }
  runtime->AddCurrentRuntimeFeaturesAsDex2OatArguments(argv);

  if (use_runtime_bcp_and_image) {
    argv->push_back("--runtime-arg");
    argv->push_back(GetClassPathOption("-Xbootclasspath:", GetLibCoreDexFileNames()));
    argv->push_back("--runtime-arg");
    argv->push_back(GetClassPathOption("-Xbootclasspath-locations:", GetLibCoreDexLocations()));

    const std::vector<gc::space::ImageSpace*>& image_spaces =
        runtime->GetHeap()->GetBootImageSpaces();
    DCHECK(!image_spaces.empty());
    argv->push_back("--boot-image=" + image_spaces[0]->GetImageLocation());
  }

  std::vector<std::string> compiler_options = runtime->GetCompilerOptions();
  argv->insert(argv->end(), compiler_options.begin(), compiler_options.end());
  return true;
}

bool CommonRuntimeTestImpl::CompileBootImage(const std::vector<std::string>& extra_args,
                                             const std::string& image_file_name_prefix,
                                             ArrayRef<const std::string> dex_files,
                                             ArrayRef<const std::string> dex_locations,
                                             std::string* error_msg,
                                             const std::string& use_fd_prefix) {
  Runtime* const runtime = Runtime::Current();
  std::vector<std::string> argv {
    runtime->GetCompilerExecutable(),
    "--runtime-arg",
    "-Xms64m",
    "--runtime-arg",
    "-Xmx64m",
    "--runtime-arg",
    "-Xverify:softfail",
  };
  CHECK_EQ(dex_files.size(), dex_locations.size());
  for (const std::string& dex_file : dex_files) {
    argv.push_back("--dex-file=" + dex_file);
  }
  for (const std::string& dex_location : dex_locations) {
    argv.push_back("--dex-location=" + dex_location);
  }
  if (runtime->IsJavaDebuggable()) {
    argv.push_back("--debuggable");
  }
  runtime->AddCurrentRuntimeFeaturesAsDex2OatArguments(&argv);

  if (!kIsTargetBuild) {
    argv.push_back("--host");
  }

  std::unique_ptr<File> art_file;
  std::unique_ptr<File> vdex_file;
  std::unique_ptr<File> oat_file;
  if (!use_fd_prefix.empty()) {
    art_file.reset(OS::CreateEmptyFile((use_fd_prefix + ".art").c_str()));
    vdex_file.reset(OS::CreateEmptyFile((use_fd_prefix + ".vdex").c_str()));
    oat_file.reset(OS::CreateEmptyFile((use_fd_prefix + ".oat").c_str()));
    argv.push_back("--image-fd=" + std::to_string(art_file->Fd()));
    argv.push_back("--output-vdex-fd=" + std::to_string(vdex_file->Fd()));
    argv.push_back("--oat-fd=" + std::to_string(oat_file->Fd()));
    argv.push_back("--oat-location=" + image_file_name_prefix + ".oat");
  } else {
    argv.push_back("--image=" + image_file_name_prefix + ".art");
    argv.push_back("--oat-file=" + image_file_name_prefix + ".oat");
    argv.push_back("--oat-location=" + image_file_name_prefix + ".oat");
  }

  std::vector<std::string> compiler_options = runtime->GetCompilerOptions();
  argv.insert(argv.end(), compiler_options.begin(), compiler_options.end());

  // We must set --android-root.
  const char* android_root = getenv("ANDROID_ROOT");
  CHECK(android_root != nullptr);
  argv.push_back("--android-root=" + std::string(android_root));
  argv.insert(argv.end(), extra_args.begin(), extra_args.end());

  bool result = RunDex2Oat(argv, error_msg);
  if (art_file != nullptr) {
    CHECK_EQ(0, art_file->FlushClose());
  }
  if (vdex_file != nullptr) {
    CHECK_EQ(0, vdex_file->FlushClose());
  }
  if (oat_file != nullptr) {
    CHECK_EQ(0, oat_file->FlushClose());
  }
  return result;
}

bool CommonRuntimeTestImpl::RunDex2Oat(const std::vector<std::string>& args,
                                       std::string* error_msg) {
  // We only want fatal logging for the error message.
  auto post_fork_fn = []() { return setenv("ANDROID_LOG_TAGS", "*:f", 1) == 0; };
  ForkAndExecResult res = ForkAndExec(args, post_fork_fn, error_msg);
  if (res.stage != ForkAndExecResult::kFinished) {
    *error_msg = strerror(errno);
    return false;
  }
  return res.StandardSuccess();
}

std::string CommonRuntimeTestImpl::GetImageDirectory() {
  std::string prefix;
  if (IsHost()) {
    const char* host_dir = getenv("ANDROID_HOST_OUT");
    CHECK(host_dir != nullptr);
    prefix = host_dir;
  }
  return prefix + kAndroidArtApexDefaultPath + "/javalib";
}

std::string CommonRuntimeTestImpl::GetImageLocation() {
  return GetImageDirectory() + "/boot.art";
}

std::string CommonRuntimeTestImpl::GetSystemImageFile() {
  std::string isa = GetInstructionSetString(kRuntimeISA);
  return GetImageDirectory() + "/" + isa + "/boot.art";
}

void CommonRuntimeTestImpl::EnterTransactionMode() {
  CHECK(!Runtime::Current()->IsActiveTransaction());
  Runtime::Current()->EnterTransactionMode(/*strict=*/ false, /*root=*/ nullptr);
}

void CommonRuntimeTestImpl::ExitTransactionMode() {
  Runtime::Current()->ExitTransactionMode();
  CHECK(!Runtime::Current()->IsActiveTransaction());
}

void CommonRuntimeTestImpl::RollbackAndExitTransactionMode() {
  Runtime::Current()->RollbackAndExitTransactionMode();
  CHECK(!Runtime::Current()->IsActiveTransaction());
}

bool CommonRuntimeTestImpl::IsTransactionAborted() {
  return Runtime::Current()->IsTransactionAborted();
}

void CommonRuntimeTestImpl::VisitDexes(ArrayRef<const std::string> dexes,
                                       const std::function<void(MethodReference)>& method_visitor,
                                       const std::function<void(TypeReference)>& class_visitor,
                                       size_t method_frequency,
                                       size_t class_frequency) {
  size_t method_counter = 0;
  size_t class_counter = 0;
  for (const std::string& dex : dexes) {
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

void CommonRuntimeTestImpl::GenerateProfile(ArrayRef<const std::string> dexes,
                                            File* out_file,
                                            size_t method_frequency,
                                            size_t type_frequency) {
  ProfileCompilationInfo profile;
  VisitDexes(
      dexes,
      [&profile](MethodReference ref) {
        uint32_t flags = ProfileCompilationInfo::MethodHotness::kFlagHot |
            ProfileCompilationInfo::MethodHotness::kFlagStartup;
        EXPECT_TRUE(profile.AddMethod(
            ProfileMethodInfo(ref),
            static_cast<ProfileCompilationInfo::MethodHotness::Flag>(flags)));
      },
      [&profile](TypeReference ref) {
        std::set<dex::TypeIndex> classes;
        classes.insert(ref.TypeIndex());
        EXPECT_TRUE(profile.AddClassesForDex(ref.dex_file, classes.begin(), classes.end()));
      },
      method_frequency,
      type_frequency);
  profile.Save(out_file->Fd());
  EXPECT_EQ(out_file->Flush(), 0);
}

CheckJniAbortCatcher::CheckJniAbortCatcher() : vm_(Runtime::Current()->GetJavaVM()) {
  vm_->SetCheckJniAbortHook(Hook, &actual_);
}

CheckJniAbortCatcher::~CheckJniAbortCatcher() {
  vm_->SetCheckJniAbortHook(nullptr, nullptr);
  EXPECT_TRUE(actual_.empty()) << actual_;
}

void CheckJniAbortCatcher::Check(const std::string& expected_text) {
  Check(expected_text.c_str());
}

void CheckJniAbortCatcher::Check(const char* expected_text) {
  EXPECT_TRUE(actual_.find(expected_text) != std::string::npos) << "\n"
      << "Expected to find: " << expected_text << "\n"
      << "In the output   : " << actual_;
  actual_.clear();
}

void CheckJniAbortCatcher::Hook(void* data, const std::string& reason) {
  // We use += because when we're hooking the aborts like this, multiple problems can be found.
  *reinterpret_cast<std::string*>(data) += reason;
}

}  // namespace art
