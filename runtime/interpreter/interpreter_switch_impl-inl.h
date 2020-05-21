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

#ifndef ART_RUNTIME_INTERPRETER_INTERPRETER_SWITCH_IMPL_INL_H_
#define ART_RUNTIME_INTERPRETER_INTERPRETER_SWITCH_IMPL_INL_H_

#include "interpreter_switch_impl.h"

#include "base/enums.h"
#include "base/globals.h"
#include "base/memory_tool.h"
#include "base/quasi_atomic.h"
#include "dex/dex_file_types.h"
#include "dex/dex_instruction_list.h"
#include "experimental_flags.h"
#include "handle_scope.h"
#include "interpreter_common.h"
#include "interpreter/shadow_frame.h"
#include "jit/jit-inl.h"
#include "jvalue-inl.h"
#include "mirror/string-alloc-inl.h"
#include "mirror/throwable.h"
#include "monitor.h"
#include "nth_caller_visitor.h"
#include "safe_math.h"
#include "shadow_frame-inl.h"
#include "thread.h"
#include "verifier/method_verifier.h"

namespace art {
namespace interpreter {

// Short-lived helper class which executes single DEX bytecode.  It is inlined by compiler.
// Any relevant execution information is stored in the fields - it should be kept to minimum.
// All instance functions must be inlined so that the fields can be stored in registers.
//
// The function names must match the names from dex_instruction_list.h and have no arguments.
// Return value: The handlers must return false if the instruction throws or returns (exits).
//
template<bool do_access_check, bool transaction_active, Instruction::Format kFormat>
class InstructionHandler {
 public:
#define HANDLER_ATTRIBUTES ALWAYS_INLINE FLATTEN WARN_UNUSED REQUIRES_SHARED(Locks::mutator_lock_)

