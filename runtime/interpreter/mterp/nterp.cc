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

/*
 * Mterp entry point and support functions.
 */
#include "mterp.h"

#include "base/quasi_atomic.h"
#include "dex/dex_instruction_utils.h"
#include "debugger.h"
#include "entrypoints/entrypoint_utils-inl.h"
#include "interpreter/interpreter_common.h"
#include "interpreter/interpreter_intrinsics.h"
#include "interpreter/shadow_frame-inl.h"
#include "mirror/string-alloc-inl.h"
#include "nterp_helpers.h"

namespace art {
namespace interpreter {

bool IsNterpSupported() {
  return !kPoisonHeapReferences && kUseReadBarrier;
}

bool CanRuntimeUseNterp() REQUIRES_SHARED(Locks::mutator_lock_) {
  // Nterp has the same restrictions as Mterp.
  return IsNterpSupported() && CanUseMterp();
}

bool CanMethodUseNterp(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_) {
  return method->SkipAccessChecks() &&
      !method->IsNative() &&
      method->GetDexFile()->IsStandardDexFile() &&
      NterpGetFrameSize(method) < kMaxNterpFrame;
}

const void* GetNterpEntryPoint() {
  return reinterpret_cast<const void*>(interpreter::ExecuteNterpImpl);
}

/*
 * Verify some constants used by the nterp interpreter.
 */
void CheckNterpAsmConstants() {
  /*
   * If we're using computed goto instruction transitions, make sure
   * none of the handlers overflows the byte limit.  This won't tell
   * which one did, but if any one is too big the total size will
   * overflow.
   */
  const int width = kMterpHandlerSize;
  ptrdiff_t interp_size = reinterpret_cast<uintptr_t>(artNterpAsmInstructionEnd) -
                          reinterpret_cast<uintptr_t>(artNterpAsmInstructionStart);
  if ((interp_size == 0) || (interp_size != (art::kNumPackedOpcodes * width))) {
      LOG(FATAL) << "ERROR: unexpected asm interp size " << interp_size
                 << "(did an instruction handler exceed " << width << " bytes?)";
  }
}

template<typename T>
inline void UpdateCache(Thread* self, uint16_t* dex_pc_ptr, T value) {
  DCHECK(kUseReadBarrier) << "Nterp only works with read barriers";
  // For simplicity, only update the cache if weak ref accesses are enabled. If
  // they are disabled, this means the GC is processing the cache, and is
  // reading it concurrently.
  if (self->GetWeakRefAccessEnabled()) {
    self->GetInterpreterCache()->Set(dex_pc_ptr, value);
  }
}

template<typename T>
inline void UpdateCache(Thread* self, uint16_t* dex_pc_ptr, T* value) {
  UpdateCache(self, dex_pc_ptr, reinterpret_cast<size_t>(value));
}

extern "C" const dex::CodeItem* NterpGetCodeItem(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedAssertNoThreadSuspension sants("In nterp");
  return method->GetCodeItem();
}

extern "C" const char* NterpGetShorty(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedAssertNoThreadSuspension sants("In nterp");
  return method->GetInterfaceMethodIfProxy(kRuntimePointerSize)->GetShorty();
}

extern "C" const char* NterpGetShortyFromMethodId(ArtMethod* caller, uint32_t method_index)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedAssertNoThreadSuspension sants("In nterp");
  return caller->GetDexFile()->GetMethodShorty(method_index);
}

extern "C" const char* NterpGetShortyFromInvokePolymorphic(ArtMethod* caller, uint16_t* dex_pc_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedAssertNoThreadSuspension sants("In nterp");
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  dex::ProtoIndex proto_idx(inst->Opcode() == Instruction::INVOKE_POLYMORPHIC
      ? inst->VRegH_45cc()
      : inst->VRegH_4rcc());
  return caller->GetDexFile()->GetShorty(proto_idx);
}

extern "C" const char* NterpGetShortyFromInvokeCustom(ArtMethod* caller, uint16_t* dex_pc_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedAssertNoThreadSuspension sants("In nterp");
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  uint16_t call_site_index = (inst->Opcode() == Instruction::INVOKE_CUSTOM
      ? inst->VRegB_35c()
      : inst->VRegB_3rc());
  const DexFile* dex_file = caller->GetDexFile();
  dex::ProtoIndex proto_idx = dex_file->GetProtoIndexForCallSite(call_site_index);
  return dex_file->GetShorty(proto_idx);
}

extern "C" size_t NterpGetMethod(Thread* self, ArtMethod* caller, uint16_t* dex_pc_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  InvokeType invoke_type = kStatic;
  uint16_t method_index = 0;
  switch (inst->Opcode()) {
    case Instruction::INVOKE_DIRECT: {
      method_index = inst->VRegB_35c();
      invoke_type = kDirect;
      break;
    }

    case Instruction::INVOKE_INTERFACE: {
      method_index = inst->VRegB_35c();
      invoke_type = kInterface;
      break;
    }

    case Instruction::INVOKE_STATIC: {
      method_index = inst->VRegB_35c();
      invoke_type = kStatic;
      break;
    }

    case Instruction::INVOKE_SUPER: {
      method_index = inst->VRegB_35c();
      invoke_type = kSuper;
      break;
    }
    case Instruction::INVOKE_VIRTUAL: {
      method_index = inst->VRegB_35c();
      invoke_type = kVirtual;
      break;
    }

    case Instruction::INVOKE_DIRECT_RANGE: {
      method_index = inst->VRegB_3rc();
      invoke_type = kDirect;
      break;
    }

    case Instruction::INVOKE_INTERFACE_RANGE: {
      method_index = inst->VRegB_3rc();
      invoke_type = kInterface;
      break;
    }

    case Instruction::INVOKE_STATIC_RANGE: {
      method_index = inst->VRegB_3rc();
      invoke_type = kStatic;
      break;
    }

    case Instruction::INVOKE_SUPER_RANGE: {
      method_index = inst->VRegB_3rc();
      invoke_type = kSuper;
      break;
    }

    case Instruction::INVOKE_VIRTUAL_RANGE: {
      method_index = inst->VRegB_3rc();
      invoke_type = kVirtual;
      break;
    }

    default:
      LOG(FATAL) << "Unknown instruction " << inst->Opcode();
  }

  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
  ArtMethod* resolved_method = caller->SkipAccessChecks()
      ? class_linker->ResolveMethod<ClassLinker::ResolveMode::kNoChecks>(
            self, method_index, caller, invoke_type)
      : class_linker->ResolveMethod<ClassLinker::ResolveMode::kCheckICCEAndIAE>(
            self, method_index, caller, invoke_type);
  if (resolved_method == nullptr) {
    DCHECK(self->IsExceptionPending());
    return 0;
  }

  // ResolveMethod returns the method based on the method_id. For super invokes
  // we must use the executing class's context to find the right method.
  if (invoke_type == kSuper) {
    ObjPtr<mirror::Class> executing_class = caller->GetDeclaringClass();
    ObjPtr<mirror::Class> referenced_class = class_linker->LookupResolvedType(
        executing_class->GetDexFile().GetMethodId(method_index).class_idx_,
        executing_class->GetDexCache(),
        executing_class->GetClassLoader());
    DCHECK(referenced_class != nullptr);  // We have already resolved a method from this class.
    if (!referenced_class->IsAssignableFrom(executing_class)) {
      // We cannot determine the target method.
      ThrowNoSuchMethodError(invoke_type,
                             resolved_method->GetDeclaringClass(),
                             resolved_method->GetName(),
                             resolved_method->GetSignature());
      return 0;
    }
    if (referenced_class->IsInterface()) {
      resolved_method = referenced_class->FindVirtualMethodForInterfaceSuper(
          resolved_method, class_linker->GetImagePointerSize());
    } else {
      uint16_t vtable_index = resolved_method->GetMethodIndex();
      ObjPtr<mirror::Class> super_class = executing_class->GetSuperClass();
      if (super_class == nullptr ||
          !super_class->HasVTable() ||
          vtable_index >= static_cast<uint32_t>(super_class->GetVTableLength())) {
        // Behavior to agree with that of the verifier.
        ThrowNoSuchMethodError(invoke_type,
                               resolved_method->GetDeclaringClass(),
                               resolved_method->GetName(),
                               resolved_method->GetSignature());
        return 0;
      } else {
        resolved_method = executing_class->GetSuperClass()->GetVTableEntry(
            vtable_index, class_linker->GetImagePointerSize());
      }
    }
  }

  if (invoke_type == kInterface) {
    if (resolved_method->GetDeclaringClass()->IsObjectClass()) {
      // Don't update the cache and return a value with high bit set to notify the
      // interpreter it should do a vtable call instead.
      DCHECK_LT(resolved_method->GetMethodIndex(), 0x10000);
      return resolved_method->GetMethodIndex() | (1U << 31);
    } else {
      DCHECK(resolved_method->GetDeclaringClass()->IsInterface());
      UpdateCache(self, dex_pc_ptr, resolved_method->GetImtIndex());
      return resolved_method->GetImtIndex();
    }
  } else if (resolved_method->GetDeclaringClass()->IsStringClass()
             && !resolved_method->IsStatic()
             && resolved_method->IsConstructor()) {
    resolved_method = WellKnownClasses::StringInitToStringFactory(resolved_method);
    // Or the result with 1 to notify to nterp this is a string init method. We
    // also don't cache the result as we don't want nterp to have its fast path always
    // check for it, and we expect a lot more regular calls than string init
    // calls.
    return reinterpret_cast<size_t>(resolved_method) | 1;
  } else if (invoke_type == kVirtual) {
    UpdateCache(self, dex_pc_ptr, resolved_method->GetMethodIndex());
    return resolved_method->GetMethodIndex();
  } else {
    UpdateCache(self, dex_pc_ptr, resolved_method);
    return reinterpret_cast<size_t>(resolved_method);
  }
}

static ArtField* ResolveFieldWithAccessChecks(Thread* self,
                                              ClassLinker* class_linker,
                                              uint16_t field_index,
                                              ArtMethod* caller,
                                              bool is_static,
                                              bool is_put)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  if (caller->SkipAccessChecks()) {
    return class_linker->ResolveField(field_index, caller, is_static);
  }

