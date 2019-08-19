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

#include "debugger_interface.h"

#include <android-base/logging.h>

#include "base/array_ref.h"
#include "base/bit_utils.h"
#include "base/logging.h"
#include "base/mutex.h"
#include "base/time_utils.h"
#include "base/utils.h"
#include "dex/dex_file.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jit/jit_memory_region.h"
#include "runtime.h"
#include "thread-current-inl.h"
#include "thread.h"

#include <atomic>
#include <cstddef>

//
// Debug interface for native tools (gdb, lldb, libunwind, simpleperf).
//
// See http://sourceware.org/gdb/onlinedocs/gdb/Declarations.html
//
// There are two ways for native tools to access the debug data safely:
//
// 1) Synchronously, by setting a breakpoint in the __*_debug_register_code
//    method, which is called after every modification of the linked list.
//    GDB does this, but it is complex to set up and it stops the process.
//
// 2) Asynchronously, by monitoring the action_seqlock_.
//   * The seqlock is a monotonically increasing counter which is incremented
//     before and after every modification of the linked list. Odd value of
//     the counter means the linked list is being modified (it is locked).
//   * The tool should read the value of the seqlock both before and after
//     copying the linked list.  If the seqlock values match and are even,
//     the copy is consistent.  Otherwise, the reader should try again.
//     * Note that using the data directly while is it being modified
//       might crash the tool.  Therefore, the only safe way is to make
//       a copy and use the copy only after the seqlock has been checked.
//     * Note that the process might even free and munmap the data while
//       it is being copied, therefore the reader should either handle
//       SEGV or use OS calls to read the memory (e.g. process_vm_readv).
//   * The seqlock can be used to determine the number of modifications of
//     the linked list, which can be used to intelligently cache the data.
//     Note the possible overflow of the seqlock.  It is intentionally
//     32-bit, since 64-bit atomics can be tricky on some architectures.
//   * The timestamps on the entry record the time when the entry was
//     created which is relevant if the unwinding is not live and is
//     postponed until much later.  All timestamps must be unique.
//   * Memory barriers are used to make it possible to reason about
//     the data even when it is being modified (e.g. the process crashed
//     while that data was locked, and thus it will be never unlocked).
//     * In particular, it should be possible to:
//       1) read the seqlock and then the linked list head pointer.
//       2) copy the entry and check that seqlock has not changed.
//       3) copy the symfile and check that seqlock has not changed.
//       4) go back to step 2 using the next pointer (if non-null).
//       This safely creates copy of all symfiles, although other data
//       might be inconsistent/unusable (e.g. prev_, action_timestamp_).
//   * For full conformance with the C++ memory model, all seqlock
//     protected accesses should be atomic. We currently do this in the
//     more critical cases. The rest will have to be fixed before
//     attempting to run TSAN on this code.
//

