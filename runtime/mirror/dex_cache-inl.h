/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ART_RUNTIME_MIRROR_DEX_CACHE_INL_H_
#define ART_RUNTIME_MIRROR_DEX_CACHE_INL_H_

#include "dex_cache.h"

#include <android-base/logging.h>

#include "art_field.h"
#include "art_method.h"
#include "base/casts.h"
#include "base/enums.h"
#include "class_linker.h"
#include "dex/dex_file.h"
#include "gc_root-inl.h"
#include "linear_alloc.h"
#include "mirror/call_site.h"
#include "mirror/class.h"
#include "mirror/method_type.h"
#include "obj_ptr.h"
#include "object-inl.h"
#include "runtime.h"
#include "write_barrier-inl.h"

#include <atomic>

namespace art {
namespace mirror {

template<typename DexCachePair>
static void InitializeArray(std::atomic<DexCachePair>* array) {
  DexCachePair::Initialize(array);
}

template<typename T>
static void InitializeArray(GcRoot<T>*) {
  // No special initialization is needed.
}

template<typename T, size_t kMaxCacheSize>
T* DexCache::AllocArray(MemberOffset obj_offset, MemberOffset num_offset, size_t num) {
  num = std::min<size_t>(num, kMaxCacheSize);
  if (num == 0) {
    return nullptr;
  }
  Thread* self = Thread::Current();
  ClassLinker* linker = Runtime::Current()->GetClassLinker();
  LinearAlloc* alloc = linker->GetOrCreateAllocatorForClassLoader(GetClassLoader());
  MutexLock mu(self, *Locks::dex_cache_lock_);  // Avoid allocation by multiple threads.
  T* array = GetFieldPtr64<T*>(obj_offset);
  if (array != nullptr) {
    DCHECK(alloc->Contains(array));
    return array;  // Other thread just allocated the array.
  }
  array = reinterpret_cast<T*>(alloc->AllocAlign16(self, RoundUp(num * sizeof(T), 16)));
  InitializeArray(array);  // Ensure other threads see the array initialized.
  SetField32Volatile<false, false>(num_offset, num);
  SetField64Volatile<false, false>(obj_offset, reinterpret_cast<uint64_t>(array));
  return array;
}

template <typename T>
inline DexCachePair<T>::DexCachePair(ObjPtr<T> object, uint32_t index)
    : object(object), index(index) {}

template <typename T>
inline void DexCachePair<T>::Initialize(std::atomic<DexCachePair<T>>* dex_cache) {
  DexCachePair<T> first_elem;
  first_elem.object = GcRoot<T>(nullptr);
  first_elem.index = InvalidIndexForSlot(0);
  dex_cache[0].store(first_elem, std::memory_order_relaxed);
}

template <typename T>
inline T* DexCachePair<T>::GetObjectForIndex(uint32_t idx) {
  if (idx != index) {
    return nullptr;
  }
  DCHECK(!object.IsNull());
  return object.Read();
}

template <typename T>
inline void NativeDexCachePair<T>::Initialize(std::atomic<NativeDexCachePair<T>>* dex_cache) {
  NativeDexCachePair<T> first_elem;
  first_elem.object = nullptr;
  first_elem.index = InvalidIndexForSlot(0);
  DexCache::SetNativePair(dex_cache, 0, first_elem);
}

inline uint32_t DexCache::ClassSize(PointerSize pointer_size) {
  const uint32_t vtable_entries = Object::kVTableLength;
  return Class::ComputeClassSize(true, vtable_entries, 0, 0, 0, 0, 0, pointer_size);
}

inline uint32_t DexCache::StringSlotIndex(dex::StringIndex string_idx) {
  DCHECK_LT(string_idx.index_, GetDexFile()->NumStringIds());
  const uint32_t slot_idx = string_idx.index_ % kDexCacheStringCacheSize;
  DCHECK_LT(slot_idx, NumStrings());
  return slot_idx;
}

inline String* DexCache::GetResolvedString(dex::StringIndex string_idx) {
  StringDexCacheType* strings = GetStrings();
  if (UNLIKELY(strings == nullptr)) {
    return nullptr;
  }
  return strings[StringSlotIndex(string_idx)].load(
      std::memory_order_relaxed).GetObjectForIndex(string_idx.index_);
}

inline void DexCache::SetResolvedString(dex::StringIndex string_idx, ObjPtr<String> resolved) {
  DCHECK(resolved != nullptr);
  StringDexCacheType* strings = GetStrings();
  if (UNLIKELY(strings == nullptr)) {
    strings = AllocArray<StringDexCacheType, kDexCacheStringCacheSize>(
        StringsOffset(), NumStringsOffset(), GetDexFile()->NumStringIds());
  }
  strings[StringSlotIndex(string_idx)].store(
      StringDexCachePair(resolved, string_idx.index_), std::memory_order_relaxed);
  Runtime* const runtime = Runtime::Current();
  if (UNLIKELY(runtime->IsActiveTransaction())) {
    DCHECK(runtime->IsAotCompiler());
    runtime->RecordResolveString(this, string_idx);
  }
  // TODO: Fine-grained marking, so that we don't need to go through all arrays in full.
  WriteBarrier::ForEveryFieldWrite(this);
}

inline void DexCache::ClearString(dex::StringIndex string_idx) {
  DCHECK(Runtime::Current()->IsAotCompiler());
  uint32_t slot_idx = StringSlotIndex(string_idx);
  StringDexCacheType* strings = GetStrings();
  if (UNLIKELY(strings == nullptr)) {
    return;
  }
  StringDexCacheType* slot = &strings[slot_idx];
  // This is racy but should only be called from the transactional interpreter.
  if (slot->load(std::memory_order_relaxed).index == string_idx.index_) {
    StringDexCachePair cleared(nullptr, StringDexCachePair::InvalidIndexForSlot(slot_idx));
    slot->store(cleared, std::memory_order_relaxed);
  }
}

inline uint32_t DexCache::TypeSlotIndex(dex::TypeIndex type_idx) {
  DCHECK_LT(type_idx.index_, GetDexFile()->NumTypeIds());
  const uint32_t slot_idx = type_idx.index_ % kDexCacheTypeCacheSize;
  DCHECK_LT(slot_idx, NumResolvedTypes());
  return slot_idx;
}

inline Class* DexCache::GetResolvedType(dex::TypeIndex type_idx) {
  // It is theorized that a load acquire is not required since obtaining the resolved class will
  // always have an address dependency or a lock.
  TypeDexCacheType* resolved_types = GetResolvedTypes();
  if (UNLIKELY(resolved_types == nullptr)) {
    return nullptr;
  }
  return resolved_types[TypeSlotIndex(type_idx)].load(
      std::memory_order_relaxed).GetObjectForIndex(type_idx.index_);
}

inline void DexCache::SetResolvedType(dex::TypeIndex type_idx, ObjPtr<Class> resolved) {
  DCHECK(resolved != nullptr);
  DCHECK(resolved->IsResolved()) << resolved->GetStatus();
  TypeDexCacheType* resolved_types = GetResolvedTypes();
  if (UNLIKELY(resolved_types == nullptr)) {
    resolved_types = AllocArray<TypeDexCacheType, kDexCacheTypeCacheSize>(
        ResolvedTypesOffset(), NumResolvedTypesOffset(), GetDexFile()->NumTypeIds());
  }
  // TODO default transaction support.
  // Use a release store for SetResolvedType. This is done to prevent other threads from seeing a
  // class but not necessarily seeing the loaded members like the static fields array.
  // See b/32075261.
  resolved_types[TypeSlotIndex(type_idx)].store(
      TypeDexCachePair(resolved, type_idx.index_), std::memory_order_release);
  // TODO: Fine-grained marking, so that we don't need to go through all arrays in full.
  WriteBarrier::ForEveryFieldWrite(this);
}

inline void DexCache::ClearResolvedType(dex::TypeIndex type_idx) {
  DCHECK(Runtime::Current()->IsAotCompiler());
  TypeDexCacheType* resolved_types = GetResolvedTypes();
  if (UNLIKELY(resolved_types == nullptr)) {
    return;
  }
  uint32_t slot_idx = TypeSlotIndex(type_idx);
  TypeDexCacheType* slot = &resolved_types[slot_idx];
  // This is racy but should only be called from the single-threaded ImageWriter and tests.
  if (slot->load(std::memory_order_relaxed).index == type_idx.index_) {
    TypeDexCachePair cleared(nullptr, TypeDexCachePair::InvalidIndexForSlot(slot_idx));
    slot->store(cleared, std::memory_order_relaxed);
  }
}

inline uint32_t DexCache::MethodTypeSlotIndex(dex::ProtoIndex proto_idx) {
  DCHECK(Runtime::Current()->IsMethodHandlesEnabled());
  DCHECK_LT(proto_idx.index_, GetDexFile()->NumProtoIds());
  const uint32_t slot_idx = proto_idx.index_ % kDexCacheMethodTypeCacheSize;
  DCHECK_LT(slot_idx, NumResolvedMethodTypes());
  return slot_idx;
}

inline MethodType* DexCache::GetResolvedMethodType(dex::ProtoIndex proto_idx) {
  MethodTypeDexCacheType* methods = GetResolvedMethodTypes();
  if (UNLIKELY(methods == nullptr)) {
    return nullptr;
  }
  return methods[MethodTypeSlotIndex(proto_idx)].load(
      std::memory_order_relaxed).GetObjectForIndex(proto_idx.index_);
}

inline void DexCache::SetResolvedMethodType(dex::ProtoIndex proto_idx, MethodType* resolved) {
  DCHECK(resolved != nullptr);
  MethodTypeDexCacheType* methods = GetResolvedMethodTypes();
  if (UNLIKELY(methods == nullptr)) {
    methods = AllocArray<MethodTypeDexCacheType, kDexCacheMethodTypeCacheSize>(
        ResolvedMethodTypesOffset(), NumResolvedMethodTypesOffset(), GetDexFile()->NumProtoIds());
  }
  methods[MethodTypeSlotIndex(proto_idx)].store(
      MethodTypeDexCachePair(resolved, proto_idx.index_), std::memory_order_relaxed);
  // TODO: Fine-grained marking, so that we don't need to go through all arrays in full.
  WriteBarrier::ForEveryFieldWrite(this);
}

inline CallSite* DexCache::GetResolvedCallSite(uint32_t call_site_idx) {
  DCHECK(Runtime::Current()->IsMethodHandlesEnabled());
  DCHECK_LT(call_site_idx, GetDexFile()->NumCallSiteIds());
  GcRoot<CallSite>* call_sites = GetResolvedCallSites();
  if (UNLIKELY(call_sites == nullptr)) {
    return nullptr;
  }
  GcRoot<mirror::CallSite>& target = call_sites[call_site_idx];
  Atomic<GcRoot<mirror::CallSite>>& ref =
      reinterpret_cast<Atomic<GcRoot<mirror::CallSite>>&>(target);
  return ref.load(std::memory_order_seq_cst).Read();
}

inline ObjPtr<CallSite> DexCache::SetResolvedCallSite(uint32_t call_site_idx,
                                                      ObjPtr<CallSite> call_site) {
  DCHECK(Runtime::Current()->IsMethodHandlesEnabled());
  DCHECK_LT(call_site_idx, GetDexFile()->NumCallSiteIds());

  GcRoot<mirror::CallSite> null_call_site(nullptr);
  GcRoot<mirror::CallSite> candidate(call_site);
  GcRoot<CallSite>* call_sites = GetResolvedCallSites();
  if (UNLIKELY(call_sites == nullptr)) {
    call_sites = AllocArray<GcRoot<CallSite>, std::numeric_limits<size_t>::max()>(
        ResolvedCallSitesOffset(), NumResolvedCallSitesOffset(), GetDexFile()->NumCallSiteIds());
  }
  GcRoot<mirror::CallSite>& target = call_sites[call_site_idx];

  // The first assignment for a given call site wins.
  Atomic<GcRoot<mirror::CallSite>>& ref =
      reinterpret_cast<Atomic<GcRoot<mirror::CallSite>>&>(target);
  if (ref.CompareAndSetStrongSequentiallyConsistent(null_call_site, candidate)) {
    // TODO: Fine-grained marking, so that we don't need to go through all arrays in full.
    WriteBarrier::ForEveryFieldWrite(this);
    return call_site;
  } else {
    return target.Read();
  }
}

inline uint32_t DexCache::FieldSlotIndex(uint32_t field_idx) {
  DCHECK_LT(field_idx, GetDexFile()->NumFieldIds());
  const uint32_t slot_idx = field_idx % kDexCacheFieldCacheSize;
  DCHECK_LT(slot_idx, NumResolvedFields());
  return slot_idx;
}

inline ArtField* DexCache::GetResolvedField(uint32_t field_idx) {
  FieldDexCacheType* fields = GetResolvedFields();
  if (UNLIKELY(fields == nullptr)) {
    return nullptr;
  }
  auto pair = GetNativePair(fields, FieldSlotIndex(field_idx));
  return pair.GetObjectForIndex(field_idx);
}

inline void DexCache::SetResolvedField(uint32_t field_idx, ArtField* field) {
  DCHECK(field != nullptr);
  FieldDexCachePair pair(field, field_idx);
  FieldDexCacheType* fields = GetResolvedFields();
  if (UNLIKELY(fields == nullptr)) {
    fields = AllocArray<FieldDexCacheType, kDexCacheFieldCacheSize>(
        ResolvedFieldsOffset(), NumResolvedFieldsOffset(), GetDexFile()->NumFieldIds());
  }
  SetNativePair(fields, FieldSlotIndex(field_idx), pair);
}

inline uint32_t DexCache::MethodSlotIndex(uint32_t method_idx) {
  DCHECK_LT(method_idx, GetDexFile()->NumMethodIds());
  const uint32_t slot_idx = method_idx % kDexCacheMethodCacheSize;
  DCHECK_LT(slot_idx, NumResolvedMethods());
  return slot_idx;
}

inline ArtMethod* DexCache::GetResolvedMethod(uint32_t method_idx) {
  MethodDexCacheType* methods = GetResolvedMethods();
  if (UNLIKELY(methods == nullptr)) {
    return nullptr;
  }
  auto pair = GetNativePair(methods, MethodSlotIndex(method_idx));
  return pair.GetObjectForIndex(method_idx);
}

inline void DexCache::SetResolvedMethod(uint32_t method_idx, ArtMethod* method) {
  DCHECK(method != nullptr);
  MethodDexCachePair pair(method, method_idx);
  MethodDexCacheType* methods = GetResolvedMethods();
  if (UNLIKELY(methods == nullptr)) {
    methods = AllocArray<MethodDexCacheType, kDexCacheMethodCacheSize>(
        ResolvedMethodsOffset(), NumResolvedMethodsOffset(), GetDexFile()->NumMethodIds());
  }
  SetNativePair(methods, MethodSlotIndex(method_idx), pair);
}

template <typename T>
NativeDexCachePair<T> DexCache::GetNativePair(std::atomic<NativeDexCachePair<T>>* pair_array,
                                              size_t idx) {
  if (kRuntimePointerSize == PointerSize::k64) {
    auto* array = reinterpret_cast<std::atomic<ConversionPair64>*>(pair_array);
    ConversionPair64 value = AtomicLoadRelaxed16B(&array[idx]);
    return NativeDexCachePair<T>(reinterpret_cast64<T*>(value.first),
                                 dchecked_integral_cast<size_t>(value.second));
  } else {
    auto* array = reinterpret_cast<std::atomic<ConversionPair32>*>(pair_array);
    ConversionPair32 value = array[idx].load(std::memory_order_relaxed);
    return NativeDexCachePair<T>(reinterpret_cast32<T*>(value.first), value.second);
  }
}

template <typename T>
void DexCache::SetNativePair(std::atomic<NativeDexCachePair<T>>* pair_array,
                             size_t idx,
                             NativeDexCachePair<T> pair) {
  if (kRuntimePointerSize == PointerSize::k64) {
    auto* array = reinterpret_cast<std::atomic<ConversionPair64>*>(pair_array);
    ConversionPair64 v(reinterpret_cast64<uint64_t>(pair.object), pair.index);
    AtomicStoreRelease16B(&array[idx], v);
  } else {
    auto* array = reinterpret_cast<std::atomic<ConversionPair32>*>(pair_array);
    ConversionPair32 v(reinterpret_cast32<uint32_t>(pair.object),
                       dchecked_integral_cast<uint32_t>(pair.index));
    array[idx].store(v, std::memory_order_release);
  }
}

template <typename T,
          ReadBarrierOption kReadBarrierOption,
          typename Visitor>
inline void VisitDexCachePairs(std::atomic<DexCachePair<T>>* pairs,
                               size_t num_pairs,
                               const Visitor& visitor)
    REQUIRES_SHARED(Locks::mutator_lock_) REQUIRES(Locks::heap_bitmap_lock_) {
  for (size_t i = 0; pairs != nullptr && i < num_pairs; ++i) {
    DexCachePair<T> source = pairs[i].load(std::memory_order_relaxed);
    // NOTE: We need the "template" keyword here to avoid a compilation
    // failure. GcRoot<T> is a template argument-dependent type and we need to
    // tell the compiler to treat "Read" as a template rather than a field or
    // function. Otherwise, on encountering the "<" token, the compiler would
    // treat "Read" as a field.
    T* const before = source.object.template Read<kReadBarrierOption>();
    visitor.VisitRootIfNonNull(source.object.AddressWithoutBarrier());
    if (source.object.template Read<kReadBarrierOption>() != before) {
      pairs[i].store(source, std::memory_order_relaxed);
    }
  }
}

template <bool kVisitNativeRoots,
          VerifyObjectFlags kVerifyFlags,
          ReadBarrierOption kReadBarrierOption,
          typename Visitor>
inline void DexCache::VisitReferences(ObjPtr<Class> klass, const Visitor& visitor) {
  // Visit instance fields first.
  VisitInstanceFieldsReferences<kVerifyFlags, kReadBarrierOption>(klass, visitor);
  // Visit arrays after.
  if (kVisitNativeRoots) {
    VisitDexCachePairs<String, kReadBarrierOption, Visitor>(
        GetStrings<kVerifyFlags>(), NumStrings<kVerifyFlags>(), visitor);

    VisitDexCachePairs<Class, kReadBarrierOption, Visitor>(
        GetResolvedTypes<kVerifyFlags>(), NumResolvedTypes<kVerifyFlags>(), visitor);

    VisitDexCachePairs<MethodType, kReadBarrierOption, Visitor>(
        GetResolvedMethodTypes<kVerifyFlags>(), NumResolvedMethodTypes<kVerifyFlags>(), visitor);

    GcRoot<mirror::CallSite>* resolved_call_sites = GetResolvedCallSites<kVerifyFlags>();
    size_t num_call_sites = NumResolvedCallSites<kVerifyFlags>();
    for (size_t i = 0; resolved_call_sites != nullptr && i != num_call_sites; ++i) {
      visitor.VisitRootIfNonNull(resolved_call_sites[i].AddressWithoutBarrier());
    }
  }
}

inline ObjPtr<String> DexCache::GetLocation() {
  return GetFieldObject<String>(OFFSET_OF_OBJECT_MEMBER(DexCache, location_));
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_DEX_CACHE_INL_H_
