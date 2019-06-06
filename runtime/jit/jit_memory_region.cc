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

#include "jit_memory_region.h"

#include <android-base/unique_fd.h>
#include "base/bit_utils.h"  // For RoundDown, RoundUp
#include "base/globals.h"
#include "base/logging.h"  // For VLOG.
#include "base/memfd.h"
#include "base/systrace.h"
#include "gc/allocator/dlmalloc.h"
#include "jit/jit_scoped_code_cache_write.h"
#include "oat_quick_method_header.h"

using android::base::unique_fd;

namespace art {
namespace jit {

// Data cache will be half of the capacity
// Code cache will be the other half of the capacity.
// TODO: Make this variable?
static constexpr size_t kCodeAndDataCapacityDivider = 2;

bool JitMemoryRegion::InitializeMappings(bool rwx_memory_allowed,
                                         bool is_zygote,
                                         std::string* error_msg) {
  ScopedTrace trace(__PRETTY_FUNCTION__);

  const size_t capacity = max_capacity_;
  const size_t data_capacity = capacity / kCodeAndDataCapacityDivider;
  const size_t exec_capacity = capacity - data_capacity;

  // File descriptor enabling dual-view mapping of code section.
  unique_fd mem_fd;

  // Zygote shouldn't create a shared mapping for JIT, so we cannot use dual view
  // for it.
  if (!is_zygote) {
    // Bionic supports memfd_create, but the call may fail on older kernels.
    mem_fd = unique_fd(art::memfd_create("/jit-cache", /* flags= */ 0));
    if (mem_fd.get() < 0) {
      std::ostringstream oss;
      oss << "Failed to initialize dual view JIT. memfd_create() error: " << strerror(errno);
      if (!rwx_memory_allowed) {
        // Without using RWX page permissions, the JIT can not fallback to single mapping as it
        // requires tranitioning the code pages to RWX for updates.
        *error_msg = oss.str();
        return false;
      }
      VLOG(jit) << oss.str();
    }
  }

  if (mem_fd.get() >= 0 && ftruncate(mem_fd, capacity) != 0) {
    std::ostringstream oss;
    oss << "Failed to initialize memory file: " << strerror(errno);
    *error_msg = oss.str();
    return false;
  }

  std::string data_cache_name = is_zygote ? "zygote-data-code-cache" : "data-code-cache";
  std::string exec_cache_name = is_zygote ? "zygote-jit-code-cache" : "jit-code-cache";

  std::string error_str;
  // Map name specific for android_os_Debug.cpp accounting.
  // Map in low 4gb to simplify accessing root tables for x86_64.
  // We could do PC-relative addressing to avoid this problem, but that
  // would require reserving code and data area before submitting, which
  // means more windows for the code memory to be RWX.
  int base_flags;
  MemMap data_pages;
  if (mem_fd.get() >= 0) {
    // Dual view of JIT code cache case. Create an initial mapping of data pages large enough
    // for data and non-writable view of JIT code pages. We use the memory file descriptor to
    // enable dual mapping - we'll create a second mapping using the descriptor below. The
    // mappings will look like:
    //
    //       VA                  PA
    //
    //       +---------------+
    //       | non exec code |\
    //       +---------------+ \
    //       :               :\ \
    //       +---------------+.\.+---------------+
    //       |  exec code    |  \|     code      |
    //       +---------------+...+---------------+
    //       |      data     |   |     data      |
    //       +---------------+...+---------------+
    //
    // In this configuration code updates are written to the non-executable view of the code
    // cache, and the executable view of the code cache has fixed RX memory protections.
    //
    // This memory needs to be mapped shared as the code portions will have two mappings.
    base_flags = MAP_SHARED;
    data_pages = MemMap::MapFile(
        data_capacity + exec_capacity,
        kProtRW,
        base_flags,
        mem_fd,
        /* start= */ 0,
        /* low_4gb= */ true,
        data_cache_name.c_str(),
        &error_str);
  } else {
    // Single view of JIT code cache case. Create an initial mapping of data pages large enough
    // for data and JIT code pages. The mappings will look like:
    //
    //       VA                  PA
    //
    //       +---------------+...+---------------+
    //       |  exec code    |   |     code      |
    //       +---------------+...+---------------+
    //       |      data     |   |     data      |
    //       +---------------+...+---------------+
    //
    // In this configuration code updates are written to the executable view of the code cache,
    // and the executable view of the code cache transitions RX to RWX for the update and then
    // back to RX after the update.
    base_flags = MAP_PRIVATE | MAP_ANON;
    data_pages = MemMap::MapAnonymous(
        data_cache_name.c_str(),
        data_capacity + exec_capacity,
        kProtRW,
        /* low_4gb= */ true,
        &error_str);
  }

  if (!data_pages.IsValid()) {
    std::ostringstream oss;
    oss << "Failed to create read write cache: " << error_str << " size=" << capacity;
    *error_msg = oss.str();
    return false;
  }

  MemMap exec_pages;
  MemMap non_exec_pages;
  if (exec_capacity > 0) {
    uint8_t* const divider = data_pages.Begin() + data_capacity;
    // Set initial permission for executable view to catch any SELinux permission problems early
    // (for processes that cannot map WX pages). Otherwise, this region does not need to be
    // executable as there is no code in the cache yet.
    exec_pages = data_pages.RemapAtEnd(divider,
                                       exec_cache_name.c_str(),
                                       kProtRX,
                                       base_flags | MAP_FIXED,
                                       mem_fd.get(),
                                       (mem_fd.get() >= 0) ? data_capacity : 0,
                                       &error_str);
    if (!exec_pages.IsValid()) {
      std::ostringstream oss;
      oss << "Failed to create read execute code cache: " << error_str << " size=" << capacity;
      *error_msg = oss.str();
      return false;
    }

    if (mem_fd.get() >= 0) {
      // For dual view, create the secondary view of code memory used for updating code. This view
      // is never executable.
      std::string name = exec_cache_name + "-rw";
      non_exec_pages = MemMap::MapFile(exec_capacity,
                                       kProtR,
                                       base_flags,
                                       mem_fd,
                                       /* start= */ data_capacity,
                                       /* low_4GB= */ false,
                                       name.c_str(),
                                       &error_str);
      if (!non_exec_pages.IsValid()) {
        static const char* kFailedNxView = "Failed to map non-executable view of JIT code cache";
        if (rwx_memory_allowed) {
          // Log and continue as single view JIT (requires RWX memory).
          VLOG(jit) << kFailedNxView;
        } else {
          *error_msg = kFailedNxView;
          return false;
        }
      }
    }
  } else {
    // Profiling only. No memory for code required.
  }

  data_pages_ = std::move(data_pages);
  exec_pages_ = std::move(exec_pages);
  non_exec_pages_ = std::move(non_exec_pages);
  return true;
}

void JitMemoryRegion::InitializeState(size_t initial_capacity, size_t max_capacity) {
  CHECK_GE(max_capacity, initial_capacity);
  CHECK(max_capacity <= 1 * GB) << "The max supported size for JIT code cache is 1GB";
  // Align both capacities to page size, as that's the unit mspaces use.
  initial_capacity_ = RoundDown(initial_capacity, 2 * kPageSize);
  max_capacity_ = RoundDown(max_capacity, 2 * kPageSize);
  current_capacity_ = initial_capacity,
  data_end_ = initial_capacity / kCodeAndDataCapacityDivider;
  exec_end_ = initial_capacity - data_end_;
}

void JitMemoryRegion::InitializeSpaces() {
  // Initialize the data heap
  data_mspace_ = create_mspace_with_base(data_pages_.Begin(), data_end_, false /*locked*/);
  CHECK(data_mspace_ != nullptr) << "create_mspace_with_base (data) failed";

  // Initialize the code heap
  MemMap* code_heap = nullptr;
  if (non_exec_pages_.IsValid()) {
    code_heap = &non_exec_pages_;
  } else if (exec_pages_.IsValid()) {
    code_heap = &exec_pages_;
  }
  if (code_heap != nullptr) {
    // Make all pages reserved for the code heap writable. The mspace allocator, that manages the
    // heap, will take and initialize pages in create_mspace_with_base().
    CheckedCall(mprotect, "create code heap", code_heap->Begin(), code_heap->Size(), kProtRW);
    exec_mspace_ = create_mspace_with_base(code_heap->Begin(), exec_end_, false /*locked*/);
    CHECK(exec_mspace_ != nullptr) << "create_mspace_with_base (exec) failed";
    SetFootprintLimit(initial_capacity_);
    // Protect pages containing heap metadata. Updates to the code heap toggle write permission to
    // perform the update and there are no other times write access is required.
    CheckedCall(mprotect, "protect code heap", code_heap->Begin(), code_heap->Size(), kProtR);
  } else {
    exec_mspace_ = nullptr;
    SetFootprintLimit(initial_capacity_);
  }
}

void JitMemoryRegion::SetFootprintLimit(size_t new_footprint) {
  size_t data_space_footprint = new_footprint / kCodeAndDataCapacityDivider;
  DCHECK(IsAlignedParam(data_space_footprint, kPageSize));
  DCHECK_EQ(data_space_footprint * kCodeAndDataCapacityDivider, new_footprint);
  mspace_set_footprint_limit(data_mspace_, data_space_footprint);
  if (HasCodeMapping()) {
    ScopedCodeCacheWrite scc(*this);
    mspace_set_footprint_limit(exec_mspace_, new_footprint - data_space_footprint);
  }
}

bool JitMemoryRegion::IncreaseCodeCacheCapacity() {
  if (current_capacity_ == max_capacity_) {
    return false;
  }

  // Double the capacity if we're below 1MB, or increase it by 1MB if
  // we're above.
  if (current_capacity_ < 1 * MB) {
    current_capacity_ *= 2;
  } else {
    current_capacity_ += 1 * MB;
  }
  if (current_capacity_ > max_capacity_) {
    current_capacity_ = max_capacity_;
  }

  VLOG(jit) << "Increasing code cache capacity to " << PrettySize(current_capacity_);

  SetFootprintLimit(current_capacity_);

  return true;
}

// NO_THREAD_SAFETY_ANALYSIS as this is called from mspace code, at which point the lock
// is already held.
void* JitMemoryRegion::MoreCore(const void* mspace, intptr_t increment) NO_THREAD_SAFETY_ANALYSIS {
  if (mspace == exec_mspace_) {
    DCHECK(exec_mspace_ != nullptr);
    const MemMap* const code_pages = GetUpdatableCodeMapping();
    void* result = code_pages->Begin() + exec_end_;
    exec_end_ += increment;
    return result;
  } else {
    DCHECK_EQ(data_mspace_, mspace);
    void* result = data_pages_.Begin() + data_end_;
    data_end_ += increment;
    return result;
  }
}

uint8_t* JitMemoryRegion::AllocateCode(size_t code_size) {
  // Each allocation should be on its own set of cache lines.
  // `code_size` covers the OatQuickMethodHeader, the JIT generated machine code,
  // and any alignment padding.
  size_t alignment = GetInstructionSetAlignment(kRuntimeISA);
  size_t header_size = RoundUp(sizeof(OatQuickMethodHeader), alignment);
  DCHECK_GT(code_size, header_size);
  uint8_t* result = reinterpret_cast<uint8_t*>(
      mspace_memalign(exec_mspace_, kJitCodeAlignment, code_size));
  // Ensure the header ends up at expected instruction alignment.
  DCHECK_ALIGNED_PARAM(reinterpret_cast<uintptr_t>(result + header_size), alignment);
  used_memory_for_code_ += mspace_usable_size(result);
  return result;
}

void JitMemoryRegion::FreeCode(uint8_t* code) {
  code = GetNonExecutableAddress(code);
  used_memory_for_code_ -= mspace_usable_size(code);
  mspace_free(exec_mspace_, code);
}

uint8_t* JitMemoryRegion::AllocateData(size_t data_size) {
  void* result = mspace_malloc(data_mspace_, data_size);
  used_memory_for_data_ += mspace_usable_size(result);
  return reinterpret_cast<uint8_t*>(result);
}

void JitMemoryRegion::FreeData(uint8_t* data) {
  used_memory_for_data_ -= mspace_usable_size(data);
  mspace_free(data_mspace_, data);
}

}  // namespace jit
}  // namespace art
