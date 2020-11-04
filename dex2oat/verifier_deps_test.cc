/*
 * Copyright (C) 2016 The Android Open Source Project
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

// Test is in compiler, as it uses compiler related code.
#include "verifier/verifier_deps.h"

#include "art_method-inl.h"
#include "base/indenter.h"
#include "class_linker.h"
#include "common_compiler_driver_test.h"
#include "compiler_callbacks.h"
#include "dex/class_accessor-inl.h"
#include "dex/class_iterator.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_file_types.h"
#include "dex/verification_results.h"
#include "dex/verified_method.h"
#include "driver/compiler_driver-inl.h"
#include "driver/compiler_options.h"
#include "handle_scope-inl.h"
#include "mirror/class_loader.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"
#include "utils/atomic_dex_ref_map-inl.h"
#include "verifier/method_verifier-inl.h"

namespace art {
namespace verifier {

class VerifierDepsCompilerCallbacks : public CompilerCallbacks {
 public:
  VerifierDepsCompilerCallbacks()
      : CompilerCallbacks(CompilerCallbacks::CallbackMode::kCompileApp),
        deps_(nullptr) {}

  void MethodVerified(verifier::MethodVerifier* verifier ATTRIBUTE_UNUSED) override {}
  void ClassRejected(ClassReference ref ATTRIBUTE_UNUSED) override {}

  verifier::VerifierDeps* GetVerifierDeps() const override { return deps_; }
  void SetVerifierDeps(verifier::VerifierDeps* deps) override { deps_ = deps; }

 private:
  verifier::VerifierDeps* deps_;
};

class VerifierDepsTest : public CommonCompilerDriverTest {
 public:
  void SetUpRuntimeOptions(RuntimeOptions* options) override {
    CommonCompilerTest::SetUpRuntimeOptions(options);
    callbacks_.reset(new VerifierDepsCompilerCallbacks());
  }

  ObjPtr<mirror::Class> FindClassByName(ScopedObjectAccess& soa, const std::string& name)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader_handle(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(class_loader_)));
    ObjPtr<mirror::Class> klass =
        class_linker_->FindClass(soa.Self(), name.c_str(), class_loader_handle);
    if (klass == nullptr) {
      DCHECK(soa.Self()->IsExceptionPending());
      soa.Self()->ClearException();
    }
    return klass;
  }

  void SetupCompilerDriver() {
    compiler_options_->image_type_ = CompilerOptions::ImageType::kNone;
    compiler_driver_->InitializeThreadPools();
  }

  void VerifyWithCompilerDriver(verifier::VerifierDeps* verifier_deps) {
    TimingLogger timings("Verify", false, false);
    // The compiler driver handles the verifier deps in the callbacks, so
    // remove what this class did for unit testing.
    if (verifier_deps == nullptr) {
      // Create some verifier deps by default if they are not already specified.
      verifier_deps = new verifier::VerifierDeps(dex_files_);
      verifier_deps_.reset(verifier_deps);
    }
    callbacks_->SetVerifierDeps(verifier_deps);
    compiler_driver_->Verify(class_loader_, dex_files_, &timings, verification_results_.get());
    callbacks_->SetVerifierDeps(nullptr);
    // Clear entries in the verification results to avoid hitting a DCHECK that
    // we always succeed inserting a new entry after verifying.
    AtomicDexRefMap<MethodReference, const VerifiedMethod*>* map =
        &verification_results_->atomic_verified_methods_;
    map->Visit([](const DexFileReference& ref ATTRIBUTE_UNUSED, const VerifiedMethod* method) {
      delete method;
    });
    map->ClearEntries();
  }

  void SetVerifierDeps(const std::vector<const DexFile*>& dex_files) {
    verifier_deps_.reset(new verifier::VerifierDeps(dex_files));
    VerifierDepsCompilerCallbacks* callbacks =
        reinterpret_cast<VerifierDepsCompilerCallbacks*>(callbacks_.get());
    callbacks->SetVerifierDeps(verifier_deps_.get());
  }

  void LoadDexFile(ScopedObjectAccess& soa, const char* name1, const char* name2 = nullptr)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    class_loader_ = (name2 == nullptr) ? LoadDex(name1) : LoadMultiDex(name1, name2);
    dex_files_ = GetDexFiles(class_loader_);
    primary_dex_file_ = dex_files_.front();

    SetVerifierDeps(dex_files_);
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::ClassLoader> loader =
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(class_loader_));
    for (const DexFile* dex_file : dex_files_) {
      class_linker_->RegisterDexFile(*dex_file, loader.Get());
    }
    for (const DexFile* dex_file : dex_files_) {
      verification_results_->AddDexFile(dex_file);
    }
    SetDexFilesForOatFile(dex_files_);
  }

  void LoadDexFile(ScopedObjectAccess& soa) REQUIRES_SHARED(Locks::mutator_lock_) {
    LoadDexFile(soa, "VerifierDeps");
    CHECK_EQ(dex_files_.size(), 1u);
    klass_Main_ = FindClassByName(soa, "LMain;");
    CHECK(klass_Main_ != nullptr);
  }

  bool VerifyMethod(const std::string& method_name) {
    ScopedObjectAccess soa(Thread::Current());
    LoadDexFile(soa);

    StackHandleScope<2> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader_handle(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(class_loader_)));
    Handle<mirror::DexCache> dex_cache_handle(hs.NewHandle(klass_Main_->GetDexCache()));

    const dex::ClassDef* class_def = klass_Main_->GetClassDef();
    ClassAccessor accessor(*primary_dex_file_, *class_def);

    bool has_failures = true;
    bool found_method = false;

    for (const ClassAccessor::Method& method : accessor.GetMethods()) {
      ArtMethod* resolved_method =
          class_linker_->ResolveMethod<ClassLinker::ResolveMode::kNoChecks>(
              method.GetIndex(),
              dex_cache_handle,
              class_loader_handle,
              /* referrer= */ nullptr,
              method.GetInvokeType(class_def->access_flags_));
      CHECK(resolved_method != nullptr);
      if (method_name == resolved_method->GetName()) {
        soa.Self()->SetVerifierDeps(callbacks_->GetVerifierDeps());
        std::unique_ptr<MethodVerifier> verifier(
            MethodVerifier::CreateVerifier(soa.Self(),
                                           primary_dex_file_,
                                           dex_cache_handle,
                                           class_loader_handle,
                                           *class_def,
                                           method.GetCodeItem(),
                                           method.GetIndex(),
                                           resolved_method,
                                           method.GetAccessFlags(),
                                           /* can_load_classes= */ true,
                                           /* allow_soft_failures= */ true,
                                           /* need_precise_constants= */ true,
                                           /* verify to dump */ false,
                                           /* allow_thread_suspension= */ true,
                                           /* api_level= */ 0));
        verifier->Verify();
        soa.Self()->SetVerifierDeps(nullptr);
        has_failures = verifier->HasFailures();
        found_method = true;
      }
    }
    CHECK(found_method) << "Expected to find method " << method_name;
    return !has_failures;
  }

  void VerifyDexFile(const char* multidex = nullptr) {
    {
      ScopedObjectAccess soa(Thread::Current());
      LoadDexFile(soa, "VerifierDeps", multidex);
    }
    SetupCompilerDriver();
    VerifyWithCompilerDriver(/* verifier_deps= */ nullptr);
  }

  bool TestAssignabilityRecording(const std::string& dst, const std::string& src) {
    ScopedObjectAccess soa(Thread::Current());
    LoadDexFile(soa);
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::Class> klass_dst = hs.NewHandle(FindClassByName(soa, dst));
    DCHECK(klass_dst != nullptr) << dst;
    ObjPtr<mirror::Class> klass_src = FindClassByName(soa, src);
    DCHECK(klass_src != nullptr) << src;
    verifier_deps_->AddAssignability(*primary_dex_file_,
                                     primary_dex_file_->GetClassDef(0),
                                     klass_dst.Get(),
                                     klass_src);
    return true;
  }

  // Check that the status of classes in `class_loader_` match the
  // expected status in `deps`.
  void VerifyClassStatus(const verifier::VerifierDeps& deps) {
    ScopedObjectAccess soa(Thread::Current());
    StackHandleScope<2> hs(soa.Self());
    Handle<mirror::ClassLoader> class_loader_handle(
        hs.NewHandle(soa.Decode<mirror::ClassLoader>(class_loader_)));
    MutableHandle<mirror::Class> cls(hs.NewHandle<mirror::Class>(nullptr));
    for (const DexFile* dex_file : dex_files_) {
      const std::vector<bool>& verified_classes = deps.GetVerifiedClasses(*dex_file);
      ASSERT_EQ(verified_classes.size(), dex_file->NumClassDefs());
      for (uint32_t i = 0; i < dex_file->NumClassDefs(); ++i) {
        const dex::ClassDef& class_def = dex_file->GetClassDef(i);
        const char* descriptor = dex_file->GetClassDescriptor(class_def);
        cls.Assign(class_linker_->FindClass(soa.Self(), descriptor, class_loader_handle));
        if (cls == nullptr) {
          CHECK(soa.Self()->IsExceptionPending());
          soa.Self()->ClearException();
        } else if (&cls->GetDexFile() != dex_file) {
          // Ignore classes from different dex files.
        } else if (verified_classes[i]) {
          ASSERT_EQ(cls->GetStatus(), ClassStatus::kVerifiedNeedsAccessChecks);
        } else {
          ASSERT_LT(cls->GetStatus(), ClassStatus::kVerified);
        }
      }
    }
  }

  uint16_t GetClassDefIndex(const std::string& cls, const DexFile& dex_file) {
    const dex::TypeId* type_id = dex_file.FindTypeId(cls.c_str());
    DCHECK(type_id != nullptr);
    dex::TypeIndex type_idx = dex_file.GetIndexForTypeId(*type_id);
    const dex::ClassDef* class_def = dex_file.FindClassDef(type_idx);
    DCHECK(class_def != nullptr);
    return dex_file.GetIndexForClassDef(*class_def);
  }

  bool HasUnverifiedClass(const std::string& cls) {
    return HasUnverifiedClass(cls, *primary_dex_file_);
  }

  bool HasUnverifiedClass(const std::string& cls, const DexFile& dex_file) {
    uint16_t class_def_idx = GetClassDefIndex(cls, dex_file);
    return !verifier_deps_->GetVerifiedClasses(dex_file)[class_def_idx];
  }

  bool HasRedefinedClass(const std::string& cls) {
    uint16_t class_def_idx = GetClassDefIndex(cls, *primary_dex_file_);
    return verifier_deps_->GetRedefinedClasses(*primary_dex_file_)[class_def_idx];
  }

  // Iterates over all assignability records and tries to find an entry which
  // matches the expected destination/source pair.
  bool HasAssignable(const std::string& expected_destination,
                     const std::string& expected_source) const {
    for (auto& dex_dep : verifier_deps_->dex_deps_) {
      const DexFile& dex_file = *dex_dep.first;
      auto& storage = dex_dep.second->assignable_types_;
      for (auto& set : storage) {
        for (auto& entry : set) {
          std::string actual_destination =
              verifier_deps_->GetStringFromId(dex_file, entry.GetDestination());
          std::string actual_source = verifier_deps_->GetStringFromId(dex_file, entry.GetSource());
          if ((expected_destination == actual_destination) && (expected_source == actual_source)) {
            return true;
          }
        }
      }
    }
    return false;
  }

  size_t NumberOfCompiledDexFiles() {
    return verifier_deps_->dex_deps_.size();
  }

  bool HasBoolValue(const std::vector<bool>& vec, bool value) {
    return std::count(vec.begin(), vec.end(), value) > 0;
  }

  bool HasEachKindOfRecord() {
    bool has_strings = false;
    bool has_assignability = false;
    bool has_verified_classes = false;
    bool has_unverified_classes = false;
    bool has_redefined_classes = false;
    bool has_not_redefined_classes = false;

    for (auto& entry : verifier_deps_->dex_deps_) {
      has_strings |= !entry.second->strings_.empty();
      has_assignability |= !entry.second->assignable_types_.empty();
      has_verified_classes |= HasBoolValue(entry.second->verified_classes_, true);
      has_unverified_classes |= HasBoolValue(entry.second->verified_classes_, false);
      has_redefined_classes |= HasBoolValue(entry.second->redefined_classes_, true);
      has_not_redefined_classes |= HasBoolValue(entry.second->redefined_classes_, false);
    }

    return has_strings &&
           has_assignability &&
           has_verified_classes &&
           has_unverified_classes &&
           has_redefined_classes &&
           has_not_redefined_classes;
  }

  // Load the dex file again with a new class loader, decode the VerifierDeps
  // in `buffer`, allow the caller to modify the deps and then run validation.
  template<typename Fn>
  bool RunValidation(Fn fn, const std::vector<uint8_t>& buffer, std::string* error_msg) {
    ScopedObjectAccess soa(Thread::Current());

    jobject second_loader = LoadDex("VerifierDeps");
    const auto& second_dex_files = GetDexFiles(second_loader);

    VerifierDeps decoded_deps(second_dex_files, /*output_only=*/ false);
    bool parsed = decoded_deps.ParseStoredData(second_dex_files, ArrayRef<const uint8_t>(buffer));
    CHECK(parsed);
    VerifierDeps::DexFileDeps* decoded_dex_deps =
        decoded_deps.GetDexFileDeps(*second_dex_files.front());

    // Let the test modify the dependencies.
    fn(*decoded_dex_deps);

    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::ClassLoader> new_class_loader =
        hs.NewHandle<mirror::ClassLoader>(soa.Decode<mirror::ClassLoader>(second_loader));

    return decoded_deps.ValidateDependencies(soa.Self(),
                                             new_class_loader,
                                             std::vector<const DexFile*>(),
                                             error_msg);
  }

  std::unique_ptr<verifier::VerifierDeps> verifier_deps_;
  std::vector<const DexFile*> dex_files_;
  const DexFile* primary_dex_file_;
  jobject class_loader_;
  ObjPtr<mirror::Class> klass_Main_;
};