  caller = caller->GetInterfaceMethodIfProxy(kRuntimePointerSize);

  StackHandleScope<2> hs(self);
  Handle<mirror::DexCache> h_dex_cache(hs.NewHandle(caller->GetDexCache()));
  Handle<mirror::ClassLoader> h_class_loader(hs.NewHandle(caller->GetClassLoader()));

  ArtField* resolved_field = class_linker->ResolveFieldJLS(field_index,
                                                           h_dex_cache,
                                                           h_class_loader);
  if (resolved_field == nullptr) {
    return nullptr;
  }

  ObjPtr<mirror::Class> fields_class = resolved_field->GetDeclaringClass();
  if (UNLIKELY(resolved_field->IsStatic() != is_static)) {
    ThrowIncompatibleClassChangeErrorField(resolved_field, is_static, caller);
    return nullptr;
  }
  ObjPtr<mirror::Class> referring_class = caller->GetDeclaringClass();
  if (UNLIKELY(!referring_class->CheckResolvedFieldAccess(fields_class,
                                                          resolved_field,
                                                          caller->GetDexCache(),
                                                          field_index))) {
    return nullptr;
  }
  if (UNLIKELY(is_put && resolved_field->IsFinal() && (fields_class != referring_class))) {
    ThrowIllegalAccessErrorFinalField(caller, resolved_field);
    return nullptr;
  }
  return resolved_field;
}

