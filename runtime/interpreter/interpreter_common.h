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

#ifndef ART_RUNTIME_INTERPRETER_INTERPRETER_COMMON_H_
#define ART_RUNTIME_INTERPRETER_INTERPRETER_COMMON_H_

#include "android-base/macros.h"
#include "instrumentation.h"
#include "interpreter.h"
#include "interpreter_intrinsics.h"
#include "transaction.h"

#include <math.h>

#include <atomic>
#include <iostream>
#include <sstream>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/enums.h"
#include "base/locks.h"
#include "base/logging.h"
#include "base/macros.h"
#include "class_linker-inl.h"
#include "class_root.h"
#include "common_dex_operations.h"
#include "common_throws.h"
#include "dex/dex_file-inl.h"
#include "dex/dex_instruction-inl.h"
#include "entrypoints/entrypoint_utils-inl.h"
#include "handle_scope-inl.h"
#include "interpreter_mterp_impl.h"
#include "interpreter_switch_impl.h"
#include "jit/jit-inl.h"
#include "mirror/call_site.h"
#include "mirror/class-inl.h"
#include "mirror/dex_cache.h"
#include "mirror/method.h"
#include "mirror/method_handles_lookup.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/string-inl.h"
#include "mterp/mterp.h"
#include "obj_ptr.h"
#include "stack.h"
#include "thread.h"
#include "unstarted_runtime.h"
#include "verifier/method_verifier.h"
#include "well_known_classes.h"

namespace art {
namespace interpreter {

void ThrowNullPointerExceptionFromInterpreter()
    REQUIRES_SHARED(Locks::mutator_lock_);

template <bool kMonitorCounting>
static inline void DoMonitorEnter(Thread* self, ShadowFrame* frame, ObjPtr<mirror::Object> ref)
    NO_THREAD_SAFETY_ANALYSIS
    REQUIRES(!Roles::uninterruptible_) {
  DCHECK(!ref.IsNull());
  StackHandleScope<1> hs(self);
  Handle<mirror::Object> h_ref(hs.NewHandle(ref));
  h_ref->MonitorEnter(self);
  DCHECK(self->HoldsLock(h_ref.Get()));
  if (UNLIKELY(self->IsExceptionPending())) {
    bool unlocked = h_ref->MonitorExit(self);
    DCHECK(unlocked);
    return;
  }
  if (kMonitorCounting && frame->GetMethod()->MustCountLocks()) {
    frame->GetLockCountData().AddMonitor(self, h_ref.Get());
  }
}

template <bool kMonitorCounting>
static inline void DoMonitorExit(Thread* self, ShadowFrame* frame, ObjPtr<mirror::Object> ref)
    NO_THREAD_SAFETY_ANALYSIS
    REQUIRES(!Roles::uninterruptible_) {
  StackHandleScope<1> hs(self);
  Handle<mirror::Object> h_ref(hs.NewHandle(ref));
  h_ref->MonitorExit(self);
  if (kMonitorCounting && frame->GetMethod()->MustCountLocks()) {
    frame->GetLockCountData().RemoveMonitorOrThrow(self, h_ref.Get());
  }
}

template <bool kMonitorCounting>
static inline bool DoMonitorCheckOnExit(Thread* self, ShadowFrame* frame)
    NO_THREAD_SAFETY_ANALYSIS
    REQUIRES(!Roles::uninterruptible_) {
  if (kMonitorCounting && frame->GetMethod()->MustCountLocks()) {
    return frame->GetLockCountData().CheckAllMonitorsReleasedOrThrow(self);
  }
  return true;
}

void AbortTransactionF(Thread* self, const char* fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)))
    REQUIRES_SHARED(Locks::mutator_lock_);

void AbortTransactionV(Thread* self, const char* fmt, va_list args)
    REQUIRES_SHARED(Locks::mutator_lock_);

void RecordArrayElementsInTransaction(ObjPtr<mirror::Array> array, int32_t count)
    REQUIRES_SHARED(Locks::mutator_lock_);

// Invokes the given method. This is part of the invocation support and is used by DoInvoke,
// DoFastInvoke and DoInvokeVirtualQuick functions.
// Returns true on success, otherwise throws an exception and returns false.
template<bool is_range, bool do_assignability_check>
bool DoCall(ArtMethod* called_method, Thread* self, ShadowFrame& shadow_frame,
            const Instruction* inst, uint16_t inst_data, JValue* result);

bool UseFastInterpreterToInterpreterInvoke(ArtMethod* method)
    REQUIRES_SHARED(Locks::mutator_lock_);

// Throws exception if we are getting close to the end of the stack.
NO_INLINE bool CheckStackOverflow(Thread* self, size_t frame_size)
    REQUIRES_SHARED(Locks::mutator_lock_);


// Sends the normal method exit event.
// Returns true if the events succeeded and false if there is a pending exception.
template <typename T> bool SendMethodExitEvents(
    Thread* self,
    const instrumentation::Instrumentation* instrumentation,
    ShadowFrame& frame,
    ObjPtr<mirror::Object> thiz,
    ArtMethod* method,
    uint32_t dex_pc,
    T& result) REQUIRES_SHARED(Locks::mutator_lock_);

static inline ALWAYS_INLINE WARN_UNUSED bool
NeedsMethodExitEvent(const instrumentation::Instrumentation* ins)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return ins->HasMethodExitListeners() || ins->HasWatchedFramePopListeners();
}

