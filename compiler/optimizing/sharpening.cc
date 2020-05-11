/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "sharpening.h"

#include "art_method-inl.h"
#include "base/casts.h"
#include "base/enums.h"
#include "base/logging.h"
#include "class_linker.h"
#include "code_generator.h"
#include "driver/compiler_options.h"
#include "driver/dex_compilation_unit.h"
#include "gc/heap.h"
#include "gc/space/image_space.h"
#include "handle_scope-inl.h"
#include "jit/jit.h"
#include "mirror/dex_cache.h"
#include "mirror/string.h"
#include "nodes.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"

namespace art {

static bool IsInBootImage(ArtMethod* method) {
  gc::Heap* heap = Runtime::Current()->GetHeap();
  DCHECK_EQ(heap->IsBootImageAddress(method),
            std::any_of(heap->GetBootImageSpaces().begin(),
                        heap->GetBootImageSpaces().end(),
                        [=](gc::space::ImageSpace* space) REQUIRES_SHARED(Locks::mutator_lock_) {
                          return space->GetImageHeader().GetMethodsSection().Contains(
                              reinterpret_cast<uint8_t*>(method) - space->Begin());
                        }));
  return heap->IsBootImageAddress(method);
}

static bool BootImageAOTCanEmbedMethod(ArtMethod* method, const CompilerOptions& compiler_options) {
  DCHECK(compiler_options.IsBootImage() || compiler_options.IsBootImageExtension());
  ScopedObjectAccess soa(Thread::Current());
  ObjPtr<mirror::Class> klass = method->GetDeclaringClass();
  DCHECK(klass != nullptr);
  const DexFile& dex_file = klass->GetDexFile();
  return compiler_options.IsImageClass(dex_file.StringByTypeIdx(klass->GetDexTypeIndex()));
}

HInvokeStaticOrDirect::DispatchInfo HSharpening::SharpenInvokeStaticOrDirect(
    ArtMethod* callee, CodeGenerator* codegen) {
  if (kIsDebugBuild) {
    ScopedObjectAccess soa(Thread::Current());  // Required for GetDeclaringClass below.
    DCHECK(callee != nullptr);
    DCHECK(!(callee->IsConstructor() && callee->GetDeclaringClass()->IsStringClass()));
  }

  HInvokeStaticOrDirect::MethodLoadKind method_load_kind;
  HInvokeStaticOrDirect::CodePtrLocation code_ptr_location;
  uint64_t method_load_data = 0u;

  // Note: we never call an ArtMethod through a known code pointer, as
  // we do not want to keep on invoking it if it gets deoptimized. This
  // applies to both AOT and JIT.
  // This also avoids having to find out if the code pointer of an ArtMethod
  // is the resolution trampoline (for ensuring the class is initialized), or
  // the interpreter entrypoint. Such code pointers we do not want to call
  // directly.
  // Only in the case of a recursive call can we call directly, as we know the
  // class is initialized already or being initialized, and the call will not
  // be invoked once the method is deoptimized.

  // We don't optimize for debuggable as it would prevent us from obsoleting the method in some
  // situations.
  const CompilerOptions& compiler_options = codegen->GetCompilerOptions();
  if (callee == codegen->GetGraph()->GetArtMethod() && !codegen->GetGraph()->IsDebuggable()) {
    // Recursive call.
    method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kRecursive;
    code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallSelf;
  } else if (compiler_options.IsBootImage() || compiler_options.IsBootImageExtension()) {
    if (!compiler_options.GetCompilePic()) {
      // Test configuration, do not sharpen.
      method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kRuntimeCall;
    } else if (IsInBootImage(callee)) {
      DCHECK(compiler_options.IsBootImageExtension());
      method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kBootImageRelRo;
    } else if (BootImageAOTCanEmbedMethod(callee, compiler_options)) {
      method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kBootImageLinkTimePcRelative;
    } else {
      // Use PC-relative access to the .bss methods array.
      method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kBssEntry;
    }
    code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod;
  } else if (compiler_options.IsJitCompiler()) {
    ScopedObjectAccess soa(Thread::Current());
    if (Runtime::Current()->GetJit()->CanEncodeMethod(
            callee,
            compiler_options.IsJitCompilerForSharedCode())) {
      method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kJitDirectAddress;
      method_load_data = reinterpret_cast<uintptr_t>(callee);
      code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod;
    } else {
      // Do not sharpen.
      method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kRuntimeCall;
      code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod;
    }
  } else if (IsInBootImage(callee)) {
    // Use PC-relative access to the .data.bimg.rel.ro methods array.
    method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kBootImageRelRo;
    code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod;
  } else {
    // Use PC-relative access to the .bss methods array.
    method_load_kind = HInvokeStaticOrDirect::MethodLoadKind::kBssEntry;
    code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod;
  }

  if (method_load_kind != HInvokeStaticOrDirect::MethodLoadKind::kRuntimeCall &&
      callee->IsCriticalNative()) {
    DCHECK_NE(method_load_kind, HInvokeStaticOrDirect::MethodLoadKind::kRecursive);
    DCHECK(callee->IsStatic());
    code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallCriticalNative;
  }

  if (codegen->GetGraph()->IsDebuggable()) {
    // For debuggable apps always use the code pointer from ArtMethod
    // so that we don't circumvent instrumentation stubs if installed.
    code_ptr_location = HInvokeStaticOrDirect::CodePtrLocation::kCallArtMethod;
  }

  HInvokeStaticOrDirect::DispatchInfo desired_dispatch_info = {
      method_load_kind, code_ptr_location, method_load_data
  };
  return codegen->GetSupportedInvokeStaticOrDirectDispatch(desired_dispatch_info, callee);
}

HLoadClass::LoadKind HSharpening::ComputeLoadClassKind(
    HLoadClass* load_class,
    CodeGenerator* codegen,
    const DexCompilationUnit& dex_compilation_unit) {
  Handle<mirror::Class> klass = load_class->GetClass();
  DCHECK(load_class->GetLoadKind() == HLoadClass::LoadKind::kRuntimeCall ||
         load_class->GetLoadKind() == HLoadClass::LoadKind::kReferrersClass)
      << load_class->GetLoadKind();
  DCHECK(!load_class->IsInBootImage()) << "HLoadClass should not be optimized before sharpening.";

  HLoadClass::LoadKind load_kind = load_class->GetLoadKind();

  if (load_class->NeedsAccessCheck()) {
    // We need to call the runtime anyway, so we simply get the class as that call's return value.
  } else if (load_kind == HLoadClass::LoadKind::kReferrersClass) {
    // Loading from the ArtMethod* is the most efficient retrieval in code size.
    // TODO: This may not actually be true for all architectures and
    // locations of target classes. The additional register pressure
    // for using the ArtMethod* should be considered.
  } else {
    const DexFile& dex_file = load_class->GetDexFile();
    dex::TypeIndex type_index = load_class->GetTypeIndex();

    bool is_in_boot_image = false;
    HLoadClass::LoadKind desired_load_kind = HLoadClass::LoadKind::kInvalid;
    Runtime* runtime = Runtime::Current();
    const CompilerOptions& compiler_options = codegen->GetCompilerOptions();
    if (compiler_options.IsBootImage() || compiler_options.IsBootImageExtension()) {
      // Compiling boot image or boot image extension. Check if the class is a boot image class.
      DCHECK(!compiler_options.IsJitCompiler());
      if (!compiler_options.GetCompilePic()) {
        // Test configuration, do not sharpen.
        desired_load_kind = HLoadClass::LoadKind::kRuntimeCall;
      } else if (klass != nullptr && runtime->GetHeap()->ObjectIsInBootImageSpace(klass.Get())) {
        DCHECK(compiler_options.IsBootImageExtension());
        is_in_boot_image = true;
        desired_load_kind = HLoadClass::LoadKind::kBootImageRelRo;
      } else if ((klass != nullptr) &&
                 compiler_options.IsImageClass(dex_file.StringByTypeIdx(type_index))) {
        is_in_boot_image = true;
        desired_load_kind = HLoadClass::LoadKind::kBootImageLinkTimePcRelative;
      } else {
        // Not a boot image class.
        desired_load_kind = HLoadClass::LoadKind::kBssEntry;
      }
    } else {
      is_in_boot_image = (klass != nullptr) &&
          runtime->GetHeap()->ObjectIsInBootImageSpace(klass.Get());
      if (compiler_options.IsJitCompiler()) {
        DCHECK(!compiler_options.GetCompilePic());
        if (is_in_boot_image) {
          desired_load_kind = HLoadClass::LoadKind::kJitBootImageAddress;
        } else if (klass != nullptr) {
          if (runtime->GetJit()->CanEncodeClass(
                  klass.Get(),
                  compiler_options.IsJitCompilerForSharedCode())) {
            desired_load_kind = HLoadClass::LoadKind::kJitTableAddress;
          } else {
            // Shared JIT code cannot encode a literal that the GC can move.
            VLOG(jit) << "Unable to encode in shared region class literal: "
                      << klass->PrettyClass();
            desired_load_kind = HLoadClass::LoadKind::kRuntimeCall;
          }
        } else {
          // Class not loaded yet. This happens when the dex code requesting
          // this `HLoadClass` hasn't been executed in the interpreter.
          // Fallback to the dex cache.
          // TODO(ngeoffray): Generate HDeoptimize instead.
          desired_load_kind = HLoadClass::LoadKind::kRuntimeCall;
        }
      } else if (is_in_boot_image) {
        // AOT app compilation, boot image class.
        desired_load_kind = HLoadClass::LoadKind::kBootImageRelRo;
      } else {
        // Not JIT and the klass is not in boot image.
        desired_load_kind = HLoadClass::LoadKind::kBssEntry;
      }
    }
    DCHECK_NE(desired_load_kind, HLoadClass::LoadKind::kInvalid);

    if (is_in_boot_image) {
      load_class->MarkInBootImage();
    }
    load_kind = codegen->GetSupportedLoadClassKind(desired_load_kind);
  }

  if (!IsSameDexFile(load_class->GetDexFile(), *dex_compilation_unit.GetDexFile())) {
    if ((load_kind == HLoadClass::LoadKind::kRuntimeCall) ||
        (load_kind == HLoadClass::LoadKind::kBssEntry)) {
      // We actually cannot reference this class, we're forced to bail.
      // We cannot reference this class with Bss, as the entrypoint will lookup the class
      // in the caller's dex file, but that dex file does not reference the class.
      return HLoadClass::LoadKind::kInvalid;
    }
  }
  return load_kind;
}

static inline bool CanUseTypeCheckBitstring(ObjPtr<mirror::Class> klass, CodeGenerator* codegen)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(!klass->IsProxyClass());
  DCHECK(!klass->IsArrayClass());

