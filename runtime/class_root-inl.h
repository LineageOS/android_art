/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef ART_RUNTIME_CLASS_ROOT_INL_H_
#define ART_RUNTIME_CLASS_ROOT_INL_H_

#include "class_root.h"

#include "class_linker-inl.h"
#include "mirror/class.h"
#include "mirror/object_array-inl.h"
#include "obj_ptr-inl.h"
#include "runtime.h"

namespace art {

template <ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::Class> GetClassRoot(ClassRoot class_root,
                                          ObjPtr<mirror::ObjectArray<mirror::Class>> class_roots) {
  DCHECK(class_roots != nullptr);
  if (kReadBarrierOption == kWithReadBarrier) {
    // With read barrier all references must point to the to-space.
    // Without read barrier, this check could fail.
    DCHECK_EQ(class_roots, Runtime::Current()->GetClassLinker()->GetClassRoots());
  }
  DCHECK_LT(static_cast<uint32_t>(class_root), static_cast<uint32_t>(ClassRoot::kMax));
  int32_t index = static_cast<int32_t>(class_root);
  ObjPtr<mirror::Class> klass =
      class_roots->GetWithoutChecks<kDefaultVerifyFlags, kReadBarrierOption>(index);
  DCHECK(klass != nullptr);
  return klass;
}

template <ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::Class> GetClassRoot(ClassRoot class_root, ClassLinker* linker)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetClassRoot<kReadBarrierOption>(class_root, linker->GetClassRoots<kReadBarrierOption>());
}

template <ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::Class> GetClassRoot(ClassRoot class_root)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetClassRoot<kReadBarrierOption>(class_root, Runtime::Current()->GetClassLinker());
}

namespace detail {

class ClassNotFoundExceptionTag;
template <class Tag> struct NoMirrorType;

template <class MirrorType>
struct ClassRootSelector;  // No definition for unspecialized ClassRoot selector.

#define SPECIALIZE_CLASS_ROOT_SELECTOR(name, descriptor, mirror_type) \
  template <>                                                         \
  struct ClassRootSelector<mirror_type> {                             \
    static constexpr ClassRoot value = ClassRoot::name;               \
  };

CLASS_ROOT_LIST(SPECIALIZE_CLASS_ROOT_SELECTOR)

#undef SPECIALIZE_CLASS_ROOT_SELECTOR

}  // namespace detail

template <class MirrorType, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::Class> GetClassRoot(ObjPtr<mirror::ObjectArray<mirror::Class>> class_roots)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetClassRoot<kReadBarrierOption>(detail::ClassRootSelector<MirrorType>::value,
                                          class_roots);
}

template <class MirrorType, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::Class> GetClassRoot(ClassLinker* linker)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetClassRoot<kReadBarrierOption>(detail::ClassRootSelector<MirrorType>::value, linker);
}

template <class MirrorType, ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::Class> GetClassRoot() REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetClassRoot<kReadBarrierOption>(detail::ClassRootSelector<MirrorType>::value);
}

}  // namespace art

#endif  // ART_RUNTIME_CLASS_ROOT_INL_H_