TEST_F(VerifierDepsTest, StringToId) {
  ScopedObjectAccess soa(Thread::Current());
  LoadDexFile(soa);

  dex::StringIndex id_Main1 = verifier_deps_->GetIdFromString(*primary_dex_file_, "LMain;");
  ASSERT_LT(id_Main1.index_, primary_dex_file_->NumStringIds());
  ASSERT_EQ("LMain;", verifier_deps_->GetStringFromId(*primary_dex_file_, id_Main1));

  dex::StringIndex id_Main2 = verifier_deps_->GetIdFromString(*primary_dex_file_, "LMain;");
  ASSERT_LT(id_Main2.index_, primary_dex_file_->NumStringIds());
  ASSERT_EQ("LMain;", verifier_deps_->GetStringFromId(*primary_dex_file_, id_Main2));

  dex::StringIndex id_Lorem1 = verifier_deps_->GetIdFromString(*primary_dex_file_, "Lorem ipsum");
  ASSERT_GE(id_Lorem1.index_, primary_dex_file_->NumStringIds());
  ASSERT_EQ("Lorem ipsum", verifier_deps_->GetStringFromId(*primary_dex_file_, id_Lorem1));

  dex::StringIndex id_Lorem2 = verifier_deps_->GetIdFromString(*primary_dex_file_, "Lorem ipsum");
  ASSERT_GE(id_Lorem2.index_, primary_dex_file_->NumStringIds());
  ASSERT_EQ("Lorem ipsum", verifier_deps_->GetStringFromId(*primary_dex_file_, id_Lorem2));

  ASSERT_EQ(id_Main1, id_Main2);
  ASSERT_EQ(id_Lorem1, id_Lorem2);
  ASSERT_NE(id_Main1, id_Lorem1);
}

