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

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "class_linker.h"
#include "class_root-inl.h"
#include "jni.h"
#include "jni/jni_internal.h"
#include "mirror/class.h"
#include "mirror/method_handle_impl.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-alloc-inl.h"
#include "reflection.h"
#include "reflective_handle.h"
#include "reflective_handle_scope-inl.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-inl.h"

namespace art {
namespace Test1985StructuralRedefineStackScope {

extern "C" JNICALL jobject JNIEXPORT Java_Main_NativeFieldScopeCheck(JNIEnv* env,
                                                                     jclass,
                                                                     jobject field,
                                                                     jobject runnable) {
  jfieldID fid = env->FromReflectedField(field);
  jclass runnable_klass = env->FindClass("java/lang/Runnable");
  jmethodID run = env->GetMethodID(runnable_klass, "run", "()V");
  ScopedObjectAccess soa(Thread::Current());
  StackHandleScope<4> hs(soa.Self());
  StackArtFieldHandleScope<1> fhs(soa.Self());
  StackArtFieldHandleScope<1> bhs(soa.Self());
  ReflectiveHandle<ArtField> rf(fhs.NewHandle(jni::DecodeArtField(fid)));
  ReflectiveHandle<ArtField> bf(bhs.NewHandle(jni::DecodeArtField(fid)));
  ArtField* pre_ptr = rf.Get();
  {
    ScopedThreadSuspension sts(soa.Self(), ThreadState::kNative);
    // Upcall to perform redefinition.
    env->CallVoidMethod(runnable, run);
  }
  Handle<mirror::ObjectArray<mirror::Class>> mt_arr(
      hs.NewHandle(mirror::ObjectArray<mirror::Class>::Alloc(
          soa.Self(),
          Runtime::Current()->GetClassLinker()->FindArrayClass(soa.Self(),
                                                               GetClassRoot<mirror::Class>()),
          0)));
  Handle<mirror::MethodType> mt(hs.NewHandle(mirror::MethodType::Create(
      soa.Self(), hs.NewHandle(GetClassRoot<mirror::Object>()), mt_arr)));
  Handle<mirror::MethodHandleImpl> mhi(hs.NewHandle(
      mirror::MethodHandleImpl::Create(soa.Self(),
                                       reinterpret_cast<uintptr_t>(rf.Get()),
                                       (rf->IsStatic() ? mirror::MethodHandle::Kind::kStaticGet
                                                       : mirror::MethodHandle::Kind::kInstanceGet),
                                       mt)));
  CHECK_EQ(rf.Get(), bf.Get()) << "rf: " << rf->PrettyField() << " bf: " << bf->PrettyField();
  // TODO Modify this to work for when run doesn't cause a change.
  CHECK_NE(pre_ptr, rf.Get()) << "pre_ptr: " << pre_ptr->PrettyField()
                              << " rf: " << rf->PrettyField();
  CHECK_EQ(fid, jni::EncodeArtField(rf));
  return soa.AddLocalReference<jobject>(mhi.Get());
}

}  // namespace Test1985StructuralRedefineStackScope
}  // namespace art
