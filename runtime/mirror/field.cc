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

#include "field-inl.h"

#include "class-inl.h"
#include "dex_cache-inl.h"
#include "object-inl.h"
#include "object_array-inl.h"
#include "write_barrier.h"

namespace art {
namespace mirror {

void Field::VisitTarget(ReflectiveValueVisitor* v) {
  HeapReflectiveSourceInfo hrsi(kSourceJavaLangReflectField, this);
  ArtField* orig = GetArtField();
  ArtField* new_value = v->VisitField(orig, hrsi);
  if (orig != new_value) {
    SetOffset<false>(new_value->GetOffset().Int32Value());
    SetDeclaringClass<false>(new_value->GetDeclaringClass());
    auto new_range =
        IsStatic() ? GetDeclaringClass()->GetSFields() : GetDeclaringClass()->GetIFields();
    auto position = std::find_if(
        new_range.begin(), new_range.end(), [&](const auto& f) { return &f == new_value; });
    DCHECK(position != new_range.end());
    SetArtFieldIndex<false>(std::distance(new_range.begin(), position));
    WriteBarrier::ForEveryFieldWrite(this);
  }
  DCHECK_EQ(new_value, GetArtField());
}

ArtField* Field::GetArtField() {
  ObjPtr<mirror::Class> declaring_class = GetDeclaringClass();
  if (IsStatic()) {
    DCHECK_LT(GetArtFieldIndex(), declaring_class->NumStaticFields());
    return declaring_class->GetStaticField(GetArtFieldIndex());
  } else {
    DCHECK_LT(GetArtFieldIndex(), declaring_class->NumInstanceFields());
    return declaring_class->GetInstanceField(GetArtFieldIndex());
  }
}

ObjPtr<mirror::Field> Field::CreateFromArtField(Thread* self,
                                                ArtField* field,
                                                bool force_resolve) {
  StackHandleScope<2> hs(self);
  // Try to resolve type before allocating since this is a thread suspension point.
  Handle<mirror::Class> type = hs.NewHandle(field->ResolveType());

  if (type == nullptr) {
    DCHECK(self->IsExceptionPending());
    if (force_resolve) {
      return nullptr;
    } else {
      // Can't resolve, clear the exception if it isn't OOME and continue with a null type.
      mirror::Throwable* exception = self->GetException();
      if (exception->GetClass()->DescriptorEquals("Ljava/lang/OutOfMemoryError;")) {
        return nullptr;
      }
      self->ClearException();
    }
  }
  auto ret = hs.NewHandle(ObjPtr<Field>::DownCast(GetClassRoot<Field>()->AllocObject(self)));
  if (UNLIKELY(ret == nullptr)) {
    self->AssertPendingOOMException();
    return nullptr;
  }
  // We're initializing a newly allocated object, so we do not need to record that under
  // a transaction. If the transaction is aborted, the whole object shall be unreachable.
  ret->SetType</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(type.Get());
  ret->SetDeclaringClass</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      field->GetDeclaringClass());
  ret->SetAccessFlags</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      field->GetAccessFlags());
  auto iter_range = field->IsStatic() ? field->GetDeclaringClass()->GetSFields()
                                      : field->GetDeclaringClass()->GetIFields();
  auto position = std::find_if(
      iter_range.begin(), iter_range.end(), [&](const auto& f) { return &f == field; });
  DCHECK(position != iter_range.end());
  ret->SetArtFieldIndex</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      std::distance(iter_range.begin(), position));
  ret->SetOffset</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      field->GetOffset().Int32Value());
  return ret.Get();
}

}  // namespace mirror
}  // namespace art