TEST_F(VerifierDepsTest, Assignable_BothInBoot) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst= */ "Ljava/util/TimeZone;",
                                         /* src= */ "Ljava/util/SimpleTimeZone;"));
  ASSERT_TRUE(HasAssignable("Ljava/util/TimeZone;", "Ljava/util/SimpleTimeZone;"));
}

TEST_F(VerifierDepsTest, Assignable_DestinationInBoot1) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst= */ "Ljava/net/Socket;",
                                         /* src= */ "LMySSLSocket;"));
  ASSERT_TRUE(HasAssignable("Ljava/net/Socket;", "Ljavax/net/ssl/SSLSocket;"));
}

TEST_F(VerifierDepsTest, Assignable_DestinationInBoot2) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst= */ "Ljava/util/TimeZone;",
                                         /* src= */ "LMySimpleTimeZone;"));
  ASSERT_TRUE(HasAssignable("Ljava/util/TimeZone;", "Ljava/util/SimpleTimeZone;"));
}

TEST_F(VerifierDepsTest, Assignable_DestinationInBoot3) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst= */ "Ljava/util/Collection;",
                                         /* src= */ "LMyThreadSet;"));
  ASSERT_TRUE(HasAssignable("Ljava/util/Collection;", "Ljava/util/Set;"));
}

