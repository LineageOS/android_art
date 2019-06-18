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

  bool NotifyCompilationOf(ArtMethod* method, Thread* self, bool osr, bool prejit)
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

  // Allocate and write code and its metadata to the code cache.
  // `cha_single_implementation_list` needs to be registered via CHA (if it's
  // still valid), since the compiled code still needs to be invalidated if the
  // single-implementation assumptions are violated later. This needs to be done
  // even if `has_should_deoptimize_flag` is false, which can happen due to CHA
  // guard elimination.
  uint8_t* CommitCode(Thread* self,
                      JitMemoryRegion* region,
                      ArtMethod* method,
                      uint8_t* stack_map,
                      uint8_t* roots_data,
                      const uint8_t* code,
                      size_t code_size,
                      size_t data_size,
                      bool osr,
                      const std::vector<Handle<mirror::Object>>& roots,
                      bool has_should_deoptimize_flag,
                      const ArenaSet<ArtMethod*>& cha_single_implementation_list)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::jit_lock_);

  // Return true if the code cache contains this pc.
  bool ContainsPc(const void* pc) const;

  // Returns true if either the method's entrypoint is JIT compiled code or it is the
  // instrumentation entrypoint and we can jump to jit code for this method. For testing use only.
  bool WillExecuteJitCode(ArtMethod* method) REQUIRES(!Locks::jit_lock_);

  // Return true if the code cache contains this method.
  bool ContainsMethod(ArtMethod* method) REQUIRES(!Locks::jit_lock_);

  // Return the code pointer for a JNI-compiled stub if the method is in the cache, null otherwise.
  const void* GetJniStubCode(ArtMethod* method) REQUIRES(!Locks::jit_lock_);

  // Allocate a region of data that contain `size` bytes, and potentially space
  // for storing `number_of_roots` roots. Returns null if there is no more room.
  // Return the number of bytes allocated.
  size_t ReserveData(Thread* self,
                     JitMemoryRegion* region,
                     size_t stack_map_size,
                     size_t number_of_roots,
                     ArtMethod* method,
                     uint8_t** stack_map_data,
                     uint8_t** roots_data)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(!Locks::jit_lock_);

  // Clear data from the data portion of the code cache.
  void ClearData(
      Thread* self, JitMemoryRegion* region, uint8_t* stack_map_data, uint8_t* roots_data)
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
    return private_region_.OwnsSpace(mspace);
  }

  void* MoreCore(const void* mspace, intptr_t increment);

  // Adds to `methods` all profiled methods which are part of any of the given dex locations.
  void GetProfiledMethods(const std::set<std::string>& dex_base_locations,
                          std::vector<ProfileMethodInfo>& methods)
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

  // If Jit-gc has been disabled (and instrumentation has been enabled) this will return the
  // jit-compiled entrypoint for this method.  Otherwise it will return null.
  const void* FindCompiledCodeForInstrumentation(ArtMethod* method)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Fetch the entrypoint that zygote may have saved for a method. The zygote saves an entrypoint
  // only for the case when the method's declaring class is not initialized.
  const void* GetZygoteSavedEntryPoint(ArtMethod* method)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void PostForkChildAction(bool is_system_server, bool is_zygote);

  // Clear the entrypoints of JIT compiled methods that belong in the zygote space.
  // This is used for removing non-debuggable JIT code at the point we realize the runtime
  // is debuggable.
  void ClearEntryPointsInZygoteExecSpace() REQUIRES(!Locks::jit_lock_) REQUIRES(Locks::mutator_lock_);

  JitMemoryRegion* GetPrivateRegion() { return &private_region_; }

 private:
  JitCodeCache();

  // Internal version of 'CommitCode' that will not retry if the
  // allocation fails. Return null if the allocation fails.
  uint8_t* CommitCodeInternal(Thread* self,
                              JitMemoryRegion* region,
                              ArtMethod* method,
                              uint8_t* stack_map,
                              uint8_t* roots_data,
                              const uint8_t* code,
                              size_t code_size,
                              size_t data_size,
                              bool osr,
                              const std::vector<Handle<mirror::Object>>& roots,
                              bool has_should_deoptimize_flag,
                              const ArenaSet<ArtMethod*>& cha_single_implementation_list)
      REQUIRES(!Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Adds the given roots to the roots_data. Only a member for annotalysis.
  void FillRootTable(uint8_t* roots_data, const std::vector<Handle<mirror::Object>>& roots)
      REQUIRES(Locks::jit_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

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
  void FreeCodeAndData(const void* code_ptr) REQUIRES(Locks::jit_lock_);

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

  bool IsInZygoteExecSpace(const void* ptr) const {
    return shared_region_.IsInExecSpace(ptr);
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

  // Holds osr compiled code associated to the ArtMethod.
  SafeMap<ArtMethod*, const void*> osr_code_map_ GUARDED_BY(Locks::jit_lock_);

  // ProfilingInfo objects we have allocated.
  std::vector<ProfilingInfo*> profiling_infos_ GUARDED_BY(Locks::jit_lock_);

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