// NO_INLINE so we won't bloat the interpreter with this very cold lock-release code.
template <bool kMonitorCounting>
static NO_INLINE void UnlockHeldMonitors(Thread* self, ShadowFrame* shadow_frame)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(shadow_frame->GetForcePopFrame());
  // Unlock all monitors.
  if (kMonitorCounting && shadow_frame->GetMethod()->MustCountLocks()) {
    // Get the monitors from the shadow-frame monitor-count data.
    shadow_frame->GetLockCountData().VisitMonitors(
      [&](mirror::Object** obj) REQUIRES_SHARED(Locks::mutator_lock_) {
        // Since we don't use the 'obj' pointer after the DoMonitorExit everything should be fine
        // WRT suspension.
        DoMonitorExit<kMonitorCounting>(self, shadow_frame, *obj);
      });
  } else {
    std::vector<verifier::MethodVerifier::DexLockInfo> locks;
    verifier::MethodVerifier::FindLocksAtDexPc(shadow_frame->GetMethod(),
                                                shadow_frame->GetDexPC(),
                                                &locks,
                                                Runtime::Current()->GetTargetSdkVersion());
    for (const auto& reg : locks) {
      if (UNLIKELY(reg.dex_registers.empty())) {
        LOG(ERROR) << "Unable to determine reference locked by "
                    << shadow_frame->GetMethod()->PrettyMethod() << " at pc "
                    << shadow_frame->GetDexPC();
      } else {
        DoMonitorExit<kMonitorCounting>(
            self, shadow_frame, shadow_frame->GetVRegReference(*reg.dex_registers.begin()));
      }
    }
  }
}

enum class MonitorState {
  kNoMonitorsLocked,
  kCountingMonitors,
  kNormalMonitors,
};

template<MonitorState kMonitorState>
static inline ALWAYS_INLINE WARN_UNUSED bool PerformNonStandardReturn(
      Thread* self,
      ShadowFrame& frame,
      JValue& result,
      const instrumentation::Instrumentation* instrumentation,
      uint16_t num_dex_inst,
      uint32_t dex_pc) REQUIRES_SHARED(Locks::mutator_lock_) {
  static constexpr bool kMonitorCounting = (kMonitorState == MonitorState::kCountingMonitors);
  if (UNLIKELY(frame.GetForcePopFrame())) {
    ObjPtr<mirror::Object> thiz(frame.GetThisObject(num_dex_inst));
    StackHandleScope<1> hs(self);
    Handle<mirror::Object> h_thiz(hs.NewHandle(thiz));
    DCHECK(Runtime::Current()->AreNonStandardExitsEnabled());
    if (UNLIKELY(self->IsExceptionPending())) {
      LOG(WARNING) << "Suppressing exception for non-standard method exit: "
                   << self->GetException()->Dump();
      self->ClearException();
    }
    if (kMonitorState != MonitorState::kNoMonitorsLocked) {
      UnlockHeldMonitors<kMonitorCounting>(self, &frame);
    }
    DoMonitorCheckOnExit<kMonitorCounting>(self, &frame);
    result = JValue();
    if (UNLIKELY(NeedsMethodExitEvent(instrumentation))) {
      SendMethodExitEvents(
          self, instrumentation, frame, h_thiz.Get(), frame.GetMethod(), dex_pc, result);
    }
    return true;
  }
  return false;
}