  const CompilerOptions& compiler_options = codegen->GetCompilerOptions();
  if (compiler_options.IsJitCompiler()) {
    // If we're JITting, try to assign a type check bitstring (fall through).
  } else if (codegen->GetCompilerOptions().IsBootImage()) {
    const char* descriptor = klass->GetDexFile().StringByTypeIdx(klass->GetDexTypeIndex());
    if (!codegen->GetCompilerOptions().IsImageClass(descriptor)) {
      return false;
    }
    // If the target is a boot image class, try to assign a type check bitstring (fall through).
    // (If --force-determinism, this was already done; repeating is OK and yields the same result.)
  } else {
    // TODO: Use the bitstring also for AOT app compilation if the target class has a bitstring
    // already assigned in the boot image.
    return false;
  }

  // Try to assign a type check bitstring.
  MutexLock subtype_check_lock(Thread::Current(), *Locks::subtype_check_lock_);
  if ((false) &&  // FIXME: Inliner does not respect CompilerDriver::ShouldCompileMethod()
                  // and we're hitting an unassigned bitstring in dex2oat_image_test. b/26687569
      kIsDebugBuild &&
      compiler_options.IsBootImage() &&
      compiler_options.IsForceDeterminism()) {
    SubtypeCheckInfo::State old_state = SubtypeCheck<ObjPtr<mirror::Class>>::GetState(klass);
    CHECK(old_state == SubtypeCheckInfo::kAssigned || old_state == SubtypeCheckInfo::kOverflowed)
        << klass->PrettyDescriptor() << "/" << old_state
        << " in " << codegen->GetGraph()->PrettyMethod();
  }
  SubtypeCheckInfo::State state = SubtypeCheck<ObjPtr<mirror::Class>>::EnsureAssigned(klass);
  return state == SubtypeCheckInfo::kAssigned;
}

