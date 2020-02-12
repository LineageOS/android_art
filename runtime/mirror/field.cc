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
  DCHECK_LT(GetArtFieldIndex(),
            IsStatic() ? declaring_class->NumStaticFields() : declaring_class->NumInstanceFields());
  if (IsStatic()) {
    return declaring_class->GetStaticField(GetArtFieldIndex());
  } else {
    return declaring_class->GetInstanceField(GetArtFieldIndex());
  }
}

}  // namespace mirror
}  // namespace art