// Handles all invoke-XXX/range instructions except for invoke-polymorphic[/range].
// Returns true on success, otherwise throws an exception and returns false.
template<InvokeType type, bool is_range, bool do_access_check, bool is_mterp, bool is_quick = false>
static ALWAYS_INLINE bool DoInvoke(Thread* self,
                                   ShadowFrame& shadow_frame,
                                   const Instruction* inst,
                                   uint16_t inst_data,
                                   JValue* result)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // Make sure to check for async exceptions before anything else.
  if (is_mterp && self->UseMterp()) {
    DCHECK(!self->ObserveAsyncException());
  } else if (UNLIKELY(self->ObserveAsyncException())) {
    return false;
  }
  const uint32_t method_idx = (is_range) ? inst->VRegB_3rc() : inst->VRegB_35c();
  const uint32_t vregC = (is_range) ? inst->VRegC_3rc() : inst->VRegC_35c();
  ArtMethod* sf_method = shadow_frame.GetMethod();

  // Try to find the method in small thread-local cache first (only used when
  // nterp is not used as mterp and nterp use the cache in an incompatible way).
  InterpreterCache* tls_cache = self->GetInterpreterCache();
  size_t tls_value;
  ArtMethod* resolved_method;
  if (is_quick) {
    resolved_method = nullptr;  // We don't know/care what the original method was.
  } else if (!IsNterpSupported() && LIKELY(tls_cache->Get(inst, &tls_value))) {
    resolved_method = reinterpret_cast<ArtMethod*>(tls_value);
  } else {
    ClassLinker* const class_linker = Runtime::Current()->GetClassLinker();
    constexpr ClassLinker::ResolveMode resolve_mode =
        do_access_check ? ClassLinker::ResolveMode::kCheckICCEAndIAE
                        : ClassLinker::ResolveMode::kNoChecks;
    resolved_method = class_linker->ResolveMethod<resolve_mode>(self, method_idx, sf_method, type);
    if (UNLIKELY(resolved_method == nullptr)) {
      CHECK(self->IsExceptionPending());
      result->SetJ(0);
      return false;
    }
    if (!IsNterpSupported()) {
      tls_cache->Set(inst, reinterpret_cast<size_t>(resolved_method));
    }
  }

  // Null pointer check and virtual method resolution.
  ObjPtr<mirror::Object> receiver =
      (type == kStatic) ? nullptr : shadow_frame.GetVRegReference(vregC);
  ArtMethod* called_method;
  if (is_quick) {
    if (UNLIKELY(receiver == nullptr)) {
      // We lost the reference to the method index so we cannot get a more precise exception.
      ThrowNullPointerExceptionFromDexPC();
      return false;
    }
    DCHECK(receiver->GetClass()->ShouldHaveEmbeddedVTable());
    called_method = receiver->GetClass()->GetEmbeddedVTableEntry(
        /*vtable_idx=*/ method_idx, Runtime::Current()->GetClassLinker()->GetImagePointerSize());
  } else {
    called_method = FindMethodToCall<type, do_access_check>(
        method_idx, resolved_method, &receiver, sf_method, self);
  }
  if (UNLIKELY(called_method == nullptr)) {
    CHECK(self->IsExceptionPending());
    result->SetJ(0);
    return false;
  }
  if (UNLIKELY(!called_method->IsInvokable())) {
    called_method->ThrowInvocationTimeError();
    result->SetJ(0);
    return false;
  }

  jit::Jit* jit = Runtime::Current()->GetJit();
  if (jit != nullptr && (type == kVirtual || type == kInterface)) {
    jit->InvokeVirtualOrInterface(receiver, sf_method, shadow_frame.GetDexPC(), called_method);
  }

  if (is_mterp && !is_range && called_method->IsIntrinsic()) {
    if (MterpHandleIntrinsic(&shadow_frame, called_method, inst, inst_data,
                             shadow_frame.GetResultRegister())) {
      if (jit != nullptr && sf_method != nullptr) {
        jit->NotifyInterpreterToCompiledCodeTransition(self, sf_method);
      }
      return !self->IsExceptionPending();
    }
  }

  // Check whether we can use the fast path. The result is cached in the ArtMethod.
  // If the bit is not set, we explicitly recheck all the conditions.
  // If any of the conditions get falsified, it is important to clear the bit.
  bool use_fast_path = false;
  if (is_mterp && self->UseMterp()) {
    use_fast_path = called_method->UseFastInterpreterToInterpreterInvoke();
    if (!use_fast_path) {
      use_fast_path = UseFastInterpreterToInterpreterInvoke(called_method);
      if (use_fast_path) {
        called_method->SetFastInterpreterToInterpreterInvokeFlag();
      }
    }
  }

  if (use_fast_path) {
    DCHECK(Runtime::Current()->IsStarted());
    DCHECK(!Runtime::Current()->IsActiveTransaction());
    DCHECK(called_method->SkipAccessChecks());
    DCHECK(!called_method->IsNative());
    DCHECK(!called_method->IsProxyMethod());
    DCHECK(!called_method->IsIntrinsic());
    DCHECK(!(called_method->GetDeclaringClass()->IsStringClass() &&
        called_method->IsConstructor()));
    DCHECK(type != kStatic || called_method->GetDeclaringClass()->IsVisiblyInitialized());

    const uint16_t number_of_inputs =
        (is_range) ? inst->VRegA_3rc(inst_data) : inst->VRegA_35c(inst_data);
    CodeItemDataAccessor accessor(called_method->DexInstructionData());
    uint32_t num_regs = accessor.RegistersSize();
    DCHECK_EQ(number_of_inputs, accessor.InsSize());
    DCHECK_GE(num_regs, number_of_inputs);
    size_t first_dest_reg = num_regs - number_of_inputs;

    if (UNLIKELY(!CheckStackOverflow(self, ShadowFrame::ComputeSize(num_regs)))) {
      return false;
    }

    if (jit != nullptr) {
      jit->AddSamples(self, called_method, 1, /* with_backedges */false);
    }

    // Create shadow frame on the stack.
    const char* old_cause = self->StartAssertNoThreadSuspension("DoFastInvoke");
    ShadowFrameAllocaUniquePtr shadow_frame_unique_ptr =
        CREATE_SHADOW_FRAME(num_regs, &shadow_frame, called_method, /* dex pc */ 0);
    ShadowFrame* new_shadow_frame = shadow_frame_unique_ptr.get();
    if (is_range) {
      size_t src = vregC;
      for (size_t i = 0, dst = first_dest_reg; i < number_of_inputs; ++i, ++dst, ++src) {
        *new_shadow_frame->GetVRegAddr(dst) = *shadow_frame.GetVRegAddr(src);
        *new_shadow_frame->GetShadowRefAddr(dst) = *shadow_frame.GetShadowRefAddr(src);
      }
    } else {
      uint32_t arg[Instruction::kMaxVarArgRegs];
      inst->GetVarArgs(arg, inst_data);
      for (size_t i = 0, dst = first_dest_reg; i < number_of_inputs; ++i, ++dst) {
        *new_shadow_frame->GetVRegAddr(dst) = *shadow_frame.GetVRegAddr(arg[i]);
        *new_shadow_frame->GetShadowRefAddr(dst) = *shadow_frame.GetShadowRefAddr(arg[i]);
      }
    }
    self->PushShadowFrame(new_shadow_frame);
    self->EndAssertNoThreadSuspension(old_cause);

    VLOG(interpreter) << "Interpreting " << called_method->PrettyMethod();

    DCheckStaticState(self, called_method);
    while (true) {
      // Mterp does not support all instrumentation/debugging.
      if (!self->UseMterp()) {
        *result =
            ExecuteSwitchImpl<false, false>(self, accessor, *new_shadow_frame, *result, false);
        break;
      }
      if (ExecuteMterpImpl(self, accessor.Insns(), new_shadow_frame, result)) {
        break;
      } else {
        // Mterp didn't like that instruction.  Single-step it with the reference interpreter.
        *result = ExecuteSwitchImpl<false, false>(self, accessor, *new_shadow_frame, *result, true);
        if (new_shadow_frame->GetDexPC() == dex::kDexNoIndex) {
          break;  // Single-stepped a return or an exception not handled locally.
        }
      }
    }
    self->PopShadowFrame();

    return !self->IsExceptionPending();
  }

  return DoCall<is_range, do_access_check>(called_method, self, shadow_frame, inst, inst_data,
                                           result);
}