namespace art {

static Mutex g_jit_debug_lock("JIT native debug entries", kNativeDebugInterfaceLock);
static Mutex g_dex_debug_lock("DEX native debug entries", kNativeDebugInterfaceLock);

extern "C" {
  enum JITAction {
    JIT_NOACTION = 0,
    JIT_REGISTER_FN,
    JIT_UNREGISTER_FN
  };

  // Public/stable binary interface.
  struct JITCodeEntryPublic {
    // Atomic to ensure the reader can always iterate over the linked list
    // (e.g. the process could crash in the middle of writing this field).
    std::atomic<const JITCodeEntry*> next_;
    const JITCodeEntry* prev_;     // For linked list deletion.  Unused in readers.
    const uint8_t* symfile_addr_;  // Address of the in-memory ELF file.
    uint64_t symfile_size_;        // Beware of the offset (12 on x86; but 16 on ARM32).

    // Android-specific fields:
    uint64_t register_timestamp_;  // CLOCK_MONOTONIC time of entry registration.
  };

  // Implementation-specific fields (which can be used only in this file).
  struct JITCodeEntry : public JITCodeEntryPublic {
    // Unpacked entries: Code address of the symbol in the ELF file.
    // Packed entries: The start address of the covered memory range.
    const void* addr_ = nullptr;
    // Allow merging of ELF files to save space.
    // Packing drops advanced DWARF data, so it is not always desirable.
    bool allow_packing_ = false;
    // Whether this entry has been LZMA compressed.
    // Compression is expensive, so we don't always do it.
    bool is_compressed_ = false;
  };

  struct JITDescriptor {
    uint32_t version_ = 1;                            // NB: GDB supports only version 1.
    uint32_t action_flag_ = JIT_NOACTION;             // One of the JITAction enum values.
    const JITCodeEntry* relevant_entry_ = nullptr;    // The entry affected by the action.
    std::atomic<const JITCodeEntry*> head_{nullptr};  // Head of link list of all entries.

    // Android-specific fields:
    uint8_t magic_[8] = {'A', 'n', 'd', 'r', 'o', 'i', 'd', '1'};
    uint32_t flags_ = 0;  // Reserved for future use. Must be 0.
    uint32_t sizeof_descriptor = sizeof(JITDescriptor);
    uint32_t sizeof_entry = sizeof(JITCodeEntryPublic);
    std::atomic_uint32_t action_seqlock_{0};  // Incremented before and after any modification.
    uint64_t action_timestamp_ = 1;           // CLOCK_MONOTONIC time of last action.
  };

  // Check that std::atomic has the expected layout.
  static_assert(alignof(std::atomic_uint32_t) == alignof(uint32_t), "Weird alignment");
  static_assert(sizeof(std::atomic_uint32_t) == sizeof(uint32_t), "Weird size");
  static_assert(alignof(std::atomic<void*>) == alignof(void*), "Weird alignment");
  static_assert(sizeof(std::atomic<void*>) == sizeof(void*), "Weird size");

  // GDB may set breakpoint here. We must ensure it is not removed or deduplicated.
  void __attribute__((noinline)) __jit_debug_register_code() {
    __asm__("");
  }

  // Alternatively, native tools may overwrite this field to execute custom handler.
  void (*__jit_debug_register_code_ptr)() = __jit_debug_register_code;

  // The root data structure describing of all JITed methods.
  JITDescriptor __jit_debug_descriptor GUARDED_BY(g_jit_debug_lock) {};

  // The following globals mirror the ones above, but are used to register dex files.
  void __attribute__((noinline)) __dex_debug_register_code() {
    __asm__("");
  }
  void (*__dex_debug_register_code_ptr)() = __dex_debug_register_code;
  JITDescriptor __dex_debug_descriptor GUARDED_BY(g_dex_debug_lock) {};
}

struct DexNativeInfo {
  static constexpr bool kCopySymfileData = false;  // Just reference DEX files.
  static JITDescriptor& Descriptor() { return __dex_debug_descriptor; }
  static void NotifyNativeDebugger() { __dex_debug_register_code_ptr(); }
  static const void* Alloc(size_t size) { return malloc(size); }
  static void Free(const void* ptr) { free(const_cast<void*>(ptr)); }
  template<class T> static T* Writable(const T* v) { return const_cast<T*>(v); }
};

struct JitNativeInfo {
  static constexpr bool kCopySymfileData = true;  // Copy debug info to JIT memory.
  static JITDescriptor& Descriptor() { return __jit_debug_descriptor; }
  static void NotifyNativeDebugger() { __jit_debug_register_code_ptr(); }
  static const void* Alloc(size_t size) { return Memory()->AllocateData(size); }
  static void Free(const void* ptr) {
    Memory()->FreeData(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(ptr)));
  }
  static void Free(void* ptr) = delete;
  template<class T> static T* Writable(const T* v) {
    return const_cast<T*>(Memory()->GetWritableDataAddress(v));
  }

  static jit::JitMemoryRegion* Memory() ASSERT_CAPABILITY(Locks::jit_lock_) {
    Locks::jit_lock_->AssertHeld(Thread::Current());
    jit::JitCodeCache* jit_code_cache = Runtime::Current()->GetJitCodeCache();
    CHECK(jit_code_cache != nullptr);
    jit::JitMemoryRegion* memory = jit_code_cache->GetCurrentRegion();
    CHECK(memory->IsValid());
    return memory;
  }
};

ArrayRef<const uint8_t> GetJITCodeEntrySymFile(const JITCodeEntry* entry) {
  return ArrayRef<const uint8_t>(entry->symfile_addr_, entry->symfile_size_);
}

