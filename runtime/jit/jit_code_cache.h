/*
 * Copyright 2014 The Android Open Source Project
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

#ifndef ART_RUNTIME_JIT_JIT_CODE_CACHE_H_
#define ART_RUNTIME_JIT_JIT_CODE_CACHE_H_

#include <iosfwd>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "base/arena_containers.h"
#include "base/array_ref.h"
#include "base/atomic.h"
#include "base/histogram.h"
#include "base/macros.h"
#include "base/mem_map.h"
#include "base/mutex.h"
#include "base/safe_map.h"
#include "jit_memory_region.h"

namespace art {

class ArtMethod;
template<class T> class Handle;
class LinearAlloc;
class InlineCache;
class IsMarkedVisitor;
class JitJniStubTestHelper;
class OatQuickMethodHeader;
struct ProfileMethodInfo;
class ProfilingInfo;
class Thread;

namespace gc {
namespace accounting {
template<size_t kAlignment> class MemoryRangeBitmap;
}  // namespace accounting
}  // namespace gc

namespace mirror {
class Class;
class Object;
template<class T> class ObjectArray;
}  // namespace mirror

namespace gc {
namespace accounting {
template<size_t kAlignment> class MemoryRangeBitmap;
}  // namespace accounting
}  // namespace gc

namespace mirror {
class Class;
class Object;
template<class T> class ObjectArray;
}  // namespace mirror

namespace jit {

class MarkCodeClosure;

// Type of bitmap used for tracking live functions in the JIT code cache for the purposes
// of garbage collecting code.
using CodeCacheBitmap = gc::accounting::MemoryRangeBitmap<kJitCodeAccountingBytes>;

// The state of profile-based compilation in the zygote.
// - kInProgress:      JIT compilation is happening
// - kDone:            JIT compilation is finished, and the zygote is preparing notifying
//                     the other processes.
// - kNotifiedOk:      the zygote has notified the other processes, which can start
//                     sharing the boot image method mappings.
// - kNotifiedFailure: the zygote has notified the other processes, but they
//                     cannot share the boot image method mappings due to
//                     unexpected errors
enum class ZygoteCompilationState : uint8_t {
  kInProgress = 0,
  kDone = 1,
  kNotifiedOk = 2,
  kNotifiedFailure = 3,
};

// Class abstraction over a map of ArtMethod -> compiled code, where the
// ArtMethod are compiled by the zygote, and the map acts as a communication
// channel between the zygote and the other processes.
// For the zygote process, this map is the only map it is placing the compiled
// code. JitCodeCache.method_code_map_ is empty.
//
// This map is writable only by the zygote, and readable by all children.
class ZygoteMap {
 public:
  struct Entry {
    ArtMethod* method;
    // Note we currently only allocate code in the low 4g, so we could just reserve 4 bytes
    // for the code pointer. For simplicity and in the case we move to 64bit
    // addresses for code, just keep it void* for now.
    const void* code_ptr;
  };

  explicit ZygoteMap(JitMemoryRegion* region)
      : map_(), region_(region), compilation_state_(nullptr) {}

  // Initialize the data structure so it can hold `number_of_methods` mappings.
  // Note that the map is fixed size and never grows.
  void Initialize(uint32_t number_of_methods) REQUIRES(!Locks::jit_lock_);

  // Add the mapping method -> code.
  void Put(const void* code, ArtMethod* method) REQUIRES(Locks::jit_lock_);

  // Return the code pointer for the given method. If pc is not zero, check that
  // the pc falls into that code range. Return null otherwise.
  const void* GetCodeFor(ArtMethod* method, uintptr_t pc = 0) const;

  // Return whether the map has associated code for the given method.
  bool ContainsMethod(ArtMethod* method) const {
    return GetCodeFor(method) != nullptr;
  }

  void SetCompilationState(ZygoteCompilationState state) {
    region_->WriteData(compilation_state_, state);
  }

  bool IsCompilationDoneButNotNotified() const {
    return compilation_state_ != nullptr && *compilation_state_ == ZygoteCompilationState::kDone;
  }

  bool IsCompilationNotified() const {
    return compilation_state_ != nullptr && *compilation_state_ > ZygoteCompilationState::kDone;
  }

  bool CanMapBootImageMethods() const {
    return compilation_state_ != nullptr &&
           *compilation_state_ == ZygoteCompilationState::kNotifiedOk;
  }

  ArrayRef<const Entry>::const_iterator cbegin() const {
    return map_.cbegin();
  }
  ArrayRef<const Entry>::iterator begin() {
    return map_.begin();
  }
  ArrayRef<const Entry>::const_iterator cend() const {
    return map_.cend();
  }
  ArrayRef<const Entry>::iterator end() {
    return map_.end();
  }

 private:
  // The map allocated with `region_`.
  ArrayRef<const Entry> map_;

  // The region in which the map is allocated.
  JitMemoryRegion* const region_;

  // The current state of compilation in the zygote. Starts with kInProgress,
  // and should end with kNotifiedOk or kNotifiedFailure.
  const ZygoteCompilationState* compilation_state_;

  DISALLOW_COPY_AND_ASSIGN(ZygoteMap);
};

class JitCodeCache {
 public:
  static constexpr size_t kMaxCapacity = 64 * MB;
  // Put the default to a very low amount for debug builds to stress the code cache
  // collection.
  static constexpr size_t kInitialCapacity = kIsDebugBuild ? 8 * KB : 64 * KB;

  // By default, do not GC until reaching 256KB.
  static constexpr size_t kReservedCapacity = kInitialCapacity * 4;

  // Create the code cache with a code + data capacity equal to "capacity", error message is passed
  // in the out arg error_msg.
  static JitCodeCache* Create(bool used_only_for_profile_data,
                              bool rwx_memory_allowed,
                              bool is_zygote,
                              std::string* error_msg);
  ~JitCodeCache();

  bool NotifyCompilationOf(ArtMethod* method,
                           Thread* self,
                           bool osr,
                           bool prejit,
                           bool baseline,
                           JitMemoryRegion* region)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::jit_lock_);

  void NotifyMethodRedefined(ArtMethod* method)
      REQUIRES(Locks::mutator_lock_)
      REQUIRES(!Locks::jit_lock_);

  // Notify to the code cache that the compiler wants to use the
  // profiling info of `method` to drive optimizations,
  // and therefore ensure the returned profiling info object is not
  // collected.
  ProfilingInfo* NotifyCompilerUse(ArtMethod* method, Thread* self)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::jit_lock_);

  void DoneCompiling(ArtMethod* method, Thread* self, bool osr)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::jit_lock_);

  void DoneCompilerUse(ArtMethod* method, Thread* self)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::jit_lock_);

  // Return true if the code cache contains this pc.
  bool ContainsPc(const void* pc) const;

  // Return true if the code cache contains this pc in the private region (i.e. not from zygote).
  bool PrivateRegionContainsPc(const void* pc) const;

  // Returns true if either the method's entrypoint is JIT compiled code or it is the
  // instrumentation entrypoint and we can jump to jit code for this method. For testing use only.
  bool WillExecuteJitCode(ArtMethod* method) REQUIRES(!Locks::jit_lock_);

  // Return true if the code cache contains this method.
  bool ContainsMethod(ArtMethod* method) REQUIRES(!Locks::jit_lock_);

  // Return the code pointer for a JNI-compiled stub if the method is in the cache, null otherwise.
  const void* GetJniStubCode(ArtMethod* method) REQUIRES(!Locks::jit_lock_);

  // Allocate a region for both code and data in the JIT code cache.
  // The reserved memory is left completely uninitialized.
  bool Reserve(Thread* self,
               JitMemoryRegion* region,
               size_t code_size,
               size_t stack_map_size,
               size_t number_of_roots,
               ArtMethod* method,
               /*out*/ArrayRef<const uint8_t>* reserved_code,
               /*out*/ArrayRef<const uint8_t>* reserved_data)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::jit_lock_);

  // Initialize code and data of previously allocated memory.
  //
  // `cha_single_implementation_list` needs to be registered via CHA (if it's
  // still valid), since the compiled code still needs to be invalidated if the
  // single-implementation assumptions are violated later. This needs to be done
  // even if `has_should_deoptimize_flag` is false, which can happen due to CHA
  // guard elimination.
  bool Commit(Thread* self,
              JitMemoryRegion* region,
              ArtMethod* method,
              ArrayRef<const uint8_t> reserved_code,  // Uninitialized destination.
              ArrayRef<const uint8_t> code,           // Compiler output (source).
              ArrayRef<const uint8_t> reserved_data,  // Uninitialized destination.
              const std::vector<Handle<mirror::Object>>& roots,
              ArrayRef<const uint8_t> stack_map,      // Compiler output (source).
              bool osr,
              bool has_should_deoptimize_flag,
              const ArenaSet<ArtMethod*>& cha_single_implementation_list)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::jit_lock_);

  // Free the previously allocated memory regions.
  void Free(Thread* self, JitMemoryRegion* region, const uint8_t* code, const uint8_t* data)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::jit_lock_);

  // Perform a collection on the code cache.
  void GarbageCollectCache(Thread* self)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Given the 'pc', try to find the JIT compiled code associated with it.
  // Return null if 'pc' is not in the code cache. 'method' is passed for
  // sanity check.
  OatQuickMethodHeader* LookupMethodHeader(uintptr_t pc, ArtMethod* method)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  OatQuickMethodHeader* LookupOsrMethodHeader(ArtMethod* method)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Removes method from the cache for testing purposes. The caller
  // must ensure that all threads are suspended and the method should
  // not be in any thread's stack.
  bool RemoveMethod(ArtMethod* method, bool release_memory)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES(Locks::mutator_lock_);

  // Remove all methods in our cache that were allocated by 'alloc'.
  void RemoveMethodsIn(Thread* self, const LinearAlloc& alloc)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void CopyInlineCacheInto(const InlineCache& ic, Handle<mirror::ObjectArray<mirror::Class>> array)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Create a 'ProfileInfo' for 'method'. If 'retry_allocation' is true,
  // will collect and retry if the first allocation is unsuccessful.
  ProfilingInfo* AddProfilingInfo(Thread* self,
                                  ArtMethod* method,
                                  const std::vector<uint32_t>& entries,
                                  bool retry_allocation)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool OwnsSpace(const void* mspace) const NO_THREAD_SAFETY_ANALYSIS {
    return private_region_.OwnsSpace(mspace) || shared_region_.OwnsSpace(mspace);
  }

  void* MoreCore(const void* mspace, intptr_t increment);

  // Adds to `methods` all profiled methods which are part of any of the given dex locations.
  void GetProfiledMethods(const std::set<std::string>& dex_base_locations,
                          std::vector<ProfileMethodInfo>& methods)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void InvalidateAllCompiledCode()
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void InvalidateCompiledCodeFor(ArtMethod* method, const OatQuickMethodHeader* code)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void Dump(std::ostream& os) REQUIRES(!Locks::jit_lock_);

  bool IsOsrCompiled(ArtMethod* method) REQUIRES(!Locks::jit_lock_);

  void SweepRootTables(IsMarkedVisitor* visitor)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // The GC needs to disallow the reading of inline caches when it processes them,
  // to avoid having a class being used while it is being deleted.
  void AllowInlineCacheAccess() REQUIRES(!Locks::jit_lock_);
  void DisallowInlineCacheAccess() REQUIRES(!Locks::jit_lock_);
  void BroadcastForInlineCacheAccess() REQUIRES(!Locks::jit_lock_);

  // Notify the code cache that the method at the pointer 'old_method' is being moved to the pointer
  // 'new_method' since it is being made obsolete.
  void MoveObsoleteMethod(ArtMethod* old_method, ArtMethod* new_method)
      REQUIRES(!Locks::jit_lock_) REQUIRES(Locks::mutator_lock_);

  // Dynamically change whether we want to garbage collect code.
  void SetGarbageCollectCode(bool value) REQUIRES(!Locks::jit_lock_);

  bool GetGarbageCollectCode() REQUIRES(!Locks::jit_lock_);

  // Unsafe variant for debug checks.
  bool GetGarbageCollectCodeUnsafe() const NO_THREAD_SAFETY_ANALYSIS {
    return garbage_collect_code_;
  }
  ZygoteMap* GetZygoteMap() {
    return &zygote_map_;
  }

  // If Jit-gc has been disabled (and instrumentation has been enabled) this will return the
  // jit-compiled entrypoint for this method.  Otherwise it will return null.
  const void* FindCompiledCodeForInstrumentation(ArtMethod* method)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Fetch the code of a method that was JITted, but the JIT could not
  // update its entrypoint due to the resolution trampoline.
  const void* GetSavedEntryPointOfPreCompiledMethod(ArtMethod* method)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void PostForkChildAction(bool is_system_server, bool is_zygote);

  // Clear the entrypoints of JIT compiled methods that belong in the zygote space.
  // This is used for removing non-debuggable JIT code at the point we realize the runtime
  // is debuggable. Also clear the Precompiled flag from all methods so the non-debuggable code
  // doesn't come back.
  void TransitionToDebuggable() REQUIRES(!Locks::jit_lock_) REQUIRES(Locks::mutator_lock_);

  JitMemoryRegion* GetCurrentRegion();
  bool IsSharedRegion(const JitMemoryRegion& region) const { return &region == &shared_region_; }
  bool CanAllocateProfilingInfo() {
    // If we don't have a private region, we cannot allocate a profiling info.
    // A shared region doesn't support in general GC objects, which a profiling info
    // can reference.
    JitMemoryRegion* region = GetCurrentRegion();
    return region->IsValid() && !IsSharedRegion(*region);
  }

  // Return whether the given `ptr` is in the zygote executable memory space.
  bool IsInZygoteExecSpace(const void* ptr) const {
    return shared_region_.IsInExecSpace(ptr);
  }

 private:
  JitCodeCache();

  ProfilingInfo* AddProfilingInfoInternal(Thread* self,
                                          ArtMethod* method,
                                          const std::vector<uint32_t>& entries)
      REQUIRES(Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // If a collection is in progress, wait for it to finish. Must be called with the mutator lock.
  // The non-mutator lock version should be used if possible. This method will release then
  // re-acquire the mutator lock.
  void WaitForPotentialCollectionToCompleteRunnable(Thread* self)
      REQUIRES(Locks::jit_lock_, !Roles::uninterruptible_) REQUIRES_SHARED(Locks::mutator_lock_);

  // If a collection is in progress, wait for it to finish. Return
  // whether the thread actually waited.
  bool WaitForPotentialCollectionToComplete(Thread* self)
      REQUIRES(Locks::jit_lock_) REQUIRES(!Locks::mutator_lock_);

  // Remove CHA dependents and underlying allocations for entries in `method_headers`.
  void FreeAllMethodHeaders(const std::unordered_set<OatQuickMethodHeader*>& method_headers)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES(!Locks::cha_lock_);

  // Removes method from the cache. The caller must ensure that all threads
  // are suspended and the method should not be in any thread's stack.
  bool RemoveMethodLocked(ArtMethod* method, bool release_memory)
      REQUIRES(Locks::jit_lock_)
      REQUIRES(Locks::mutator_lock_);

  // Free code and data allocations for `code_ptr`.
  void FreeCodeAndData(const void* code_ptr, bool free_debug_info = true)
      REQUIRES(Locks::jit_lock_);

  // Number of bytes allocated in the code cache.
  size_t CodeCacheSize() REQUIRES(!Locks::jit_lock_);

  // Number of bytes allocated in the data cache.
  size_t DataCacheSize() REQUIRES(!Locks::jit_lock_);

  // Number of bytes allocated in the code cache.
  size_t CodeCacheSizeLocked() REQUIRES(Locks::jit_lock_);

  // Number of bytes allocated in the data cache.
  size_t DataCacheSizeLocked() REQUIRES(Locks::jit_lock_);

  // Notify all waiting threads that a collection is done.
  void NotifyCollectionDone(Thread* self) REQUIRES(Locks::jit_lock_);

  // Return whether we should do a full collection given the current state of the cache.
  bool ShouldDoFullCollection()
      REQUIRES(Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void DoCollection(Thread* self, bool collect_profiling_info)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void RemoveUnmarkedCode(Thread* self)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void MarkCompiledCodeOnThreadStacks(Thread* self)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  CodeCacheBitmap* GetLiveBitmap() const {
    return live_bitmap_.get();
  }

  bool IsInZygoteDataSpace(const void* ptr) const {
    return shared_region_.IsInDataSpace(ptr);
  }

  bool IsWeakAccessEnabled(Thread* self) const;
  void WaitUntilInlineCacheAccessible(Thread* self)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  class JniStubKey;
  class JniStubData;

  // Whether the GC allows accessing weaks in inline caches. Note that this
  // is not used by the concurrent collector, which uses
  // Thread::SetWeakRefAccessEnabled instead.
  Atomic<bool> is_weak_access_enabled_;

  // Condition to wait on for accessing inline caches.
  ConditionVariable inline_cache_cond_ GUARDED_BY(Locks::jit_lock_);

  // -------------- JIT memory regions ------------------------------------- //

  // Shared region, inherited from the zygote.
  JitMemoryRegion shared_region_;

  // Process's own region.
  JitMemoryRegion private_region_;

  // -------------- Global JIT maps --------------------------------------- //

  // Holds compiled code associated with the shorty for a JNI stub.
  SafeMap<JniStubKey, JniStubData> jni_stubs_map_ GUARDED_BY(Locks::jit_lock_);

  // Holds compiled code associated to the ArtMethod.
  SafeMap<const void*, ArtMethod*> method_code_map_ GUARDED_BY(Locks::jit_lock_);

  // Holds compiled code associated to the ArtMethod. Used when pre-jitting
  // methods whose entrypoints have the resolution stub.
  SafeMap<ArtMethod*, const void*> saved_compiled_methods_map_ GUARDED_BY(Locks::jit_lock_);

  // Holds osr compiled code associated to the ArtMethod.
  SafeMap<ArtMethod*, const void*> osr_code_map_ GUARDED_BY(Locks::jit_lock_);

  // ProfilingInfo objects we have allocated.
  std::vector<ProfilingInfo*> profiling_infos_ GUARDED_BY(Locks::jit_lock_);

  // Methods that the zygote has compiled and can be shared across processes
  // forked from the zygote.
  ZygoteMap zygote_map_;

  // -------------- JIT GC related data structures ----------------------- //

  // Condition to wait on during collection.
  ConditionVariable lock_cond_ GUARDED_BY(Locks::jit_lock_);

  // Whether there is a code cache collection in progress.
  bool collection_in_progress_ GUARDED_BY(Locks::jit_lock_);

  // Bitmap for collecting code and data.
  std::unique_ptr<CodeCacheBitmap> live_bitmap_;

  // Whether the last collection round increased the code cache.
  bool last_collection_increased_code_cache_ GUARDED_BY(Locks::jit_lock_);

  // Whether we can do garbage collection. Not 'const' as tests may override this.
  bool garbage_collect_code_ GUARDED_BY(Locks::jit_lock_);

  // ---------------- JIT statistics -------------------------------------- //

  // Number of compilations done throughout the lifetime of the JIT.
  size_t number_of_compilations_ GUARDED_BY(Locks::jit_lock_);

  // Number of compilations for on-stack-replacement done throughout the lifetime of the JIT.
  size_t number_of_osr_compilations_ GUARDED_BY(Locks::jit_lock_);

  // Number of code cache collections done throughout the lifetime of the JIT.
  size_t number_of_collections_ GUARDED_BY(Locks::jit_lock_);

  // Histograms for keeping track of stack map size statistics.
  Histogram<uint64_t> histogram_stack_map_memory_use_ GUARDED_BY(Locks::jit_lock_);

  // Histograms for keeping track of code size statistics.
  Histogram<uint64_t> histogram_code_memory_use_ GUARDED_BY(Locks::jit_lock_);

  // Histograms for keeping track of profiling info statistics.
  Histogram<uint64_t> histogram_profiling_info_memory_use_ GUARDED_BY(Locks::jit_lock_);

  friend class art::JitJniStubTestHelper;
  friend class ScopedCodeCacheWrite;
  friend class MarkCodeClosure;

  DISALLOW_COPY_AND_ASSIGN(JitCodeCache);
};

}  // namespace jit
}  // namespace art

#endif  // ART_RUNTIME_JIT_JIT_CODE_CACHE_H_