extern "C" size_t NterpGetStaticField(Thread* self, ArtMethod* caller, uint16_t* dex_pc_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  uint16_t field_index = inst->VRegB_21c();
  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
  ArtField* resolved_field = ResolveFieldWithAccessChecks(
      self,
      class_linker,
      field_index,
      caller,
      /* is_static */ true,
      /* is_put */ IsInstructionSPut(inst->Opcode()));

  if (resolved_field == nullptr) {
    DCHECK(self->IsExceptionPending());
    return 0;
  }
  if (UNLIKELY(!resolved_field->GetDeclaringClass()->IsVisiblyInitialized())) {
    StackHandleScope<1> hs(self);
    Handle<mirror::Class> h_class(hs.NewHandle(resolved_field->GetDeclaringClass()));
    if (UNLIKELY(!class_linker->EnsureInitialized(
                      self, h_class, /*can_init_fields=*/ true, /*can_init_parents=*/ true))) {
      DCHECK(self->IsExceptionPending());
      return 0;
    }
    DCHECK(h_class->IsInitializing());
  }
  if (resolved_field->IsVolatile()) {
    // Or the result with 1 to notify to nterp this is a volatile field. We
    // also don't cache the result as we don't want nterp to have its fast path always
    // check for it.
    return reinterpret_cast<size_t>(resolved_field) | 1;
  } else {
    UpdateCache(self, dex_pc_ptr, resolved_field);
    return reinterpret_cast<size_t>(resolved_field);
  }
}

extern "C" uint32_t NterpGetInstanceFieldOffset(Thread* self,
                                                ArtMethod* caller,
                                                uint16_t* dex_pc_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  uint16_t field_index = inst->VRegC_22c();
  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
  ArtField* resolved_field = ResolveFieldWithAccessChecks(
      self,
      class_linker,
      field_index,
      caller,
      /* is_static */ false,
      /* is_put */ IsInstructionIPut(inst->Opcode()));
  if (resolved_field == nullptr) {
    DCHECK(self->IsExceptionPending());
    return 0;
  }
  if (resolved_field->IsVolatile()) {
    // Don't cache for a volatile field, and return a negative offset as marker
    // of volatile.
    return -resolved_field->GetOffset().Uint32Value();
  }
  UpdateCache(self, dex_pc_ptr, resolved_field->GetOffset().Uint32Value());
  return resolved_field->GetOffset().Uint32Value();
}