static inline ObjPtr<mirror::MethodHandle> ResolveMethodHandle(Thread* self,
                                                               uint32_t method_handle_index,
                                                               ArtMethod* referrer)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  return class_linker->ResolveMethodHandle(self, method_handle_index, referrer);
}

static inline ObjPtr<mirror::MethodType> ResolveMethodType(Thread* self,
                                                           dex::ProtoIndex method_type_index,
                                                           ArtMethod* referrer)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  return class_linker->ResolveMethodType(self, method_type_index, referrer);
}

#define DECLARE_SIGNATURE_POLYMORPHIC_HANDLER(Name, ...)              \
bool Do ## Name(Thread* self,                                         \
                ShadowFrame& shadow_frame,                            \
                const Instruction* inst,                              \
                uint16_t inst_data,                                   \
                JValue* result) REQUIRES_SHARED(Locks::mutator_lock_);
#include "intrinsics_list.h"
INTRINSICS_LIST(DECLARE_SIGNATURE_POLYMORPHIC_HANDLER)
#undef INTRINSICS_LIST
#undef DECLARE_SIGNATURE_POLYMORPHIC_HANDLER

// Performs a invoke-polymorphic or invoke-polymorphic-range.
template<bool is_range>
bool DoInvokePolymorphic(Thread* self,
                         ShadowFrame& shadow_frame,
                         const Instruction* inst,
                         uint16_t inst_data,
                         JValue* result)
    REQUIRES_SHARED(Locks::mutator_lock_);

bool DoInvokeCustom(Thread* self,
                    ShadowFrame& shadow_frame,
                    uint32_t call_site_idx,
                    const InstructionOperands* operands,
                    JValue* result)
    REQUIRES_SHARED(Locks::mutator_lock_);

// Performs a custom invoke (invoke-custom/invoke-custom-range).
template<bool is_range>
bool DoInvokeCustom(Thread* self,
                    ShadowFrame& shadow_frame,
                    const Instruction* inst,
                    uint16_t inst_data,
                    JValue* result)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const uint32_t call_site_idx = is_range ? inst->VRegB_3rc() : inst->VRegB_35c();
  if (is_range) {
    RangeInstructionOperands operands(inst->VRegC_3rc(), inst->VRegA_3rc());
    return DoInvokeCustom(self, shadow_frame, call_site_idx, &operands, result);
  } else {
    uint32_t args[Instruction::kMaxVarArgRegs];
    inst->GetVarArgs(args, inst_data);
    VarArgsInstructionOperands operands(args, inst->VRegA_35c());
    return DoInvokeCustom(self, shadow_frame, call_site_idx, &operands, result);
  }
}

template<Primitive::Type field_type>
ALWAYS_INLINE static JValue GetFieldValue(const ShadowFrame& shadow_frame, uint32_t vreg)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  JValue field_value;
  switch (field_type) {
    case Primitive::kPrimBoolean:
      field_value.SetZ(static_cast<uint8_t>(shadow_frame.GetVReg(vreg)));
      break;
    case Primitive::kPrimByte:
      field_value.SetB(static_cast<int8_t>(shadow_frame.GetVReg(vreg)));
      break;
    case Primitive::kPrimChar:
      field_value.SetC(static_cast<uint16_t>(shadow_frame.GetVReg(vreg)));
      break;
    case Primitive::kPrimShort:
      field_value.SetS(static_cast<int16_t>(shadow_frame.GetVReg(vreg)));
      break;
    case Primitive::kPrimInt:
      field_value.SetI(shadow_frame.GetVReg(vreg));
      break;
    case Primitive::kPrimLong:
      field_value.SetJ(shadow_frame.GetVRegLong(vreg));
      break;
    case Primitive::kPrimNot:
      field_value.SetL(shadow_frame.GetVRegReference(vreg));
      break;
    default:
      LOG(FATAL) << "Unreachable: " << field_type;
      UNREACHABLE();
  }
  return field_value;
}

// Handles iget-XXX and sget-XXX instructions.
// Returns true on success, otherwise throws an exception and returns false.
template<FindFieldType find_type, Primitive::Type field_type, bool do_access_check,
         bool transaction_active = false>
ALWAYS_INLINE bool DoFieldGet(Thread* self, ShadowFrame& shadow_frame, const Instruction* inst,
                              uint16_t inst_data) REQUIRES_SHARED(Locks::mutator_lock_) {
  const bool is_static = (find_type == StaticObjectRead) || (find_type == StaticPrimitiveRead);
  const uint32_t field_idx = is_static ? inst->VRegB_21c() : inst->VRegC_22c();
  ArtField* f =
      FindFieldFromCode<find_type, do_access_check>(field_idx, shadow_frame.GetMethod(), self,
                                                    Primitive::ComponentSize(field_type));
  if (UNLIKELY(f == nullptr)) {
    CHECK(self->IsExceptionPending());
    return false;
  }
  ObjPtr<mirror::Object> obj;
  if (is_static) {
    obj = f->GetDeclaringClass();
    if (transaction_active) {
      if (Runtime::Current()->GetTransaction()->ReadConstraint(self, obj)) {
        Runtime::Current()->AbortTransactionAndThrowAbortError(self, "Can't read static fields of "
            + obj->PrettyTypeOf() + " since it does not belong to clinit's class.");
        return false;
      }
    }
  } else {
    obj = shadow_frame.GetVRegReference(inst->VRegB_22c(inst_data));
    if (UNLIKELY(obj == nullptr)) {
      ThrowNullPointerExceptionForFieldAccess(f, true);
      return false;
    }
  }

  JValue result;
  if (UNLIKELY(!DoFieldGetCommon<field_type>(self, shadow_frame, obj, f, &result))) {
    // Instrumentation threw an error!
    CHECK(self->IsExceptionPending());
    return false;
  }
  uint32_t vregA = is_static ? inst->VRegA_21c(inst_data) : inst->VRegA_22c(inst_data);
  switch (field_type) {
    case Primitive::kPrimBoolean:
      shadow_frame.SetVReg(vregA, result.GetZ());
      break;
    case Primitive::kPrimByte:
      shadow_frame.SetVReg(vregA, result.GetB());
      break;
    case Primitive::kPrimChar:
      shadow_frame.SetVReg(vregA, result.GetC());
      break;
    case Primitive::kPrimShort:
      shadow_frame.SetVReg(vregA, result.GetS());
      break;
    case Primitive::kPrimInt:
      shadow_frame.SetVReg(vregA, result.GetI());
      break;
    case Primitive::kPrimLong:
      shadow_frame.SetVRegLong(vregA, result.GetJ());
      break;
    case Primitive::kPrimNot:
      shadow_frame.SetVRegReference(vregA, result.GetL());
      break;
    default:
      LOG(FATAL) << "Unreachable: " << field_type;
      UNREACHABLE();
  }
  return true;
}

