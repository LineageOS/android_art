/*
 * Copyright 2019 The Android Open Source Project
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

#ifndef ART_RUNTIME_JIT_JIT_MEMORY_REGION_H_
#define ART_RUNTIME_JIT_JIT_MEMORY_REGION_H_

#include <string>

#include "arch/instruction_set.h"
#include "base/globals.h"
#include "base/locks.h"
#include "base/mem_map.h"
#include "gc_root-inl.h"
#include "handle.h"

namespace art {

namespace mirror {
class Object;
}

namespace jit {

class TestZygoteMemory;

// Number of bytes represented by a bit in the CodeCacheBitmap. Value is reasonable for all
// architectures.
static constexpr int kJitCodeAccountingBytes = 16;

// Helper to get the size required for emitting `number_of_roots` in the
// data portion of a JIT memory region.
uint32_t inline ComputeRootTableSize(uint32_t number_of_roots) {
  return sizeof(uint32_t) + number_of_roots * sizeof(GcRoot<mirror::Object>);
}

// Represents a memory region for the JIT, where code and data are stored. This class
// provides allocation and deallocation primitives.
class JitMemoryRegion {
 public:
  JitMemoryRegion()
      : initial_capacity_(0),
        max_capacity_(0),
        current_capacity_(0),
        data_end_(0),
        exec_end_(0),
        used_memory_for_code_(0),
        used_memory_for_data_(0),
        data_pages_(),
        writable_data_pages_(),
        exec_pages_(),
        non_exec_pages_(),
        data_mspace_(nullptr),
        exec_mspace_(nullptr) {}

  bool Initialize(size_t initial_capacity,
                  size_t max_capacity,
                  bool rwx_memory_allowed,
                  bool is_zygote,
                  std::string* error_msg)
      REQUIRES(Locks::jit_lock_);

  // Try to increase the current capacity of the code cache. Return whether we
  // succeeded at doing so.
  bool IncreaseCodeCacheCapacity() REQUIRES(Locks::jit_lock_);

  // Set the footprint limit of the code cache.
  void SetFootprintLimit(size_t new_footprint) REQUIRES(Locks::jit_lock_);

  const uint8_t* AllocateCode(size_t code_size) REQUIRES(Locks::jit_lock_);
  void FreeCode(const uint8_t* code) REQUIRES(Locks::jit_lock_);
  const uint8_t* AllocateData(size_t data_size) REQUIRES(Locks::jit_lock_);
  void FreeData(const uint8_t* data) REQUIRES(Locks::jit_lock_);
  void FreeData(uint8_t* writable_data) REQUIRES(Locks::jit_lock_) = delete;
  void FreeWritableData(uint8_t* writable_data) REQUIRES(Locks::jit_lock_);

  // Emit header and code into the memory pointed by `reserved_code` (despite it being const).
  // Returns pointer to copied code (within reserved_code region; after OatQuickMethodHeader).
  const uint8_t* CommitCode(ArrayRef<const uint8_t> reserved_code,
                            ArrayRef<const uint8_t> code,
                            const uint8_t* stack_map,
                            bool has_should_deoptimize_flag)
      REQUIRES(Locks::jit_lock_);

  // Emit roots and stack map into the memory pointed by `roots_data` (despite it being const).
  bool CommitData(ArrayRef<const uint8_t> reserved_data,
                  const std::vector<Handle<mirror::Object>>& roots,
                  ArrayRef<const uint8_t> stack_map)
      REQUIRES(Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void ResetWritableMappings() REQUIRES(Locks::jit_lock_) {
    non_exec_pages_.ResetInForkedProcess();
    writable_data_pages_.ResetInForkedProcess();
    // Also clear the mspaces, which, in their implementation,
    // point to the discarded mappings.
    exec_mspace_ = nullptr;
    data_mspace_ = nullptr;
  }

  bool IsValid() const NO_THREAD_SAFETY_ANALYSIS {
    return exec_mspace_ != nullptr || data_mspace_ != nullptr;
  }

  template <typename T>
  void FillData(const T* address, size_t n, const T& t)  REQUIRES(Locks::jit_lock_) {
    std::fill_n(GetWritableDataAddress(address), n, t);
  }

  // Generic helper for writing abritrary data in the data portion of the
  // region.
  template <typename T>
  void WriteData(const T* address, const T& value) {
    *GetWritableDataAddress(address) = value;
  }

  bool HasDualCodeMapping() const {
    return non_exec_pages_.IsValid();
  }

  bool HasDualDataMapping() const {
    return writable_data_pages_.IsValid();
  }

  bool HasCodeMapping() const {
    return exec_pages_.IsValid();
  }

  bool IsInDataSpace(const void* ptr) const {
    return data_pages_.HasAddress(ptr);
  }

  bool IsInExecSpace(const void* ptr) const {
    return exec_pages_.HasAddress(ptr);
  }

  const MemMap* GetExecPages() const {
    return &exec_pages_;
  }

  void* MoreCore(const void* mspace, intptr_t increment);

  bool OwnsSpace(const void* mspace) const NO_THREAD_SAFETY_ANALYSIS {
    return mspace == data_mspace_ || mspace == exec_mspace_;
  }

  size_t GetCurrentCapacity() const REQUIRES(Locks::jit_lock_) {
    return current_capacity_;
  }

  size_t GetMaxCapacity() const REQUIRES(Locks::jit_lock_) {
    return max_capacity_;
  }

  size_t GetUsedMemoryForCode() const REQUIRES(Locks::jit_lock_) {
    return used_memory_for_code_;
  }

  size_t GetResidentMemoryForCode() const REQUIRES(Locks::jit_lock_) {
    return exec_end_;
  }

  size_t GetUsedMemoryForData() const REQUIRES(Locks::jit_lock_) {
    return used_memory_for_data_;
  }

  size_t GetResidentMemoryForData() const REQUIRES(Locks::jit_lock_) {
    return data_end_;
  }

  template <typename T> T* GetWritableDataAddress(const T* src_ptr) {
    if (!HasDualDataMapping()) {
      return const_cast<T*>(src_ptr);
    }
    return const_cast<T*>(TranslateAddress(src_ptr, data_pages_, writable_data_pages_));
  }

 private:
  template <typename T>
  T* TranslateAddress(T* src_ptr, const MemMap& src, const MemMap& dst) {
    CHECK(src.HasAddress(src_ptr)) << reinterpret_cast<const void*>(src_ptr);
    const uint8_t* const raw_src_ptr = reinterpret_cast<const uint8_t*>(src_ptr);
    return reinterpret_cast<T*>(raw_src_ptr - src.Begin() + dst.Begin());
  }

  const MemMap* GetUpdatableCodeMapping() const {
    if (HasDualCodeMapping()) {
      return &non_exec_pages_;
    } else if (HasCodeMapping()) {
      return &exec_pages_;
    } else {
      return nullptr;
    }
  }

  const MemMap* GetWritableDataMapping() const {
    if (HasDualDataMapping()) {
      return &writable_data_pages_;
    } else {
      return &data_pages_;
    }
  }

  template <typename T> T* GetNonWritableDataAddress(T* src_ptr) {
    if (!HasDualDataMapping()) {
      return src_ptr;
    }
    return TranslateAddress(src_ptr, writable_data_pages_, data_pages_);
  }

  template <typename T> T* GetExecutableAddress(T* src_ptr) {
    if (!HasDualCodeMapping()) {
      return src_ptr;
    }
    return TranslateAddress(src_ptr, non_exec_pages_, exec_pages_);
  }

  template <typename T> T* GetNonExecutableAddress(T* src_ptr) {
    if (!HasDualCodeMapping()) {
      return src_ptr;
    }
    return TranslateAddress(src_ptr, exec_pages_, non_exec_pages_);
  }

  static int CreateZygoteMemory(size_t capacity, std::string* error_msg);
  static bool ProtectZygoteMemory(int fd, std::string* error_msg);

  // The initial capacity in bytes this code region starts with.
  size_t initial_capacity_ GUARDED_BY(Locks::jit_lock_);

  // The maximum capacity in bytes this region can go to.
  size_t max_capacity_ GUARDED_BY(Locks::jit_lock_);

  // The current capacity in bytes of the region.
  size_t current_capacity_ GUARDED_BY(Locks::jit_lock_);

  // The current footprint in bytes of the data portion of the region.
  size_t data_end_ GUARDED_BY(Locks::jit_lock_);

  // The current footprint in bytes of the code portion of the region.
  size_t exec_end_ GUARDED_BY(Locks::jit_lock_);

  // The size in bytes of used memory for the code portion of the region.
  size_t used_memory_for_code_ GUARDED_BY(Locks::jit_lock_);

  // The size in bytes of used memory for the data portion of the region.
  size_t used_memory_for_data_ GUARDED_BY(Locks::jit_lock_);

  // Mem map which holds data (stack maps and profiling info).
  MemMap data_pages_;

  // Mem map which holds data with writable permission. Only valid for dual view
  // JIT when this is the writable view and data_pages_ is the readable view.
  MemMap writable_data_pages_;

  // Mem map which holds code and has executable permission.
  MemMap exec_pages_;

  // Mem map which holds code with non executable permission. Only valid for dual view JIT when
  // this is the non-executable view of code used to write updates.
  MemMap non_exec_pages_;

  // The opaque mspace for allocating data.
  void* data_mspace_ GUARDED_BY(Locks::jit_lock_);

  // The opaque mspace for allocating code.
  void* exec_mspace_ GUARDED_BY(Locks::jit_lock_);

  friend class ScopedCodeCacheWrite;  // For GetUpdatableCodeMapping
  friend class TestZygoteMemory;
};

}  // namespace jit
}  // namespace art

#endif  // ART_RUNTIME_JIT_JIT_MEMORY_REGION_H_