  HANDLER_ATTRIBUTES bool CheckForceReturn() {
    if (PerformNonStandardReturn<kMonitorState>(self,
                                                shadow_frame,
                                                ctx->result,
                                                instrumentation,
                                                Accessor().InsSize(),
                                                inst->GetDexPc(Insns()))) {
      exit_interpreter_loop = true;
      return false;
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool HandlePendingException() {
    DCHECK(self->IsExceptionPending());
    self->AllowThreadSuspension();
    if (!CheckForceReturn()) {
      return false;
    }
    bool skip_event = shadow_frame.GetSkipNextExceptionEvent();
    shadow_frame.SetSkipNextExceptionEvent(false);
    if (!MoveToExceptionHandler(self, shadow_frame, skip_event ? nullptr : instrumentation)) {
      /* Structured locking is to be enforced for abnormal termination, too. */
      DoMonitorCheckOnExit<do_assignability_check>(self, &shadow_frame);
      ctx->result = JValue(); /* Handled in caller. */
      exit_interpreter_loop = true;
      return false;  // Return to caller.
    }
    if (!CheckForceReturn()) {
      return false;
    }
    int32_t displacement =
        static_cast<int32_t>(shadow_frame.GetDexPC()) - static_cast<int32_t>(dex_pc);
    SetNextInstruction(inst->RelativeAt(displacement));
    return true;
  }

  HANDLER_ATTRIBUTES bool PossiblyHandlePendingExceptionOnInvoke(bool is_exception_pending) {
    if (UNLIKELY(shadow_frame.GetForceRetryInstruction())) {
      /* Don't need to do anything except clear the flag and exception. We leave the */
      /* instruction the same so it will be re-executed on the next go-around.       */
      DCHECK(inst->IsInvoke());
      shadow_frame.SetForceRetryInstruction(false);
      if (UNLIKELY(is_exception_pending)) {
        DCHECK(self->IsExceptionPending());
        if (kIsDebugBuild) {
          LOG(WARNING) << "Suppressing exception for instruction-retry: "
                       << self->GetException()->Dump();
        }
        self->ClearException();
      }
      SetNextInstruction(inst);
    } else if (UNLIKELY(is_exception_pending)) {
      /* Should have succeeded. */
      DCHECK(!shadow_frame.GetForceRetryInstruction());
      return false;  // Pending exception.
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool HandleMonitorChecks() {
    if (!DoMonitorCheckOnExit<do_assignability_check>(self, &shadow_frame)) {
      return false;  // Pending exception.
    }
    return true;
  }

  // Code to run before each dex instruction.
  HANDLER_ATTRIBUTES bool Preamble() {
    /* We need to put this before & after the instrumentation to avoid having to put in a */
    /* post-script macro.                                                                 */
    if (!CheckForceReturn()) {
      return false;
    }
    if (UNLIKELY(instrumentation->HasDexPcListeners())) {
      uint8_t opcode = inst->Opcode(inst_data);
      bool is_move_result_object = (opcode == Instruction::MOVE_RESULT_OBJECT);
      JValue* save_ref = is_move_result_object ? &ctx->result_register : nullptr;
      if (UNLIKELY(!DoDexPcMoveEvent(self,
                                     Accessor(),
                                     shadow_frame,
                                     dex_pc,
                                     instrumentation,
                                     save_ref))) {
        DCHECK(self->IsExceptionPending());
        // Do not raise exception event if it is caused by other instrumentation event.
        shadow_frame.SetSkipNextExceptionEvent(true);
        return false;  // Pending exception.
      }
      if (!CheckForceReturn()) {
        return false;
      }
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool BranchInstrumentation(int32_t offset) {
    if (UNLIKELY(instrumentation->HasBranchListeners())) {
      instrumentation->Branch(self, shadow_frame.GetMethod(), dex_pc, offset);
    }
    JValue result;
    if (jit::Jit::MaybeDoOnStackReplacement(self,
                                            shadow_frame.GetMethod(),
                                            dex_pc,
                                            offset,
                                            &result)) {
      ctx->result = result;
      exit_interpreter_loop = true;
      return false;
    }
    return true;
  }

  ALWAYS_INLINE void HotnessUpdate()
      REQUIRES_SHARED(Locks::mutator_lock_) {
    jit::Jit* jit = Runtime::Current()->GetJit();
    if (jit != nullptr) {
      jit->AddSamples(self, shadow_frame.GetMethod(), 1, /*with_backedges=*/ true);
    }
  }

  HANDLER_ATTRIBUTES bool HandleAsyncException() {
    if (UNLIKELY(self->ObserveAsyncException())) {
      return false;  // Pending exception.
    }
    return true;
  }

  ALWAYS_INLINE void HandleBackwardBranch(int32_t offset)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    if (IsBackwardBranch(offset)) {
      HotnessUpdate();
      /* Record new dex pc early to have consistent suspend point at loop header. */
      shadow_frame.SetDexPC(next->GetDexPc(Insns()));
      self->AllowThreadSuspension();
    }
  }

  // Unlike most other events the DexPcMovedEvent can be sent when there is a pending exception (if
  // the next instruction is MOVE_EXCEPTION). This means it needs to be handled carefully to be able
  // to detect exceptions thrown by the DexPcMovedEvent itself. These exceptions could be thrown by
  // jvmti-agents while handling breakpoint or single step events. We had to move this into its own
  // function because it was making ExecuteSwitchImpl have too large a stack.
  NO_INLINE static bool DoDexPcMoveEvent(Thread* self,
                                         const CodeItemDataAccessor& accessor,
                                         const ShadowFrame& shadow_frame,
                                         uint32_t dex_pc,
                                         const instrumentation::Instrumentation* instrumentation,
                                         JValue* save_ref)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(instrumentation->HasDexPcListeners());
    StackHandleScope<2> hs(self);
    Handle<mirror::Throwable> thr(hs.NewHandle(self->GetException()));
    mirror::Object* null_obj = nullptr;
    HandleWrapper<mirror::Object> h(
        hs.NewHandleWrapper(LIKELY(save_ref == nullptr) ? &null_obj : save_ref->GetGCRoot()));
    self->ClearException();
    instrumentation->DexPcMovedEvent(self,
                                     shadow_frame.GetThisObject(accessor.InsSize()),
                                     shadow_frame.GetMethod(),
                                     dex_pc);
    if (UNLIKELY(self->IsExceptionPending())) {
      // We got a new exception in the dex-pc-moved event.
      // We just let this exception replace the old one.
      // TODO It would be good to add the old exception to the
      // suppressed exceptions of the new one if possible.
      return false;  // Pending exception.
    } else {
      if (UNLIKELY(!thr.IsNull())) {
        self->SetException(thr.Get());
      }
      return true;
    }
  }

  HANDLER_ATTRIBUTES bool HandleReturn(JValue result) {
    self->AllowThreadSuspension();
    if (!HandleMonitorChecks()) {
      return false;
    }
    if (UNLIKELY(NeedsMethodExitEvent(instrumentation) &&
                 !SendMethodExitEvents(self,
                                       instrumentation,
                                       shadow_frame,
                                       shadow_frame.GetThisObject(Accessor().InsSize()),
                                       shadow_frame.GetMethod(),
                                       inst->GetDexPc(Insns()),
                                       result))) {
      DCHECK(self->IsExceptionPending());
      // Do not raise exception event if it is caused by other instrumentation event.
      shadow_frame.SetSkipNextExceptionEvent(true);
      return false;  // Pending exception.
    }
    ctx->result = result;
    exit_interpreter_loop = true;
    return false;
  }

  HANDLER_ATTRIBUTES bool HandleGoto(int32_t offset) {
    if (!HandleAsyncException()) {
      return false;
    }
    if (!BranchInstrumentation(offset)) {
      return false;
    }
    SetNextInstruction(inst->RelativeAt(offset));
    HandleBackwardBranch(offset);
    return true;
  }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"

  template<typename T>
  HANDLER_ATTRIBUTES bool HandleCmpl(T val1, T val2) {
    int32_t result;
    if (val1 > val2) {
      result = 1;
    } else if (val1 == val2) {
      result = 0;
    } else {
      result = -1;
    }
    SetVReg(A(), result);
    return true;
  }

  // Returns the same result as the function above. It only differs for NaN values.
  template<typename T>
  HANDLER_ATTRIBUTES bool HandleCmpg(T val1, T val2) {
    int32_t result;
    if (val1 < val2) {
      result = -1;
    } else if (val1 == val2) {
      result = 0;
    } else {
      result = 1;
    }
    SetVReg(A(), result);
    return true;
  }

#pragma clang diagnostic pop

  HANDLER_ATTRIBUTES bool HandleIf(bool cond, int32_t offset) {
    if (cond) {
      if (!BranchInstrumentation(offset)) {
        return false;
      }
      SetNextInstruction(inst->RelativeAt(offset));
      HandleBackwardBranch(offset);
    } else {
      if (!BranchInstrumentation(2)) {
        return false;
      }
    }
    return true;
  }

  template<typename ArrayType, typename SetVRegFn>
  HANDLER_ATTRIBUTES bool HandleAGet(SetVRegFn setVReg) {
    ObjPtr<mirror::Object> a = GetVRegReference(B());
    if (UNLIKELY(a == nullptr)) {
      ThrowNullPointerExceptionFromInterpreter();
      return false;  // Pending exception.
    }
    int32_t index = GetVReg(C());
    ObjPtr<ArrayType> array = ObjPtr<ArrayType>::DownCast(a);
    if (UNLIKELY(!array->CheckIsValidIndex(index))) {
      return false;  // Pending exception.
    } else {
      (this->*setVReg)(A(), array->GetWithoutChecks(index));
    }
    return true;
  }

  template<typename ArrayType, typename T>
  HANDLER_ATTRIBUTES bool HandleAPut(T value) {
    ObjPtr<mirror::Object> a = GetVRegReference(B());
    if (UNLIKELY(a == nullptr)) {
      ThrowNullPointerExceptionFromInterpreter();
      return false;  // Pending exception.
    }
    int32_t index = GetVReg(C());
    ObjPtr<ArrayType> array = ObjPtr<ArrayType>::DownCast(a);
    if (UNLIKELY(!array->CheckIsValidIndex(index))) {
      return false;  // Pending exception.
    } else {
      if (transaction_active && !CheckWriteConstraint(self, array)) {
        return false;
      }
      array->template SetWithoutChecks<transaction_active>(index, value);
    }
    return true;
  }

  template<FindFieldType find_type, Primitive::Type field_type>
  HANDLER_ATTRIBUTES bool HandleGet() {
    return DoFieldGet<find_type, field_type, do_access_check, transaction_active>(
        self, shadow_frame, inst, inst_data);
  }

  template<Primitive::Type field_type>
  HANDLER_ATTRIBUTES bool HandleGetQuick() {
    return DoIGetQuick<field_type>(shadow_frame, inst, inst_data);
  }

  template<FindFieldType find_type, Primitive::Type field_type>
  HANDLER_ATTRIBUTES bool HandlePut() {
    return DoFieldPut<find_type, field_type, do_access_check, transaction_active>(
        self, shadow_frame, inst, inst_data);
  }

  template<Primitive::Type field_type>
  HANDLER_ATTRIBUTES bool HandlePutQuick() {
    return DoIPutQuick<field_type, transaction_active>(
        shadow_frame, inst, inst_data);
  }

  template<InvokeType type, bool is_range, bool is_quick = false>
  HANDLER_ATTRIBUTES bool HandleInvoke() {
    bool success = DoInvoke<type, is_range, do_access_check, /*is_mterp=*/ false, is_quick>(
        self, shadow_frame, inst, inst_data, ResultRegister());
    return PossiblyHandlePendingExceptionOnInvoke(!success);
  }

  HANDLER_ATTRIBUTES bool HandleUnused() {
    UnexpectedOpcode(inst, shadow_frame);
    return true;
  }

  HANDLER_ATTRIBUTES bool NOP() {
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE() {
    SetVReg(A(), GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_FROM16() {
    SetVReg(A(), GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_16() {
    SetVReg(A(), GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_WIDE() {
    SetVRegLong(A(), GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_WIDE_FROM16() {
    SetVRegLong(A(), GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_WIDE_16() {
    SetVRegLong(A(), GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_OBJECT() {
    SetVRegReference(A(), GetVRegReference(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_OBJECT_FROM16() {
    SetVRegReference(A(), GetVRegReference(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_OBJECT_16() {
    SetVRegReference(A(), GetVRegReference(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_RESULT() {
    SetVReg(A(), ResultRegister()->GetI());
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_RESULT_WIDE() {
    SetVRegLong(A(), ResultRegister()->GetJ());
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_RESULT_OBJECT() {
    SetVRegReference(A(), ResultRegister()->GetL());
    return true;
  }

  HANDLER_ATTRIBUTES bool MOVE_EXCEPTION() {
    ObjPtr<mirror::Throwable> exception = self->GetException();
    DCHECK(exception != nullptr) << "No pending exception on MOVE_EXCEPTION instruction";
    SetVRegReference(A(), exception);
    self->ClearException();
    return true;
  }

  HANDLER_ATTRIBUTES bool RETURN_VOID_NO_BARRIER() {
    JValue result;
    return HandleReturn(result);
  }

  HANDLER_ATTRIBUTES bool RETURN_VOID() {
    QuasiAtomic::ThreadFenceForConstructor();
    JValue result;
    return HandleReturn(result);
  }

  HANDLER_ATTRIBUTES bool RETURN() {
    JValue result;
    result.SetJ(0);
    result.SetI(GetVReg(A()));
    return HandleReturn(result);
  }

  HANDLER_ATTRIBUTES bool RETURN_WIDE() {
    JValue result;
    result.SetJ(GetVRegLong(A()));
    return HandleReturn(result);
  }

  HANDLER_ATTRIBUTES bool RETURN_OBJECT() {
    JValue result;
    self->AllowThreadSuspension();
    if (!HandleMonitorChecks()) {
      return false;
    }
    const size_t ref_idx = A();
    ObjPtr<mirror::Object> obj_result = GetVRegReference(ref_idx);
    if (do_assignability_check && obj_result != nullptr) {
      ObjPtr<mirror::Class> return_type = shadow_frame.GetMethod()->ResolveReturnType();
      // Re-load since it might have moved.
      obj_result = GetVRegReference(ref_idx);
      if (return_type == nullptr) {
        // Return the pending exception.
        return false;  // Pending exception.
      }
      if (!obj_result->VerifierInstanceOf(return_type)) {
        CHECK_LE(Runtime::Current()->GetTargetSdkVersion(), 29u);
        // This should never happen.
        std::string temp1, temp2;
        self->ThrowNewExceptionF("Ljava/lang/InternalError;",
                                 "Returning '%s' that is not instance of return type '%s'",
                                 obj_result->GetClass()->GetDescriptor(&temp1),
                                 return_type->GetDescriptor(&temp2));
        return false;  // Pending exception.
      }
    }
    StackHandleScope<1> hs(self);
    MutableHandle<mirror::Object> h_result(hs.NewHandle(obj_result));
    result.SetL(obj_result);
    if (UNLIKELY(NeedsMethodExitEvent(instrumentation) &&
                 !SendMethodExitEvents(self,
                                       instrumentation,
                                       shadow_frame,
                                       shadow_frame.GetThisObject(Accessor().InsSize()),
                                       shadow_frame.GetMethod(),
                                       inst->GetDexPc(Insns()),
                                       h_result))) {
      DCHECK(self->IsExceptionPending());
      // Do not raise exception event if it is caused by other instrumentation event.
      shadow_frame.SetSkipNextExceptionEvent(true);
      return false;  // Pending exception.
    }
    // Re-load since it might have moved or been replaced during the MethodExitEvent.
    result.SetL(h_result.Get());
    ctx->result = result;
    exit_interpreter_loop = true;
    return false;
  }

  HANDLER_ATTRIBUTES bool CONST_4() {
    uint4_t dst = inst->VRegA_11n(inst_data);
    int4_t val = inst->VRegB_11n(inst_data);
    SetVReg(dst, val);
    if (val == 0) {
      SetVRegReference(dst, nullptr);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_16() {
    uint8_t dst = A();
    int16_t val = B();
    SetVReg(dst, val);
    if (val == 0) {
      SetVRegReference(dst, nullptr);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST() {
    uint8_t dst = A();
    int32_t val = B();
    SetVReg(dst, val);
    if (val == 0) {
      SetVRegReference(dst, nullptr);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_HIGH16() {
    uint8_t dst = A();
    int32_t val = static_cast<int32_t>(B() << 16);
    SetVReg(dst, val);
    if (val == 0) {
      SetVRegReference(dst, nullptr);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_WIDE_16() {
    SetVRegLong(A(), B());
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_WIDE_32() {
    SetVRegLong(A(), B());
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_WIDE() {
    SetVRegLong(A(), inst->WideVRegB());
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_WIDE_HIGH16() {
    SetVRegLong(A(), static_cast<uint64_t>(B()) << 48);
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_STRING() {
    ObjPtr<mirror::String> s = ResolveString(self, shadow_frame, dex::StringIndex(B()));
    if (UNLIKELY(s == nullptr)) {
      return false;  // Pending exception.
    } else {
      SetVRegReference(A(), s);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_STRING_JUMBO() {
    ObjPtr<mirror::String> s = ResolveString(self, shadow_frame, dex::StringIndex(B()));
    if (UNLIKELY(s == nullptr)) {
      return false;  // Pending exception.
    } else {
      SetVRegReference(A(), s);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_CLASS() {
    ObjPtr<mirror::Class> c = ResolveVerifyAndClinit(dex::TypeIndex(B()),
                                                     shadow_frame.GetMethod(),
                                                     self,
                                                     false,
                                                     do_access_check);
    if (UNLIKELY(c == nullptr)) {
      return false;  // Pending exception.
    } else {
      SetVRegReference(A(), c);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_METHOD_HANDLE() {
    ClassLinker* cl = Runtime::Current()->GetClassLinker();
    ObjPtr<mirror::MethodHandle> mh = cl->ResolveMethodHandle(self,
                                                              B(),
                                                              shadow_frame.GetMethod());
    if (UNLIKELY(mh == nullptr)) {
      return false;  // Pending exception.
    } else {
      SetVRegReference(A(), mh);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool CONST_METHOD_TYPE() {
    ClassLinker* cl = Runtime::Current()->GetClassLinker();
    ObjPtr<mirror::MethodType> mt = cl->ResolveMethodType(self,
                                                          dex::ProtoIndex(B()),
                                                          shadow_frame.GetMethod());
    if (UNLIKELY(mt == nullptr)) {
      return false;  // Pending exception.
    } else {
      SetVRegReference(A(), mt);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool MONITOR_ENTER() {
    if (!HandleAsyncException()) {
      return false;
    }
    ObjPtr<mirror::Object> obj = GetVRegReference(A());
    if (UNLIKELY(obj == nullptr)) {
      ThrowNullPointerExceptionFromInterpreter();
      return false;  // Pending exception.
    } else {
      DoMonitorEnter<do_assignability_check>(self, &shadow_frame, obj);
      return !self->IsExceptionPending();
    }
  }

  HANDLER_ATTRIBUTES bool MONITOR_EXIT() {
    if (!HandleAsyncException()) {
      return false;
    }
    ObjPtr<mirror::Object> obj = GetVRegReference(A());
    if (UNLIKELY(obj == nullptr)) {
      ThrowNullPointerExceptionFromInterpreter();
      return false;  // Pending exception.
    } else {
      DoMonitorExit<do_assignability_check>(self, &shadow_frame, obj);
      return !self->IsExceptionPending();
    }
  }

  HANDLER_ATTRIBUTES bool CHECK_CAST() {
    ObjPtr<mirror::Class> c = ResolveVerifyAndClinit(dex::TypeIndex(B()),
                                                     shadow_frame.GetMethod(),
                                                     self,
                                                     false,
                                                     do_access_check);
    if (UNLIKELY(c == nullptr)) {
      return false;  // Pending exception.
    } else {
      ObjPtr<mirror::Object> obj = GetVRegReference(A());
      if (UNLIKELY(obj != nullptr && !obj->InstanceOf(c))) {
        ThrowClassCastException(c, obj->GetClass());
        return false;  // Pending exception.
      }
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool INSTANCE_OF() {
    ObjPtr<mirror::Class> c = ResolveVerifyAndClinit(dex::TypeIndex(C()),
                                                     shadow_frame.GetMethod(),
                                                     self,
                                                     false,
                                                     do_access_check);
    if (UNLIKELY(c == nullptr)) {
      return false;  // Pending exception.
    } else {
      ObjPtr<mirror::Object> obj = GetVRegReference(B());
      SetVReg(A(), (obj != nullptr && obj->InstanceOf(c)) ? 1 : 0);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool ARRAY_LENGTH() {
    ObjPtr<mirror::Object> array = GetVRegReference(B());
    if (UNLIKELY(array == nullptr)) {
      ThrowNullPointerExceptionFromInterpreter();
      return false;  // Pending exception.
    } else {
      SetVReg(A(), array->AsArray()->GetLength());
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool NEW_INSTANCE() {
    ObjPtr<mirror::Object> obj = nullptr;
    ObjPtr<mirror::Class> c = ResolveVerifyAndClinit(dex::TypeIndex(B()),
                                                     shadow_frame.GetMethod(),
                                                     self,
                                                     false,
                                                     do_access_check);
    if (LIKELY(c != nullptr)) {
      // Don't allow finalizable objects to be allocated during a transaction since these can't
      // be finalized without a started runtime.
      if (transaction_active && c->IsFinalizable()) {
        AbortTransactionF(self,
                          "Allocating finalizable object in transaction: %s",
                          c->PrettyDescriptor().c_str());
        return false;  // Pending exception.
      }
      gc::AllocatorType allocator_type = Runtime::Current()->GetHeap()->GetCurrentAllocator();
      if (UNLIKELY(c->IsStringClass())) {
        obj = mirror::String::AllocEmptyString(self, allocator_type);
      } else {
        obj = AllocObjectFromCode(c, self, allocator_type);
      }
    }
    if (UNLIKELY(obj == nullptr)) {
      return false;  // Pending exception.
    } else {
      obj->GetClass()->AssertInitializedOrInitializingInThread(self);
      SetVRegReference(A(), obj);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool NEW_ARRAY() {
    int32_t length = GetVReg(B());
    ObjPtr<mirror::Object> obj = AllocArrayFromCode<do_access_check>(
        dex::TypeIndex(C()),
        length,
        shadow_frame.GetMethod(),
        self,
        Runtime::Current()->GetHeap()->GetCurrentAllocator());
    if (UNLIKELY(obj == nullptr)) {
      return false;  // Pending exception.
    } else {
      SetVRegReference(A(), obj);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool FILLED_NEW_ARRAY() {
    return DoFilledNewArray<false, do_access_check, transaction_active>(
        inst, shadow_frame, self, ResultRegister());
  }

  HANDLER_ATTRIBUTES bool FILLED_NEW_ARRAY_RANGE() {
    return DoFilledNewArray<true, do_access_check, transaction_active>(
        inst, shadow_frame, self, ResultRegister());
  }

  HANDLER_ATTRIBUTES bool FILL_ARRAY_DATA() {
    const uint16_t* payload_addr = reinterpret_cast<const uint16_t*>(inst) + B();
    const Instruction::ArrayDataPayload* payload =
        reinterpret_cast<const Instruction::ArrayDataPayload*>(payload_addr);
    ObjPtr<mirror::Object> obj = GetVRegReference(A());
    if (!FillArrayData(obj, payload)) {
      return false;  // Pending exception.
    }
    if (transaction_active) {
      RecordArrayElementsInTransaction(obj->AsArray(), payload->element_count);
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool THROW() {
    if (!HandleAsyncException()) {
      return false;
    }
    ObjPtr<mirror::Object> exception = GetVRegReference(A());
    if (UNLIKELY(exception == nullptr)) {
      ThrowNullPointerException();
    } else if (do_assignability_check && !exception->GetClass()->IsThrowableClass()) {
      // This should never happen.
      std::string temp;
      self->ThrowNewExceptionF("Ljava/lang/InternalError;",
                               "Throwing '%s' that is not instance of Throwable",
                               exception->GetClass()->GetDescriptor(&temp));
    } else {
      self->SetException(exception->AsThrowable());
    }
    return false;  // Pending exception.
  }

  HANDLER_ATTRIBUTES bool GOTO() {
    return HandleGoto(A());
  }

  HANDLER_ATTRIBUTES bool GOTO_16() {
    return HandleGoto(A());
  }

  HANDLER_ATTRIBUTES bool GOTO_32() {
    return HandleGoto(A());
  }

  HANDLER_ATTRIBUTES bool PACKED_SWITCH() {
    int32_t offset = DoPackedSwitch(inst, shadow_frame, inst_data);
    if (!BranchInstrumentation(offset)) {
      return false;
    }
    SetNextInstruction(inst->RelativeAt(offset));
    HandleBackwardBranch(offset);
    return true;
  }

  HANDLER_ATTRIBUTES bool SPARSE_SWITCH() {
    int32_t offset = DoSparseSwitch(inst, shadow_frame, inst_data);
    if (!BranchInstrumentation(offset)) {
      return false;
    }
    SetNextInstruction(inst->RelativeAt(offset));
    HandleBackwardBranch(offset);
    return true;
  }

  HANDLER_ATTRIBUTES bool CMPL_FLOAT() {
    return HandleCmpl<float>(GetVRegFloat(B()), GetVRegFloat(C()));
  }

  HANDLER_ATTRIBUTES bool CMPG_FLOAT() {
    return HandleCmpg<float>(GetVRegFloat(B()), GetVRegFloat(C()));
  }

  HANDLER_ATTRIBUTES bool CMPL_DOUBLE() {
    return HandleCmpl<double>(GetVRegDouble(B()), GetVRegDouble(C()));
  }

  HANDLER_ATTRIBUTES bool CMPG_DOUBLE() {
    return HandleCmpg<double>(GetVRegDouble(B()), GetVRegDouble(C()));
  }

  HANDLER_ATTRIBUTES bool CMP_LONG() {
    return HandleCmpl<int64_t>(GetVRegLong(B()), GetVRegLong(C()));
  }

  HANDLER_ATTRIBUTES bool IF_EQ() {
    return HandleIf(GetVReg(A()) == GetVReg(B()), C());
  }

  HANDLER_ATTRIBUTES bool IF_NE() {
    return HandleIf(GetVReg(A()) != GetVReg(B()), C());
  }

  HANDLER_ATTRIBUTES bool IF_LT() {
    return HandleIf(GetVReg(A()) < GetVReg(B()), C());
  }

  HANDLER_ATTRIBUTES bool IF_GE() {
    return HandleIf(GetVReg(A()) >= GetVReg(B()), C());
  }

  HANDLER_ATTRIBUTES bool IF_GT() {
    return HandleIf(GetVReg(A()) > GetVReg(B()), C());
  }

  HANDLER_ATTRIBUTES bool IF_LE() {
    return HandleIf(GetVReg(A()) <= GetVReg(B()), C());
  }

  HANDLER_ATTRIBUTES bool IF_EQZ() {
    return HandleIf(GetVReg(A()) == 0, B());
  }

  HANDLER_ATTRIBUTES bool IF_NEZ() {
    return HandleIf(GetVReg(A()) != 0, B());
  }

  HANDLER_ATTRIBUTES bool IF_LTZ() {
    return HandleIf(GetVReg(A()) < 0, B());
  }

  HANDLER_ATTRIBUTES bool IF_GEZ() {
    return HandleIf(GetVReg(A()) >= 0, B());
  }

  HANDLER_ATTRIBUTES bool IF_GTZ() {
    return HandleIf(GetVReg(A()) > 0, B());
  }

  HANDLER_ATTRIBUTES bool IF_LEZ() {
    return HandleIf(GetVReg(A()) <= 0, B());
  }

  HANDLER_ATTRIBUTES bool AGET_BOOLEAN() {
    return HandleAGet<mirror::BooleanArray>(&InstructionHandler::SetVReg);
  }

  HANDLER_ATTRIBUTES bool AGET_BYTE() {
    return HandleAGet<mirror::ByteArray>(&InstructionHandler::SetVReg);
  }

  HANDLER_ATTRIBUTES bool AGET_CHAR() {
    return HandleAGet<mirror::CharArray>(&InstructionHandler::SetVReg);
  }

  HANDLER_ATTRIBUTES bool AGET_SHORT() {
    return HandleAGet<mirror::ShortArray>(&InstructionHandler::SetVReg);
  }

  HANDLER_ATTRIBUTES bool AGET() {
    return HandleAGet<mirror::IntArray>(&InstructionHandler::SetVReg);
  }

  HANDLER_ATTRIBUTES bool AGET_WIDE() {
    return HandleAGet<mirror::LongArray>(&InstructionHandler::SetVRegLong);
  }

  HANDLER_ATTRIBUTES bool AGET_OBJECT() {
    return HandleAGet<mirror::ObjectArray<mirror::Object>>(&InstructionHandler::SetVRegReference);
  }

  HANDLER_ATTRIBUTES bool APUT_BOOLEAN() {
    return HandleAPut<mirror::BooleanArray>(GetVReg(A()));
  }

  HANDLER_ATTRIBUTES bool APUT_BYTE() {
    return HandleAPut<mirror::ByteArray>(GetVReg(A()));
  }

  HANDLER_ATTRIBUTES bool APUT_CHAR() {
    return HandleAPut<mirror::CharArray>(GetVReg(A()));
  }

  HANDLER_ATTRIBUTES bool APUT_SHORT() {
    return HandleAPut<mirror::ShortArray>(GetVReg(A()));
  }

  HANDLER_ATTRIBUTES bool APUT() {
    return HandleAPut<mirror::IntArray>(GetVReg(A()));
  }

  HANDLER_ATTRIBUTES bool APUT_WIDE() {
    return HandleAPut<mirror::LongArray>(GetVRegLong(A()));
  }

  HANDLER_ATTRIBUTES bool APUT_OBJECT() {
    ObjPtr<mirror::Object> a = GetVRegReference(B());
    if (UNLIKELY(a == nullptr)) {
      ThrowNullPointerExceptionFromInterpreter();
      return false;  // Pending exception.
    }
    int32_t index = GetVReg(C());
    ObjPtr<mirror::Object> val = GetVRegReference(A());
    ObjPtr<mirror::ObjectArray<mirror::Object>> array = a->AsObjectArray<mirror::Object>();
    if (array->CheckIsValidIndex(index) && array->CheckAssignable(val)) {
      if (transaction_active &&
          (!CheckWriteConstraint(self, array) || !CheckWriteValueConstraint(self, val))) {
        return false;
      }
      array->SetWithoutChecks<transaction_active>(index, val);
    } else {
      return false;  // Pending exception.
    }
    return true;
  }

  HANDLER_ATTRIBUTES bool IGET_BOOLEAN() {
    return HandleGet<InstancePrimitiveRead, Primitive::kPrimBoolean>();
  }

  HANDLER_ATTRIBUTES bool IGET_BYTE() {
    return HandleGet<InstancePrimitiveRead, Primitive::kPrimByte>();
  }

  HANDLER_ATTRIBUTES bool IGET_CHAR() {
    return HandleGet<InstancePrimitiveRead, Primitive::kPrimChar>();
  }

  HANDLER_ATTRIBUTES bool IGET_SHORT() {
    return HandleGet<InstancePrimitiveRead, Primitive::kPrimShort>();
  }

  HANDLER_ATTRIBUTES bool IGET() {
    return HandleGet<InstancePrimitiveRead, Primitive::kPrimInt>();
  }

  HANDLER_ATTRIBUTES bool IGET_WIDE() {
    return HandleGet<InstancePrimitiveRead, Primitive::kPrimLong>();
  }

  HANDLER_ATTRIBUTES bool IGET_OBJECT() {
    return HandleGet<InstanceObjectRead, Primitive::kPrimNot>();
  }

  HANDLER_ATTRIBUTES bool IGET_QUICK() {
    return HandleGetQuick<Primitive::kPrimInt>();
  }

  HANDLER_ATTRIBUTES bool IGET_WIDE_QUICK() {
    return HandleGetQuick<Primitive::kPrimLong>();
  }

  HANDLER_ATTRIBUTES bool IGET_OBJECT_QUICK() {
    return HandleGetQuick<Primitive::kPrimNot>();
  }

  HANDLER_ATTRIBUTES bool IGET_BOOLEAN_QUICK() {
    return HandleGetQuick<Primitive::kPrimBoolean>();
  }

  HANDLER_ATTRIBUTES bool IGET_BYTE_QUICK() {
    return HandleGetQuick<Primitive::kPrimByte>();
  }

  HANDLER_ATTRIBUTES bool IGET_CHAR_QUICK() {
    return HandleGetQuick<Primitive::kPrimChar>();
  }

  HANDLER_ATTRIBUTES bool IGET_SHORT_QUICK() {
    return HandleGetQuick<Primitive::kPrimShort>();
  }

  HANDLER_ATTRIBUTES bool SGET_BOOLEAN() {
    return HandleGet<StaticPrimitiveRead, Primitive::kPrimBoolean>();
  }

  HANDLER_ATTRIBUTES bool SGET_BYTE() {
    return HandleGet<StaticPrimitiveRead, Primitive::kPrimByte>();
  }

  HANDLER_ATTRIBUTES bool SGET_CHAR() {
    return HandleGet<StaticPrimitiveRead, Primitive::kPrimChar>();
  }

  HANDLER_ATTRIBUTES bool SGET_SHORT() {
    return HandleGet<StaticPrimitiveRead, Primitive::kPrimShort>();
  }

  HANDLER_ATTRIBUTES bool SGET() {
    return HandleGet<StaticPrimitiveRead, Primitive::kPrimInt>();
  }

  HANDLER_ATTRIBUTES bool SGET_WIDE() {
    return HandleGet<StaticPrimitiveRead, Primitive::kPrimLong>();
  }

  HANDLER_ATTRIBUTES bool SGET_OBJECT() {
    return HandleGet<StaticObjectRead, Primitive::kPrimNot>();
  }

  HANDLER_ATTRIBUTES bool IPUT_BOOLEAN() {
    return HandlePut<InstancePrimitiveWrite, Primitive::kPrimBoolean>();
  }

  HANDLER_ATTRIBUTES bool IPUT_BYTE() {
    return HandlePut<InstancePrimitiveWrite, Primitive::kPrimByte>();
  }

  HANDLER_ATTRIBUTES bool IPUT_CHAR() {
    return HandlePut<InstancePrimitiveWrite, Primitive::kPrimChar>();
  }

  HANDLER_ATTRIBUTES bool IPUT_SHORT() {
    return HandlePut<InstancePrimitiveWrite, Primitive::kPrimShort>();
  }

  HANDLER_ATTRIBUTES bool IPUT() {
    return HandlePut<InstancePrimitiveWrite, Primitive::kPrimInt>();
  }

  HANDLER_ATTRIBUTES bool IPUT_WIDE() {
    return HandlePut<InstancePrimitiveWrite, Primitive::kPrimLong>();
  }

  HANDLER_ATTRIBUTES bool IPUT_OBJECT() {
    return HandlePut<InstanceObjectWrite, Primitive::kPrimNot>();
  }

  HANDLER_ATTRIBUTES bool IPUT_QUICK() {
    return HandlePutQuick<Primitive::kPrimInt>();
  }

  HANDLER_ATTRIBUTES bool IPUT_BOOLEAN_QUICK() {
    return HandlePutQuick<Primitive::kPrimBoolean>();
  }

  HANDLER_ATTRIBUTES bool IPUT_BYTE_QUICK() {
    return HandlePutQuick<Primitive::kPrimByte>();
  }

  HANDLER_ATTRIBUTES bool IPUT_CHAR_QUICK() {
    return HandlePutQuick<Primitive::kPrimChar>();
  }

  HANDLER_ATTRIBUTES bool IPUT_SHORT_QUICK() {
    return HandlePutQuick<Primitive::kPrimShort>();
  }

  HANDLER_ATTRIBUTES bool IPUT_WIDE_QUICK() {
    return HandlePutQuick<Primitive::kPrimLong>();
  }

  HANDLER_ATTRIBUTES bool IPUT_OBJECT_QUICK() {
    return HandlePutQuick<Primitive::kPrimNot>();
  }

  HANDLER_ATTRIBUTES bool SPUT_BOOLEAN() {
    return HandlePut<StaticPrimitiveWrite, Primitive::kPrimBoolean>();
  }

  HANDLER_ATTRIBUTES bool SPUT_BYTE() {
    return HandlePut<StaticPrimitiveWrite, Primitive::kPrimByte>();
  }

  HANDLER_ATTRIBUTES bool SPUT_CHAR() {
    return HandlePut<StaticPrimitiveWrite, Primitive::kPrimChar>();
  }

  HANDLER_ATTRIBUTES bool SPUT_SHORT() {
    return HandlePut<StaticPrimitiveWrite, Primitive::kPrimShort>();
  }

  HANDLER_ATTRIBUTES bool SPUT() {
    return HandlePut<StaticPrimitiveWrite, Primitive::kPrimInt>();
  }

  HANDLER_ATTRIBUTES bool SPUT_WIDE() {
    return HandlePut<StaticPrimitiveWrite, Primitive::kPrimLong>();
  }

  HANDLER_ATTRIBUTES bool SPUT_OBJECT() {
    return HandlePut<StaticObjectWrite, Primitive::kPrimNot>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_VIRTUAL() {
    return HandleInvoke<kVirtual, /*is_range=*/ false>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_VIRTUAL_RANGE() {
    return HandleInvoke<kVirtual, /*is_range=*/ true>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_SUPER() {
    return HandleInvoke<kSuper, /*is_range=*/ false>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_SUPER_RANGE() {
    return HandleInvoke<kSuper, /*is_range=*/ true>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_DIRECT() {
    return HandleInvoke<kDirect, /*is_range=*/ false>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_DIRECT_RANGE() {
    return HandleInvoke<kDirect, /*is_range=*/ true>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_INTERFACE() {
    return HandleInvoke<kInterface, /*is_range=*/ false>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_INTERFACE_RANGE() {
    return HandleInvoke<kInterface, /*is_range=*/ true>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_STATIC() {
    return HandleInvoke<kStatic, /*is_range=*/ false>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_STATIC_RANGE() {
    return HandleInvoke<kStatic, /*is_range=*/ true>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_VIRTUAL_QUICK() {
    return HandleInvoke<kVirtual, /*is_range=*/ false, /*is_quick=*/ true>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_VIRTUAL_RANGE_QUICK() {
    return HandleInvoke<kVirtual, /*is_range=*/ true, /*is_quick=*/ true>();
  }

  HANDLER_ATTRIBUTES bool INVOKE_POLYMORPHIC() {
    DCHECK(Runtime::Current()->IsMethodHandlesEnabled());
    bool success = DoInvokePolymorphic</* is_range= */ false>(
        self, shadow_frame, inst, inst_data, ResultRegister());
    return PossiblyHandlePendingExceptionOnInvoke(!success);
  }

  HANDLER_ATTRIBUTES bool INVOKE_POLYMORPHIC_RANGE() {
    DCHECK(Runtime::Current()->IsMethodHandlesEnabled());
    bool success = DoInvokePolymorphic</* is_range= */ true>(
        self, shadow_frame, inst, inst_data, ResultRegister());
    return PossiblyHandlePendingExceptionOnInvoke(!success);
  }

  HANDLER_ATTRIBUTES bool INVOKE_CUSTOM() {
    DCHECK(Runtime::Current()->IsMethodHandlesEnabled());
    bool success = DoInvokeCustom</* is_range= */ false>(
        self, shadow_frame, inst, inst_data, ResultRegister());
    return PossiblyHandlePendingExceptionOnInvoke(!success);
  }

  HANDLER_ATTRIBUTES bool INVOKE_CUSTOM_RANGE() {
    DCHECK(Runtime::Current()->IsMethodHandlesEnabled());
    bool success = DoInvokeCustom</* is_range= */ true>(
        self, shadow_frame, inst, inst_data, ResultRegister());
    return PossiblyHandlePendingExceptionOnInvoke(!success);
  }

  HANDLER_ATTRIBUTES bool NEG_INT() {
    SetVReg(A(), -GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool NOT_INT() {
    SetVReg(A(), ~GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool NEG_LONG() {
    SetVRegLong(A(), -GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool NOT_LONG() {
    SetVRegLong(A(), ~GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool NEG_FLOAT() {
    SetVRegFloat(A(), -GetVRegFloat(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool NEG_DOUBLE() {
    SetVRegDouble(A(), -GetVRegDouble(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool INT_TO_LONG() {
    SetVRegLong(A(), GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool INT_TO_FLOAT() {
    SetVRegFloat(A(), GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool INT_TO_DOUBLE() {
    SetVRegDouble(A(), GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool LONG_TO_INT() {
    SetVReg(A(), GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool LONG_TO_FLOAT() {
    SetVRegFloat(A(), GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool LONG_TO_DOUBLE() {
    SetVRegDouble(A(), GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool FLOAT_TO_INT() {
    float val = GetVRegFloat(B());
    int32_t result = art_float_to_integral<int32_t, float>(val);
    SetVReg(A(), result);
    return true;
  }

  HANDLER_ATTRIBUTES bool FLOAT_TO_LONG() {
    float val = GetVRegFloat(B());
    int64_t result = art_float_to_integral<int64_t, float>(val);
    SetVRegLong(A(), result);
    return true;
  }

  HANDLER_ATTRIBUTES bool FLOAT_TO_DOUBLE() {
    SetVRegDouble(A(), GetVRegFloat(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool DOUBLE_TO_INT() {
    double val = GetVRegDouble(B());
    int32_t result = art_float_to_integral<int32_t, double>(val);
    SetVReg(A(), result);
    return true;
  }

  HANDLER_ATTRIBUTES bool DOUBLE_TO_LONG() {
    double val = GetVRegDouble(B());
    int64_t result = art_float_to_integral<int64_t, double>(val);
    SetVRegLong(A(), result);
    return true;
  }

  HANDLER_ATTRIBUTES bool DOUBLE_TO_FLOAT() {
    SetVRegFloat(A(), GetVRegDouble(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool INT_TO_BYTE() {
    SetVReg(A(), static_cast<int8_t>(GetVReg(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool INT_TO_CHAR() {
    SetVReg(A(), static_cast<uint16_t>(GetVReg(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool INT_TO_SHORT() {
    SetVReg(A(), static_cast<int16_t>(GetVReg(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool ADD_INT() {
    SetVReg(A(), SafeAdd(GetVReg(B()), GetVReg(C())));
    return true;
  }

  HANDLER_ATTRIBUTES bool SUB_INT() {
    SetVReg(A(), SafeSub(GetVReg(B()), GetVReg(C())));
    return true;
  }

  HANDLER_ATTRIBUTES bool MUL_INT() {
    SetVReg(A(), SafeMul(GetVReg(B()), GetVReg(C())));
    return true;
  }

  HANDLER_ATTRIBUTES bool DIV_INT() {
    return DoIntDivide(shadow_frame, A(), GetVReg(B()), GetVReg(C()));
  }

  HANDLER_ATTRIBUTES bool REM_INT() {
    return DoIntRemainder(shadow_frame, A(), GetVReg(B()), GetVReg(C()));
  }

  HANDLER_ATTRIBUTES bool SHL_INT() {
    SetVReg(A(), GetVReg(B()) << (GetVReg(C()) & 0x1f));
    return true;
  }

  HANDLER_ATTRIBUTES bool SHR_INT() {
    SetVReg(A(), GetVReg(B()) >> (GetVReg(C()) & 0x1f));
    return true;
  }

  HANDLER_ATTRIBUTES bool USHR_INT() {
    SetVReg(A(), static_cast<uint32_t>(GetVReg(B())) >> (GetVReg(C()) & 0x1f));
    return true;
  }

  HANDLER_ATTRIBUTES bool AND_INT() {
    SetVReg(A(), GetVReg(B()) & GetVReg(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool OR_INT() {
    SetVReg(A(), GetVReg(B()) | GetVReg(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool XOR_INT() {
    SetVReg(A(), GetVReg(B()) ^ GetVReg(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool ADD_LONG() {
    SetVRegLong(A(), SafeAdd(GetVRegLong(B()), GetVRegLong(C())));
    return true;
  }

  HANDLER_ATTRIBUTES bool SUB_LONG() {
    SetVRegLong(A(), SafeSub(GetVRegLong(B()), GetVRegLong(C())));
    return true;
  }

  HANDLER_ATTRIBUTES bool MUL_LONG() {
    SetVRegLong(A(), SafeMul(GetVRegLong(B()), GetVRegLong(C())));
    return true;
  }

  HANDLER_ATTRIBUTES bool DIV_LONG() {
    return DoLongDivide(shadow_frame, A(), GetVRegLong(B()), GetVRegLong(C()));
  }

  HANDLER_ATTRIBUTES bool REM_LONG() {
    return DoLongRemainder(shadow_frame, A(), GetVRegLong(B()), GetVRegLong(C()));
  }

  HANDLER_ATTRIBUTES bool AND_LONG() {
    SetVRegLong(A(), GetVRegLong(B()) & GetVRegLong(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool OR_LONG() {
    SetVRegLong(A(), GetVRegLong(B()) | GetVRegLong(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool XOR_LONG() {
    SetVRegLong(A(), GetVRegLong(B()) ^ GetVRegLong(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool SHL_LONG() {
    SetVRegLong(A(), GetVRegLong(B()) << (GetVReg(C()) & 0x3f));
    return true;
  }

  HANDLER_ATTRIBUTES bool SHR_LONG() {
    SetVRegLong(A(), GetVRegLong(B()) >> (GetVReg(C()) & 0x3f));
    return true;
  }

  HANDLER_ATTRIBUTES bool USHR_LONG() {
    SetVRegLong(A(), static_cast<uint64_t>(GetVRegLong(B())) >> (GetVReg(C()) & 0x3f));
    return true;
  }

  HANDLER_ATTRIBUTES bool ADD_FLOAT() {
    SetVRegFloat(A(), GetVRegFloat(B()) + GetVRegFloat(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool SUB_FLOAT() {
    SetVRegFloat(A(), GetVRegFloat(B()) - GetVRegFloat(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MUL_FLOAT() {
    SetVRegFloat(A(), GetVRegFloat(B()) * GetVRegFloat(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool DIV_FLOAT() {
    SetVRegFloat(A(), GetVRegFloat(B()) / GetVRegFloat(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool REM_FLOAT() {
    SetVRegFloat(A(), fmodf(GetVRegFloat(B()), GetVRegFloat(C())));
    return true;
  }

  HANDLER_ATTRIBUTES bool ADD_DOUBLE() {
    SetVRegDouble(A(), GetVRegDouble(B()) + GetVRegDouble(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool SUB_DOUBLE() {
    SetVRegDouble(A(), GetVRegDouble(B()) - GetVRegDouble(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MUL_DOUBLE() {
    SetVRegDouble(A(), GetVRegDouble(B()) * GetVRegDouble(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool DIV_DOUBLE() {
    SetVRegDouble(A(), GetVRegDouble(B()) / GetVRegDouble(C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool REM_DOUBLE() {
    SetVRegDouble(A(), fmod(GetVRegDouble(B()), GetVRegDouble(C())));
    return true;
  }

  HANDLER_ATTRIBUTES bool ADD_INT_2ADDR() {
    uint4_t vregA = A();
    SetVReg(vregA, SafeAdd(GetVReg(vregA), GetVReg(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool SUB_INT_2ADDR() {
    uint4_t vregA = A();
    SetVReg(vregA, SafeSub(GetVReg(vregA), GetVReg(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool MUL_INT_2ADDR() {
    uint4_t vregA = A();
    SetVReg(vregA, SafeMul(GetVReg(vregA), GetVReg(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool DIV_INT_2ADDR() {
    uint4_t vregA = A();
    return DoIntDivide(shadow_frame, vregA, GetVReg(vregA), GetVReg(B()));
  }

  HANDLER_ATTRIBUTES bool REM_INT_2ADDR() {
    uint4_t vregA = A();
    return DoIntRemainder(shadow_frame, vregA, GetVReg(vregA), GetVReg(B()));
  }

  HANDLER_ATTRIBUTES bool SHL_INT_2ADDR() {
    uint4_t vregA = A();
    SetVReg(vregA, GetVReg(vregA) << (GetVReg(B()) & 0x1f));
    return true;
  }

  HANDLER_ATTRIBUTES bool SHR_INT_2ADDR() {
    uint4_t vregA = A();
    SetVReg(vregA, GetVReg(vregA) >> (GetVReg(B()) & 0x1f));
    return true;
  }

  HANDLER_ATTRIBUTES bool USHR_INT_2ADDR() {
    uint4_t vregA = A();
    SetVReg(vregA, static_cast<uint32_t>(GetVReg(vregA)) >> (GetVReg(B()) & 0x1f));
    return true;
  }

  HANDLER_ATTRIBUTES bool AND_INT_2ADDR() {
    uint4_t vregA = A();
    SetVReg(vregA, GetVReg(vregA) & GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool OR_INT_2ADDR() {
    uint4_t vregA = A();
    SetVReg(vregA, GetVReg(vregA) | GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool XOR_INT_2ADDR() {
    uint4_t vregA = A();
    SetVReg(vregA, GetVReg(vregA) ^ GetVReg(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool ADD_LONG_2ADDR() {
    uint4_t vregA = A();
    SetVRegLong(vregA, SafeAdd(GetVRegLong(vregA), GetVRegLong(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool SUB_LONG_2ADDR() {
    uint4_t vregA = A();
    SetVRegLong(vregA, SafeSub(GetVRegLong(vregA), GetVRegLong(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool MUL_LONG_2ADDR() {
    uint4_t vregA = A();
    SetVRegLong(vregA, SafeMul(GetVRegLong(vregA), GetVRegLong(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool DIV_LONG_2ADDR() {
    uint4_t vregA = A();
    return DoLongDivide(shadow_frame, vregA, GetVRegLong(vregA), GetVRegLong(B()));
  }

  HANDLER_ATTRIBUTES bool REM_LONG_2ADDR() {
    uint4_t vregA = A();
    return DoLongRemainder(shadow_frame, vregA, GetVRegLong(vregA), GetVRegLong(B()));
  }

  HANDLER_ATTRIBUTES bool AND_LONG_2ADDR() {
    uint4_t vregA = A();
    SetVRegLong(vregA, GetVRegLong(vregA) & GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool OR_LONG_2ADDR() {
    uint4_t vregA = A();
    SetVRegLong(vregA, GetVRegLong(vregA) | GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool XOR_LONG_2ADDR() {
    uint4_t vregA = A();
    SetVRegLong(vregA, GetVRegLong(vregA) ^ GetVRegLong(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool SHL_LONG_2ADDR() {
    uint4_t vregA = A();
    SetVRegLong(vregA, GetVRegLong(vregA) << (GetVReg(B()) & 0x3f));
    return true;
  }

  HANDLER_ATTRIBUTES bool SHR_LONG_2ADDR() {
    uint4_t vregA = A();
    SetVRegLong(vregA, GetVRegLong(vregA) >> (GetVReg(B()) & 0x3f));
    return true;
  }

  HANDLER_ATTRIBUTES bool USHR_LONG_2ADDR() {
    uint4_t vregA = A();
    SetVRegLong(vregA, static_cast<uint64_t>(GetVRegLong(vregA)) >> (GetVReg(B()) & 0x3f));
    return true;
  }

  HANDLER_ATTRIBUTES bool ADD_FLOAT_2ADDR() {
    uint4_t vregA = A();
    SetVRegFloat(vregA, GetVRegFloat(vregA) + GetVRegFloat(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool SUB_FLOAT_2ADDR() {
    uint4_t vregA = A();
    SetVRegFloat(vregA, GetVRegFloat(vregA) - GetVRegFloat(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MUL_FLOAT_2ADDR() {
    uint4_t vregA = A();
    SetVRegFloat(vregA, GetVRegFloat(vregA) * GetVRegFloat(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool DIV_FLOAT_2ADDR() {
    uint4_t vregA = A();
    SetVRegFloat(vregA, GetVRegFloat(vregA) / GetVRegFloat(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool REM_FLOAT_2ADDR() {
    uint4_t vregA = A();
    SetVRegFloat(vregA, fmodf(GetVRegFloat(vregA), GetVRegFloat(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool ADD_DOUBLE_2ADDR() {
    uint4_t vregA = A();
    SetVRegDouble(vregA, GetVRegDouble(vregA) + GetVRegDouble(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool SUB_DOUBLE_2ADDR() {
    uint4_t vregA = A();
    SetVRegDouble(vregA, GetVRegDouble(vregA) - GetVRegDouble(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool MUL_DOUBLE_2ADDR() {
    uint4_t vregA = A();
    SetVRegDouble(vregA, GetVRegDouble(vregA) * GetVRegDouble(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool DIV_DOUBLE_2ADDR() {
    uint4_t vregA = A();
    SetVRegDouble(vregA, GetVRegDouble(vregA) / GetVRegDouble(B()));
    return true;
  }

  HANDLER_ATTRIBUTES bool REM_DOUBLE_2ADDR() {
    uint4_t vregA = A();
    SetVRegDouble(vregA, fmod(GetVRegDouble(vregA), GetVRegDouble(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool ADD_INT_LIT16() {
    SetVReg(A(), SafeAdd(GetVReg(B()), C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool RSUB_INT() {
    SetVReg(A(), SafeSub(C(), GetVReg(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool MUL_INT_LIT16() {
    SetVReg(A(), SafeMul(GetVReg(B()), C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool DIV_INT_LIT16() {
    return DoIntDivide(shadow_frame, A(), GetVReg(B()), C());
  }

  HANDLER_ATTRIBUTES bool REM_INT_LIT16() {
    return DoIntRemainder(shadow_frame, A(), GetVReg(B()), C());
  }

  HANDLER_ATTRIBUTES bool AND_INT_LIT16() {
    SetVReg(A(), GetVReg(B()) & C());
    return true;
  }

  HANDLER_ATTRIBUTES bool OR_INT_LIT16() {
    SetVReg(A(), GetVReg(B()) | C());
    return true;
  }

  HANDLER_ATTRIBUTES bool XOR_INT_LIT16() {
    SetVReg(A(), GetVReg(B()) ^ C());
    return true;
  }

  HANDLER_ATTRIBUTES bool ADD_INT_LIT8() {
    SetVReg(A(), SafeAdd(GetVReg(B()), C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool RSUB_INT_LIT8() {
    SetVReg(A(), SafeSub(C(), GetVReg(B())));
    return true;
  }

  HANDLER_ATTRIBUTES bool MUL_INT_LIT8() {
    SetVReg(A(), SafeMul(GetVReg(B()), C()));
    return true;
  }

  HANDLER_ATTRIBUTES bool DIV_INT_LIT8() {
    return DoIntDivide(shadow_frame, A(), GetVReg(B()), C());
  }

  HANDLER_ATTRIBUTES bool REM_INT_LIT8() {
    return DoIntRemainder(shadow_frame, A(), GetVReg(B()), C());
  }

  HANDLER_ATTRIBUTES bool AND_INT_LIT8() {
    SetVReg(A(), GetVReg(B()) & C());
    return true;
  }

  HANDLER_ATTRIBUTES bool OR_INT_LIT8() {
    SetVReg(A(), GetVReg(B()) | C());
    return true;
  }

  HANDLER_ATTRIBUTES bool XOR_INT_LIT8() {
    SetVReg(A(), GetVReg(B()) ^ C());
    return true;
  }

  HANDLER_ATTRIBUTES bool SHL_INT_LIT8() {
    SetVReg(A(), GetVReg(B()) << (C() & 0x1f));
    return true;
  }

  HANDLER_ATTRIBUTES bool SHR_INT_LIT8() {
    SetVReg(A(), GetVReg(B()) >> (C() & 0x1f));
    return true;
  }

  HANDLER_ATTRIBUTES bool USHR_INT_LIT8() {
    SetVReg(A(), static_cast<uint32_t>(GetVReg(B())) >> (C() & 0x1f));
    return true;
  }

  HANDLER_ATTRIBUTES bool UNUSED_3E() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_3F() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_40() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_41() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_42() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_43() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_79() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_7A() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_F3() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_F4() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_F5() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_F6() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_F7() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_F8() {
    return HandleUnused();
  }

  HANDLER_ATTRIBUTES bool UNUSED_F9() {
    return HandleUnused();
  }

  ALWAYS_INLINE InstructionHandler(SwitchImplContext* ctx,
                                   const instrumentation::Instrumentation* instrumentation,
                                   Thread* self,
                                   ShadowFrame& shadow_frame,
                                   uint16_t dex_pc,
                                   const Instruction* inst,
                                   uint16_t inst_data,
                                   const Instruction*& next,
                                   bool& exit_interpreter_loop)
    : ctx(ctx),
      instrumentation(instrumentation),
      self(self),
      shadow_frame(shadow_frame),
      dex_pc(dex_pc),
      inst(inst),
      inst_data(inst_data),
      next(next),
      exit_interpreter_loop(exit_interpreter_loop) {
  }

 private:
  static constexpr bool do_assignability_check = do_access_check;
  static constexpr MonitorState kMonitorState =
      do_assignability_check ? MonitorState::kCountingMonitors : MonitorState::kNormalMonitors;

  const CodeItemDataAccessor& Accessor() { return ctx->accessor; }
  const uint16_t* Insns() { return ctx->accessor.Insns(); }
  JValue* ResultRegister() { return &ctx->result_register; }

  ALWAYS_INLINE int32_t A() { return inst->VRegA(kFormat, inst_data); }
  ALWAYS_INLINE int32_t B() { return inst->VRegB(kFormat, inst_data); }
  ALWAYS_INLINE int32_t C() { return inst->VRegC(kFormat); }

  int32_t GetVReg(size_t i) const { return shadow_frame.GetVReg(i); }
  int64_t GetVRegLong(size_t i) const { return shadow_frame.GetVRegLong(i); }
  float GetVRegFloat(size_t i) const { return shadow_frame.GetVRegFloat(i); }
  double GetVRegDouble(size_t i) const { return shadow_frame.GetVRegDouble(i); }
  ObjPtr<mirror::Object> GetVRegReference(size_t i) const REQUIRES_SHARED(Locks::mutator_lock_) {
    return shadow_frame.GetVRegReference(i);
  }

  void SetVReg(size_t i, int32_t val) { shadow_frame.SetVReg(i, val); }
  void SetVRegLong(size_t i, int64_t val) { shadow_frame.SetVRegLong(i, val); }
  void SetVRegFloat(size_t i, float val) { shadow_frame.SetVRegFloat(i, val); }
  void SetVRegDouble(size_t i, double val) { shadow_frame.SetVRegDouble(i, val); }
  void SetVRegReference(size_t i, ObjPtr<mirror::Object> val)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    shadow_frame.SetVRegReference(i, val);
  }

  // Set the next instruction to be executed.  It is the 'fall-through' instruction by default.
  ALWAYS_INLINE void SetNextInstruction(const Instruction* next_inst) {
    DCHECK_LT(next_inst->GetDexPc(Insns()), Accessor().InsnsSizeInCodeUnits());
    next = next_inst;
  }

  SwitchImplContext* const ctx;
  const instrumentation::Instrumentation* const instrumentation;
  Thread* const self;
  ShadowFrame& shadow_frame;
  uint32_t const dex_pc;
  const Instruction* const inst;
  uint16_t const inst_data;
  const Instruction*& next;

  bool& exit_interpreter_loop;
};

// Don't inline in ASAN. It would create massive stack frame.
#if defined(ADDRESS_SANITIZER) || defined(HWADDRESS_SANITIZER)
#define ASAN_NO_INLINE NO_INLINE
#else
#define ASAN_NO_INLINE ALWAYS_INLINE
#endif

#define OPCODE_CASE(OPCODE, OPCODE_NAME, NAME, FORMAT, i, a, e, v)                                \
template<bool do_access_check, bool transaction_active>                                           \
ASAN_NO_INLINE static bool OP_##OPCODE_NAME(                                                      \
    SwitchImplContext* ctx,                                                                       \
    const instrumentation::Instrumentation* instrumentation,                                      \
    Thread* self,                                                                                 \
    ShadowFrame& shadow_frame,                                                                    \
    uint16_t dex_pc,                                                                              \
    const Instruction* inst,                                                                      \
    uint16_t inst_data,                                                                           \
    const Instruction*& next,                                                                     \
    bool& exit) REQUIRES_SHARED(Locks::mutator_lock_) {                                           \
  InstructionHandler<do_access_check, transaction_active, Instruction::FORMAT> handler(           \
      ctx, instrumentation, self, shadow_frame, dex_pc, inst, inst_data, next, exit);             \
  return LIKELY(handler.OPCODE_NAME());                                                           \
}
DEX_INSTRUCTION_LIST(OPCODE_CASE)
#undef OPCODE_CASE

template<bool do_access_check, bool transaction_active>
void ExecuteSwitchImplCpp(SwitchImplContext* ctx) {
  Thread* self = ctx->self;
  const CodeItemDataAccessor& accessor = ctx->accessor;
  ShadowFrame& shadow_frame = ctx->shadow_frame;
  self->VerifyStack();

  uint32_t dex_pc = shadow_frame.GetDexPC();
  const auto* const instrumentation = Runtime::Current()->GetInstrumentation();
  const uint16_t* const insns = accessor.Insns();
  const Instruction* next = Instruction::At(insns + dex_pc);

  DCHECK(!shadow_frame.GetForceRetryInstruction())
      << "Entered interpreter from invoke without retry instruction being handled!";

  bool const interpret_one_instruction = ctx->interpret_one_instruction;
  while (true) {
    const Instruction* const inst = next;
    dex_pc = inst->GetDexPc(insns);
    shadow_frame.SetDexPC(dex_pc);
    TraceExecution(shadow_frame, inst, dex_pc);
    uint16_t inst_data = inst->Fetch16(0);
    bool exit = false;
    bool success;  // Moved outside to keep frames small under asan.
    if (InstructionHandler<do_access_check, transaction_active, Instruction::kInvalidFormat>(
            ctx, instrumentation, self, shadow_frame, dex_pc, inst, inst_data, next, exit).
            Preamble()) {
      DCHECK_EQ(self->IsExceptionPending(), inst->Opcode(inst_data) == Instruction::MOVE_EXCEPTION);
      switch (inst->Opcode(inst_data)) {
#define OPCODE_CASE(OPCODE, OPCODE_NAME, NAME, FORMAT, i, a, e, v)                                \
        case OPCODE: {                                                                            \
          next = inst->RelativeAt(Instruction::SizeInCodeUnits(Instruction::FORMAT));             \
          success = OP_##OPCODE_NAME<do_access_check, transaction_active>(                        \
              ctx, instrumentation, self, shadow_frame, dex_pc, inst, inst_data, next, exit);     \
          if (success && LIKELY(!interpret_one_instruction)) {                                    \
            continue;                                                                             \
          }                                                                                       \
          if (exit) {                                                                             \
            shadow_frame.SetDexPC(dex::kDexNoIndex);                                              \
            return;                                                                               \
          }                                                                                       \
          break;                                                                                  \
        }
  DEX_INSTRUCTION_LIST(OPCODE_CASE)
#undef OPCODE_CASE
      }
    } else {
      // Preamble returned false due to debugger event.
      if (exit) {
        shadow_frame.SetDexPC(dex::kDexNoIndex);
        return;  // Return statement or debugger forced exit.
      }
    }
    if (self->IsExceptionPending()) {
      if (!InstructionHandler<do_access_check, transaction_active, Instruction::kInvalidFormat>(
              ctx, instrumentation, self, shadow_frame, dex_pc, inst, inst_data, next, exit).
              HandlePendingException()) {
        shadow_frame.SetDexPC(dex::kDexNoIndex);
        return;  // Locally unhandled exception - return to caller.
      }
      // Continue execution in the catch block.
    }
    if (interpret_one_instruction) {
      shadow_frame.SetDexPC(next->GetDexPc(insns));  // Record where we stopped.
      ctx->result = ctx->result_register;
      return;
    }
  }
}  // NOLINT(readability/fn_size)

}  // namespace interpreter
}  // namespace art

#endif  // ART_RUNTIME_INTERPRETER_INTERPRETER_SWITCH_IMPL_INL_H_