// Handles iget-quick, iget-wide-quick and iget-object-quick instructions.
// Returns true on success, otherwise throws an exception and returns false.
template<Primitive::Type field_type>
ALWAYS_INLINE bool DoIGetQuick(ShadowFrame& shadow_frame, const Instruction* inst,
                               uint16_t inst_data) REQUIRES_SHARED(Locks::mutator_lock_) {
  ObjPtr<mirror::Object> obj = shadow_frame.GetVRegReference(inst->VRegB_22c(inst_data));
  if (UNLIKELY(obj == nullptr)) {
    // We lost the reference to the field index so we cannot get a more
    // precised exception message.
    ThrowNullPointerExceptionFromDexPC();
    return false;
  }
  MemberOffset field_offset(inst->VRegC_22c());
  // Report this field access to instrumentation if needed. Since we only have the offset of
  // the field from the base of the object, we need to look for it first.
  instrumentation::Instrumentation* instrumentation = Runtime::Current()->GetInstrumentation();
  if (UNLIKELY(instrumentation->HasFieldReadListeners())) {
    ArtField* f = ArtField::FindInstanceFieldWithOffset(obj->GetClass(),
                                                        field_offset.Uint32Value());
    DCHECK(f != nullptr);
    DCHECK(!f->IsStatic());
    Thread* self = Thread::Current();
    StackHandleScope<1> hs(self);
    // Save obj in case the instrumentation event has thread suspension.
    HandleWrapperObjPtr<mirror::Object> h = hs.NewHandleWrapper(&obj);
    instrumentation->FieldReadEvent(self,
                                    obj,
                                    shadow_frame.GetMethod(),
                                    shadow_frame.GetDexPC(),
                                    f);
    if (UNLIKELY(self->IsExceptionPending())) {
      return false;
    }
  }
  // Note: iget-x-quick instructions are only for non-volatile fields.
  const uint32_t vregA = inst->VRegA_22c(inst_data);
  switch (field_type) {
    case Primitive::kPrimInt:
      shadow_frame.SetVReg(vregA, static_cast<int32_t>(obj->GetField32(field_offset)));
      break;
    case Primitive::kPrimBoolean:
      shadow_frame.SetVReg(vregA, static_cast<int32_t>(obj->GetFieldBoolean(field_offset)));
      break;
    case Primitive::kPrimByte:
      shadow_frame.SetVReg(vregA, static_cast<int32_t>(obj->GetFieldByte(field_offset)));
      break;
    case Primitive::kPrimChar:
      shadow_frame.SetVReg(vregA, static_cast<int32_t>(obj->GetFieldChar(field_offset)));
      break;
    case Primitive::kPrimShort:
      shadow_frame.SetVReg(vregA, static_cast<int32_t>(obj->GetFieldShort(field_offset)));
      break;
    case Primitive::kPrimLong:
      shadow_frame.SetVRegLong(vregA, static_cast<int64_t>(obj->GetField64(field_offset)));
      break;
    case Primitive::kPrimNot:
      shadow_frame.SetVRegReference(vregA, obj->GetFieldObject<mirror::Object>(field_offset));
      break;
    default:
      LOG(FATAL) << "Unreachable: " << field_type;
      UNREACHABLE();
  }
  return true;
}

static inline bool CheckWriteConstraint(Thread* self, ObjPtr<mirror::Object> obj)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  Runtime* runtime = Runtime::Current();
  if (runtime->GetTransaction()->WriteConstraint(self, obj)) {
    DCHECK(runtime->GetHeap()->ObjectIsInBootImageSpace(obj) || obj->IsClass());
    const char* base_msg = runtime->GetHeap()->ObjectIsInBootImageSpace(obj)
        ? "Can't set fields of boot image "
        : "Can't set fields of ";
    runtime->AbortTransactionAndThrowAbortError(self, base_msg + obj->PrettyTypeOf());
    return false;
  }
  return true;
}

static inline bool CheckWriteValueConstraint(Thread* self, ObjPtr<mirror::Object> value)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  Runtime* runtime = Runtime::Current();
  if (runtime->GetTransaction()->WriteValueConstraint(self, value)) {
    DCHECK(value != nullptr);
    std::string msg = value->IsClass()
        ? "Can't store reference to class " + value->AsClass()->PrettyDescriptor()
        : "Can't store reference to instance of " + value->GetClass()->PrettyDescriptor();
    runtime->AbortTransactionAndThrowAbortError(self, msg);
    return false;
  }
  return true;
}

