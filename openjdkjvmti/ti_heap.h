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

#ifndef ART_OPENJDKJVMTI_TI_HEAP_H_
#define ART_OPENJDKJVMTI_TI_HEAP_H_

#include <unordered_map>

#include "jvmti.h"

#include "base/locks.h"

namespace art {
class Thread;
template<typename T> class ObjPtr;
class HashObjPtr;
namespace mirror {
class Object;
}  // namespace mirror
}  // namespace art

namespace openjdkjvmti {

class EventHandler;
class ObjectTagTable;

class HeapUtil {
 public:
  explicit HeapUtil(ObjectTagTable* tags) : tags_(tags) {
  }

  jvmtiError GetLoadedClasses(jvmtiEnv* env, jint* class_count_ptr, jclass** classes_ptr);

  jvmtiError IterateOverInstancesOfClass(jvmtiEnv* env,
                                         jclass klass,
                                         jvmtiHeapObjectFilter filter,
                                         jvmtiHeapObjectCallback cb,
                                         const void* user_data);

  jvmtiError IterateThroughHeap(jvmtiEnv* env,
                                jint heap_filter,
                                jclass klass,
                                const jvmtiHeapCallbacks* callbacks,
                                const void* user_data);

  jvmtiError FollowReferences(jvmtiEnv* env,
                              jint heap_filter,
                              jclass klass,
                              jobject initial_object,
                              const jvmtiHeapCallbacks* callbacks,
                              const void* user_data);

  static jvmtiError ForceGarbageCollection(jvmtiEnv* env);

  ObjectTagTable* GetTags() {
    return tags_;
  }

  static void Register();
  static void Unregister();

 private:
  ObjectTagTable* tags_;
};

class HeapExtensions {
 public:
  static void Register(EventHandler* eh);

  static jvmtiError JNICALL GetObjectHeapId(jvmtiEnv* env, jlong tag, jint* heap_id, ...);
  static jvmtiError JNICALL GetHeapName(jvmtiEnv* env, jint heap_id, char** heap_name, ...);

  static jvmtiError JNICALL IterateThroughHeapExt(jvmtiEnv* env,
                                                  jint heap_filter,
                                                  jclass klass,
                                                  const jvmtiHeapCallbacks* callbacks,
                                                  const void* user_data);

  static jvmtiError JNICALL ChangeArraySize(jvmtiEnv* env, jobject arr, jsize new_size);

  static void ReplaceReferences(
      art::Thread* self,
      const std::unordered_map<art::ObjPtr<art::mirror::Object>,
                               art::ObjPtr<art::mirror::Object>,
                               art::HashObjPtr>& refs)
        REQUIRES(art::Locks::mutator_lock_, art::Roles::uninterruptible_);

  static void ReplaceReference(art::Thread* self,
                               art::ObjPtr<art::mirror::Object> original,
                               art::ObjPtr<art::mirror::Object> replacement)
      REQUIRES(art::Locks::mutator_lock_,
               art::Roles::uninterruptible_);

 private:
  static EventHandler* gEventHandler;
};

}  // namespace openjdkjvmti

#endif  // ART_OPENJDKJVMTI_TI_HEAP_H_
