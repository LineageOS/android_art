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

#include "verifier_deps.h"

#include <cstring>
#include <sstream>

#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/indenter.h"
#include "base/leb128.h"
#include "base/mutex-inl.h"
#include "compiler_callbacks.h"
#include "dex/class_accessor-inl.h"
#include "dex/dex_file-inl.h"
#include "mirror/class-inl.h"
#include "mirror/class_loader.h"
#include "oat_file.h"
#include "obj_ptr-inl.h"
#include "reg_type.h"
#include "reg_type_cache-inl.h"
#include "runtime.h"

namespace art {
namespace verifier {

VerifierDeps::VerifierDeps(const std::vector<const DexFile*>& dex_files, bool output_only)
    : output_only_(output_only) {
  for (const DexFile* dex_file : dex_files) {
    DCHECK(GetDexFileDeps(*dex_file) == nullptr);
    std::unique_ptr<DexFileDeps> deps(new DexFileDeps(dex_file->NumClassDefs()));
    dex_deps_.emplace(dex_file, std::move(deps));
  }
}

// Perform logical OR on two bit vectors and assign back to LHS, i.e. `to_update |= other`.
// Size of the two vectors must be equal.
// Size of `other` must be equal to size of `to_update`.
static inline void BitVectorOr(std::vector<bool>& to_update, const std::vector<bool>& other) {
  DCHECK_EQ(to_update.size(), other.size());
  std::transform(other.begin(),
                 other.end(),
                 to_update.begin(),
                 to_update.begin(),
                 std::logical_or<bool>());
}

void VerifierDeps::MergeWith(std::unique_ptr<VerifierDeps> other,
                             const std::vector<const DexFile*>& dex_files) {
  DCHECK(other != nullptr);
  DCHECK_EQ(dex_deps_.size(), other->dex_deps_.size());
  for (const DexFile* dex_file : dex_files) {
    DexFileDeps* my_deps = GetDexFileDeps(*dex_file);
    DexFileDeps& other_deps = *other->GetDexFileDeps(*dex_file);
    // We currently collect extra strings only on the main `VerifierDeps`,
    // which should be the one passed as `this` in this method.
    DCHECK(other_deps.strings_.empty());
    // Size is the number of class definitions in the dex file, and must be the
    // same between the two `VerifierDeps`.
    DCHECK_EQ(my_deps->assignable_types_.size(), other_deps.assignable_types_.size());
    for (uint32_t i = 0; i < my_deps->assignable_types_.size(); ++i) {
      my_deps->assignable_types_[i].merge(other_deps.assignable_types_[i]);
    }
    BitVectorOr(my_deps->verified_classes_, other_deps.verified_classes_);
  }
}

VerifierDeps::DexFileDeps* VerifierDeps::GetDexFileDeps(const DexFile& dex_file) {
  auto it = dex_deps_.find(&dex_file);
  return (it == dex_deps_.end()) ? nullptr : it->second.get();
}

const VerifierDeps::DexFileDeps* VerifierDeps::GetDexFileDeps(const DexFile& dex_file) const {
  auto it = dex_deps_.find(&dex_file);
  return (it == dex_deps_.end()) ? nullptr : it->second.get();
}

dex::StringIndex VerifierDeps::GetClassDescriptorStringId(const DexFile& dex_file,
                                                          ObjPtr<mirror::Class> klass) {
  DCHECK(klass != nullptr);
  ObjPtr<mirror::DexCache> dex_cache = klass->GetDexCache();
  // Array and proxy classes do not have a dex cache.
  if (!klass->IsArrayClass() && !klass->IsProxyClass()) {
    DCHECK(dex_cache != nullptr) << klass->PrettyClass();
    if (dex_cache->GetDexFile() == &dex_file) {
      // FindStringId is slow, try to go through the class def if we have one.
      const dex::ClassDef* class_def = klass->GetClassDef();
      DCHECK(class_def != nullptr) << klass->PrettyClass();
      const dex::TypeId& type_id = dex_file.GetTypeId(class_def->class_idx_);
      if (kIsDebugBuild) {
        std::string temp;
        CHECK_EQ(GetIdFromString(dex_file, klass->GetDescriptor(&temp)), type_id.descriptor_idx_);
      }
      return type_id.descriptor_idx_;
    }
  }
  std::string temp;
  return GetIdFromString(dex_file, klass->GetDescriptor(&temp));
}

static inline VerifierDeps* GetMainVerifierDeps(VerifierDeps* local_deps) {
  // The main VerifierDeps is the one set in the compiler callbacks, which at the
  // end of verification will have all the per-thread VerifierDeps merged into it.
  CompilerCallbacks* callbacks = Runtime::Current()->GetCompilerCallbacks();
  if (callbacks == nullptr) {
    DCHECK(!Runtime::Current()->IsAotCompiler());
    return local_deps;
  }
  DCHECK(Runtime::Current()->IsAotCompiler());
  return callbacks->GetVerifierDeps();
}

static bool FindExistingStringId(const std::vector<std::string>& strings,
                                 const std::string& str,
                                 uint32_t* found_id) {
  uint32_t num_extra_ids = strings.size();
  for (size_t i = 0; i < num_extra_ids; ++i) {
    if (strings[i] == str) {
      *found_id = i;
      return true;
    }
  }
  return false;
}

dex::StringIndex VerifierDeps::GetIdFromString(const DexFile& dex_file, const std::string& str) {
  const dex::StringId* string_id = dex_file.FindStringId(str.c_str());
  if (string_id != nullptr) {
    // String is in the DEX file. Return its ID.
    return dex_file.GetIndexForStringId(*string_id);
  }

  // String is not in the DEX file. Assign a new ID to it which is higher than
  // the number of strings in the DEX file.

  // We use the main `VerifierDeps` for adding new strings to simplify
  // synchronization/merging of these entries between threads.
  VerifierDeps* singleton = GetMainVerifierDeps(this);
  DexFileDeps* deps = singleton->GetDexFileDeps(dex_file);
  DCHECK(deps != nullptr);

  uint32_t num_ids_in_dex = dex_file.NumStringIds();
  uint32_t found_id;

  {
    ReaderMutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
    if (FindExistingStringId(deps->strings_, str, &found_id)) {
      return dex::StringIndex(num_ids_in_dex + found_id);
    }
  }
  {
    WriterMutexLock mu(Thread::Current(), *Locks::verifier_deps_lock_);
    if (FindExistingStringId(deps->strings_, str, &found_id)) {
      return dex::StringIndex(num_ids_in_dex + found_id);
    }
    deps->strings_.push_back(str);
    dex::StringIndex new_id(num_ids_in_dex + deps->strings_.size() - 1);
    CHECK_GE(new_id.index_, num_ids_in_dex);  // check for overflows
    DCHECK_EQ(str, singleton->GetStringFromId(dex_file, new_id));
    return new_id;
  }
}

std::string VerifierDeps::GetStringFromId(const DexFile& dex_file, dex::StringIndex string_id)
    const {
  uint32_t num_ids_in_dex = dex_file.NumStringIds();
  if (string_id.index_ < num_ids_in_dex) {
    return std::string(dex_file.StringDataByIdx(string_id));
  } else {
    const DexFileDeps* deps = GetDexFileDeps(dex_file);
    DCHECK(deps != nullptr);
    string_id.index_ -= num_ids_in_dex;
    CHECK_LT(string_id.index_, deps->strings_.size());
    return deps->strings_[string_id.index_];
  }
}

void VerifierDeps::AddAssignability(const DexFile& dex_file,
                                    const dex::ClassDef& class_def,
                                    ObjPtr<mirror::Class> destination,
                                    ObjPtr<mirror::Class> source) {
  // Test that the method is only called on reference types.
  // Note that concurrent verification of `destination` and `source` may have
  // set their status to erroneous. However, the tests performed below rely
  // merely on no issues with linking (valid access flags, superclass and
  // implemented interfaces). If the class at any point reached the IsResolved
  // status, the requirement holds. This is guaranteed by RegTypeCache::ResolveClass.
  DCHECK(destination != nullptr);
  DCHECK(source != nullptr);

  if (destination->IsPrimitive() || source->IsPrimitive()) {
    // Primitive types are trivially non-assignable to anything else.
    // We do not need to record trivial assignability, as it will
    // not change across releases.
    return;
  }

  if (destination == source || destination->IsObjectClass()) {
    // Cases when `destination` is trivially assignable from `source`.
    return;
  }

  if (destination->IsArrayClass() && source->IsArrayClass()) {
    // Both types are arrays. Break down to component types and add recursively.
    // This helps filter out destinations from compiled DEX files (see below)
    // and deduplicate entries with the same canonical component type.
    ObjPtr<mirror::Class> destination_component = destination->GetComponentType();
    ObjPtr<mirror::Class> source_component = source->GetComponentType();

    // Only perform the optimization if both types are resolved which guarantees
    // that they linked successfully, as required at the top of this method.
    if (destination_component->IsResolved() && source_component->IsResolved()) {
      AddAssignability(dex_file,
                       class_def,
                       destination_component,
                       source_component);
      return;
    }
  }

  DexFileDeps* dex_deps = GetDexFileDeps(dex_file);
  if (dex_deps == nullptr) {
    // This invocation is from verification of a DEX file which is not being compiled.
    return;
  }

  // Get string IDs for both descriptors and store in the appropriate set.
  dex::StringIndex destination_id = GetClassDescriptorStringId(dex_file, destination);
  dex::StringIndex source_id = GetClassDescriptorStringId(dex_file, source);

  uint16_t index = dex_file.GetIndexForClassDef(class_def);
  dex_deps->assignable_types_[index].emplace(TypeAssignability(destination_id, source_id));
}

void VerifierDeps::AddAssignability(const DexFile& dex_file,
                                    const dex::ClassDef& class_def,
                                    const RegType& destination,
                                    const RegType& source) {
  DexFileDeps* dex_deps = GetDexFileDeps(dex_file);
  if (dex_deps == nullptr) {
    // This invocation is from verification of a DEX file which is not being compiled.
    return;
  }

  CHECK(destination.IsUnresolvedReference() || destination.HasClass());
  CHECK(!destination.IsUnresolvedMergedReference());

  if (source.IsUnresolvedReference() || source.HasClass()) {
    // Get string IDs for both descriptors and store in the appropriate set.
    dex::StringIndex destination_id =
        GetIdFromString(dex_file, std::string(destination.GetDescriptor()));
    dex::StringIndex source_id = GetIdFromString(dex_file, std::string(source.GetDescriptor()));
    uint16_t index = dex_file.GetIndexForClassDef(class_def);
    dex_deps->assignable_types_[index].emplace(TypeAssignability(destination_id, source_id));
  } else if (source.IsZeroOrNull()) {
    // Nothing to record, null is always assignable.
  } else {
    CHECK(source.IsUnresolvedMergedReference()) << source.Dump();
    const UnresolvedMergedType& merge = *down_cast<const UnresolvedMergedType*>(&source);
    AddAssignability(dex_file, class_def, destination, merge.GetResolvedPart());
    for (uint32_t idx : merge.GetUnresolvedTypes().Indexes()) {
      AddAssignability(dex_file, class_def, destination, merge.GetRegTypeCache()->GetFromId(idx));
    }
  }
}

void VerifierDeps::MaybeRecordVerificationStatus(VerifierDeps* verifier_deps,
                                                 const DexFile& dex_file,
                                                 const dex::ClassDef& class_def,
                                                 FailureKind failure_kind) {
  if (verifier_deps != nullptr) {
    switch (failure_kind) {
      case verifier::FailureKind::kHardFailure:
      case verifier::FailureKind::kSoftFailure: {
        // Class will be verified at runtime.
        DexFileDeps* dex_deps = verifier_deps->GetDexFileDeps(dex_file);
        uint16_t index = dex_file.GetIndexForClassDef(class_def);
        dex_deps->assignable_types_[index].clear();
        break;
      }
      case verifier::FailureKind::kAccessChecksFailure:
      case verifier::FailureKind::kTypeChecksFailure:
      case verifier::FailureKind::kNoFailure: {
        verifier_deps->RecordClassVerified(dex_file, class_def);
        break;
      }
    }
  }
}

void VerifierDeps::RecordClassVerified(const DexFile& dex_file, const dex::ClassDef& class_def) {
  DexFileDeps* dex_deps = GetDexFileDeps(dex_file);
  DCHECK_EQ(dex_deps->verified_classes_.size(), dex_file.NumClassDefs());
  dex_deps->verified_classes_[dex_file.GetIndexForClassDef(class_def)] = true;
}

bool VerifierDeps::HasRecordedVerifiedStatus(const DexFile& dex_file,
                                             const dex::ClassDef& class_def) {
  DexFileDeps* dex_deps = GetDexFileDeps(dex_file);
  DCHECK_EQ(dex_deps->verified_classes_.size(), dex_file.NumClassDefs());
  return dex_deps->verified_classes_[dex_file.GetIndexForClassDef(class_def)];
}

void VerifierDeps::MaybeRecordAssignability(VerifierDeps* verifier_deps,
                                            const DexFile& dex_file,
                                            const dex::ClassDef& class_def,
                                            ObjPtr<mirror::Class> destination,
                                            ObjPtr<mirror::Class> source) {
  if (verifier_deps != nullptr) {
    verifier_deps->AddAssignability(dex_file, class_def, destination, source);
  }
}

void VerifierDeps::MaybeRecordAssignability(VerifierDeps* verifier_deps,
                                            const DexFile& dex_file,
                                            const dex::ClassDef& class_def,
                                            const RegType& destination,
                                            const RegType& source) {
  if (verifier_deps != nullptr) {
    verifier_deps->AddAssignability(dex_file, class_def, destination, source);
  }
}

namespace {

template<typename T> inline uint32_t Encode(T in);

template<> inline uint32_t Encode<dex::StringIndex>(dex::StringIndex in) {
  return in.index_;
}

template<typename T> inline T Decode(uint32_t in);

template<> inline dex::StringIndex Decode<dex::StringIndex>(uint32_t in) {
  return dex::StringIndex(in);
}

template<typename T1, typename T2>
static inline void EncodeTuple(std::vector<uint8_t>* out, const std::tuple<T1, T2>& t) {
  EncodeUnsignedLeb128(out, Encode(std::get<0>(t)));
  EncodeUnsignedLeb128(out, Encode(std::get<1>(t)));
}

template<typename T1, typename T2>
static inline bool DecodeTuple(const uint8_t** in, const uint8_t* end, std::tuple<T1, T2>* t) {
  uint32_t v1, v2;
  if (UNLIKELY(!DecodeUnsignedLeb128Checked(in, end, &v1)) ||
      UNLIKELY(!DecodeUnsignedLeb128Checked(in, end, &v2))) {
    return false;
  }
  *t = std::make_tuple(Decode<T1>(v1), Decode<T2>(v2));
  return true;
}

template<typename T1, typename T2, typename T3>
static inline void EncodeTuple(std::vector<uint8_t>* out, const std::tuple<T1, T2, T3>& t) {
  EncodeUnsignedLeb128(out, Encode(std::get<0>(t)));
  EncodeUnsignedLeb128(out, Encode(std::get<1>(t)));
  EncodeUnsignedLeb128(out, Encode(std::get<2>(t)));
}

template<typename T1, typename T2, typename T3>
static inline bool DecodeTuple(const uint8_t** in, const uint8_t* end, std::tuple<T1, T2, T3>* t) {
  uint32_t v1, v2, v3;
  if (UNLIKELY(!DecodeUnsignedLeb128Checked(in, end, &v1)) ||
      UNLIKELY(!DecodeUnsignedLeb128Checked(in, end, &v2)) ||
      UNLIKELY(!DecodeUnsignedLeb128Checked(in, end, &v3))) {
    return false;
  }
  *t = std::make_tuple(Decode<T1>(v1), Decode<T2>(v2), Decode<T3>(v3));
  return true;
}

static void SetUint32InUint8Array(std::vector<uint8_t>* out,
                                  uint32_t uint8_offset,
                                  uint32_t uint32_offset,
                                  uint32_t value) {
  DCHECK(IsAligned<sizeof(uint32_t)>(out->data() + uint8_offset));
  (reinterpret_cast<uint32_t*>(out->data() + uint8_offset))[uint32_offset] = value;
}

template<typename T>
static void EncodeSetVector(std::vector<uint8_t>* out,
                            const std::vector<std::set<T>>& vector,
                            const std::vector<bool>& verified_classes) {
  uint32_t offsets_index = out->size();
  // Make room for offsets for each class, +1 for marking the end of the
  // assignability types data.
  out->resize(out->size() + (vector.size() + 1) * sizeof(uint32_t));
  uint32_t class_def_index = 0;
  for (const std::set<T>& set : vector) {
    if (verified_classes[class_def_index]) {
      // Store the offset of the set for this class.
      SetUint32InUint8Array(out, offsets_index, class_def_index, out->size());
      for (const T& entry : set) {
        EncodeTuple(out, entry);
      }
    } else {
      SetUint32InUint8Array(out, offsets_index, class_def_index, VerifierDeps::kNotVerifiedMarker);
    }
    class_def_index++;
  }
  SetUint32InUint8Array(out, offsets_index, class_def_index, out->size());
}

template<bool kFillSet, typename T>
static bool DecodeSetVector(const uint8_t** cursor,
                            const uint8_t* start,
                            const uint8_t* end,
                            std::vector<std::set<T>>* vector,
                            std::vector<bool>* verified_classes,
                            size_t num_class_defs) {
  const uint32_t* offsets = reinterpret_cast<const uint32_t*>(*cursor);
  uint32_t next_valid_offset_index = 1;
  // Put the cursor after the offsets of each class, +1 for the offset of the
  // end of the assignable types data.
  *cursor += (num_class_defs + 1) * sizeof(uint32_t);
  for (uint32_t i = 0; i < num_class_defs; ++i) {
    uint32_t offset = offsets[i];
    if (offset == VerifierDeps::kNotVerifiedMarker) {
      (*verified_classes)[i] = false;
      continue;
    }
    (*verified_classes)[i] = true;
    *cursor = start + offset;
    // Fetch the assignability checks.
    std::set<T>& set = (*vector)[i];
    // Find the offset of the next entry. This will tell us where to stop when
    // reading the checks. Note that the last entry in the `offsets` array points
    // to the end of the assignability types data, so the loop will terminate correctly.
    while (next_valid_offset_index <= i ||
           offsets[next_valid_offset_index] == VerifierDeps::kNotVerifiedMarker) {
      next_valid_offset_index++;
    }
    const uint8_t* set_end = start + offsets[next_valid_offset_index];
    // Decode each check.
    while (*cursor < set_end) {
      T tuple;
      if (UNLIKELY(!DecodeTuple(cursor, end, &tuple))) {
        return false;
      }
      if (kFillSet) {
        set.emplace(tuple);
      }
    }
  }
  // Align the cursor to start decoding the strings.
  *cursor = AlignUp(*cursor, sizeof(uint32_t));
  return true;
}

static inline void EncodeStringVector(std::vector<uint8_t>* out,
                                      const std::vector<std::string>& strings) {
  uint32_t offsets_index = out->size();
  // Make room for offsets for each string, +1 for putting the number of
  // strings.
  out->resize(out->size() + (strings.size() + 1 ) * sizeof(uint32_t));
  (reinterpret_cast<uint32_t*>(out->data() + offsets_index))[0] = strings.size();
  uint32_t string_index = 1;
  for (const std::string& str : strings) {
    // Store the offset of the string.
    (reinterpret_cast<uint32_t*>(out->data() + offsets_index))[string_index++] = out->size();

    // Store the string data.
    const uint8_t* data = reinterpret_cast<const uint8_t*>(str.c_str());
    size_t length = str.length() + 1;
    out->insert(out->end(), data, data + length);
    DCHECK_EQ(0u, out->back());
  }
}

template<bool kFillVector>
static inline bool DecodeStringVector(const uint8_t** cursor,
                                      const uint8_t* start,
                                      const uint8_t* end,
                                      std::vector<std::string>* strings) {
  DCHECK(strings->empty());
  uint32_t num_strings = reinterpret_cast<const uint32_t*>(*cursor)[0];
  if (kFillVector) {
    strings->reserve(num_strings);
  }
  const uint8_t* offsets = *cursor;
  *cursor += sizeof(uint32_t) + num_strings * sizeof(uint32_t);
  for (uint32_t i = 0; i < num_strings; ++i) {
    uint32_t string_offset = reinterpret_cast<const uint32_t*>(offsets)[i + 1];
    const char* string_start = reinterpret_cast<const char*>(start + string_offset);
    const char* string_end = reinterpret_cast<const char*>(
        memchr(string_start, 0, end - start - string_offset));
    if (UNLIKELY(string_end == nullptr)) {
      return false;
    }
    size_t string_length = string_end - string_start;
    if (kFillVector) {
      strings->emplace_back(string_start, string_length);
    }
    *cursor = reinterpret_cast<const uint8_t*>(string_end + 1);
  }
  return true;
}

}  // namespace

void VerifierDeps::Encode(const std::vector<const DexFile*>& dex_files,
                          std::vector<uint8_t>* buffer) const {
  DCHECK(buffer->empty());
  buffer->resize(dex_files.size() * sizeof(uint32_t));
  uint32_t dex_file_index = 0;
  for (const DexFile* dex_file : dex_files) {
    // Four byte alignment before encoding the data.
    buffer->resize(RoundUp(buffer->size(), sizeof(uint32_t)));
    (reinterpret_cast<uint32_t*>(buffer->data()))[dex_file_index++] = buffer->size();
    const DexFileDeps& deps = *GetDexFileDeps(*dex_file);
    EncodeSetVector(buffer, deps.assignable_types_, deps.verified_classes_);
    // Four byte alignment before encoding strings.
    buffer->resize(RoundUp(buffer->size(), sizeof(uint32_t)));
    EncodeStringVector(buffer, deps.strings_);
  }
}

template <bool kOnlyVerifiedClasses>
bool VerifierDeps::DecodeDexFileDeps(DexFileDeps& deps,
                                     const uint8_t** cursor,
                                     const uint8_t* data_start,
                                     const uint8_t* data_end,
                                     size_t num_class_defs) {
  return
      DecodeSetVector</*kFillSet=*/ !kOnlyVerifiedClasses>(
          cursor,
          data_start,
          data_end,
          &deps.assignable_types_,
          &deps.verified_classes_,
          num_class_defs) &&
      DecodeStringVector</*kFillVector=*/ !kOnlyVerifiedClasses>(
          cursor, data_start, data_end, &deps.strings_);
}

bool VerifierDeps::ParseStoredData(const std::vector<const DexFile*>& dex_files,
                                   ArrayRef<const uint8_t> data) {
  if (data.empty()) {
    // Return eagerly, as the first thing we expect from VerifierDeps data is
    // the number of created strings, even if there is no dependency.
    // Currently, only the boot image does not have any VerifierDeps data.
    return true;
  }
  const uint8_t* data_start = data.data();
  const uint8_t* data_end = data_start + data.size();
  const uint8_t* cursor = data_start;
  uint32_t dex_file_index = 0;
  for (const DexFile* dex_file : dex_files) {
    DexFileDeps* deps = GetDexFileDeps(*dex_file);
    // Fetch the offset of this dex file's verifier data.
    cursor = data_start + reinterpret_cast<const uint32_t*>(data_start)[dex_file_index++];
    size_t num_class_defs = dex_file->NumClassDefs();
    if (UNLIKELY(!DecodeDexFileDeps</*kOnlyVerifiedClasses=*/ false>(*deps,
                                                                     &cursor,
                                                                     data_start,
                                                                     data_end,
                                                                     num_class_defs))) {
      LOG(ERROR) << "Failed to parse dex file dependencies for " << dex_file->GetLocation();
      return false;
    }
  }
  // TODO: We should check that `data_start == data_end`. Why are we passing excessive data?
  return true;
}

bool VerifierDeps::ParseVerifiedClasses(
    const std::vector<const DexFile*>& dex_files,
    ArrayRef<const uint8_t> data,
    /*out*/std::vector<std::vector<bool>>* verified_classes_per_dex) {
  DCHECK(!data.empty());
  DCHECK(!dex_files.empty());
  DCHECK(verified_classes_per_dex->empty());

  verified_classes_per_dex->reserve(dex_files.size());

  const uint8_t* data_start = data.data();
  const uint8_t* data_end = data_start + data.size();
  const uint8_t* cursor = data_start;
  uint32_t dex_file_index = 0;
  for (const DexFile* dex_file : dex_files) {
    DexFileDeps deps(/*num_class_defs=*/ 0u);  // Do not initialize vectors.
    // Fetch the offset of this dex file's verifier data.
    cursor = data_start + reinterpret_cast<const uint32_t*>(data_start)[dex_file_index++];
    size_t num_class_defs = dex_file->NumClassDefs();
    deps.verified_classes_.resize(num_class_defs);
    if (UNLIKELY(!DecodeDexFileDeps</*kOnlyVerifiedClasses=*/ true>(deps,
                                                                    &cursor,
                                                                    data_start,
                                                                    data_end,
                                                                    num_class_defs))) {
      LOG(ERROR) << "Failed to parse dex file dependencies for " << dex_file->GetLocation();
      return false;
    }
    verified_classes_per_dex->push_back(std::move(deps.verified_classes_));
  }
  // TODO: We should check that `data_start == data_end`. Why are we passing excessive data?
  return true;
}

bool VerifierDeps::Equals(const VerifierDeps& rhs) const {
  if (dex_deps_.size() != rhs.dex_deps_.size()) {
    return false;
  }

  auto lhs_it = dex_deps_.begin();
  auto rhs_it = rhs.dex_deps_.begin();

  for (; (lhs_it != dex_deps_.end()) && (rhs_it != rhs.dex_deps_.end()); lhs_it++, rhs_it++) {
    const DexFile* lhs_dex_file = lhs_it->first;
    const DexFile* rhs_dex_file = rhs_it->first;
    if (lhs_dex_file != rhs_dex_file) {
      return false;
    }

    DexFileDeps* lhs_deps = lhs_it->second.get();
    DexFileDeps* rhs_deps = rhs_it->second.get();
    if (!lhs_deps->Equals(*rhs_deps)) {
      return false;
    }
  }

  DCHECK((lhs_it == dex_deps_.end()) && (rhs_it == rhs.dex_deps_.end()));
  return true;
}

bool VerifierDeps::DexFileDeps::Equals(const VerifierDeps::DexFileDeps& rhs) const {
  return (strings_ == rhs.strings_) &&
         (assignable_types_ == rhs.assignable_types_) &&
         (verified_classes_ == rhs.verified_classes_);
}

void VerifierDeps::Dump(VariableIndentationOutputStream* vios) const {
  // Sort dex files by their location to ensure deterministic ordering.
  using DepsEntry = std::pair<const DexFile*, const DexFileDeps*>;
  std::vector<DepsEntry> dex_deps;
  dex_deps.reserve(dex_deps_.size());
  for (const auto& dep : dex_deps_) {
    dex_deps.emplace_back(dep.first, dep.second.get());
  }
  std::sort(
      dex_deps.begin(),
      dex_deps.end(),
      [](const DepsEntry& lhs, const DepsEntry& rhs) {
        return lhs.first->GetLocation() < rhs.first->GetLocation();
      });
  for (const auto& dep : dex_deps) {
    const DexFile& dex_file = *dep.first;
    vios->Stream()
        << "Dependencies of "
        << dex_file.GetLocation()
        << ":\n";

    ScopedIndentation indent(vios);

    for (const std::string& str : dep.second->strings_) {
      vios->Stream() << "Extra string: " << str << "\n";
    }

    for (size_t idx = 0; idx < dep.second->assignable_types_.size(); idx++) {
      vios->Stream()
          << "Dependencies of "
          << dex_file.GetClassDescriptor(dex_file.GetClassDef(idx))
          << ":\n";
      for (const TypeAssignability& entry : dep.second->assignable_types_[idx]) {
        vios->Stream()
          << GetStringFromId(dex_file, entry.GetSource())
          << " must be assignable to "
          << GetStringFromId(dex_file, entry.GetDestination())
          << "\n";
      }
    }

    for (size_t idx = 0; idx < dep.second->verified_classes_.size(); idx++) {
      if (!dep.second->verified_classes_[idx]) {
        vios->Stream()
            << dex_file.GetClassDescriptor(dex_file.GetClassDef(idx))
            << " will be verified at runtime\n";
      }
    }
  }
}

bool VerifierDeps::ValidateDependencies(Thread* self,
                                        Handle<mirror::ClassLoader> class_loader,
                                        /* out */ std::string* error_msg) const {
  for (const auto& entry : dex_deps_) {
    if (!VerifyDexFile(class_loader, *entry.first, *entry.second, self, error_msg)) {
      return false;
    }
  }
  return true;
}

// TODO: share that helper with other parts of the compiler that have
// the same lookup pattern.
static ObjPtr<mirror::Class> FindClassAndClearException(ClassLinker* class_linker,
                                                        Thread* self,
                                                        const std::string& name,
                                                        Handle<mirror::ClassLoader> class_loader)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  ObjPtr<mirror::Class> result = class_linker->FindClass(self, name.c_str(), class_loader);
  if (result == nullptr) {
    DCHECK(self->IsExceptionPending());
    self->ClearException();
  }
  return result;
}

bool VerifierDeps::VerifyAssignability(Handle<mirror::ClassLoader> class_loader,
                                       const DexFile& dex_file,
                                       const std::vector<std::set<TypeAssignability>>& assignables,
                                       Thread* self,
                                       /* out */ std::string* error_msg) const {
  StackHandleScope<2> hs(self);
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  MutableHandle<mirror::Class> source(hs.NewHandle<mirror::Class>(nullptr));
  MutableHandle<mirror::Class> destination(hs.NewHandle<mirror::Class>(nullptr));

  for (const auto& vec : assignables) {
    for (const auto& entry : vec) {
      const std::string& destination_desc = GetStringFromId(dex_file, entry.GetDestination());
      destination.Assign(
          FindClassAndClearException(class_linker, self, destination_desc.c_str(), class_loader));
      const std::string& source_desc = GetStringFromId(dex_file, entry.GetSource());
      source.Assign(
          FindClassAndClearException(class_linker, self, source_desc.c_str(), class_loader));

      if (destination == nullptr || source == nullptr) {
        // We currently don't use assignability information for unresolved
        // types, as the status of the class using unresolved types will be soft
        // fail in the vdex.
        continue;
      }

      DCHECK(destination->IsResolved() && source->IsResolved());
      if (!destination->IsAssignableFrom(source.Get())) {
        *error_msg = "Class " + destination_desc + " not assignable from " + source_desc;
        return false;
      }
    }
  }
  return true;
}

bool VerifierDeps::VerifyDexFile(Handle<mirror::ClassLoader> class_loader,
                                 const DexFile& dex_file,
                                 const DexFileDeps& deps,
                                 Thread* self,
                                 /* out */ std::string* error_msg) const {
  return VerifyAssignability(class_loader,
                             dex_file,
                             deps.assignable_types_,
                             self,
                             error_msg);
}

}  // namespace verifier
}  // namespace art
