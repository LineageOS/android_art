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

#ifndef ART_RUNTIME_VERIFIER_VERIFIER_DEPS_H_
#define ART_RUNTIME_VERIFIER_VERIFIER_DEPS_H_

#include <map>
#include <set>
#include <vector>

#include "base/array_ref.h"
#include "base/locks.h"
#include "dex/dex_file_structs.h"
#include "dex/dex_file_types.h"
#include "handle.h"
#include "obj_ptr.h"
#include "thread.h"
#include "verifier_enums.h"  // For MethodVerifier::FailureKind.

namespace art {

class ArtField;
class ArtMethod;
class DexFile;
class VariableIndentationOutputStream;

namespace mirror {
class Class;
class ClassLoader;
}  // namespace mirror

namespace verifier {

class RegType;

// Verification dependencies collector class used by the MethodVerifier to record
// resolution outcomes and type assignability tests of classes/methods/fields
// not present in the set of compiled DEX files, that is classes/methods/fields
// defined in the classpath.
// The compilation driver initializes the class and registers all DEX files
// which are being compiled. Classes defined in DEX files outside of this set
// (or synthesized classes without associated DEX files) are considered being
// in the classpath.
// During code-flow verification, the MethodVerifier informs VerifierDeps
// about the outcome of every resolution and assignability test, and
// the VerifierDeps object records them if their outcome may change with
// changes in the classpath.
class VerifierDeps {
 public:
  explicit VerifierDeps(const std::vector<const DexFile*>& dex_files, bool output_only = true);

  // Marker to know whether a class is verified. A non-verified class will have
  // this marker as its offset entry in the encoded data.
  static uint32_t constexpr kNotVerifiedMarker = std::numeric_limits<uint32_t>::max();

  // Fill dependencies from stored data. Returns true on success, false on failure.
  bool ParseStoredData(const std::vector<const DexFile*>& dex_files, ArrayRef<const uint8_t> data);

  // Merge `other` into this `VerifierDeps`'. `other` and `this` must be for the
  // same set of dex files.
  void MergeWith(std::unique_ptr<VerifierDeps> other, const std::vector<const DexFile*>& dex_files);

  // Record information that a class was verified.
  // Note that this function is different from MaybeRecordVerificationStatus() which
  // looks up thread-local VerifierDeps first.
  void RecordClassVerified(const DexFile& dex_file, const dex::ClassDef& class_def)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Record the verification status of the class defined in `class_def`.
  static void MaybeRecordVerificationStatus(VerifierDeps* verifier_deps,
                                            const DexFile& dex_file,
                                            const dex::ClassDef& class_def,
                                            FailureKind failure_kind)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Record the outcome `is_assignable` of type assignability test from `source`
  // to `destination` as defined by RegType::AssignableFrom. `dex_file` is the
  // owner of the method for which MethodVerifier performed the assignability test.
  static void MaybeRecordAssignability(VerifierDeps* verifier_deps,
                                       const DexFile& dex_file,
                                       const dex::ClassDef& class_def,
                                       ObjPtr<mirror::Class> destination,
                                       ObjPtr<mirror::Class> source)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Record that `source` is assignable to `destination`. `dex_file` is the
  // owner of the method for which MethodVerifier performed the assignability test.
  static void MaybeRecordAssignability(VerifierDeps* verifier_deps,
                                       const DexFile& dex_file,
                                       const dex::ClassDef& class_def,
                                       const RegType& destination,
                                       const RegType& source)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Serialize the recorded dependencies and store the data into `buffer`.
  // `dex_files` provides the order of the dex files in which the dependencies
  // should be emitted.
  void Encode(const std::vector<const DexFile*>& dex_files, std::vector<uint8_t>* buffer) const;

  void Dump(VariableIndentationOutputStream* vios) const;

  // Verify the encoded dependencies of this `VerifierDeps` are still valid.
  bool ValidateDependencies(Thread* self,
                            Handle<mirror::ClassLoader> class_loader,
                            /* out */ std::string* error_msg) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  const std::vector<bool>& GetVerifiedClasses(const DexFile& dex_file) const {
    return GetDexFileDeps(dex_file)->verified_classes_;
  }

  // Whether this `verifier_deps` has recorded that the given class is verified.
  bool HasRecordedVerifiedStatus(const DexFile& dex_file, const dex::ClassDef& class_def)
      REQUIRES(!Locks::verifier_deps_lock_);

  bool OutputOnly() const {
    return output_only_;
  }

  bool ContainsDexFile(const DexFile& dex_file) const {
    return GetDexFileDeps(dex_file) != nullptr;
  }

  // Parses raw VerifierDeps data to extract bitvectors of which class def indices
  // were verified or not. The given `dex_files` must match the order and count of
  // dex files used to create the VerifierDeps.
  static bool ParseVerifiedClasses(
      const std::vector<const DexFile*>& dex_files,
      ArrayRef<const uint8_t> data,
      /*out*/std::vector<std::vector<bool>>* verified_classes_per_dex);