TypeCheckKind HSharpening::ComputeTypeCheckKind(ObjPtr<mirror::Class> klass,
                                                CodeGenerator* codegen,
                                                bool needs_access_check) {
  if (klass == nullptr) {
    return TypeCheckKind::kUnresolvedCheck;
  } else if (klass->IsInterface()) {
    return TypeCheckKind::kInterfaceCheck;
  } else if (klass->IsArrayClass()) {
    if (klass->GetComponentType()->IsObjectClass()) {
      return TypeCheckKind::kArrayObjectCheck;
    } else if (klass->CannotBeAssignedFromOtherTypes()) {
      return TypeCheckKind::kExactCheck;
    } else {
      return TypeCheckKind::kArrayCheck;
    }
  } else if (klass->IsFinal()) {  // TODO: Consider using bitstring for final classes.
    return TypeCheckKind::kExactCheck;
  } else if (kBitstringSubtypeCheckEnabled &&
             !needs_access_check &&
             CanUseTypeCheckBitstring(klass, codegen)) {
    // TODO: We should not need the `!needs_access_check` check but getting rid of that
    // requires rewriting some optimizations in instruction simplifier.
    return TypeCheckKind::kBitstringCheck;
  } else if (klass->IsAbstract()) {
    return TypeCheckKind::kAbstractClassCheck;
  } else {
    return TypeCheckKind::kClassHierarchyCheck;
  }
}