// Mark the descriptor as "locked", so native tools know the data is being modified.
static void ActionSeqlock(JITDescriptor& descriptor) {
  DCHECK_EQ(descriptor.action_seqlock_.load() & 1, 0u) << "Already locked";
  descriptor.action_seqlock_.fetch_add(1, std::memory_order_relaxed);
  // Ensure that any writes within the locked section cannot be reordered before the increment.
  std::atomic_thread_fence(std::memory_order_release);
}

// Mark the descriptor as "unlocked", so native tools know the data is safe to read.
static void ActionSequnlock(JITDescriptor& descriptor) {
  DCHECK_EQ(descriptor.action_seqlock_.load() & 1, 1u) << "Already unlocked";
  // Ensure that any writes within the locked section cannot be reordered after the increment.
  std::atomic_thread_fence(std::memory_order_release);
  descriptor.action_seqlock_.fetch_add(1, std::memory_order_relaxed);
}

template<class NativeInfo>
static const JITCodeEntry* CreateJITCodeEntryInternal(
    ArrayRef<const uint8_t> symfile,
    const void* addr = nullptr,
    bool allow_packing = false,
    bool is_compressed = false) {
  JITDescriptor& descriptor = NativeInfo::Descriptor();

  // Make a copy of the buffer to shrink it and to pass ownership to JITCodeEntry.
  const uint8_t* copy = nullptr;
  if (NativeInfo::kCopySymfileData) {
    copy = reinterpret_cast<const uint8_t*>(NativeInfo::Alloc(symfile.size()));
    if (copy == nullptr) {
      LOG(ERROR) << "Failed to allocate memory for native debug info";
      return nullptr;
    }
    memcpy(NativeInfo::Writable(copy), symfile.data(), symfile.size());
    symfile = ArrayRef<const uint8_t>(copy, symfile.size());
  }

  // Ensure the timestamp is monotonically increasing even in presence of low
  // granularity system timer.  This ensures each entry has unique timestamp.
  uint64_t timestamp = std::max(descriptor.action_timestamp_ + 1, NanoTime());

  const JITCodeEntry* head = descriptor.head_.load(std::memory_order_relaxed);
  const void* memory = NativeInfo::Alloc(sizeof(JITCodeEntry));
  if (memory == nullptr) {
    LOG(ERROR) << "Failed to allocate memory for native debug info";
    if (copy != nullptr) {
      NativeInfo::Free(copy);
    }
    return nullptr;
  }
  const JITCodeEntry* entry = reinterpret_cast<const JITCodeEntry*>(memory);
  JITCodeEntry* writable_entry = NativeInfo::Writable(entry);
  writable_entry->symfile_addr_ = symfile.data();
  writable_entry->symfile_size_ = symfile.size();
  writable_entry->prev_ = nullptr;
  writable_entry->next_.store(head, std::memory_order_relaxed);
  writable_entry->register_timestamp_ = timestamp;
  writable_entry->addr_ = addr;
  writable_entry->allow_packing_ = allow_packing;
  writable_entry->is_compressed_ = is_compressed;

  // We are going to modify the linked list, so take the seqlock.
  ActionSeqlock(descriptor);
  if (head != nullptr) {
    NativeInfo::Writable(head)->prev_ = entry;
  }
  descriptor.head_.store(entry, std::memory_order_relaxed);
  descriptor.relevant_entry_ = entry;
  descriptor.action_flag_ = JIT_REGISTER_FN;
  descriptor.action_timestamp_ = timestamp;
  ActionSequnlock(descriptor);

  NativeInfo::NotifyNativeDebugger();

  return entry;
}