  using TypeAssignabilityBase = std::tuple<dex::StringIndex, dex::StringIndex>;
  struct TypeAssignability : public TypeAssignabilityBase {
    TypeAssignability() = default;
    TypeAssignability(const TypeAssignability&) = default;
    TypeAssignability(dex::StringIndex destination_idx, dex::StringIndex source_idx)
        : TypeAssignabilityBase(destination_idx, source_idx) {}

    dex::StringIndex GetDestination() const { return std::get<0>(*this); }
    dex::StringIndex GetSource() const { return std::get<1>(*this); }
  };

 private:
  // Data structure representing dependencies collected during verification of
  // methods inside one DexFile.
  struct DexFileDeps {
    explicit DexFileDeps(size_t num_class_defs)
        : assignable_types_(num_class_defs),
          verified_classes_(num_class_defs) {}

    // Vector of strings which are not present in the corresponding DEX file.
    // These are referred to with ids starting with `NumStringIds()` of that DexFile.
    std::vector<std::string> strings_;

    // Vector that contains for each class def defined in a dex file, a set of class pairs recording
    // the outcome of assignability test from one of the two types to the other.
    std::vector<std::set<TypeAssignability>> assignable_types_;

    // Bit vector indexed by class def indices indicating whether the corresponding
    // class was successfully verified.
    std::vector<bool> verified_classes_;

    bool Equals(const DexFileDeps& rhs) const;
  };

  // Helper function to share DexFileDeps decoding code.
  // Returns true on success, false on failure.
  template <bool kOnlyVerifiedClasses>
  static bool DecodeDexFileDeps(DexFileDeps& deps,
                                const uint8_t** cursor,
                                const uint8_t* data_start,
                                const uint8_t* data_end,
                                size_t num_class_defs);

  // Finds the DexFileDep instance associated with `dex_file`, or nullptr if
  // `dex_file` is not reported as being compiled.
  DexFileDeps* GetDexFileDeps(const DexFile& dex_file);

  const DexFileDeps* GetDexFileDeps(const DexFile& dex_file) const;

  // Returns the index of `str`. If it is defined in `dex_file_`, this is the dex
  // string ID. If not, an ID is assigned to the string and cached in `strings_`
  // of the corresponding DexFileDeps structure (either provided or inferred from
  // `dex_file`).
  dex::StringIndex GetIdFromString(const DexFile& dex_file, const std::string& str)
      REQUIRES(!Locks::verifier_deps_lock_);

  // Returns the string represented by `id`.
  std::string GetStringFromId(const DexFile& dex_file, dex::StringIndex string_id) const;

  // Returns a string ID of the descriptor of the class.
  dex::StringIndex GetClassDescriptorStringId(const DexFile& dex_file, ObjPtr<mirror::Class> klass)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::verifier_deps_lock_);

  void AddAssignability(const DexFile& dex_file,
                        const dex::ClassDef& class_def,
                        ObjPtr<mirror::Class> destination,
                        ObjPtr<mirror::Class> source)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void AddAssignability(const DexFile& dex_file,
                        const dex::ClassDef& class_def,
                        const RegType& destination,
                        const RegType& source)
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool Equals(const VerifierDeps& rhs) const;

  // Verify `dex_file` according to the `deps`, that is going over each
  // `DexFileDeps` field, and checking that the recorded information still
  // holds.
  bool VerifyDexFile(Handle<mirror::ClassLoader> class_loader,
                     const DexFile& dex_file,
                     const DexFileDeps& deps,
                     Thread* self,
                     /* out */ std::string* error_msg) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Iterates over `dex_files` and tries to find a class def matching `descriptor`.
  // Returns true if such class def is found.
  bool IsInDexFiles(const char* descriptor,
                    size_t hash,
                    const std::vector<const DexFile*>& dex_files,
                    /* out */ const DexFile** cp_dex_file) const;

  bool VerifyAssignability(Handle<mirror::ClassLoader> class_loader,
                           const DexFile& dex_file,
                           const std::vector<std::set<TypeAssignability>>& assignables,
                           Thread* self,
                           /* out */ std::string* error_msg) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Map from DexFiles into dependencies collected from verification of their methods.
  std::map<const DexFile*, std::unique_ptr<DexFileDeps>> dex_deps_;

  // Output only signifies if we are using the verifier deps to verify or just to generate them.
  const bool output_only_;

  friend class VerifierDepsTest;
  ART_FRIEND_TEST(VerifierDepsTest, StringToId);
  ART_FRIEND_TEST(VerifierDepsTest, EncodeDecode);
  ART_FRIEND_TEST(VerifierDepsTest, EncodeDecodeMulti);
  ART_FRIEND_TEST(VerifierDepsTest, VerifyDeps);
  ART_FRIEND_TEST(VerifierDepsTest, CompilerDriver);
};

}  // namespace verifier
}  // namespace art

#endif  // ART_RUNTIME_VERIFIER_VERIFIER_DEPS_H_