void HSharpening::ProcessLoadString(
    HLoadString* load_string,
    CodeGenerator* codegen,
    const DexCompilationUnit& dex_compilation_unit,
    VariableSizedHandleScope* handles) {
  DCHECK_EQ(load_string->GetLoadKind(), HLoadString::LoadKind::kRuntimeCall);

  const DexFile& dex_file = load_string->GetDexFile();
  dex::StringIndex string_index = load_string->GetStringIndex();

  HLoadString::LoadKind desired_load_kind = static_cast<HLoadString::LoadKind>(-1);
  {
    Runtime* runtime = Runtime::Current();
    ClassLinker* class_linker = runtime->GetClassLinker();
    ScopedObjectAccess soa(Thread::Current());
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::DexCache> dex_cache = IsSameDexFile(dex_file, *dex_compilation_unit.GetDexFile())
        ? dex_compilation_unit.GetDexCache()
        : hs.NewHandle(class_linker->FindDexCache(soa.Self(), dex_file));
    ObjPtr<mirror::String> string = nullptr;

    const CompilerOptions& compiler_options = codegen->GetCompilerOptions();
    if (compiler_options.IsBootImage() || compiler_options.IsBootImageExtension()) {
      // Compiling boot image or boot image extension. Resolve the string and allocate it
      // if needed, to ensure the string will be added to the boot image.
      DCHECK(!compiler_options.IsJitCompiler());
      if (compiler_options.GetCompilePic()) {
        if (compiler_options.IsForceDeterminism()) {
          // Strings for methods we're compiling should be pre-resolved but Strings in inlined
          // methods may not be if these inlined methods are not in the boot image profile.
          // Multiple threads allocating new Strings can cause non-deterministic boot image
          // because of the image relying on the order of GC roots we walk. (We could fix that
          // by ordering the roots we walk in ImageWriter.) Therefore we avoid allocating these
          // strings even if that results in omitting them from the boot image and using the
          // sub-optimal load kind kBssEntry.
          string = class_linker->LookupString(string_index, dex_cache.Get());
        } else {
          string = class_linker->ResolveString(string_index, dex_cache);
          CHECK(string != nullptr);
        }
        if (string != nullptr) {
          if (runtime->GetHeap()->ObjectIsInBootImageSpace(string)) {
            DCHECK(compiler_options.IsBootImageExtension());
            desired_load_kind = HLoadString::LoadKind::kBootImageRelRo;
          } else {
            desired_load_kind = HLoadString::LoadKind::kBootImageLinkTimePcRelative;
          }
        } else {
          desired_load_kind = HLoadString::LoadKind::kBssEntry;
        }
      } else {
        // Test configuration, do not sharpen.
        desired_load_kind = HLoadString::LoadKind::kRuntimeCall;
      }
    } else if (compiler_options.IsJitCompiler()) {
      DCHECK(!codegen->GetCompilerOptions().GetCompilePic());
      string = class_linker->LookupString(string_index, dex_cache.Get());
      if (string != nullptr) {
        gc::Heap* heap = runtime->GetHeap();
        if (heap->ObjectIsInBootImageSpace(string)) {
          desired_load_kind = HLoadString::LoadKind::kJitBootImageAddress;
        } else if (runtime->GetJit()->CanEncodeString(
                  string,
                  compiler_options.IsJitCompilerForSharedCode())) {
          desired_load_kind = HLoadString::LoadKind::kJitTableAddress;
        } else {
          // Shared JIT code cannot encode a literal that the GC can move.
          VLOG(jit) << "Unable to encode in shared region string literal: "
                    << string->ToModifiedUtf8();
          desired_load_kind = HLoadString::LoadKind::kRuntimeCall;
        }
      } else {
        desired_load_kind = HLoadString::LoadKind::kRuntimeCall;
      }
    } else {
      // AOT app compilation. Try to lookup the string without allocating if not found.
      string = class_linker->LookupString(string_index, dex_cache.Get());
      if (string != nullptr && runtime->GetHeap()->ObjectIsInBootImageSpace(string)) {
        desired_load_kind = HLoadString::LoadKind::kBootImageRelRo;
      } else {
        desired_load_kind = HLoadString::LoadKind::kBssEntry;
      }
    }
    if (string != nullptr) {
      load_string->SetString(handles->NewHandle(string));
    }
  }
  DCHECK_NE(desired_load_kind, static_cast<HLoadString::LoadKind>(-1));

  HLoadString::LoadKind load_kind = codegen->GetSupportedLoadStringKind(desired_load_kind);
  load_string->SetLoadKind(load_kind);
}

}  // namespace art