// Handles iput-XXX and sput-XXX instructions.
// Returns true on success, otherwise throws an exception and returns false.
template<FindFieldType find_type, Primitive::Type field_type, bool do_access_check,
         bool transaction_active>
ALWAYS_INLINE bool DoFieldPut(Thread* self, const ShadowFrame& shadow_frame,
                              const Instruction* inst, uint16_t inst_data)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const bool do_assignability_check = do_access_check;
  bool is_static = (find_type == StaticObjectWrite) || (find_type == StaticPrimitiveWrite);
  uint32_t field_idx = is_static ? inst->VRegB_21c() : inst->VRegC_22c();
  ArtField* f =
      FindFieldFromCode<find_type, do_access_check>(field_idx, shadow_frame.GetMethod(), self,
                                                    Primitive::ComponentSize(field_type));
  if (UNLIKELY(f == nullptr)) {
    CHECK(self->IsExceptionPending());
    return false;
  }
  ObjPtr<mirror::Object> obj;
  if (is_static) {
    obj = f->GetDeclaringClass();
  } else {
    obj = shadow_frame.GetVRegReference(inst->VRegB_22c(inst_data));
    if (UNLIKELY(obj == nullptr)) {
      ThrowNullPointerExceptionForFieldAccess(f, false);
      return false;
    }
  }
  if (transaction_active && !CheckWriteConstraint(self, obj)) {
    return false;
  }

  uint32_t vregA = is_static ? inst->VRegA_21c(inst_data) : inst->VRegA_22c(inst_data);
  JValue value = GetFieldValue<field_type>(shadow_frame, vregA);

  if (transaction_active &&
      field_type == Primitive::kPrimNot &&
      !CheckWriteValueConstraint(self, value.GetL())) {
    return false;
  }

  return DoFieldPutCommon<field_type, do_assignability_check, transaction_active>(self,
                                                                                  shadow_frame,
                                                                                  obj,
                                                                                  f,
                                                                                  value);
}

// Handles iput-quick, iput-wide-quick and iput-object-quick instructions.
// Returns true on success, otherwise throws an exception and returns false.
template<Primitive::Type field_type, bool transaction_active>
ALWAYS_INLINE bool DoIPutQuick(const ShadowFrame& shadow_frame, const Instruction* inst,
                               uint16_t inst_data) REQUIRES_SHARED(Locks::mutator_lock_) {
  ObjPtr<mirror::Object> obj = shadow_frame.GetVRegReference(inst->VRegB_22c(inst_data));
  if (UNLIKELY(obj == nullptr)) {
    // We lost the reference to the field index so we cannot get a more
    // precised exception message.
    ThrowNullPointerExceptionFromDexPC();
    return false;
  }
  MemberOffset field_offset(inst->VRegC_22c());
  const uint32_t vregA = inst->VRegA_22c(inst_data);
  // Report this field modification to instrumentation if needed. Since we only have the offset of
  // the field from the base of the object, we need to look for it first.
  instrumentation::Instrumentation* instrumentation = Runtime::Current()->GetInstrumentation();
  if (UNLIKELY(instrumentation->HasFieldWriteListeners())) {
    ArtField* f = ArtField::FindInstanceFieldWithOffset(obj->GetClass(),
                                                        field_offset.Uint32Value());
    DCHECK(f != nullptr);
    DCHECK(!f->IsStatic());
    JValue field_value = GetFieldValue<field_type>(shadow_frame, vregA);
    Thread* self = Thread::Current();
    StackHandleScope<2> hs(self);
    // Save obj in case the instrumentation event has thread suspension.
    HandleWrapperObjPtr<mirror::Object> h = hs.NewHandleWrapper(&obj);
    mirror::Object* fake_root = nullptr;
    HandleWrapper<mirror::Object> ret(hs.NewHandleWrapper<mirror::Object>(
        field_type == Primitive::kPrimNot ? field_value.GetGCRoot() : &fake_root));
    instrumentation->FieldWriteEvent(self,
                                     obj,
                                     shadow_frame.GetMethod(),
                                     shadow_frame.GetDexPC(),
                                     f,
                                     field_value);
    if (UNLIKELY(self->IsExceptionPending())) {
      return false;
    }
    if (UNLIKELY(shadow_frame.GetForcePopFrame())) {
      // Don't actually set the field. The next instruction will force us to pop.
      DCHECK(Runtime::Current()->AreNonStandardExitsEnabled());
      return true;
    }
  }
  // Note: iput-x-quick instructions are only for non-volatile fields.
  switch (field_type) {
    case Primitive::kPrimBoolean:
      obj->SetFieldBoolean<transaction_active>(field_offset, shadow_frame.GetVReg(vregA));
      break;
    case Primitive::kPrimByte:
      obj->SetFieldByte<transaction_active>(field_offset, shadow_frame.GetVReg(vregA));
      break;
    case Primitive::kPrimChar:
      obj->SetFieldChar<transaction_active>(field_offset, shadow_frame.GetVReg(vregA));
      break;
    case Primitive::kPrimShort:
      obj->SetFieldShort<transaction_active>(field_offset, shadow_frame.GetVReg(vregA));
      break;
    case Primitive::kPrimInt:
      obj->SetField32<transaction_active>(field_offset, shadow_frame.GetVReg(vregA));
      break;
    case Primitive::kPrimLong:
      obj->SetField64<transaction_active>(field_offset, shadow_frame.GetVRegLong(vregA));
      break;
    case Primitive::kPrimNot:
      obj->SetFieldObject<transaction_active>(field_offset, shadow_frame.GetVRegReference(vregA));
      break;
    default:
      LOG(FATAL) << "Unreachable: " << field_type;
      UNREACHABLE();
  }
  return true;
}