template<class NativeInfo>
static void DeleteJITCodeEntryInternal(const JITCodeEntry* entry) {
  CHECK(entry != nullptr);
  const uint8_t* symfile = entry->symfile_addr_;
  JITDescriptor& descriptor = NativeInfo::Descriptor();

  // Ensure the timestamp is monotonically increasing even in presence of low
  // granularity system timer.  This ensures each entry has unique timestamp.
  uint64_t timestamp = std::max(descriptor.action_timestamp_ + 1, NanoTime());

  // We are going to modify the linked list, so take the seqlock.
  ActionSeqlock(descriptor);
  const JITCodeEntry* next = entry->next_.load(std::memory_order_relaxed);
  if (entry->prev_ != nullptr) {
    NativeInfo::Writable(entry->prev_)->next_.store(next, std::memory_order_relaxed);
  } else {
    descriptor.head_.store(next, std::memory_order_relaxed);
  }
  if (next != nullptr) {
    NativeInfo::Writable(next)->prev_ = entry->prev_;
  }
  descriptor.relevant_entry_ = entry;
  descriptor.action_flag_ = JIT_UNREGISTER_FN;
  descriptor.action_timestamp_ = timestamp;
  ActionSequnlock(descriptor);

  NativeInfo::NotifyNativeDebugger();

  // Ensure that clear below can not be reordered above the unlock above.
  std::atomic_thread_fence(std::memory_order_release);

  // Aggressively clear the entry as an extra check of the synchronisation.
  memset(NativeInfo::Writable(entry), 0, sizeof(*entry));

  NativeInfo::Free(entry);
  if (NativeInfo::kCopySymfileData) {
    NativeInfo::Free(symfile);
  }
}

void AddNativeDebugInfoForDex(Thread* self, const DexFile* dexfile) {
  MutexLock mu(self, g_dex_debug_lock);
  DCHECK(dexfile != nullptr);
  const ArrayRef<const uint8_t> symfile(dexfile->Begin(), dexfile->Size());
  CreateJITCodeEntryInternal<DexNativeInfo>(symfile);
}

void RemoveNativeDebugInfoForDex(Thread* self, const DexFile* dexfile) {
  MutexLock mu(self, g_dex_debug_lock);
  DCHECK(dexfile != nullptr);
  // We register dex files in the class linker and free them in DexFile_closeDexFile, but
  // there might be cases where we load the dex file without using it in the class linker.
  // On the other hand, single dex file might also be used with different class-loaders.
  for (const JITCodeEntry* entry = __dex_debug_descriptor.head_; entry != nullptr; ) {
    const JITCodeEntry* next = entry->next_;  // Save next pointer before we free the memory.
    if (entry->symfile_addr_ == dexfile->Begin()) {
      DeleteJITCodeEntryInternal<DexNativeInfo>(entry);
    }
    entry = next;
  }
}

// Size of JIT code range covered by each packed JITCodeEntry.
static constexpr uint32_t kJitRepackGroupSize = 64 * KB;

// Automatically call the repack method every 'n' new entries.
static constexpr uint32_t kJitRepackFrequency = 64;
static uint32_t g_jit_num_unpacked_entries = 0;

