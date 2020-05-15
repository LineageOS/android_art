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

#ifndef ART_RUNTIME_MIRROR_FIELD_INL_H_
#define ART_RUNTIME_MIRROR_FIELD_INL_H_

#include "field.h"

#include "art_field-inl.h"
#include "class-alloc-inl.h"
#include "class_root-inl.h"
#include "object-inl.h"

namespace art {

namespace mirror {

inline ObjPtr<mirror::Class> Field::GetDeclaringClass() REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetFieldObject<Class>(OFFSET_OF_OBJECT_MEMBER(Field, declaring_class_));
}

inline Primitive::Type Field::GetTypeAsPrimitiveType() {
  return GetType()->GetPrimitiveType();
}

inline ObjPtr<mirror::Class> Field::GetType() {
  return GetFieldObject<mirror::Class>(OFFSET_OF_OBJECT_MEMBER(Field, type_));
}

template<bool kTransactionActive, bool kCheckTransaction>
inline void Field::SetDeclaringClass(ObjPtr<Class> c) {
  SetFieldObject<kTransactionActive, kCheckTransaction>(DeclaringClassOffset(), c);
}

template<bool kTransactionActive, bool kCheckTransaction>
inline void Field::SetType(ObjPtr<Class> type) {
  SetFieldObject<kTransactionActive, kCheckTransaction>(TypeOffset(), type);
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_FIELD_INL_H_
