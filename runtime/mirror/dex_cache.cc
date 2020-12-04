/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "dex_cache-inl.h"

#include "art_method-inl.h"
#include "class_linker.h"
#include "gc/accounting/card_table-inl.h"
#include "gc/heap.h"
#include "linear_alloc.h"
#include "oat_file.h"
#include "object-inl.h"
#include "object.h"
#include "object_array-inl.h"
#include "reflective_value_visitor.h"
#include "runtime.h"
#include "runtime_globals.h"
#include "string.h"
#include "thread.h"
#include "write_barrier.h"

namespace art {
namespace mirror {

template<typename T>
static T* AllocArray(Thread* self, LinearAlloc* alloc, size_t num) {
  if (num == 0) {
    return nullptr;
  }
  return reinterpret_cast<T*>(alloc->AllocAlign16(self, RoundUp(num * sizeof(T), 16)));
}

void DexCache::InitializeNativeFields(const DexFile* dex_file, LinearAlloc* linear_alloc) {
  DCHECK(GetDexFile() == nullptr);
  DCHECK(GetStrings() == nullptr);
  DCHECK(GetResolvedTypes() == nullptr);
  DCHECK(GetResolvedMethods() == nullptr);
  DCHECK(GetResolvedFields() == nullptr);
  DCHECK(GetResolvedMethodTypes() == nullptr);
  DCHECK(GetResolvedCallSites() == nullptr);

  ScopedAssertNoThreadSuspension sants(__FUNCTION__);
  Thread* self = Thread::Current();
  const PointerSize image_pointer_size = kRuntimePointerSize;

  size_t num_strings = std::min<size_t>(kDexCacheStringCacheSize, dex_file->NumStringIds());
  size_t num_types = std::min<size_t>(kDexCacheTypeCacheSize, dex_file->NumTypeIds());
  size_t num_fields = std::min<size_t>(kDexCacheFieldCacheSize, dex_file->NumFieldIds());
  size_t num_methods = std::min<size_t>(kDexCacheMethodCacheSize, dex_file->NumMethodIds());
  size_t num_method_types = std::min<size_t>(kDexCacheMethodTypeCacheSize, dex_file->NumProtoIds());
  size_t num_call_sites = dex_file->NumCallSiteIds();  // Full size.

  static_assert(ArenaAllocator::kAlignment == 8, "Expecting arena alignment of 8.");
  StringDexCacheType* strings =
      AllocArray<StringDexCacheType>(self, linear_alloc, num_strings);
  TypeDexCacheType* types =
      AllocArray<TypeDexCacheType>(self, linear_alloc, num_types);
  MethodDexCacheType* methods =
      AllocArray<MethodDexCacheType>(self, linear_alloc, num_methods);
  FieldDexCacheType* fields =
      AllocArray<FieldDexCacheType>(self, linear_alloc, num_fields);
  MethodTypeDexCacheType* method_types =
      AllocArray<MethodTypeDexCacheType>(self, linear_alloc, num_method_types);
  GcRoot<mirror::CallSite>* call_sites =
      AllocArray<GcRoot<CallSite>>(self, linear_alloc, num_call_sites);

  DCHECK_ALIGNED(types, alignof(StringDexCacheType)) <<
                 "Expected StringsOffset() to align to StringDexCacheType.";
  DCHECK_ALIGNED(strings, alignof(StringDexCacheType)) <<
                 "Expected strings to align to StringDexCacheType.";
  static_assert(alignof(StringDexCacheType) == 8u,
                "Expected StringDexCacheType to have align of 8.");
  if (kIsDebugBuild) {
    // Consistency check to make sure all the dex cache arrays are empty. b/28992179
    for (size_t i = 0; i < num_strings; ++i) {
      CHECK_EQ(strings[i].load(std::memory_order_relaxed).index, 0u);
      CHECK(strings[i].load(std::memory_order_relaxed).object.IsNull());
    }
    for (size_t i = 0; i < num_types; ++i) {
      CHECK_EQ(types[i].load(std::memory_order_relaxed).index, 0u);
      CHECK(types[i].load(std::memory_order_relaxed).object.IsNull());
    }
    for (size_t i = 0; i < num_methods; ++i) {
      CHECK_EQ(GetNativePairPtrSize(methods, i, image_pointer_size).index, 0u);
      CHECK(GetNativePairPtrSize(methods, i, image_pointer_size).object == nullptr);
    }
    for (size_t i = 0; i < num_fields; ++i) {
      CHECK_EQ(GetNativePairPtrSize(fields, i, image_pointer_size).index, 0u);
      CHECK(GetNativePairPtrSize(fields, i, image_pointer_size).object == nullptr);
    }
    for (size_t i = 0; i < num_method_types; ++i) {
      CHECK_EQ(method_types[i].load(std::memory_order_relaxed).index, 0u);
      CHECK(method_types[i].load(std::memory_order_relaxed).object.IsNull());
    }
    for (size_t i = 0; i < dex_file->NumCallSiteIds(); ++i) {
      CHECK(call_sites[i].IsNull());
    }
  }
  if (strings != nullptr) {
    mirror::StringDexCachePair::Initialize(strings);
  }
  if (types != nullptr) {
    mirror::TypeDexCachePair::Initialize(types);
  }
  if (fields != nullptr) {
    mirror::FieldDexCachePair::Initialize(fields, image_pointer_size);
  }
  if (methods != nullptr) {
    mirror::MethodDexCachePair::Initialize(methods, image_pointer_size);
  }
  if (method_types != nullptr) {
    mirror::MethodTypeDexCachePair::Initialize(method_types);
  }
  SetDexFile(dex_file);
  SetNativeArrays(strings,
                  num_strings,
                  types,
                  num_types,
                  methods,
                  num_methods,
                  fields,
                  num_fields,
                  method_types,
                  num_method_types,
                  call_sites,
                  num_call_sites);
}

void DexCache::ResetNativeFields() {
  SetDexFile(nullptr);
  SetNativeArrays(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);
}

void DexCache::VisitReflectiveTargets(ReflectiveValueVisitor* visitor) {
  bool wrote = false;
  for (size_t i = 0; i < NumResolvedFields(); i++) {
    auto pair(GetNativePairPtrSize(GetResolvedFields(), i, kRuntimePointerSize));
    if (pair.index == FieldDexCachePair::InvalidIndexForSlot(i)) {
      continue;
    }
    ArtField* new_val = visitor->VisitField(
        pair.object, DexCacheSourceInfo(kSourceDexCacheResolvedField, pair.index, this));
    if (UNLIKELY(new_val != pair.object)) {
      if (new_val == nullptr) {
        pair = FieldDexCachePair(nullptr, FieldDexCachePair::InvalidIndexForSlot(i));
      } else {
        pair.object = new_val;
      }
      SetNativePairPtrSize(GetResolvedFields(), i, pair, kRuntimePointerSize);
      wrote = true;
    }
  }
  for (size_t i = 0; i < NumResolvedMethods(); i++) {
    auto pair(GetNativePairPtrSize(GetResolvedMethods(), i, kRuntimePointerSize));
    if (pair.index == MethodDexCachePair::InvalidIndexForSlot(i)) {
      continue;
    }
    ArtMethod* new_val = visitor->VisitMethod(
        pair.object, DexCacheSourceInfo(kSourceDexCacheResolvedMethod, pair.index, this));
    if (UNLIKELY(new_val != pair.object)) {
      if (new_val == nullptr) {
        pair = MethodDexCachePair(nullptr, MethodDexCachePair::InvalidIndexForSlot(i));
      } else {
        pair.object = new_val;
      }
      SetNativePairPtrSize(GetResolvedMethods(), i, pair, kRuntimePointerSize);
      wrote = true;
    }
  }
  if (wrote) {
    WriteBarrier::ForEveryFieldWrite(this);
  }
}

bool DexCache::AddPreResolvedStringsArray() {
  DCHECK_EQ(NumPreResolvedStrings(), 0u);
  Thread* const self = Thread::Current();
  LinearAlloc* linear_alloc = Runtime::Current()->GetLinearAlloc();
  const size_t num_strings = GetDexFile()->NumStringIds();
  if (num_strings != 0) {
    GcRoot<mirror::String>* strings =
        linear_alloc->AllocArray<GcRoot<mirror::String>>(self, num_strings);
    if (strings == nullptr) {
      // Failed to allocate pre-resolved string array (probably due to address fragmentation), bail.
      return false;
    }
    SetField32<false>(NumPreResolvedStringsOffset(), num_strings);

    CHECK(strings != nullptr);
    SetPreResolvedStrings(strings);
    for (size_t i = 0; i < GetDexFile()->NumStringIds(); ++i) {
      CHECK(GetPreResolvedStrings()[i].Read() == nullptr);
    }
  }
  return true;
}

void DexCache::SetNativeArrays(StringDexCacheType* strings,
                               uint32_t num_strings,
                               TypeDexCacheType* resolved_types,
                               uint32_t num_resolved_types,
                               MethodDexCacheType* resolved_methods,
                               uint32_t num_resolved_methods,
                               FieldDexCacheType* resolved_fields,
                               uint32_t num_resolved_fields,
                               MethodTypeDexCacheType* resolved_method_types,
                               uint32_t num_resolved_method_types,
                               GcRoot<CallSite>* resolved_call_sites,
                               uint32_t num_resolved_call_sites) {
  CHECK_EQ(num_strings != 0u, strings != nullptr);
  CHECK_EQ(num_resolved_types != 0u, resolved_types != nullptr);
  CHECK_EQ(num_resolved_methods != 0u, resolved_methods != nullptr);
  CHECK_EQ(num_resolved_fields != 0u, resolved_fields != nullptr);
  CHECK_EQ(num_resolved_method_types != 0u, resolved_method_types != nullptr);
  CHECK_EQ(num_resolved_call_sites != 0u, resolved_call_sites != nullptr);
  SetStrings(strings);
  SetResolvedTypes(resolved_types);
  SetResolvedMethods(resolved_methods);
  SetResolvedFields(resolved_fields);
  SetResolvedMethodTypes(resolved_method_types);
  SetResolvedCallSites(resolved_call_sites);
  SetField32<false>(NumStringsOffset(), num_strings);
  SetField32<false>(NumResolvedTypesOffset(), num_resolved_types);
  SetField32<false>(NumResolvedMethodsOffset(), num_resolved_methods);
  SetField32<false>(NumResolvedFieldsOffset(), num_resolved_fields);
  SetField32<false>(NumResolvedMethodTypesOffset(), num_resolved_method_types);
  SetField32<false>(NumResolvedCallSitesOffset(), num_resolved_call_sites);
}

void DexCache::SetLocation(ObjPtr<mirror::String> location) {
  SetFieldObject<false>(OFFSET_OF_OBJECT_MEMBER(DexCache, location_), location);
}

void DexCache::SetClassLoader(ObjPtr<ClassLoader> class_loader) {
  SetFieldObject<false>(OFFSET_OF_OBJECT_MEMBER(DexCache, class_loader_), class_loader);
}

#if !defined(__aarch64__) && !defined(__x86_64__)
static pthread_mutex_t dex_cache_slow_atomic_mutex = PTHREAD_MUTEX_INITIALIZER;

DexCache::ConversionPair64 DexCache::AtomicLoadRelaxed16B(std::atomic<ConversionPair64>* target) {
  pthread_mutex_lock(&dex_cache_slow_atomic_mutex);
  DexCache::ConversionPair64 value = *reinterpret_cast<ConversionPair64*>(target);
  pthread_mutex_unlock(&dex_cache_slow_atomic_mutex);
  return value;
}

void DexCache::AtomicStoreRelease16B(std::atomic<ConversionPair64>* target,
                                     ConversionPair64 value) {
  pthread_mutex_lock(&dex_cache_slow_atomic_mutex);
  *reinterpret_cast<ConversionPair64*>(target) = value;
  pthread_mutex_unlock(&dex_cache_slow_atomic_mutex);
}
#endif

}  // namespace mirror
}  // namespace art