// Split the JIT code cache into groups of fixed size and create single JITCodeEntry for each group.
// The start address of method's code determines which group it belongs to.  The end is irrelevant.
// New mini debug infos will be merged if possible, and entries for GCed functions will be removed.
static void RepackEntries(bool compress, ArrayRef<const void*> removed)
    REQUIRES(g_jit_debug_lock) {
  DCHECK(std::is_sorted(removed.begin(), removed.end()));
  jit::Jit* jit = Runtime::Current()->GetJit();
  if (jit == nullptr) {
    return;
  }

  // Collect entries that we want to pack.
  std::vector<const JITCodeEntry*> entries;
  entries.reserve(2 * kJitRepackFrequency);
  for (const JITCodeEntry* it = __jit_debug_descriptor.head_; it != nullptr; it = it->next_) {
    if (it->allow_packing_) {
      if (!compress && it->is_compressed_ && removed.empty()) {
        continue;  // If we are not compressing, also avoid decompressing.
      }
      entries.push_back(it);
    }
  }
  auto cmp = [](const JITCodeEntry* l, const JITCodeEntry* r) { return l->addr_ < r->addr_; };
  std::sort(entries.begin(), entries.end(), cmp);  // Sort by address.

  // Process the entries in groups (each spanning memory range of size kJitRepackGroupSize).
  for (auto group_it = entries.begin(); group_it != entries.end();) {
    const void* group_ptr = AlignDown((*group_it)->addr_, kJitRepackGroupSize);
    const void* group_end = reinterpret_cast<const uint8_t*>(group_ptr) + kJitRepackGroupSize;

    // Find all entries in this group (each entry is an in-memory ELF file).
    auto begin = group_it;
    auto end = std::find_if(begin, entries.end(), [=](auto* e) { return e->addr_ >= group_end; });
    CHECK(end > begin);
    ArrayRef<const JITCodeEntry*> elfs(&*begin, end - begin);

    // Find all symbols that have been removed in this memory range.
    auto removed_begin = std::lower_bound(removed.begin(), removed.end(), group_ptr);
    auto removed_end = std::lower_bound(removed.begin(), removed.end(), group_end);
    CHECK(removed_end >= removed_begin);
    ArrayRef<const void*> removed_subset(&*removed_begin, removed_end - removed_begin);

    // Bail out early if there is nothing to do for this group.
    if (elfs.size() == 1 && removed_subset.empty() && (*begin)->is_compressed_ == compress) {
      group_it = end;  // Go to next group.
      continue;
    }

    // Create new single JITCodeEntry that covers this memory range.
    uint64_t start_time = MicroTime();
    size_t live_symbols;
    std::vector<uint8_t> packed = jit->GetJitCompiler()->PackElfFileForJIT(
        elfs, removed_subset, compress, &live_symbols);
    VLOG(jit)
        << "JIT mini-debug-info repacked"
        << " for " << group_ptr
        << " in " << MicroTime() - start_time << "us"
        << " elfs=" << elfs.size()
        << " dead=" << removed_subset.size()
        << " live=" << live_symbols
        << " size=" << packed.size() << (compress ? "(lzma)" : "");

    // Replace the old entries with the new one (with their lifetime temporally overlapping).
    CreateJITCodeEntryInternal<JitNativeInfo>(ArrayRef<const uint8_t>(packed),
                                              /*addr_=*/ group_ptr,
                                              /*allow_packing_=*/ true,
                                              /*is_compressed_=*/ compress);
    for (auto it : elfs) {
      DeleteJITCodeEntryInternal<JitNativeInfo>(/*entry=*/ it);
    }
    group_it = end;  // Go to next group.
  }
  g_jit_num_unpacked_entries = 0;
}

void AddNativeDebugInfoForJit(const void* code_ptr,
                              const std::vector<uint8_t>& symfile,
                              bool allow_packing) {
  MutexLock mu(Thread::Current(), g_jit_debug_lock);
  DCHECK_NE(symfile.size(), 0u);

  if (Runtime::Current()->IsZygote()) {
    return;  // TODO: Implement memory sharing with the zygote process.
  }

  CreateJITCodeEntryInternal<JitNativeInfo>(ArrayRef<const uint8_t>(symfile),
                                            /*addr=*/ code_ptr,
                                            /*allow_packing=*/ allow_packing,
                                            /*is_compressed=*/ false);

  VLOG(jit)
      << "JIT mini-debug-info added"
      << " for " << code_ptr
      << " size=" << PrettySize(symfile.size());

  // Automatically repack entries on regular basis to save space.
  // Pack (but don't compress) recent entries - this is cheap and reduces memory use by ~4x.
  // We delay compression until after GC since it is more expensive (and saves further ~4x).
  if (++g_jit_num_unpacked_entries >= kJitRepackFrequency) {
    RepackEntries(/*compress=*/ false, /*removed=*/ ArrayRef<const void*>());
  }
}

void RemoveNativeDebugInfoForJit(ArrayRef<const void*> removed) {
  MutexLock mu(Thread::Current(), g_jit_debug_lock);
  RepackEntries(/*compress=*/ true, removed);

  // Remove entries which are not allowed to be packed (containing single method each).
  for (const JITCodeEntry* it = __jit_debug_descriptor.head_; it != nullptr; it = it->next_) {
    if (!it->allow_packing_ && std::binary_search(removed.begin(), removed.end(), it->addr_)) {
      DeleteJITCodeEntryInternal<JitNativeInfo>(/*entry=*/ it);
    }
  }
}

size_t GetJitMiniDebugInfoMemUsage() {
  MutexLock mu(Thread::Current(), g_jit_debug_lock);
  size_t size = 0;
  for (const JITCodeEntry* it = __jit_debug_descriptor.head_; it != nullptr; it = it->next_) {
    size += sizeof(JITCodeEntry) + it->symfile_size_;
  }
  return size;
}

Mutex* GetNativeDebugInfoLock() {
  return &g_jit_debug_lock;
}

}  // namespace art