extern "C" mirror::Object* NterpGetClassOrAllocateObject(Thread* self,
                                                         ArtMethod* caller,
                                                         uint16_t* dex_pc_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  dex::TypeIndex index;
  switch (inst->Opcode()) {
    case Instruction::NEW_INSTANCE:
      index = dex::TypeIndex(inst->VRegB_21c());
      break;
    case Instruction::CHECK_CAST:
      index = dex::TypeIndex(inst->VRegB_21c());
      break;
    case Instruction::INSTANCE_OF:
      index = dex::TypeIndex(inst->VRegC_22c());
      break;
    case Instruction::CONST_CLASS:
      index = dex::TypeIndex(inst->VRegB_21c());
      break;
    case Instruction::NEW_ARRAY:
      index = dex::TypeIndex(inst->VRegC_22c());
      break;
    default:
      LOG(FATAL) << "Unreachable";
  }
  ObjPtr<mirror::Class> c =
      ResolveVerifyAndClinit(index,
                             caller,
                             self,
                             /* can_run_clinit= */ false,
                             /* verify_access= */ !caller->SkipAccessChecks());
  if (c == nullptr) {
    DCHECK(self->IsExceptionPending());
    return nullptr;
  }

  if (inst->Opcode() == Instruction::NEW_INSTANCE) {
    gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
    if (UNLIKELY(c->IsStringClass())) {
      // We don't cache the class for strings as we need to special case their
      // allocation.
      return mirror::String::AllocEmptyString(self, allocator_type).Ptr();
    } else {
      if (!c->IsFinalizable() && c->IsInstantiable()) {
        // Cache non-finalizable classes for next calls.
        UpdateCache(self, dex_pc_ptr, c.Ptr());
      }
      return AllocObjectFromCode(c, self, allocator_type).Ptr();
    }
  } else {
    // For all other cases, cache the class.
    UpdateCache(self, dex_pc_ptr, c.Ptr());
  }
  return c.Ptr();
}

extern "C" mirror::Object* NterpLoadObject(Thread* self, ArtMethod* caller, uint16_t* dex_pc_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
  switch (inst->Opcode()) {
    case Instruction::CONST_STRING:
    case Instruction::CONST_STRING_JUMBO: {
      dex::StringIndex string_index(
          (inst->Opcode() == Instruction::CONST_STRING)
              ? inst->VRegB_21c()
              : inst->VRegB_31c());
      ObjPtr<mirror::String> str = class_linker->ResolveString(string_index, caller);
      if (str == nullptr) {
        DCHECK(self->IsExceptionPending());
        return nullptr;
      }
      UpdateCache(self, dex_pc_ptr, str.Ptr());
      return str.Ptr();
    }
    case Instruction::CONST_METHOD_HANDLE: {
      // Don't cache: we don't expect this to be performance sensitive, and we
      // don't want the cache to conflict with a performance sensitive entry.
      return class_linker->ResolveMethodHandle(self, inst->VRegB_21c(), caller).Ptr();
    }
    case Instruction::CONST_METHOD_TYPE: {
      // Don't cache: we don't expect this to be performance sensitive, and we
      // don't want the cache to conflict with a performance sensitive entry.
      return class_linker->ResolveMethodType(
          self, dex::ProtoIndex(inst->VRegB_21c()), caller).Ptr();
    }
    default:
      LOG(FATAL) << "Unreachable";
  }
  return nullptr;
}

extern "C" void NterpUnimplemented() {
  LOG(FATAL) << "Unimplemented";
}