// Handles string resolution for const-string and const-string-jumbo instructions. Also ensures the
// java.lang.String class is initialized.
static inline ObjPtr<mirror::String> ResolveString(Thread* self,
                                                   ShadowFrame& shadow_frame,
                                                   dex::StringIndex string_idx)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ObjPtr<mirror::Class> java_lang_string_class = GetClassRoot<mirror::String>();
  if (UNLIKELY(!java_lang_string_class->IsVisiblyInitialized())) {
    StackHandleScope<1> hs(self);
    Handle<mirror::Class> h_class(hs.NewHandle(java_lang_string_class));
    if (UNLIKELY(!Runtime::Current()->GetClassLinker()->EnsureInitialized(
                      self, h_class, /*can_init_fields=*/ true, /*can_init_parents=*/ true))) {
      DCHECK(self->IsExceptionPending());
      return nullptr;
    }
    DCHECK(h_class->IsInitializing());
  }
  ArtMethod* method = shadow_frame.GetMethod();
  ObjPtr<mirror::String> string_ptr =
      Runtime::Current()->GetClassLinker()->ResolveString(string_idx, method);
  return string_ptr;
}

// Handles div-int, div-int/2addr, div-int/li16 and div-int/lit8 instructions.
// Returns true on success, otherwise throws a java.lang.ArithmeticException and return false.
static inline bool DoIntDivide(ShadowFrame& shadow_frame, size_t result_reg,
                               int32_t dividend, int32_t divisor)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  constexpr int32_t kMinInt = std::numeric_limits<int32_t>::min();
  if (UNLIKELY(divisor == 0)) {
    ThrowArithmeticExceptionDivideByZero();
    return false;
  }
  if (UNLIKELY(dividend == kMinInt && divisor == -1)) {
    shadow_frame.SetVReg(result_reg, kMinInt);
  } else {
    shadow_frame.SetVReg(result_reg, dividend / divisor);
  }
  return true;
}

// Handles rem-int, rem-int/2addr, rem-int/li16 and rem-int/lit8 instructions.
// Returns true on success, otherwise throws a java.lang.ArithmeticException and return false.
static inline bool DoIntRemainder(ShadowFrame& shadow_frame, size_t result_reg,
                                  int32_t dividend, int32_t divisor)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  constexpr int32_t kMinInt = std::numeric_limits<int32_t>::min();
  if (UNLIKELY(divisor == 0)) {
    ThrowArithmeticExceptionDivideByZero();
    return false;
  }
  if (UNLIKELY(dividend == kMinInt && divisor == -1)) {
    shadow_frame.SetVReg(result_reg, 0);
  } else {
    shadow_frame.SetVReg(result_reg, dividend % divisor);
  }
  return true;
}

// Handles div-long and div-long-2addr instructions.
// Returns true on success, otherwise throws a java.lang.ArithmeticException and return false.
static inline bool DoLongDivide(ShadowFrame& shadow_frame,
                                size_t result_reg,
                                int64_t dividend,
                                int64_t divisor)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const int64_t kMinLong = std::numeric_limits<int64_t>::min();
  if (UNLIKELY(divisor == 0)) {
    ThrowArithmeticExceptionDivideByZero();
    return false;
  }
  if (UNLIKELY(dividend == kMinLong && divisor == -1)) {
    shadow_frame.SetVRegLong(result_reg, kMinLong);
  } else {
    shadow_frame.SetVRegLong(result_reg, dividend / divisor);
  }
  return true;
}

// Handles rem-long and rem-long-2addr instructions.
// Returns true on success, otherwise throws a java.lang.ArithmeticException and return false.
static inline bool DoLongRemainder(ShadowFrame& shadow_frame,
                                   size_t result_reg,
                                   int64_t dividend,
                                   int64_t divisor)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const int64_t kMinLong = std::numeric_limits<int64_t>::min();
  if (UNLIKELY(divisor == 0)) {
    ThrowArithmeticExceptionDivideByZero();
    return false;
  }
  if (UNLIKELY(dividend == kMinLong && divisor == -1)) {
    shadow_frame.SetVRegLong(result_reg, 0);
  } else {
    shadow_frame.SetVRegLong(result_reg, dividend % divisor);
  }
  return true;
}

// Handles filled-new-array and filled-new-array-range instructions.
// Returns true on success, otherwise throws an exception and returns false.
template <bool is_range, bool do_access_check, bool transaction_active>
bool DoFilledNewArray(const Instruction* inst, const ShadowFrame& shadow_frame,
                      Thread* self, JValue* result);

// Handles packed-switch instruction.
// Returns the branch offset to the next instruction to execute.
static inline int32_t DoPackedSwitch(const Instruction* inst, const ShadowFrame& shadow_frame,
                                     uint16_t inst_data)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(inst->Opcode() == Instruction::PACKED_SWITCH);
  const uint16_t* switch_data = reinterpret_cast<const uint16_t*>(inst) + inst->VRegB_31t();
  int32_t test_val = shadow_frame.GetVReg(inst->VRegA_31t(inst_data));
  DCHECK_EQ(switch_data[0], static_cast<uint16_t>(Instruction::kPackedSwitchSignature));
  uint16_t size = switch_data[1];
  if (size == 0) {
    // Empty packed switch, move forward by 3 (size of PACKED_SWITCH).
    return 3;
  }
  const int32_t* keys = reinterpret_cast<const int32_t*>(&switch_data[2]);
  DCHECK_ALIGNED(keys, 4);
  int32_t first_key = keys[0];
  const int32_t* targets = reinterpret_cast<const int32_t*>(&switch_data[4]);
  DCHECK_ALIGNED(targets, 4);
  int32_t index = test_val - first_key;
  if (index >= 0 && index < size) {
    return targets[index];
  } else {
    // No corresponding value: move forward by 3 (size of PACKED_SWITCH).
    return 3;
  }
}

