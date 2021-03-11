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

#include <android-base/logging.h>

#include "art_method-inl.h"
#include "base/casts.h"
#include "entrypoints/entrypoint_utils-inl.h"
#include "indirect_reference_table.h"
#include "mirror/object-inl.h"
#include "thread-inl.h"
#include "verify_object.h"

namespace art {

static_assert(sizeof(IRTSegmentState) == sizeof(uint32_t), "IRTSegmentState size unexpected");
static_assert(std::is_trivial<IRTSegmentState>::value, "IRTSegmentState not trivial");

static inline void GoToRunnableFast(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_);

extern void ReadBarrierJni(mirror::CompressedReference<mirror::Class>* declaring_class,
                           Thread* self ATTRIBUTE_UNUSED) {
  DCHECK(kUseReadBarrier);
  if (kUseBakerReadBarrier) {
    DCHECK(declaring_class->AsMirrorPtr() != nullptr)
        << "The class of a static jni call must not be null";
    // Check the mark bit and return early if it's already marked.
    if (LIKELY(declaring_class->AsMirrorPtr()->GetMarkBit() != 0)) {
      return;
    }
  }
  // Call the read barrier and update the handle.
  mirror::Class* to_ref = ReadBarrier::BarrierForRoot(declaring_class);
  declaring_class->Assign(to_ref);
}

// Called on entry to fast JNI, push a new local reference table only.
extern uint32_t JniMethodFastStart(Thread* self) {
  JNIEnvExt* env = self->GetJniEnv();
  DCHECK(env != nullptr);
  uint32_t saved_local_ref_cookie = bit_cast<uint32_t>(env->GetLocalRefCookie());
  env->SetLocalRefCookie(env->GetLocalsSegmentState());

  if (kIsDebugBuild) {
    ArtMethod* native_method = *self->GetManagedStack()->GetTopQuickFrame();
    CHECK(native_method->IsFastNative()) << native_method->PrettyMethod();
  }

  return saved_local_ref_cookie;
}

// Called on entry to JNI, transition out of Runnable and release share of mutator_lock_.
extern uint32_t JniMethodStart(Thread* self) {
  JNIEnvExt* env = self->GetJniEnv();
  DCHECK(env != nullptr);
  uint32_t saved_local_ref_cookie = bit_cast<uint32_t>(env->GetLocalRefCookie());
  env->SetLocalRefCookie(env->GetLocalsSegmentState());

  if (kIsDebugBuild) {
    ArtMethod* native_method = *self->GetManagedStack()->GetTopQuickFrame();
    CHECK(!native_method->IsFastNative()) << native_method->PrettyMethod();
  }

  // Transition out of runnable.
  self->TransitionFromRunnableToSuspended(kNative);
  return saved_local_ref_cookie;
}

extern uint32_t JniMethodStartSynchronized(jobject to_lock, Thread* self) {
  self->DecodeJObject(to_lock)->MonitorEnter(self);
  return JniMethodStart(self);
}

// TODO: NO_THREAD_SAFETY_ANALYSIS due to different control paths depending on fast JNI.
static void GoToRunnable(Thread* self) NO_THREAD_SAFETY_ANALYSIS {
  if (kIsDebugBuild) {
    ArtMethod* native_method = *self->GetManagedStack()->GetTopQuickFrame();
    CHECK(!native_method->IsFastNative()) << native_method->PrettyMethod();
  }

  self->TransitionFromSuspendedToRunnable();
}

ALWAYS_INLINE static inline void GoToRunnableFast(Thread* self) {
  if (kIsDebugBuild) {
    // Should only enter here if the method is @FastNative.
    ArtMethod* native_method = *self->GetManagedStack()->GetTopQuickFrame();
    CHECK(native_method->IsFastNative()) << native_method->PrettyMethod();
  }

  // When we are in @FastNative, we are already Runnable.
  // Only do a suspend check on the way out of JNI.
  if (UNLIKELY(self->TestAllFlags())) {
    // In fast JNI mode we never transitioned out of runnable. Perform a suspend check if there
    // is a flag raised.
    DCHECK(Locks::mutator_lock_->IsSharedHeld(self));
    self->CheckSuspend();
  }
}

static void PopLocalReferences(uint32_t saved_local_ref_cookie, Thread* self)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  JNIEnvExt* env = self->GetJniEnv();
  if (UNLIKELY(env->IsCheckJniEnabled())) {
    env->CheckNoHeldMonitors();
  }
  env->SetLocalSegmentState(env->GetLocalRefCookie());
  env->SetLocalRefCookie(bit_cast<IRTSegmentState>(saved_local_ref_cookie));
}

// TODO: annotalysis disabled as monitor semantics are maintained in Java code.
static inline void UnlockJniSynchronizedMethod(jobject locked, Thread* self)
    NO_THREAD_SAFETY_ANALYSIS REQUIRES(!Roles::uninterruptible_) {
  // Save any pending exception over monitor exit call.
  ObjPtr<mirror::Throwable> saved_exception = nullptr;
  if (UNLIKELY(self->IsExceptionPending())) {
    saved_exception = self->GetException();
    self->ClearException();
  }
  // Decode locked object and unlock, before popping local references.
  self->DecodeJObject(locked)->MonitorExit(self);
  if (UNLIKELY(self->IsExceptionPending())) {
    LOG(FATAL) << "Synchronized JNI code returning with an exception:\n"
        << saved_exception->Dump()
        << "\nEncountered second exception during implicit MonitorExit:\n"
        << self->GetException()->Dump();
  }
  // Restore pending exception.
  if (saved_exception != nullptr) {
    self->SetException(saved_exception);
  }
}