static mirror::Object* DoFilledNewArray(Thread* self,
                                        ArtMethod* caller,
                                        uint16_t* dex_pc_ptr,
                                        int32_t* regs,
                                        bool is_range)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const Instruction* inst = Instruction::At(dex_pc_ptr);
  if (kIsDebugBuild) {
    if (is_range) {
      DCHECK_EQ(inst->Opcode(), Instruction::FILLED_NEW_ARRAY_RANGE);
    } else {
      DCHECK_EQ(inst->Opcode(), Instruction::FILLED_NEW_ARRAY);
    }
  }
  const int32_t length = is_range ? inst->VRegA_3rc() : inst->VRegA_35c();
  DCHECK_GE(length, 0);
  if (!is_range) {
    // Checks FILLED_NEW_ARRAY's length does not exceed 5 arguments.
    DCHECK_LE(length, 5);
  }
  uint16_t type_idx = is_range ? inst->VRegB_3rc() : inst->VRegB_35c();
  ObjPtr<mirror::Class> array_class = ResolveVerifyAndClinit(dex::TypeIndex(type_idx),
                                                             caller,
                                                             self,
                                                             /* can_run_clinit= */ true,
                                                             /* verify_access= */ false);
  if (UNLIKELY(array_class == nullptr)) {
    DCHECK(self->IsExceptionPending());
    return nullptr;
  }
  DCHECK(array_class->IsArrayClass());
  ObjPtr<mirror::Class> component_class = array_class->GetComponentType();
  const bool is_primitive_int_component = component_class->IsPrimitiveInt();
  if (UNLIKELY(component_class->IsPrimitive() && !is_primitive_int_component)) {
    if (component_class->IsPrimitiveLong() || component_class->IsPrimitiveDouble()) {
      ThrowRuntimeException("Bad filled array request for type %s",
                            component_class->PrettyDescriptor().c_str());
    } else {
      self->ThrowNewExceptionF(
          "Ljava/lang/InternalError;",
          "Found type %s; filled-new-array not implemented for anything but 'int'",
          component_class->PrettyDescriptor().c_str());
    }
    return nullptr;
  }
  ObjPtr<mirror::Object> new_array = mirror::Array::Alloc(
      self,
      array_class,
      length,
      array_class->GetComponentSizeShift(),
      Runtime::Current()->GetHeap()->GetCurrentAllocator());
  if (UNLIKELY(new_array == nullptr)) {
    self->AssertPendingOOMException();
    return nullptr;
  }
  uint32_t arg[Instruction::kMaxVarArgRegs];  // only used in filled-new-array.
  uint32_t vregC = 0;   // only used in filled-new-array-range.
  if (is_range) {
    vregC = inst->VRegC_3rc();
  } else {
    inst->GetVarArgs(arg);
  }
  for (int32_t i = 0; i < length; ++i) {
    size_t src_reg = is_range ? vregC + i : arg[i];
    if (is_primitive_int_component) {
      new_array->AsIntArray()->SetWithoutChecks</* kTransactionActive= */ false>(i, regs[src_reg]);
    } else {
      new_array->AsObjectArray<mirror::Object>()->SetWithoutChecks</* kTransactionActive= */ false>(
          i, reinterpret_cast<mirror::Object*>(regs[src_reg]));
    }
  }
  return new_array.Ptr();
}

extern "C" mirror::Object* NterpFilledNewArray(Thread* self,
                                               ArtMethod* caller,
                                               int32_t* registers,
                                               uint16_t* dex_pc_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return DoFilledNewArray(self, caller, dex_pc_ptr, registers, /* is_range= */ false);
}

extern "C" mirror::Object* NterpFilledNewArrayRange(Thread* self,
                                                    ArtMethod* caller,
                                                    int32_t* registers,
                                                    uint16_t* dex_pc_ptr)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return DoFilledNewArray(self, caller, dex_pc_ptr, registers, /* is_range= */ true);
}

extern "C" jit::OsrData* NterpHotMethod(ArtMethod* method, uint16_t* dex_pc_ptr, uint32_t* vregs)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ScopedAssertNoThreadSuspension sants("In nterp");
  jit::Jit* jit = Runtime::Current()->GetJit();
  if (jit != nullptr) {
    // Nterp passes null on entry where we don't want to OSR.
    if (dex_pc_ptr != nullptr) {
      // This could be a loop back edge, check if we can OSR.
      CodeItemInstructionAccessor accessor(method->DexInstructions());
      uint32_t dex_pc = dex_pc_ptr - accessor.Insns();
      jit::OsrData* osr_data = jit->PrepareForOsr(
          method->GetInterfaceMethodIfProxy(kRuntimePointerSize), dex_pc, vregs);
      if (osr_data != nullptr) {
        return osr_data;
      }
    }
    jit->EnqueueCompilationFromNterp(method, Thread::Current());
  }
  return nullptr;
}

extern "C" ssize_t MterpDoPackedSwitch(const uint16_t* switchData, int32_t testVal);
extern "C" ssize_t NterpDoPackedSwitch(const uint16_t* switchData, int32_t testVal)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return MterpDoPackedSwitch(switchData, testVal);
}

extern "C" ssize_t MterpDoSparseSwitch(const uint16_t* switchData, int32_t testVal);
extern "C" ssize_t NterpDoSparseSwitch(const uint16_t* switchData, int32_t testVal)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return MterpDoSparseSwitch(switchData, testVal);
}

}  // namespace interpreter
}  // namespace art
