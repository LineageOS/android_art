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
// 2) Asynchronously, using the entry seqlocks.
//   * The seqlock is a monotonically increasing counter, which
//     is even if the entry is valid and odd if it is invalid.
//     It is set to even value after all other fields are set,
//     and it is set to odd value before the entry is deleted.
//   * This makes it possible to safely read the symfile data:
//     * The reader should read the value of the seqlock both
//       before and after reading the symfile. If the seqlock
//       values match and are even the copy is consistent.
//   * Entries are recycled, but never freed, which guarantees
//     that the seqlock is not overwritten by a random value.
//   * The linked-list is one level higher.  The next-pointer
//     must always point to an entry with even seqlock, which
//     ensures that entries of a crashed process can be read.
//     This means the entry must be added after it is created
//     and it must be removed before it is invalidated (odd).
//   * When iterating over the linked list the reader can use
//     the timestamps to ensure that current and next entry
//     were not deleted using the following steps:
//       1) Read next pointer and the next entry's seqlock.
//       2) Read the symfile and re-read the next pointer.
//       3) Re-read both the current and next seqlock.
//       4) Go to step 1 with using new entry and seqlock.
//

namespace art {

static Mutex g_jit_debug_lock("JIT native debug entries", kNativeDebugInterfaceLock);
static Mutex g_dex_debug_lock("DEX native debug entries", kNativeDebugInterfaceLock);

// Most loads and stores need no synchronization since all memory is protected by the global locks.
// Some writes are synchronized so libunwindstack can read the memory safely from another process.
constexpr std::memory_order kNonRacingRelaxed = std::memory_order_relaxed;

// Public binary interface between ART and native tools (gdb, libunwind, etc).
extern "C" {
  enum JITAction {
    JIT_NOACTION = 0,
    JIT_REGISTER_FN,
    JIT_UNREGISTER_FN
  };

  // Public/stable binary interface.
  struct JITCodeEntryPublic {
    std::atomic<const JITCodeEntry*> next_;  // Atomic to guarantee consistency after crash.
    const JITCodeEntry* prev_ = nullptr;     // For linked list deletion.  Unused in readers.
    const uint8_t* symfile_addr_ = nullptr;  // Address of the in-memory ELF file.
    uint64_t symfile_size_ = 0;              // Note that the offset is 12 on x86, but 16 on ARM32.

    // Android-specific fields:
    std::atomic_uint32_t seqlock_{1};        // Synchronization. Even value if entry is valid.
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

  // Public/stable binary interface.
  struct JITDescriptorPublic {
    uint32_t version_ = 1;                            // NB: GDB supports only version 1.
    uint32_t action_flag_ = JIT_NOACTION;             // One of the JITAction enum values.
    const JITCodeEntry* relevant_entry_ = nullptr;    // The entry affected by the action.
    std::atomic<const JITCodeEntry*> head_{nullptr};  // Head of link list of all entries.
  };

  // Implementation-specific fields (which can be used only in this file).
  struct JITDescriptor : public JITDescriptorPublic {
    const JITCodeEntry* free_entries_ = nullptr;  // List of deleted entries ready for reuse.

    // Used for memory sharing with zygote. See NativeDebugInfoPreFork().
    const JITCodeEntry* zygote_head_entry_ = nullptr;
    JITCodeEntry application_tail_entry_{};
  };

  // Public interface: Can be used by reader to check the structs have the expected size.
  uint32_t g_art_sizeof_jit_code_entry = sizeof(JITCodeEntryPublic);
  uint32_t g_art_sizeof_jit_descriptor = sizeof(JITDescriptorPublic);

  // Check that std::atomic has the expected layout.
  static_assert(alignof(std::atomic_uint32_t) == alignof(uint32_t), "Weird alignment");
  static_assert(sizeof(std::atomic_uint32_t) == sizeof(uint32_t), "Weird size");
  static_assert(std::atomic_uint32_t::is_always_lock_free, "Expected to be lock free");
  static_assert(alignof(std::atomic<void*>) == alignof(void*), "Weird alignment");
  static_assert(sizeof(std::atomic<void*>) == sizeof(void*), "Weird size");
  static_assert(std::atomic<void*>::is_always_lock_free, "Expected to be lock free");

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
  static void Free(const void* ptr) { Memory()->FreeData(reinterpret_cast<const uint8_t*>(ptr)); }
  static void Free(void* ptr) = delete;

  template<class T> static T* Writable(const T* v) {
    // Special case: This entry is in static memory and not allocated in JIT memory.
    if (v == reinterpret_cast<const void*>(&Descriptor().application_tail_entry_)) {
      return const_cast<T*>(v);
    }
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

// This must be called with the appropriate lock taken (g_{jit,dex}_debug_lock).
template<class NativeInfo>
static const JITCodeEntry* CreateJITCodeEntryInternal(
    ArrayRef<const uint8_t> symfile = ArrayRef<const uint8_t>(),
    const void* addr = nullptr,
    bool allow_packing = false,
    bool is_compressed = false) {
  JITDescriptor& descriptor = NativeInfo::Descriptor();

  // Allocate JITCodeEntry if needed.
  if (descriptor.free_entries_ == nullptr) {
    const void* memory = NativeInfo::Alloc(sizeof(JITCodeEntry));
    if (memory == nullptr) {
      LOG(ERROR) << "Failed to allocate memory for native debug info";
      return nullptr;
    }
    new (NativeInfo::Writable(memory)) JITCodeEntry();
    descriptor.free_entries_ = reinterpret_cast<const JITCodeEntry*>(memory);
  }

  // Make a copy of the buffer to shrink it and to pass ownership to JITCodeEntry.
  if (NativeInfo::kCopySymfileData && !symfile.empty()) {
    const uint8_t* copy = reinterpret_cast<const uint8_t*>(NativeInfo::Alloc(symfile.size()));
    if (copy == nullptr) {
      LOG(ERROR) << "Failed to allocate memory for native debug info";
      return nullptr;
    }
    memcpy(NativeInfo::Writable(copy), symfile.data(), symfile.size());
    symfile = ArrayRef<const uint8_t>(copy, symfile.size());
  }

  // Zygote must insert entries at specific place.  See NativeDebugInfoPreFork().
  std::atomic<const JITCodeEntry*>* head = &descriptor.head_;
  const JITCodeEntry* prev = nullptr;
  if (Runtime::Current()->IsZygote() && descriptor.zygote_head_entry_ != nullptr) {
    head = &NativeInfo::Writable(descriptor.zygote_head_entry_)->next_;
    prev = descriptor.zygote_head_entry_;
  }
  const JITCodeEntry* next = head->load(kNonRacingRelaxed);

  // Pop entry from the free list.
  const JITCodeEntry* entry = descriptor.free_entries_;
  descriptor.free_entries_ = descriptor.free_entries_->next_.load(kNonRacingRelaxed);
  CHECK_EQ(entry->seqlock_.load(kNonRacingRelaxed) & 1, 1u) << "Expected invalid entry";

  // Create the entry and set all its fields.
  JITCodeEntry* writable_entry = NativeInfo::Writable(entry);
  writable_entry->next_.store(next, std::memory_order_relaxed);
  writable_entry->prev_ = prev;
  writable_entry->symfile_addr_ = symfile.data();
  writable_entry->symfile_size_ = symfile.size();
  writable_entry->addr_ = addr;
  writable_entry->allow_packing_ = allow_packing;
  writable_entry->is_compressed_ = is_compressed;
  writable_entry->seqlock_.fetch_add(1, std::memory_order_release);  // Mark as valid.

  // Add the entry to the main link-list.
  if (next != nullptr) {
    NativeInfo::Writable(next)->prev_ = entry;
  }
  head->store(entry, std::memory_order_release);
  descriptor.relevant_entry_ = entry;
  descriptor.action_flag_ = JIT_REGISTER_FN;
  NativeInfo::NotifyNativeDebugger();

  return entry;
}

template<class NativeInfo>
static void DeleteJITCodeEntryInternal(const JITCodeEntry* entry) {
  CHECK(entry != nullptr);
  const uint8_t* symfile = entry->symfile_addr_;
  JITDescriptor& descriptor = NativeInfo::Descriptor();

  // Remove the entry from the main linked-list.
  const JITCodeEntry* next = entry->next_.load(kNonRacingRelaxed);
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
  NativeInfo::NotifyNativeDebugger();

  // Delete the entry.
  JITCodeEntry* writable_entry = NativeInfo::Writable(entry);
  CHECK_EQ(writable_entry->seqlock_.load(kNonRacingRelaxed) & 1, 0u) << "Expected valid entry";
  // Release: Ensures that "next_" points to valid entry at any time in reader.
  writable_entry->seqlock_.fetch_add(1, std::memory_order_release);  // Mark as invalid.
  // Release: Ensures that the entry is seen as invalid before it's data is freed.
  std::atomic_thread_fence(std::memory_order_release);
  if (NativeInfo::kCopySymfileData && symfile != nullptr) {
    NativeInfo::Free(symfile);
  }

  // Push the entry to the free list.
  writable_entry->next_.store(descriptor.free_entries_, kNonRacingRelaxed);
  descriptor.free_entries_ = entry;
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

// Splits the linked linked in to two parts:
// The first part (including the static head pointer) is owned by the application.
// The second part is owned by zygote and might be concurrently modified by it.
//
// We add two empty entries at the boundary which are never removed (app_tail, zygote_head).
// These entries are needed to preserve the next/prev pointers in the linked list,
// since zygote can not modify the application's data and vice versa.
//
//          <--- owned by the application memory ---> <--- owned by zygote memory --->
//         |----------------------|------------------|-------------|-----------------|
// head -> | application_entries* | application_tail | zygote_head | zygote_entries* |
//         |----------------------|------------------|-------------|-----------------|
//
void NativeDebugInfoPreFork() {
  CHECK(Runtime::Current()->IsZygote());
  JITDescriptor& descriptor = JitNativeInfo::Descriptor();
  if (descriptor.zygote_head_entry_ != nullptr) {
    return;  // Already done - we need to do this only on the first fork.
  }

  // Create the zygote-owned head entry (with no ELF file).
  // The data will be allocated from the current JIT memory (owned by zygote).
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);  // Needed to alloc entry.
  const JITCodeEntry* zygote_head = CreateJITCodeEntryInternal<JitNativeInfo>();
  CHECK(zygote_head != nullptr);
  descriptor.zygote_head_entry_ = zygote_head;

  // Create the child-owned tail entry (with no ELF file).
  // The data is statically allocated since it must be owned by the forked process.
  JITCodeEntry* app_tail = &descriptor.application_tail_entry_;
  app_tail->next_ = zygote_head;
  app_tail->seqlock_.store(2, kNonRacingRelaxed);  // Mark as valid.
  descriptor.head_.store(app_tail, std::memory_order_release);
}

void NativeDebugInfoPostFork() {
  JITDescriptor& descriptor = JitNativeInfo::Descriptor();
  if (!Runtime::Current()->IsZygote()) {
    descriptor.free_entries_ = nullptr;  // Don't reuse zygote's entries.
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
  JITDescriptor& descriptor = __jit_debug_descriptor;
  bool is_zygote = Runtime::Current()->IsZygote();

  // Collect entries that we want to pack.
  std::vector<const JITCodeEntry*> entries;
  entries.reserve(2 * kJitRepackFrequency);
  for (const JITCodeEntry* it = descriptor.head_; it != nullptr; it = it->next_) {
    if (it == descriptor.zygote_head_entry_ && !is_zygote) {
      break;  // Memory owned by the zygote process (read-only for an app).
    }
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
  // Always compress zygote, since it does not GC and we want to keep the high-water mark low.
  if (++g_jit_num_unpacked_entries >= kJitRepackFrequency) {
    bool is_zygote = Runtime::Current()->IsZygote();
    RepackEntries(/*compress=*/ is_zygote, /*removed=*/ ArrayRef<const void*>());
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
