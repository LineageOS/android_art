/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "common_compiler_test.h"

#include "aot_class_linker.h"
#include "base/casts.h"
#include "class_linker-inl.h"
#include "handle.h"
#include "handle_scope-inl.h"
#include "mirror/class_loader-inl.h"
#include "mirror/dex_cache.h"
#include "mirror/object-inl.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"
#include "well_known_classes.h"

namespace art {

class ModuleExclusionTest : public CommonCompilerTest {
 public:
  explicit ModuleExclusionTest(const std::string& module)
      : CommonCompilerTest(),
        module_(module) {}

  std::vector<std::string> GetLibCoreModuleNames() const override {
    std::vector<std::string> modules = CommonCompilerTest::GetLibCoreModuleNames();
    // Exclude `module_` from boot class path.
    auto it = std::find(modules.begin(), modules.end(), module_);
    if (it != modules.end()) {
      modules.erase(it);
    }
    return modules;
  }

  void DoTest() {
    Thread* self = Thread::Current();
    ScopedObjectAccess soa(self);
    StackHandleScope<2u> hs(self);
    Runtime* runtime = Runtime::Current();
    ASSERT_TRUE(runtime->IsAotCompiler());
    ClassLinker* class_linker = runtime->GetClassLinker();
    CHECK(loaded_dex_files_.empty());
    Handle<mirror::ClassLoader> class_loader = hs.NewHandle(LoadModule(soa, class_linker));
    MutableHandle<mirror::DexCache> dex_cache = hs.NewHandle<mirror::DexCache>(nullptr);
    CHECK(!loaded_dex_files_.empty());

    // Verify that classes defined in the loaded dex files cannot be resolved.
    for (const std::unique_ptr<const DexFile>& dex_file : loaded_dex_files_) {
      dex_cache.Assign(class_linker->RegisterDexFile(*dex_file, class_loader.Get()));
      for (size_t i = 0u, size = dex_file->NumClassDefs(); i != size; ++i) {
        const dex::ClassDef& class_def = dex_file->GetClassDef(i);
        ObjPtr<mirror::Class> resolved_type =
            class_linker->ResolveType(class_def.class_idx_, dex_cache, class_loader);
        ASSERT_TRUE(resolved_type == nullptr) << resolved_type->PrettyDescriptor();
        ASSERT_TRUE(self->IsExceptionPending());
        self->ClearException();
      }
    }
  }

 protected:
  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    CommonCompilerTest::SetUpRuntimeOptions(options);

    // Set up the image location to be used by StartDex2OatCommandLine().
    // Using a prebuilt image also makes the test run faster.
    options->push_back(std::make_pair("-Ximage:" + GetImageLocation(), nullptr));
  }

  std::string GetModuleFileName() const {
    std::vector<std::string> filename = GetLibCoreDexFileNames({ module_ });
    CHECK_EQ(filename.size(), 1u);
    return filename[0];
  }

  // Load the module as an app, i.e. in a class loader other than the boot class loader.
  ObjPtr<mirror::ClassLoader> LoadModule(ScopedObjectAccess& soa, ClassLinker* class_linker)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    std::string filename = GetModuleFileName();
    std::vector<std::unique_ptr<const DexFile>> dex_files = OpenDexFiles(filename.c_str());

    std::vector<const DexFile*> class_path;
    CHECK_NE(0U, dex_files.size());
    for (auto& dex_file : dex_files) {
      class_path.push_back(dex_file.get());
      loaded_dex_files_.push_back(std::move(dex_file));
    }

    StackHandleScope<1u> hs(soa.Self());
    Handle<mirror::Class> loader_class(hs.NewHandle(
        soa.Decode<mirror::Class>(WellKnownClasses::dalvik_system_PathClassLoader)));
    ScopedNullHandle<mirror::ClassLoader> parent_loader;
    ScopedNullHandle<mirror::ObjectArray<mirror::ClassLoader>> shared_libraries;

    ObjPtr<mirror::ClassLoader> result = class_linker->CreateWellKnownClassLoader(
        soa.Self(),
        class_path,
        loader_class,
        parent_loader,
        shared_libraries);

    // Verify that the result has the correct class.
    CHECK_EQ(loader_class.Get(), result->GetClass());
    // Verify that the parent is not null. The boot class loader will be set up as a
    // proper BootClassLoader object.
    ObjPtr<mirror::ClassLoader> actual_parent(result->GetParent());
    CHECK(actual_parent != nullptr);
    CHECK(class_linker->IsBootClassLoader(soa, actual_parent));

    return result;
  }

  const std::string module_;
};

class ConscryptExclusionTest : public ModuleExclusionTest {
 public:
  ConscryptExclusionTest() : ModuleExclusionTest("conscrypt") {}
};

TEST_F(ConscryptExclusionTest, Test) {
  Runtime* runtime = Runtime::Current();
  ASSERT_TRUE(runtime->IsAotCompiler());
  AotClassLinker* aot_class_linker = down_cast<AotClassLinker*>(runtime->GetClassLinker());
  const std::vector<std::string> package_list = {
      // Reserved conscrypt packages (includes sub-packages under these paths).
      "android.net.ssl",
      "com.android.org.conscrypt",
  };
  bool list_applied = aot_class_linker->SetUpdatableBootClassPackages(package_list);
  ASSERT_TRUE(list_applied);
  DoTest();

  // Also test passing the list to dex2oat.
  ScratchFile package_list_file;
  for (const std::string& package : package_list) {
    std::string data = package + '\n';
    ASSERT_TRUE(package_list_file.GetFile()->WriteFully(data.data(), data.size()));
  }
  ASSERT_EQ(0, package_list_file.GetFile()->Flush());
  ScratchDir scratch_dir;
  std::string jar_name = GetModuleFileName();
  std::string odex_name = scratch_dir.GetPath() + module_ + ".odex";
  std::vector<std::string> argv;
  std::string error_msg;
  bool success = StartDex2OatCommandLine(&argv, &error_msg);
  ASSERT_TRUE(success) << error_msg;
  argv.insert(argv.end(), {
      "--dex-file=" + jar_name,
      "--dex-location=" + jar_name,
      "--oat-file=" + odex_name,
      "--compiler-filter=speed",
      "--updatable-bcp-packages-file=" + package_list_file.GetFilename()
  });
  success = RunDex2Oat(argv, &error_msg);
  ASSERT_TRUE(success) << error_msg;
  // Load the odex file.
  std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/ -1,
                                                   odex_name.c_str(),
                                                   odex_name.c_str(),
                                                   /*executable=*/ false,
                                                   /*low_4gb=*/ false,
                                                   jar_name,
                                                   &error_msg));
  ASSERT_TRUE(odex_file != nullptr) << error_msg;
  // Check that no classes have been resolved.
  for (const OatDexFile* oat_dex_file : odex_file->GetOatDexFiles()) {
    std::unique_ptr<const DexFile> dex_file = oat_dex_file->OpenDexFile(&error_msg);
    ASSERT_TRUE(dex_file != nullptr);
    for (size_t i = 0, num_class_defs = dex_file->NumClassDefs(); i != num_class_defs; ++i) {
      ClassStatus status = oat_dex_file->GetOatClass(i).GetStatus();
      ASSERT_FALSE(mirror::Class::IsErroneous(status));
      ASSERT_LT(status, ClassStatus::kResolved);
    }
  }
}

}  // namespace art
