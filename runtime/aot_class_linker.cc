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

#include "aot_class_linker.h"

#include "class_status.h"
#include "compiler_callbacks.h"
#include "dex/class_reference.h"
#include "gc/heap.h"
#include "handle_scope-inl.h"
#include "mirror/class-inl.h"
#include "runtime.h"
#include "verifier/verifier_enums.h"

namespace art {

AotClassLinker::AotClassLinker(InternTable* intern_table)
    : ClassLinker(intern_table, /*fast_class_not_found_exceptions=*/ false) {}

AotClassLinker::~AotClassLinker() {}

bool AotClassLinker::CanAllocClass() {
  // AllocClass doesn't work under transaction, so we abort.
  if (Runtime::Current()->IsActiveTransaction()) {
    Runtime::Current()->AbortTransactionAndThrowAbortError(
        Thread::Current(), "Can't resolve type within transaction.");
    return false;
  }
  return ClassLinker::CanAllocClass();
}

// Wrap the original InitializeClass with creation of transaction when in strict mode.
bool AotClassLinker::InitializeClass(Thread* self,
                                     Handle<mirror::Class> klass,
                                     bool can_init_statics,
                                     bool can_init_parents) {
  Runtime* const runtime = Runtime::Current();
  bool strict_mode = runtime->IsActiveStrictTransactionMode();

  DCHECK(klass != nullptr);
  if (klass->IsInitialized() || klass->IsInitializing()) {
    return ClassLinker::InitializeClass(self, klass, can_init_statics, can_init_parents);
  }

  // When compiling a boot image extension, do not initialize a class defined
  // in a dex file belonging to the boot image we're compiling against.
  // However, we must allow the initialization of TransactionAbortError,
  // VerifyError, etc. outside of a transaction.
  if (!strict_mode && runtime->GetHeap()->ObjectIsInBootImageSpace(klass->GetDexCache())) {
    if (runtime->IsActiveTransaction()) {
      runtime->AbortTransactionAndThrowAbortError(self, "Can't initialize " + klass->PrettyTypeOf()
           + " because it is defined in a boot image dex file.");
      return false;
    }
    CHECK(klass->IsThrowableClass()) << klass->PrettyDescriptor();
  }

  // When in strict_mode, don't initialize a class if it belongs to boot but not initialized.
  if (strict_mode && klass->IsBootStrapClassLoaded()) {
    runtime->AbortTransactionAndThrowAbortError(self, "Can't resolve "
        + klass->PrettyTypeOf() + " because it is an uninitialized boot class.");
    return false;
  }

  // Don't initialize klass if it's superclass is not initialized, because superclass might abort
  // the transaction and rolled back after klass's change is commited.
  if (strict_mode && !klass->IsInterface() && klass->HasSuperClass()) {
    if (klass->GetSuperClass()->GetStatus() == ClassStatus::kInitializing) {
      runtime->AbortTransactionAndThrowAbortError(self, "Can't resolve "
          + klass->PrettyTypeOf() + " because it's superclass is not initialized.");
      return false;
    }
  }

  if (strict_mode) {
    runtime->EnterTransactionMode(/*strict=*/ true, klass.Get());
  }
  bool success = ClassLinker::InitializeClass(self, klass, can_init_statics, can_init_parents);

  if (strict_mode) {
    if (success) {
      // Exit Transaction if success.
      runtime->ExitTransactionMode();
    } else {
      // If not successfully initialized, don't rollback immediately, leave the cleanup to compiler
      // driver which needs abort message and exception.
      DCHECK(self->IsExceptionPending());
    }
  }
  return success;
}

verifier::FailureKind AotClassLinker::PerformClassVerification(Thread* self,
                                                               Handle<mirror::Class> klass,
                                                               verifier::HardFailLogMode log_level,
                                                               std::string* error_msg) {
  Runtime* const runtime = Runtime::Current();
  CompilerCallbacks* callbacks = runtime->GetCompilerCallbacks();
  ClassStatus old_status = callbacks->GetPreviousClassState(
      ClassReference(&klass->GetDexFile(), klass->GetDexClassDefIndex()));
  // Was it verified? Report no failure.
  if (old_status >= ClassStatus::kVerified) {
    return verifier::FailureKind::kNoFailure;
  }
  if (old_status >= ClassStatus::kVerifiedNeedsAccessChecks) {
    return verifier::FailureKind::kAccessChecksFailure;
  }
  // Does it need to be verified at runtime? Report soft failure.
  if (old_status >= ClassStatus::kRetryVerificationAtRuntime) {
    // Error messages from here are only reported through -verbose:class. It is not worth it to
    // create a message.
    return verifier::FailureKind::kSoftFailure;
  }
  // Do the actual work.
  return ClassLinker::PerformClassVerification(self, klass, log_level, error_msg);
}

bool AotClassLinker::CanReferenceInBootImageExtension(ObjPtr<mirror::Class> klass, gc::Heap* heap) {
  // Do not allow referencing a class or instance of a class defined in a dex file
  // belonging to the boot image we're compiling against but not itself in the boot image;
  // or a class referencing such classes as component type, superclass or interface.
  // Allowing this could yield duplicate class objects from multiple extensions.

  if (heap->ObjectIsInBootImageSpace(klass)) {
    return true;  // Already included in the boot image we're compiling against.
  }

  // Treat arrays and primitive types specially because they do not have a DexCache that we
  // can use to check whether the dex file belongs to the boot image we're compiling against.
  DCHECK(!klass->IsPrimitive());  // Primitive classes must be in the primary boot image.
  if (klass->IsArrayClass()) {
    DCHECK(heap->ObjectIsInBootImageSpace(klass->GetIfTable()));  // IfTable is OK.
    // Arrays of all dimensions are tied to the dex file of the non-array component type.
    do {
      klass = klass->GetComponentType();
    } while (klass->IsArrayClass());
    if (klass->IsPrimitive()) {
      return false;
    }
    // Do not allow arrays of erroneous classes (the array class is not itself erroneous).
    if (klass->IsErroneous()) {
      return false;
    }
  }

  // Check the class itself.
  if (heap->ObjectIsInBootImageSpace(klass->GetDexCache())) {
    return false;
  }

  // Check superclasses.
  ObjPtr<mirror::Class> superclass = klass->GetSuperClass();
  while (!heap->ObjectIsInBootImageSpace(superclass)) {
    DCHECK(superclass != nullptr);  // Cannot skip Object which is in the primary boot image.
    if (heap->ObjectIsInBootImageSpace(superclass->GetDexCache())) {
      return false;
    }
    superclass = superclass->GetSuperClass();
  }

  // Check IfTable. This includes direct and indirect interfaces.
  ObjPtr<mirror::IfTable> if_table = klass->GetIfTable();
  for (size_t i = 0, num_interfaces = klass->GetIfTableCount(); i < num_interfaces; ++i) {
    ObjPtr<mirror::Class> interface = if_table->GetInterface(i);
    DCHECK(interface != nullptr);
    if (!heap->ObjectIsInBootImageSpace(interface) &&
        heap->ObjectIsInBootImageSpace(interface->GetDexCache())) {
      return false;
    }
  }

  if (kIsDebugBuild) {
    // All virtual methods must come from classes we have already checked above.
    PointerSize pointer_size = Runtime::Current()->GetClassLinker()->GetImagePointerSize();
    ObjPtr<mirror::Class> k = klass;
    while (!heap->ObjectIsInBootImageSpace(k)) {
      for (auto& m : k->GetVirtualMethods(pointer_size)) {
        ObjPtr<mirror::Class> declaring_class = m.GetDeclaringClass();
        CHECK(heap->ObjectIsInBootImageSpace(declaring_class) ||
              !heap->ObjectIsInBootImageSpace(declaring_class->GetDexCache()));
      }
      k = k->GetSuperClass();
    }
  }

  return true;
}

bool AotClassLinker::SetUpdatableBootClassPackages(const std::vector<std::string>& packages) {
  DCHECK(updatable_boot_class_path_descriptor_prefixes_.empty());
  // Transform package names to descriptor prefixes.
  std::vector<std::string> prefixes;
  prefixes.reserve(packages.size());
  for (const std::string& package : packages) {
    if (package.empty() || package.find('/') != std::string::npos) {
      LOG(ERROR) << "Invalid package name: " << package;
      return false;
    }
    std::string prefix = 'L' + package + '/';
    std::replace(prefix.begin(), prefix.end(), '.', '/');
    prefixes.push_back(std::move(prefix));
  }
  // Sort and remove unnecessary prefixes.
  std::sort(prefixes.begin(), prefixes.end());
  std::string last_prefix;
  auto end_it = std::remove_if(
      prefixes.begin(),
      prefixes.end(),
      [&last_prefix](const std::string& s) {
        if (!last_prefix.empty() && StartsWith(s, last_prefix)) {
          return true;
        } else {
          last_prefix = s;
          return false;
        }
      });
  prefixes.resize(std::distance(prefixes.begin(), end_it));
  prefixes.shrink_to_fit();
  updatable_boot_class_path_descriptor_prefixes_.swap(prefixes);
  return true;
}

bool AotClassLinker::IsUpdatableBootClassPathDescriptor(const char* descriptor) {
  std::string_view descriptor_sv(descriptor);
  for (const std::string& prefix : updatable_boot_class_path_descriptor_prefixes_) {
    if (StartsWith(descriptor_sv, prefix)) {
      return true;
    }
  }
  return false;
}

}  // namespace art
