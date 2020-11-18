/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "java_lang_ref_Reference.h"

#include "nativehelper/jni_macros.h"

#include "gc/heap.h"
#include "gc/reference_processor.h"
#include "jni/jni_internal.h"
#include "mirror/object-inl.h"
#include "mirror/reference-inl.h"
#include "native_util.h"
#include "scoped_fast_native_object_access-inl.h"

namespace art {

static jobject Reference_getReferent(JNIEnv* env, jobject javaThis) {
  ScopedFastNativeObjectAccess soa(env);
  const ObjPtr<mirror::Reference> ref = soa.Decode<mirror::Reference>(javaThis);
  const ObjPtr<mirror::Object> referent =
      Runtime::Current()->GetHeap()->GetReferenceProcessor()->GetReferent(soa.Self(), ref);
  return soa.AddLocalReference<jobject>(referent);
}

static jboolean Reference_refersTo0(JNIEnv* env, jobject javaThis, jobject o) {
  if (kUseReadBarrier && !kUseBakerReadBarrier) {
    // Fall back to naive implementation that may block and needlessly preserve javaThis.
    return env->IsSameObject(Reference_getReferent(env, javaThis), o);
  }
  ScopedFastNativeObjectAccess soa(env);
  const ObjPtr<mirror::Reference> ref = soa.Decode<mirror::Reference>(javaThis);
  const ObjPtr<mirror::Object> other = soa.Decode<mirror::Reference>(o);
  const ObjPtr<mirror::Object> referent = ref->template GetReferent<kWithoutReadBarrier>();
  if (referent == other) {
      return JNI_TRUE;
  }
  if (!kUseReadBarrier || referent.IsNull() || other.IsNull()) {
    return JNI_FALSE;
  }
  // Explicitly handle the case in which referent is a from-space pointer.  Don't use a
  // read-barrier, since that could easily mark an object we no longer need and, since it
  // creates new gray objects, may not be safe without blocking.
  //
  // ConcurrentCopying::Copy ensure that whenever a pointer to a to_space object is published,
  // the forwarding pointer is also visible. We need that guarantee to ensure that if referent
  // == other and referent is in from-space, then referent has a forwarding pointer. In order to
  // use that guarantee, we need to ensure that the forwarding pointer is loaded after we
  // retrieved other. Hence this fence:
  atomic_thread_fence(std::memory_order_acquire);
  // Note: On ARM, the above could be replaced by an asm fake-dependency hack to make
  // referent appear to depend on other. That would be faster and uglier.
  return gc::collector::ConcurrentCopying::GetFwdPtrUnchecked(referent.Ptr()) == other.Ptr() ?
      JNI_TRUE : JNI_FALSE;
}

static void Reference_clearReferent(JNIEnv* env, jobject javaThis) {
  ScopedFastNativeObjectAccess soa(env);
  const ObjPtr<mirror::Reference> ref = soa.Decode<mirror::Reference>(javaThis);
  Runtime::Current()->GetHeap()->GetReferenceProcessor()->ClearReferent(ref);
}

static JNINativeMethod gMethods[] = {
  FAST_NATIVE_METHOD(Reference, getReferent, "()Ljava/lang/Object;"),
  FAST_NATIVE_METHOD(Reference, clearReferent, "()V"),
  FAST_NATIVE_METHOD(Reference, refersTo0, "(Ljava/lang/Object;)Z"),
};

void register_java_lang_ref_Reference(JNIEnv* env) {
  REGISTER_NATIVE_METHODS("java/lang/ref/Reference");
}

}  // namespace art