// Handles sparse-switch instruction.
// Returns the branch offset to the next instruction to execute.
static inline int32_t DoSparseSwitch(const Instruction* inst, const ShadowFrame& shadow_frame,
                                     uint16_t inst_data)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(inst->Opcode() == Instruction::SPARSE_SWITCH);
  const uint16_t* switch_data = reinterpret_cast<const uint16_t*>(inst) + inst->VRegB_31t();
  int32_t test_val = shadow_frame.GetVReg(inst->VRegA_31t(inst_data));
  DCHECK_EQ(switch_data[0], static_cast<uint16_t>(Instruction::kSparseSwitchSignature));
  uint16_t size = switch_data[1];
  // Return length of SPARSE_SWITCH if size is 0.
  if (size == 0) {
    return 3;
  }
  const int32_t* keys = reinterpret_cast<const int32_t*>(&switch_data[2]);
  DCHECK_ALIGNED(keys, 4);
  const int32_t* entries = keys + size;
  DCHECK_ALIGNED(entries, 4);
  int lo = 0;
  int hi = size - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int32_t foundVal = keys[mid];
    if (test_val < foundVal) {
      hi = mid - 1;
    } else if (test_val > foundVal) {
      lo = mid + 1;
    } else {
      return entries[mid];
    }
  }
  // No corresponding value: move forward by 3 (size of SPARSE_SWITCH).
  return 3;
}

// We execute any instrumentation events triggered by throwing and/or handing the pending exception
// and change the shadow_frames dex_pc to the appropriate exception handler if the current method
// has one. If the exception has been handled and the shadow_frame is now pointing to a catch clause
// we return true. If the current method is unable to handle the exception we return false.
// This function accepts a null Instrumentation* as a way to cause instrumentation events not to be
// reported.
// TODO We might wish to reconsider how we cause some events to be ignored.
bool MoveToExceptionHandler(Thread* self,
                            ShadowFrame& shadow_frame,
                            const instrumentation::Instrumentation* instrumentation)
    REQUIRES_SHARED(Locks::mutator_lock_);

NO_RETURN void UnexpectedOpcode(const Instruction* inst, const ShadowFrame& shadow_frame)
  __attribute__((cold))
  REQUIRES_SHARED(Locks::mutator_lock_);

// Set true if you want TraceExecution invocation before each bytecode execution.
constexpr bool kTraceExecutionEnabled = false;

static inline void TraceExecution(const ShadowFrame& shadow_frame, const Instruction* inst,
                                  const uint32_t dex_pc)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  if (kTraceExecutionEnabled) {
#define TRACE_LOG std::cerr
    std::ostringstream oss;
    oss << shadow_frame.GetMethod()->PrettyMethod()
        << android::base::StringPrintf("\n0x%x: ", dex_pc)
        << inst->DumpString(shadow_frame.GetMethod()->GetDexFile()) << "\n";
    for (uint32_t i = 0; i < shadow_frame.NumberOfVRegs(); ++i) {
      uint32_t raw_value = shadow_frame.GetVReg(i);
      ObjPtr<mirror::Object> ref_value = shadow_frame.GetVRegReference(i);
      oss << android::base::StringPrintf(" vreg%u=0x%08X", i, raw_value);
      if (ref_value != nullptr) {
        if (ref_value->GetClass()->IsStringClass() &&
            !ref_value->AsString()->IsValueNull()) {
          oss << "/java.lang.String \"" << ref_value->AsString()->ToModifiedUtf8() << "\"";
        } else {
          oss << "/" << ref_value->PrettyTypeOf();
        }
      }
    }
    TRACE_LOG << oss.str() << "\n";
#undef TRACE_LOG
  }
}

static inline bool IsBackwardBranch(int32_t branch_offset) {
  return branch_offset <= 0;
}

// The arg_offset is the offset to the first input register in the frame.
void ArtInterpreterToCompiledCodeBridge(Thread* self,
                                        ArtMethod* caller,
                                        ShadowFrame* shadow_frame,
                                        uint16_t arg_offset,
                                        JValue* result);

static inline bool IsStringInit(const DexFile* dex_file, uint32_t method_idx)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  const dex::MethodId& method_id = dex_file->GetMethodId(method_idx);
  const char* class_name = dex_file->StringByTypeIdx(method_id.class_idx_);
  const char* method_name = dex_file->GetMethodName(method_id);
  // Instead of calling ResolveMethod() which has suspend point and can trigger
  // GC, look up the method symbolically.
  // Compare method's class name and method name against string init.
  // It's ok since it's not allowed to create your own java/lang/String.
  // TODO: verify that assumption.
  if ((strcmp(class_name, "Ljava/lang/String;") == 0) &&
      (strcmp(method_name, "<init>") == 0)) {
    return true;
  }
  return false;
}

static inline bool IsStringInit(const Instruction* instr, ArtMethod* caller)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  if (instr->Opcode() == Instruction::INVOKE_DIRECT ||
      instr->Opcode() == Instruction::INVOKE_DIRECT_RANGE) {
    uint16_t callee_method_idx = (instr->Opcode() == Instruction::INVOKE_DIRECT_RANGE) ?
        instr->VRegB_3rc() : instr->VRegB_35c();
    return IsStringInit(caller->GetDexFile(), callee_method_idx);
  }
  return false;
}

// Set string value created from StringFactory.newStringFromXXX() into all aliases of
// StringFactory.newEmptyString().
void SetStringInitValueToAllAliases(ShadowFrame* shadow_frame,
                                    uint16_t this_obj_vreg,
                                    JValue result);

}  // namespace interpreter
}  // namespace art

#endif  // ART_RUNTIME_INTERPRETER_INTERPRETER_COMMON_H_
