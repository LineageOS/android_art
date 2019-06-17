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

#include "intrinsic_objects.h"

#include "art_field-inl.h"
#include "base/casts.h"
#include "base/logging.h"
#include "image.h"
#include "obj_ptr-inl.h"

namespace art {

static constexpr size_t kIntrinsicObjectsOffset =
    enum_cast<size_t>(ImageHeader::kIntrinsicObjectsStart);

ObjPtr<mirror::ObjectArray<mirror::Object>> IntrinsicObjects::LookupIntegerCache(
    Thread* self, ClassLinker* class_linker) {
  ObjPtr<mirror::Class> integer_cache_class = class_linker->LookupClass(
      self, "Ljava/lang/Integer$IntegerCache;", /* class_loader= */ nullptr);
  if (integer_cache_class == nullptr || !integer_cache_class->IsInitialized()) {
    return nullptr;
  }
  ArtField* cache_field =
      integer_cache_class->FindDeclaredStaticField("cache", "[Ljava/lang/Integer;");
  CHECK(cache_field != nullptr);
  ObjPtr<mirror::ObjectArray<mirror::Object>> integer_cache =
      ObjPtr<mirror::ObjectArray<mirror::Object>>::DownCast(
          cache_field->GetObject(integer_cache_class));
  CHECK(integer_cache != nullptr);
  return integer_cache;
}

static bool HasIntrinsicObjects(
    ObjPtr<mirror::ObjectArray<mirror::Object>> boot_image_live_objects)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(boot_image_live_objects != nullptr);
  uint32_t length = static_cast<uint32_t>(boot_image_live_objects->GetLength());
  DCHECK_GE(length, kIntrinsicObjectsOffset);
  return length != kIntrinsicObjectsOffset;
}

ObjPtr<mirror::ObjectArray<mirror::Object>> IntrinsicObjects::GetIntegerValueOfCache(
    ObjPtr<mirror::ObjectArray<mirror::Object>> boot_image_live_objects) {
  if (!HasIntrinsicObjects(boot_image_live_objects)) {
    return nullptr;  // No intrinsic objects.
  }
  // No need for read barrier for boot image object or for verifying the value that was just stored.
  ObjPtr<mirror::Object> result =
      boot_image_live_objects->GetWithoutChecks<kVerifyNone, kWithoutReadBarrier>(
          kIntrinsicObjectsOffset);
  DCHECK(result != nullptr);
  DCHECK(result->IsObjectArray());
  DCHECK(result->GetClass()->DescriptorEquals("[Ljava/lang/Integer;"));
  return ObjPtr<mirror::ObjectArray<mirror::Object>>::DownCast(result);
}

ObjPtr<mirror::Object> IntrinsicObjects::GetIntegerValueOfObject(
    ObjPtr<mirror::ObjectArray<mirror::Object>> boot_image_live_objects,
    uint32_t index) {
  DCHECK(HasIntrinsicObjects(boot_image_live_objects));
  DCHECK_LT(index,
            static_cast<uint32_t>(GetIntegerValueOfCache(boot_image_live_objects)->GetLength()));

  // No need for read barrier for boot image object or for verifying the value that was just stored.
  ObjPtr<mirror::Object> result =
      boot_image_live_objects->GetWithoutChecks<kVerifyNone, kWithoutReadBarrier>(
          kIntrinsicObjectsOffset + /* skip the IntegerCache.cache */ 1u + index);
  DCHECK(result != nullptr);
  DCHECK(result->GetClass()->DescriptorEquals("Ljava/lang/Integer;"));
  return result;
}

MemberOffset IntrinsicObjects::GetIntegerValueOfArrayDataOffset(
    ObjPtr<mirror::ObjectArray<mirror::Object>> boot_image_live_objects) {
  DCHECK(HasIntrinsicObjects(boot_image_live_objects));
  MemberOffset result =
      mirror::ObjectArray<mirror::Object>::OffsetOfElement(kIntrinsicObjectsOffset + 1u);
  DCHECK_EQ(GetIntegerValueOfObject(boot_image_live_objects, 0u),
            (boot_image_live_objects
                 ->GetFieldObject<mirror::Object, kVerifyNone, kWithoutReadBarrier>(result)));
  return result;
}

}  // namespace art