// TODO: These should probably be templatized or macro-ized.
// Otherwise there's just too much repetitive boilerplate.

extern void JniMethodEnd(uint32_t saved_local_ref_cookie, Thread* self) {
  GoToRunnable(self);
  PopLocalReferences(saved_local_ref_cookie, self);
}

extern void JniMethodFastEnd(uint32_t saved_local_ref_cookie, Thread* self) {
  GoToRunnableFast(self);
  PopLocalReferences(saved_local_ref_cookie, self);
}

extern void JniMethodEndSynchronized(uint32_t saved_local_ref_cookie,
                                     jobject locked,
                                     Thread* self) {
  GoToRunnable(self);
  UnlockJniSynchronizedMethod(locked, self);  // Must decode before pop.
  PopLocalReferences(saved_local_ref_cookie, self);
}

// Common result handling for EndWithReference.
static mirror::Object* JniMethodEndWithReferenceHandleResult(jobject result,
                                                             uint32_t saved_local_ref_cookie,
                                                             Thread* self)
    NO_THREAD_SAFETY_ANALYSIS {
  // Must decode before pop. The 'result' may not be valid in case of an exception, though.
  ObjPtr<mirror::Object> o;
  if (!self->IsExceptionPending()) {
    o = self->DecodeJObject(result);
  }
  PopLocalReferences(saved_local_ref_cookie, self);
  // Process result.
  if (UNLIKELY(self->GetJniEnv()->IsCheckJniEnabled())) {
    // CheckReferenceResult can resolve types.
    StackHandleScope<1> hs(self);
    HandleWrapperObjPtr<mirror::Object> h_obj(hs.NewHandleWrapper(&o));
    CheckReferenceResult(h_obj, self);
  }
  VerifyObject(o);
  return o.Ptr();
}

extern mirror::Object* JniMethodFastEndWithReference(jobject result,
                                                     uint32_t saved_local_ref_cookie,
                                                     Thread* self) {
  GoToRunnableFast(self);
  return JniMethodEndWithReferenceHandleResult(result, saved_local_ref_cookie, self);
}

extern mirror::Object* JniMethodEndWithReference(jobject result,
                                                 uint32_t saved_local_ref_cookie,
                                                 Thread* self) {
  GoToRunnable(self);
  return JniMethodEndWithReferenceHandleResult(result, saved_local_ref_cookie, self);
}

extern mirror::Object* JniMethodEndWithReferenceSynchronized(jobject result,
                                                             uint32_t saved_local_ref_cookie,
                                                             jobject locked,
                                                             Thread* self) {
  GoToRunnable(self);
  UnlockJniSynchronizedMethod(locked, self);
  return JniMethodEndWithReferenceHandleResult(result, saved_local_ref_cookie, self);
}

extern uint64_t GenericJniMethodEnd(Thread* self,
                                    uint32_t saved_local_ref_cookie,
                                    jvalue result,
                                    uint64_t result_f,
                                    ArtMethod* called)
    // TODO: NO_THREAD_SAFETY_ANALYSIS as GoToRunnable() is NO_THREAD_SAFETY_ANALYSIS
    NO_THREAD_SAFETY_ANALYSIS {
  bool critical_native = called->IsCriticalNative();
  bool fast_native = called->IsFastNative();
  bool normal_native = !critical_native && !fast_native;

  // @CriticalNative does not do a state transition. @FastNative usually does not do a state
  // transition either but it performs a suspend check that may do state transitions.
  if (LIKELY(normal_native)) {
    GoToRunnable(self);
  } else if (fast_native) {
    GoToRunnableFast(self);
  }
  // We need the mutator lock (i.e., calling GoToRunnable()) before accessing the shorty or the
  // locked object.
  if (called->IsSynchronized()) {
    DCHECK(normal_native) << "@FastNative/@CriticalNative and synchronize is not supported";
    jobject lock = GetGenericJniSynchronizationObject(self, called);
    DCHECK(lock != nullptr);
    UnlockJniSynchronizedMethod(lock, self);
  }
  char return_shorty_char = called->GetShorty()[0];
  if (return_shorty_char == 'L') {
    return reinterpret_cast<uint64_t>(JniMethodEndWithReferenceHandleResult(
        result.l, saved_local_ref_cookie, self));
  } else {
    if (LIKELY(!critical_native)) {
      PopLocalReferences(saved_local_ref_cookie, self);
    }
    switch (return_shorty_char) {
      case 'F': {
        if (kRuntimeISA == InstructionSet::kX86) {
          // Convert back the result to float.
          double d = bit_cast<double, uint64_t>(result_f);
          return bit_cast<uint32_t, float>(static_cast<float>(d));
        } else {
          return result_f;
        }
      }
      case 'D':
        return result_f;
      case 'Z':
        return result.z;
      case 'B':
        return result.b;
      case 'C':
        return result.c;
      case 'S':
        return result.s;
      case 'I':
        return result.i;
      case 'J':
        return result.j;
      case 'V':
        return 0;
      default:
        LOG(FATAL) << "Unexpected return shorty character " << return_shorty_char;
        UNREACHABLE();
    }
  }
}

}  // namespace art