TEST_F(VerifierDepsTest, Assignable_BothArrays_Resolved) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst= */ "[[Ljava/util/TimeZone;",
                                         /* src= */ "[[Ljava/util/SimpleTimeZone;"));
  // If the component types of both arrays are resolved, we optimize the list of
  // dependencies by recording a dependency on the component types.
  ASSERT_FALSE(HasAssignable("[[Ljava/util/TimeZone;", "[[Ljava/util/SimpleTimeZone;"));
  ASSERT_FALSE(HasAssignable("[Ljava/util/TimeZone;", "[Ljava/util/SimpleTimeZone;"));
  ASSERT_TRUE(HasAssignable("Ljava/util/TimeZone;", "Ljava/util/SimpleTimeZone;"));
}

TEST_F(VerifierDepsTest, ReturnType_Reference) {
  ASSERT_TRUE(VerifyMethod("ReturnType_Reference"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/lang/IllegalStateException;"));
}

TEST_F(VerifierDepsTest, InvokeArgumentType) {
  ASSERT_TRUE(VerifyMethod("InvokeArgumentType"));
  ASSERT_TRUE(HasAssignable("Ljava/util/TimeZone;", "Ljava/util/SimpleTimeZone;"));
}

TEST_F(VerifierDepsTest, MergeTypes_RegisterLines) {
  ASSERT_TRUE(VerifyMethod("MergeTypes_RegisterLines"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Exception;", "Ljava/net/SocketTimeoutException;"));
  ASSERT_TRUE(HasAssignable(
      "Ljava/lang/Exception;", "Ljava/util/concurrent/TimeoutException;"));
}

TEST_F(VerifierDepsTest, MergeTypes_IfInstanceOf) {
  ASSERT_TRUE(VerifyMethod("MergeTypes_IfInstanceOf"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Exception;", "Ljava/net/SocketTimeoutException;"));
  ASSERT_TRUE(HasAssignable(
      "Ljava/lang/Exception;", "Ljava/util/concurrent/TimeoutException;"));
}

TEST_F(VerifierDepsTest, MergeTypes_Unresolved) {
  ASSERT_TRUE(VerifyMethod("MergeTypes_Unresolved"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Exception;", "Ljava/net/SocketTimeoutException;"));
  ASSERT_TRUE(HasAssignable(
      "Ljava/lang/Exception;", "Ljava/util/concurrent/TimeoutException;"));
}

TEST_F(VerifierDepsTest, Throw) {
  ASSERT_TRUE(VerifyMethod("Throw"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/lang/IllegalStateException;"));
}

TEST_F(VerifierDepsTest, MoveException_Resolved) {
  ASSERT_TRUE(VerifyMethod("MoveException_Resolved"));

  // Testing that all exception types are assignable to Throwable.
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/io/InterruptedIOException;"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/net/SocketTimeoutException;"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/util/zip/ZipException;"));

  // Testing that the merge type is assignable to Throwable.
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/io/IOException;"));

  // Merging of exception types.
  ASSERT_TRUE(HasAssignable("Ljava/io/IOException;", "Ljava/io/InterruptedIOException;"));
  ASSERT_TRUE(HasAssignable("Ljava/io/IOException;", "Ljava/util/zip/ZipException;"));
  ASSERT_TRUE(HasAssignable(
      "Ljava/io/InterruptedIOException;", "Ljava/net/SocketTimeoutException;"));
}

TEST_F(VerifierDepsTest, InstanceField_Resolved_DeclaredInReferenced) {
  ASSERT_TRUE(VerifyMethod("InstanceField_Resolved_DeclaredInReferenced"));
  ASSERT_TRUE(HasAssignable(
      "Ljava/io/InterruptedIOException;", "Ljava/net/SocketTimeoutException;"));
}

TEST_F(VerifierDepsTest, InstanceField_Resolved_DeclaredInSuperclass1) {
  ASSERT_TRUE(VerifyMethod("InstanceField_Resolved_DeclaredInSuperclass1"));
  ASSERT_TRUE(HasAssignable(
      "Ljava/io/InterruptedIOException;", "Ljava/net/SocketTimeoutException;"));
}

TEST_F(VerifierDepsTest, InstanceField_Resolved_DeclaredInSuperclass2) {
  ASSERT_TRUE(VerifyMethod("InstanceField_Resolved_DeclaredInSuperclass2"));
  ASSERT_TRUE(HasAssignable(
      "Ljava/io/InterruptedIOException;", "Ljava/net/SocketTimeoutException;"));
}

TEST_F(VerifierDepsTest, InvokeVirtual_Resolved_DeclaredInReferenced) {
  ASSERT_TRUE(VerifyMethod("InvokeVirtual_Resolved_DeclaredInReferenced"));
  // Type dependency on `this` argument.
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/net/SocketTimeoutException;"));
}

TEST_F(VerifierDepsTest, InvokeVirtual_Resolved_DeclaredInSuperclass1) {
  ASSERT_TRUE(VerifyMethod("InvokeVirtual_Resolved_DeclaredInSuperclass1"));
  // Type dependency on `this` argument.
  ASSERT_TRUE(HasAssignable("Ljava/lang/Throwable;", "Ljava/net/SocketTimeoutException;"));
}

TEST_F(VerifierDepsTest, InvokeSuper_ThisAssignable) {
  ASSERT_TRUE(VerifyMethod("InvokeSuper_ThisAssignable"));
  ASSERT_TRUE(HasAssignable("Ljava/lang/Runnable;", "Ljava/lang/Thread;"));
}

TEST_F(VerifierDepsTest, EncodeDecode) {
  VerifyDexFile();

  ASSERT_EQ(1u, NumberOfCompiledDexFiles());
  ASSERT_TRUE(HasEachKindOfRecord());

  std::vector<uint8_t> buffer;
  verifier_deps_->Encode(dex_files_, &buffer);
  ASSERT_FALSE(buffer.empty());

  VerifierDeps decoded_deps(dex_files_, /*output_only=*/ false);
  bool parsed = decoded_deps.ParseStoredData(dex_files_, ArrayRef<const uint8_t>(buffer));
  ASSERT_TRUE(parsed);
  ASSERT_TRUE(verifier_deps_->Equals(decoded_deps));
}

TEST_F(VerifierDepsTest, EncodeDecodeMulti) {
  VerifyDexFile("MultiDex");

  ASSERT_GT(NumberOfCompiledDexFiles(), 1u);
  std::vector<uint8_t> buffer;
  verifier_deps_->Encode(dex_files_, &buffer);
  ASSERT_FALSE(buffer.empty());

  // Create new DexFile, to mess with std::map order: the verifier deps used
  // to iterate over the map, which doesn't guarantee insertion order. We fixed
  // this by passing the expected order when encoding/decoding.
  std::vector<std::unique_ptr<const DexFile>> first_dex_files = OpenTestDexFiles("VerifierDeps");
  std::vector<std::unique_ptr<const DexFile>> second_dex_files = OpenTestDexFiles("MultiDex");
  std::vector<const DexFile*> dex_files;
  for (auto& dex_file : first_dex_files) {
    dex_files.push_back(dex_file.get());
  }
  for (auto& dex_file : second_dex_files) {
    dex_files.push_back(dex_file.get());
  }

  // Dump the new verifier deps to ensure it can properly read the data.
  VerifierDeps decoded_deps(dex_files, /*output_only=*/ false);
  bool parsed = decoded_deps.ParseStoredData(dex_files, ArrayRef<const uint8_t>(buffer));
  ASSERT_TRUE(parsed);
  std::ostringstream stream;
  VariableIndentationOutputStream os(&stream);
  decoded_deps.Dump(&os);
}

TEST_F(VerifierDepsTest, UnverifiedClasses) {
  VerifyDexFile();
  ASSERT_FALSE(HasUnverifiedClass("LMyThread;"));
  // Test that a class with a soft failure is recorded.
  ASSERT_TRUE(HasUnverifiedClass("LMain;"));
  // Test that a class with hard failure is recorded.
  ASSERT_TRUE(HasUnverifiedClass("LMyVerificationFailure;"));
  // Test that a class with unresolved super is recorded.
  ASSERT_TRUE(HasUnverifiedClass("LMyClassWithNoSuper;"));
  // Test that a class with unresolved super and hard failure is recorded.
  ASSERT_TRUE(HasUnverifiedClass("LMyClassWithNoSuperButFailures;"));
}

TEST_F(VerifierDepsTest, RedefinedClass) {
  VerifyDexFile();
  // Test that a class which redefines a boot classpath class has dependencies recorded.
  ASSERT_TRUE(HasRedefinedClass("Ljava/net/SocketTimeoutException;"));
}

TEST_F(VerifierDepsTest, UnverifiedOrder) {
  ScopedObjectAccess soa(Thread::Current());
  jobject loader = LoadDex("VerifierDeps");
  std::vector<const DexFile*> dex_files = GetDexFiles(loader);
  ASSERT_GT(dex_files.size(), 0u);
  const DexFile* dex_file = dex_files[0];
  VerifierDeps deps1(dex_files);
  Thread* const self = Thread::Current();
  ASSERT_TRUE(self->GetVerifierDeps() == nullptr);
  self->SetVerifierDeps(&deps1);
  deps1.MaybeRecordVerificationStatus(*dex_file,
                                      dex_file->GetClassDef(0u),
                                      verifier::FailureKind::kHardFailure);
  deps1.MaybeRecordVerificationStatus(*dex_file,
                                      dex_file->GetClassDef(1u),
                                      verifier::FailureKind::kHardFailure);
  VerifierDeps deps2(dex_files);
  self->SetVerifierDeps(nullptr);
  self->SetVerifierDeps(&deps2);
  deps2.MaybeRecordVerificationStatus(*dex_file,
                                      dex_file->GetClassDef(1u),
                                      verifier::FailureKind::kHardFailure);
  deps2.MaybeRecordVerificationStatus(*dex_file,
                                      dex_file->GetClassDef(0u),
                                      verifier::FailureKind::kHardFailure);
  self->SetVerifierDeps(nullptr);
  std::vector<uint8_t> buffer1;
  deps1.Encode(dex_files, &buffer1);
  std::vector<uint8_t> buffer2;
  deps2.Encode(dex_files, &buffer2);
  EXPECT_EQ(buffer1, buffer2);
}

TEST_F(VerifierDepsTest, VerifyDeps) {
  std::string error_msg;

  VerifyDexFile();
  ASSERT_EQ(1u, NumberOfCompiledDexFiles());
  ASSERT_TRUE(HasEachKindOfRecord());

  // When validating, we create a new class loader, as
  // the existing `class_loader_` may contain erroneous classes,
  // that ClassLinker::FindClass won't return.

  std::vector<uint8_t> buffer;
  verifier_deps_->Encode(dex_files_, &buffer);
  ASSERT_FALSE(buffer.empty());

  // Check that dependencies are satisfied after decoding `buffer`.
  ASSERT_TRUE(RunValidation([](VerifierDeps::DexFileDeps&) {}, buffer, &error_msg))
      << error_msg;
}

TEST_F(VerifierDepsTest, CompilerDriver) {
  SetupCompilerDriver();

  // Test both multi-dex and single-dex configuration.
  for (const char* multi : { "MultiDex", static_cast<const char*>(nullptr) }) {
    // Test that the compiler driver behaves as expected when the dependencies
    // verify and when they don't verify.
    for (bool verify_failure : { false, true }) {
      {
        ScopedObjectAccess soa(Thread::Current());
        LoadDexFile(soa, "VerifierDeps", multi);
      }
      VerifyWithCompilerDriver(/* verifier_deps= */ nullptr);

      std::vector<uint8_t> buffer;
      verifier_deps_->Encode(dex_files_, &buffer);

      {
        ScopedObjectAccess soa(Thread::Current());
        LoadDexFile(soa, "VerifierDeps", multi);
      }
      VerifierDeps decoded_deps(dex_files_, /*output_only=*/ false);
      bool parsed = decoded_deps.ParseStoredData(dex_files_, ArrayRef<const uint8_t>(buffer));
      ASSERT_TRUE(parsed);
      VerifyWithCompilerDriver(&decoded_deps);

      if (verify_failure) {
        ASSERT_FALSE(verifier_deps_ == nullptr);
        ASSERT_FALSE(verifier_deps_->Equals(decoded_deps));
      } else {
        VerifyClassStatus(decoded_deps);
      }
    }
  }
}

TEST_F(VerifierDepsTest, MultiDexVerification) {
  VerifyDexFile("VerifierDepsMulti");
  ASSERT_EQ(NumberOfCompiledDexFiles(), 2u);

  ASSERT_TRUE(HasUnverifiedClass("LMySoftVerificationFailure;", *dex_files_[1]));
  ASSERT_TRUE(HasUnverifiedClass("LMySub1SoftVerificationFailure;", *dex_files_[0]));
  ASSERT_TRUE(HasUnverifiedClass("LMySub2SoftVerificationFailure;", *dex_files_[0]));

  std::vector<uint8_t> buffer;
  verifier_deps_->Encode(dex_files_, &buffer);
  ASSERT_FALSE(buffer.empty());
}

TEST_F(VerifierDepsTest, Assignable_Arrays) {
  ASSERT_TRUE(TestAssignabilityRecording(/* dst= */ "[LIface;",
                                         /* src= */ "[LMyClassExtendingInterface;"));
  ASSERT_FALSE(HasAssignable(
      "LIface;", "LMyClassExtendingInterface;"));
}

}  // namespace verifier
}  // namespace art
