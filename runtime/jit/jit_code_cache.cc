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

#include "jit_code_cache.h"

#include <sstream>

#include <android-base/logging.h>

#include "arch/context.h"
#include "art_method-inl.h"
#include "base/enums.h"
#include "base/histogram-inl.h"
#include "base/logging.h"  // For VLOG.
#include "base/membarrier.h"
#include "base/memfd.h"
#include "base/mem_map.h"
#include "base/quasi_atomic.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/time_utils.h"
#include "base/utils.h"
#include "cha.h"
#include "debugger_interface.h"
#include "dex/dex_file_loader.h"
#include "dex/method_reference.h"
#include "entrypoints/entrypoint_utils-inl.h"
#include "entrypoints/runtime_asm_entrypoints.h"
#include "gc/accounting/bitmap-inl.h"
#include "gc/allocator/dlmalloc.h"
#include "gc/scoped_gc_critical_section.h"
#include "handle.h"
#include "instrumentation.h"
#include "intern_table.h"
#include "jit/jit.h"
#include "jit/profiling_info.h"
#include "jit/jit_scoped_code_cache_write.h"
#include "linear_alloc.h"
#include "oat_file-inl.h"
#include "oat_quick_method_header.h"
#include "object_callbacks.h"
#include "profile/profile_compilation_info.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "thread-current-inl.h"
#include "thread_list.h"

namespace art {
namespace jit {

static constexpr size_t kCodeSizeLogThreshold = 50 * KB;
static constexpr size_t kStackMapSizeLogThreshold = 50 * KB;

class JitCodeCache::JniStubKey {
 public:
  explicit JniStubKey(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_)
      : shorty_(method->GetShorty()),
        is_static_(method->IsStatic()),
        is_fast_native_(method->IsFastNative()),
        is_critical_native_(method->IsCriticalNative()),
        is_synchronized_(method->IsSynchronized()) {
    DCHECK(!(is_fast_native_ && is_critical_native_));
  }

  bool operator<(const JniStubKey& rhs) const {
    if (is_static_ != rhs.is_static_) {
      return rhs.is_static_;
    }
    if (is_synchronized_ != rhs.is_synchronized_) {
      return rhs.is_synchronized_;
    }
    if (is_fast_native_ != rhs.is_fast_native_) {
      return rhs.is_fast_native_;
    }
    if (is_critical_native_ != rhs.is_critical_native_) {
      return rhs.is_critical_native_;
    }
    return strcmp(shorty_, rhs.shorty_) < 0;
  }

  // Update the shorty to point to another method's shorty. Call this function when removing
  // the method that references the old shorty from JniCodeData and not removing the entire
  // JniCodeData; the old shorty may become a dangling pointer when that method is unloaded.
  void UpdateShorty(ArtMethod* method) const REQUIRES_SHARED(Locks::mutator_lock_) {
    const char* shorty = method->GetShorty();
    DCHECK_STREQ(shorty_, shorty);
    shorty_ = shorty;
  }

 private:
  // The shorty points to a DexFile data and may need to change
  // to point to the same shorty in a different DexFile.
  mutable const char* shorty_;

  const bool is_static_;
  const bool is_fast_native_;
  const bool is_critical_native_;
  const bool is_synchronized_;
};

class JitCodeCache::JniStubData {
 public:
  JniStubData() : code_(nullptr), methods_() {}

  void SetCode(const void* code) {
    DCHECK(code != nullptr);
    code_ = code;
  }

  void UpdateEntryPoints(const void* entrypoint) REQUIRES_SHARED(Locks::mutator_lock_) {
    DCHECK(IsCompiled());
    DCHECK(entrypoint == OatQuickMethodHeader::FromCodePointer(GetCode())->GetEntryPoint());
    instrumentation::Instrumentation* instrum = Runtime::Current()->GetInstrumentation();
    for (ArtMethod* m : GetMethods()) {
      // Because `m` might be in the process of being deleted:
      // - Call the dedicated method instead of the more generic UpdateMethodsCode
      // - Check the class status without a full read barrier; use ReadBarrier::IsMarked().
      bool can_set_entrypoint = true;
      if (NeedsClinitCheckBeforeCall(m)) {
        // To avoid resurrecting an unreachable object, we must not use a full read
        // barrier but we do not want to miss updating an entrypoint under common
        // circumstances, i.e. during a GC the class becomes visibly initialized,
        // the method becomes hot, we compile the thunk and want to update the
        // entrypoint while the method's declaring class field still points to the
        // from-space class object with the old status. Therefore we read the
        // declaring class without a read barrier and check if it's already marked.
        // If yes, we check the status of the to-space class object as intended.
        // Otherwise, there is no to-space object and the from-space class object
        // contains the most recent value of the status field; even if this races
        // with another thread doing a read barrier and updating the status, that's
        // no different from a race with a thread that just updates the status.
        // Such race can happen only for the zygote method pre-compilation, as we
        // otherwise compile only thunks for methods of visibly initialized classes.
        ObjPtr<mirror::Class> klass = m->GetDeclaringClass<kWithoutReadBarrier>();
        ObjPtr<mirror::Class> marked = ReadBarrier::IsMarked(klass.Ptr());
        ObjPtr<mirror::Class> checked_klass = (marked != nullptr) ? marked : klass;
        can_set_entrypoint = checked_klass->IsVisiblyInitialized();
      }
      if (can_set_entrypoint) {
        instrum->UpdateNativeMethodsCodeToJitCode(m, entrypoint);
      }
    }
  }

  const void* GetCode() const {
    return code_;
  }

  bool IsCompiled() const {
    return GetCode() != nullptr;
  }

  void AddMethod(ArtMethod* method) {
    if (!ContainsElement(methods_, method)) {
      methods_.push_back(method);
    }
  }

  const std::vector<ArtMethod*>& GetMethods() const {
    return methods_;
  }

  void RemoveMethodsIn(const LinearAlloc& alloc) REQUIRES_SHARED(Locks::mutator_lock_) {
    auto kept_end = std::partition(
        methods_.begin(),
        methods_.end(),
        [&alloc](ArtMethod* method) { return !alloc.ContainsUnsafe(method); });
    for (auto it = kept_end; it != methods_.end(); it++) {
      VLOG(jit) << "JIT removed (JNI) " << (*it)->PrettyMethod() << ": " << code_;
    }
    methods_.erase(kept_end, methods_.end());
  }

  bool RemoveMethod(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_) {
    auto it = std::find(methods_.begin(), methods_.end(), method);
    if (it != methods_.end()) {
      VLOG(jit) << "JIT removed (JNI) " << (*it)->PrettyMethod() << ": " << code_;
      methods_.erase(it);
      return true;
    } else {
      return false;
    }
  }

  void MoveObsoleteMethod(ArtMethod* old_method, ArtMethod* new_method) {
    std::replace(methods_.begin(), methods_.end(), old_method, new_method);
  }

 private:
  const void* code_;
  std::vector<ArtMethod*> methods_;
};

JitCodeCache* JitCodeCache::Create(bool used_only_for_profile_data,
                                   bool rwx_memory_allowed,
                                   bool is_zygote,
                                   std::string* error_msg) {
  // Register for membarrier expedited sync core if JIT will be generating code.
  if (!used_only_for_profile_data) {
    if (art::membarrier(art::MembarrierCommand::kRegisterPrivateExpeditedSyncCore) != 0) {
      // MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE ensures that CPU instruction pipelines are
      // flushed and it's used when adding code to the JIT. The memory used by the new code may
      // have just been released and, in theory, the old code could still be in a pipeline.
      VLOG(jit) << "Kernel does not support membarrier sync-core";
    }
  }

  size_t initial_capacity = Runtime::Current()->GetJITOptions()->GetCodeCacheInitialCapacity();
  // Check whether the provided max capacity in options is below 1GB.
  size_t max_capacity = Runtime::Current()->GetJITOptions()->GetCodeCacheMaxCapacity();
  // We need to have 32 bit offsets from method headers in code cache which point to things
  // in the data cache. If the maps are more than 4G apart, having multiple maps wouldn't work.
  // Ensure we're below 1 GB to be safe.
  if (max_capacity > 1 * GB) {
    std::ostringstream oss;
    oss << "Maxium code cache capacity is limited to 1 GB, "
        << PrettySize(max_capacity) << " is too big";
    *error_msg = oss.str();
    return nullptr;
  }

  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  JitMemoryRegion region;
  if (!region.Initialize(initial_capacity,
                         max_capacity,
                         rwx_memory_allowed,
                         is_zygote,
                         error_msg)) {
    return nullptr;
  }

  std::unique_ptr<JitCodeCache> jit_code_cache(new JitCodeCache());
  if (is_zygote) {
    // Zygote should never collect code to share the memory with the children.
    jit_code_cache->garbage_collect_code_ = false;
    jit_code_cache->shared_region_ = std::move(region);
  } else {
    jit_code_cache->private_region_ = std::move(region);
  }

  VLOG(jit) << "Created jit code cache: initial capacity="
            << PrettySize(initial_capacity)
            << ", maximum capacity="
            << PrettySize(max_capacity);

  return jit_code_cache.release();
}

JitCodeCache::JitCodeCache()
    : is_weak_access_enabled_(true),
      inline_cache_cond_("Jit inline cache condition variable", *Locks::jit_lock_),
      zygote_map_(&shared_region_),
      lock_cond_("Jit code cache condition variable", *Locks::jit_lock_),
      collection_in_progress_(false),
      last_collection_increased_code_cache_(false),
      garbage_collect_code_(true),
      number_of_compilations_(0),
      number_of_osr_compilations_(0),
      number_of_collections_(0),
      histogram_stack_map_memory_use_("Memory used for stack maps", 16),
      histogram_code_memory_use_("Memory used for compiled code", 16),
      histogram_profiling_info_memory_use_("Memory used for profiling info", 16) {
}

JitCodeCache::~JitCodeCache() {}

bool JitCodeCache::PrivateRegionContainsPc(const void* ptr) const {
  return private_region_.IsInExecSpace(ptr);
}

bool JitCodeCache::ContainsPc(const void* ptr) const {
  return PrivateRegionContainsPc(ptr) || shared_region_.IsInExecSpace(ptr);
}

bool JitCodeCache::WillExecuteJitCode(ArtMethod* method) {
  ScopedObjectAccess soa(art::Thread::Current());
  ScopedAssertNoThreadSuspension sants(__FUNCTION__);
  if (ContainsPc(method->GetEntryPointFromQuickCompiledCode())) {
    return true;
  } else if (method->GetEntryPointFromQuickCompiledCode() == GetQuickInstrumentationEntryPoint()) {
    return FindCompiledCodeForInstrumentation(method) != nullptr;
  }
  return false;
}

bool JitCodeCache::ContainsMethod(ArtMethod* method) {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  if (UNLIKELY(method->IsNative())) {
    auto it = jni_stubs_map_.find(JniStubKey(method));
    if (it != jni_stubs_map_.end() &&
        it->second.IsCompiled() &&
        ContainsElement(it->second.GetMethods(), method)) {
      return true;
    }
  } else {
    for (const auto& it : method_code_map_) {
      if (it.second == method) {
        return true;
      }
    }
    if (zygote_map_.ContainsMethod(method)) {
      return true;
    }
  }
  return false;
}

const void* JitCodeCache::GetJniStubCode(ArtMethod* method) {
  DCHECK(method->IsNative());
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  auto it = jni_stubs_map_.find(JniStubKey(method));
  if (it != jni_stubs_map_.end()) {
    JniStubData& data = it->second;
    if (data.IsCompiled() && ContainsElement(data.GetMethods(), method)) {
      return data.GetCode();
    }
  }
  return nullptr;
}

const void* JitCodeCache::FindCompiledCodeForInstrumentation(ArtMethod* method) {
  // If jit-gc is still on we use the SavedEntryPoint field for doing that and so cannot use it to
  // find the instrumentation entrypoint.
  if (LIKELY(GetGarbageCollectCode())) {
    return nullptr;
  }
  ProfilingInfo* info = method->GetProfilingInfo(kRuntimePointerSize);
  if (info == nullptr) {
    return nullptr;
  }
  // When GC is disabled for trampoline tracing we will use SavedEntrypoint to hold the actual
  // jit-compiled version of the method. If jit-gc is disabled for other reasons this will just be
  // nullptr.
  return info->GetSavedEntryPoint();
}

const void* JitCodeCache::GetSavedEntryPointOfPreCompiledMethod(ArtMethod* method) {
  if (method->IsPreCompiled()) {
    const void* code_ptr = nullptr;
    if (method->GetDeclaringClass()->GetClassLoader() == nullptr) {
      code_ptr = zygote_map_.GetCodeFor(method);
    } else {
      MutexLock mu(Thread::Current(), *Locks::jit_lock_);
      auto it = saved_compiled_methods_map_.find(method);
      if (it != saved_compiled_methods_map_.end()) {
        code_ptr = it->second;
      }
    }
    if (code_ptr != nullptr) {
      OatQuickMethodHeader* method_header = OatQuickMethodHeader::FromCodePointer(code_ptr);
      return method_header->GetEntryPoint();
    }
  }
  return nullptr;
}

bool JitCodeCache::WaitForPotentialCollectionToComplete(Thread* self) {
  bool in_collection = false;
  while (collection_in_progress_) {
    in_collection = true;
    lock_cond_.Wait(self);
  }
  return in_collection;
}

static uintptr_t FromCodeToAllocation(const void* code) {
  size_t alignment = GetInstructionSetAlignment(kRuntimeISA);
  return reinterpret_cast<uintptr_t>(code) - RoundUp(sizeof(OatQuickMethodHeader), alignment);
}

static const void* FromAllocationToCode(const uint8_t* alloc) {
  size_t alignment = GetInstructionSetAlignment(kRuntimeISA);
  return reinterpret_cast<const void*>(alloc + RoundUp(sizeof(OatQuickMethodHeader), alignment));
}

static uint32_t GetNumberOfRoots(const uint8_t* stack_map) {
  // The length of the table is stored just before the stack map (and therefore at the end of
  // the table itself), in order to be able to fetch it from a `stack_map` pointer.
  return reinterpret_cast<const uint32_t*>(stack_map)[-1];
}

static void DCheckRootsAreValid(const std::vector<Handle<mirror::Object>>& roots,
                                bool is_shared_region)
    REQUIRES(!Locks::intern_table_lock_) REQUIRES_SHARED(Locks::mutator_lock_) {
  if (!kIsDebugBuild) {
    return;
  }
  // Put all roots in `roots_data`.
  for (Handle<mirror::Object> object : roots) {
    // Ensure the string is strongly interned. b/32995596
    if (object->IsString()) {
      ObjPtr<mirror::String> str = object->AsString();
      ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
      CHECK(class_linker->GetInternTable()->LookupStrong(Thread::Current(), str) != nullptr);
    }
    // Ensure that we don't put movable objects in the shared region.
    if (is_shared_region) {
      CHECK(!Runtime::Current()->GetHeap()->IsMovableObject(object.Get()));
    }
  }
}

static const uint8_t* GetRootTable(const void* code_ptr, uint32_t* number_of_roots = nullptr) {
  OatQuickMethodHeader* method_header = OatQuickMethodHeader::FromCodePointer(code_ptr);
  uint8_t* data = method_header->GetOptimizedCodeInfoPtr();
  uint32_t roots = GetNumberOfRoots(data);
  if (number_of_roots != nullptr) {
    *number_of_roots = roots;
  }
  return data - ComputeRootTableSize(roots);
}

void JitCodeCache::SweepRootTables(IsMarkedVisitor* visitor) {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  for (const auto& entry : method_code_map_) {
    uint32_t number_of_roots = 0;
    const uint8_t* root_table = GetRootTable(entry.first, &number_of_roots);
    uint8_t* roots_data = private_region_.IsInDataSpace(root_table)
        ? private_region_.GetWritableDataAddress(root_table)
        : shared_region_.GetWritableDataAddress(root_table);
    GcRoot<mirror::Object>* roots = reinterpret_cast<GcRoot<mirror::Object>*>(roots_data);
    for (uint32_t i = 0; i < number_of_roots; ++i) {
      // This does not need a read barrier because this is called by GC.
      mirror::Object* object = roots[i].Read<kWithoutReadBarrier>();
      if (object == nullptr || object == Runtime::GetWeakClassSentinel()) {
        // entry got deleted in a previous sweep.
      } else if (object->IsString<kDefaultVerifyFlags>()) {
        mirror::Object* new_object = visitor->IsMarked(object);
        // We know the string is marked because it's a strongly-interned string that
        // is always alive. The IsMarked implementation of the CMS collector returns
        // null for newly allocated objects, but we know those haven't moved. Therefore,
        // only update the entry if we get a different non-null string.
        // TODO: Do not use IsMarked for j.l.Class, and adjust once we move this method
        // out of the weak access/creation pause. b/32167580
        if (new_object != nullptr && new_object != object) {
          DCHECK(new_object->IsString());
          roots[i] = GcRoot<mirror::Object>(new_object);
        }
      } else {
        Runtime::ProcessWeakClass(
            reinterpret_cast<GcRoot<mirror::Class>*>(&roots[i]),
            visitor,
            Runtime::GetWeakClassSentinel());
      }
    }
  }
  // Walk over inline caches to clear entries containing unloaded classes.
  for (ProfilingInfo* info : profiling_infos_) {
    for (size_t i = 0; i < info->number_of_inline_caches_; ++i) {
      InlineCache* cache = &info->cache_[i];
      for (size_t j = 0; j < InlineCache::kIndividualCacheSize; ++j) {
        Runtime::ProcessWeakClass(&cache->classes_[j], visitor, nullptr);
      }
    }
  }
}

void JitCodeCache::FreeCodeAndData(const void* code_ptr) {
  if (IsInZygoteExecSpace(code_ptr)) {
    // No need to free, this is shared memory.
    return;
  }
  uintptr_t allocation = FromCodeToAllocation(code_ptr);
  const uint8_t* data = nullptr;
  if (OatQuickMethodHeader::FromCodePointer(code_ptr)->IsOptimized()) {
    data = GetRootTable(code_ptr);
  }  // else this is a JNI stub without any data.

  FreeLocked(&private_region_, reinterpret_cast<uint8_t*>(allocation), data);
}

void JitCodeCache::FreeAllMethodHeaders(
    const std::unordered_set<OatQuickMethodHeader*>& method_headers) {
  // We need to remove entries in method_headers from CHA dependencies
  // first since once we do FreeCode() below, the memory can be reused
  // so it's possible for the same method_header to start representing
  // different compile code.
  {
    MutexLock mu2(Thread::Current(), *Locks::cha_lock_);
    Runtime::Current()->GetClassLinker()->GetClassHierarchyAnalysis()
        ->RemoveDependentsWithMethodHeaders(method_headers);
  }

  ScopedCodeCacheWrite scc(private_region_);
  for (const OatQuickMethodHeader* method_header : method_headers) {
    FreeCodeAndData(method_header->GetCode());
  }

  // We have potentially removed a lot of debug info. Do maintenance pass to save space.
  RepackNativeDebugInfoForJit();

  // Check that the set of compiled methods exactly matches native debug information.
  if (kIsDebugBuild) {
    std::map<const void*, ArtMethod*> compiled_methods;
    VisitAllMethods([&](const void* addr, ArtMethod* method) {
      CHECK(addr != nullptr && method != nullptr);
      compiled_methods.emplace(addr, method);
    });
    std::set<const void*> debug_info;
    ForEachNativeDebugSymbol([&](const void* addr, size_t, const char* name) {
      addr = AlignDown(addr, GetInstructionSetInstructionAlignment(kRuntimeISA));  // Thumb-bit.
      CHECK(debug_info.emplace(addr).second) << "Duplicate debug info: " << addr << " " << name;
      CHECK_EQ(compiled_methods.count(addr), 1u) << "Extra debug info: " << addr << " " << name;
    });
    if (!debug_info.empty()) {  // If debug-info generation is enabled.
      for (auto it : compiled_methods) {
        CHECK_EQ(debug_info.count(it.first), 1u) << "No debug info: " << it.second->PrettyMethod();
      }
    }
  }
}

void JitCodeCache::RemoveMethodsIn(Thread* self, const LinearAlloc& alloc) {
  ScopedTrace trace(__PRETTY_FUNCTION__);
  // We use a set to first collect all method_headers whose code need to be
  // removed. We need to free the underlying code after we remove CHA dependencies
  // for entries in this set. And it's more efficient to iterate through
  // the CHA dependency map just once with an unordered_set.
  std::unordered_set<OatQuickMethodHeader*> method_headers;
  {
    MutexLock mu(self, *Locks::jit_lock_);
    // We do not check if a code cache GC is in progress, as this method comes
    // with the classlinker_classes_lock_ held, and suspending ourselves could
    // lead to a deadlock.
    {
      for (auto it = jni_stubs_map_.begin(); it != jni_stubs_map_.end();) {
        it->second.RemoveMethodsIn(alloc);
        if (it->second.GetMethods().empty()) {
          method_headers.insert(OatQuickMethodHeader::FromCodePointer(it->second.GetCode()));
          it = jni_stubs_map_.erase(it);
        } else {
          it->first.UpdateShorty(it->second.GetMethods().front());
          ++it;
        }
      }
      for (auto it = method_code_map_.begin(); it != method_code_map_.end();) {
        if (alloc.ContainsUnsafe(it->second)) {
          method_headers.insert(OatQuickMethodHeader::FromCodePointer(it->first));
          VLOG(jit) << "JIT removed " << it->second->PrettyMethod() << ": " << it->first;
          it = method_code_map_.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (auto it = osr_code_map_.begin(); it != osr_code_map_.end();) {
      if (alloc.ContainsUnsafe(it->first)) {
        // Note that the code has already been pushed to method_headers in the loop
        // above and is going to be removed in FreeCode() below.
        it = osr_code_map_.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = profiling_infos_.begin(); it != profiling_infos_.end();) {
      ProfilingInfo* info = *it;
      if (alloc.ContainsUnsafe(info->GetMethod())) {
        info->GetMethod()->SetProfilingInfo(nullptr);
        private_region_.FreeWritableData(reinterpret_cast<uint8_t*>(info));
        it = profiling_infos_.erase(it);
      } else {
        ++it;
      }
    }
    FreeAllMethodHeaders(method_headers);
  }
}

bool JitCodeCache::IsWeakAccessEnabled(Thread* self) const {
  return kUseReadBarrier
      ? self->GetWeakRefAccessEnabled()
      : is_weak_access_enabled_.load(std::memory_order_seq_cst);
}

void JitCodeCache::WaitUntilInlineCacheAccessible(Thread* self) {
  if (IsWeakAccessEnabled(self)) {
    return;
  }
  ScopedThreadSuspension sts(self, kWaitingWeakGcRootRead);
  MutexLock mu(self, *Locks::jit_lock_);
  while (!IsWeakAccessEnabled(self)) {
    inline_cache_cond_.Wait(self);
  }
}

void JitCodeCache::BroadcastForInlineCacheAccess() {
  Thread* self = Thread::Current();
  MutexLock mu(self, *Locks::jit_lock_);
  inline_cache_cond_.Broadcast(self);
}

void JitCodeCache::AllowInlineCacheAccess() {
  DCHECK(!kUseReadBarrier);
  is_weak_access_enabled_.store(true, std::memory_order_seq_cst);
  BroadcastForInlineCacheAccess();
}

void JitCodeCache::DisallowInlineCacheAccess() {
  DCHECK(!kUseReadBarrier);
  is_weak_access_enabled_.store(false, std::memory_order_seq_cst);
}

void JitCodeCache::CopyInlineCacheInto(const InlineCache& ic,
                                       Handle<mirror::ObjectArray<mirror::Class>> array) {
  WaitUntilInlineCacheAccessible(Thread::Current());
  // Note that we don't need to lock `lock_` here, the compiler calling
  // this method has already ensured the inline cache will not be deleted.
  for (size_t in_cache = 0, in_array = 0;
       in_cache < InlineCache::kIndividualCacheSize;
       ++in_cache) {
    mirror::Class* object = ic.classes_[in_cache].Read();
    if (object != nullptr) {
      array->Set(in_array++, object);
    }
  }
}

static void ClearMethodCounter(ArtMethod* method, bool was_warm)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  if (was_warm) {
    method->SetPreviouslyWarm();
  }
  // We reset the counter to 1 so that the profile knows that the method was executed at least once.
  // This is required for layout purposes.
  // We also need to make sure we'll pass the warmup threshold again, so we set to 0 if
  // the warmup threshold is 1.
  uint16_t jit_warmup_threshold = Runtime::Current()->GetJITOptions()->GetWarmupThreshold();
  method->SetCounter(std::min(jit_warmup_threshold - 1, 1));
}

void JitCodeCache::WaitForPotentialCollectionToCompleteRunnable(Thread* self) {
  while (collection_in_progress_) {
    Locks::jit_lock_->Unlock(self);
    {
      ScopedThreadSuspension sts(self, kSuspended);
      MutexLock mu(self, *Locks::jit_lock_);
      WaitForPotentialCollectionToComplete(self);
    }
    Locks::jit_lock_->Lock(self);
  }
}

bool JitCodeCache::Commit(Thread* self,
                          JitMemoryRegion* region,
                          ArtMethod* method,
                          ArrayRef<const uint8_t> reserved_code,
                          ArrayRef<const uint8_t> code,
                          ArrayRef<const uint8_t> reserved_data,
                          const std::vector<Handle<mirror::Object>>& roots,
                          ArrayRef<const uint8_t> stack_map,
                          const std::vector<uint8_t>& debug_info,
                          bool is_full_debug_info,
                          bool osr,
                          bool has_should_deoptimize_flag,
                          const ArenaSet<ArtMethod*>& cha_single_implementation_list) {
  DCHECK(!method->IsNative() || !osr);

  if (!method->IsNative()) {
    // We need to do this before grabbing the lock_ because it needs to be able to see the string
    // InternTable. Native methods do not have roots.
    DCheckRootsAreValid(roots, IsSharedRegion(*region));
  }

  const uint8_t* roots_data = reserved_data.data();
  size_t root_table_size = ComputeRootTableSize(roots.size());
  const uint8_t* stack_map_data = roots_data + root_table_size;

  MutexLock mu(self, *Locks::jit_lock_);
  // We need to make sure that there will be no jit-gcs going on and wait for any ongoing one to
  // finish.
  WaitForPotentialCollectionToCompleteRunnable(self);
  const uint8_t* code_ptr = region->CommitCode(
      reserved_code, code, stack_map_data, has_should_deoptimize_flag);
  if (code_ptr == nullptr) {
    return false;
  }
  OatQuickMethodHeader* method_header = OatQuickMethodHeader::FromCodePointer(code_ptr);

  // Commit roots and stack maps before updating the entry point.
  if (!region->CommitData(reserved_data, roots, stack_map)) {
    return false;
  }

  number_of_compilations_++;

  // We need to update the debug info before the entry point gets set.
  // At the same time we want to do under JIT lock so that debug info and JIT maps are in sync.
  if (!debug_info.empty()) {
    // NB: Don't allow packing of full info since it would remove non-backtrace data.
    AddNativeDebugInfoForJit(code_ptr, debug_info, /*allow_packing=*/ !is_full_debug_info);
  }

  // We need to update the entry point in the runnable state for the instrumentation.
  {
    // The following needs to be guarded by cha_lock_ also. Otherwise it's possible that the
    // compiled code is considered invalidated by some class linking, but below we still make the
    // compiled code valid for the method.  Need cha_lock_ for checking all single-implementation
    // flags and register dependencies.
    MutexLock cha_mu(self, *Locks::cha_lock_);
    bool single_impl_still_valid = true;
    for (ArtMethod* single_impl : cha_single_implementation_list) {
      if (!single_impl->HasSingleImplementation()) {
        // Simply discard the compiled code. Clear the counter so that it may be recompiled later.
        // Hopefully the class hierarchy will be more stable when compilation is retried.
        single_impl_still_valid = false;
        ClearMethodCounter(method, /*was_warm=*/ false);
        break;
      }
    }

    // Discard the code if any single-implementation assumptions are now invalid.
    if (UNLIKELY(!single_impl_still_valid)) {
      VLOG(jit) << "JIT discarded jitted code due to invalid single-implementation assumptions.";
      return false;
    }
    DCHECK(cha_single_implementation_list.empty() || !Runtime::Current()->IsJavaDebuggable())
        << "Should not be using cha on debuggable apps/runs!";

    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    for (ArtMethod* single_impl : cha_single_implementation_list) {
      class_linker->GetClassHierarchyAnalysis()->AddDependency(single_impl, method, method_header);
    }

    if (UNLIKELY(method->IsNative())) {
      auto it = jni_stubs_map_.find(JniStubKey(method));
      DCHECK(it != jni_stubs_map_.end())
          << "Entry inserted in NotifyCompilationOf() should be alive.";
      JniStubData* data = &it->second;
      DCHECK(ContainsElement(data->GetMethods(), method))
          << "Entry inserted in NotifyCompilationOf() should contain this method.";
      data->SetCode(code_ptr);
      data->UpdateEntryPoints(method_header->GetEntryPoint());
    } else {
      if (method->IsPreCompiled() && IsSharedRegion(*region)) {
        zygote_map_.Put(code_ptr, method);
      } else {
        method_code_map_.Put(code_ptr, method);
      }
      if (osr) {
        number_of_osr_compilations_++;
        osr_code_map_.Put(method, code_ptr);
      } else if (NeedsClinitCheckBeforeCall(method) &&
                 !method->GetDeclaringClass()->IsVisiblyInitialized()) {
        // This situation currently only occurs in the jit-zygote mode.
        DCHECK(!garbage_collect_code_);
        DCHECK(method->IsPreCompiled());
        // The shared region can easily be queried. For the private region, we
        // use a side map.
        if (!IsSharedRegion(*region)) {
          saved_compiled_methods_map_.Put(method, code_ptr);
        }
      } else {
        Runtime::Current()->GetInstrumentation()->UpdateMethodsCode(
            method, method_header->GetEntryPoint());
      }
    }
    if (collection_in_progress_) {
      // We need to update the live bitmap if there is a GC to ensure it sees this new
      // code.
      GetLiveBitmap()->AtomicTestAndSet(FromCodeToAllocation(code_ptr));
    }
    VLOG(jit)
        << "JIT added (osr=" << std::boolalpha << osr << std::noboolalpha << ") "
        << ArtMethod::PrettyMethod(method) << "@" << method
        << " ccache_size=" << PrettySize(CodeCacheSizeLocked()) << ": "
        << " dcache_size=" << PrettySize(DataCacheSizeLocked()) << ": "
        << reinterpret_cast<const void*>(method_header->GetEntryPoint()) << ","
        << reinterpret_cast<const void*>(method_header->GetEntryPoint() +
                                         method_header->GetCodeSize());
  }

  return true;
}

size_t JitCodeCache::CodeCacheSize() {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  return CodeCacheSizeLocked();
}

bool JitCodeCache::RemoveMethod(ArtMethod* method, bool release_memory) {
  // This function is used only for testing and only with non-native methods.
  CHECK(!method->IsNative());

  MutexLock mu(Thread::Current(), *Locks::jit_lock_);

  bool osr = osr_code_map_.find(method) != osr_code_map_.end();
  bool in_cache = RemoveMethodLocked(method, release_memory);

  if (!in_cache) {
    return false;
  }

  method->SetCounter(0);
  Runtime::Current()->GetInstrumentation()->UpdateMethodsCode(
      method, GetQuickToInterpreterBridge());
  VLOG(jit)
      << "JIT removed (osr=" << std::boolalpha << osr << std::noboolalpha << ") "
      << ArtMethod::PrettyMethod(method) << "@" << method
      << " ccache_size=" << PrettySize(CodeCacheSizeLocked()) << ": "
      << " dcache_size=" << PrettySize(DataCacheSizeLocked());
  return true;
}

bool JitCodeCache::RemoveMethodLocked(ArtMethod* method, bool release_memory) {
  if (LIKELY(!method->IsNative())) {
    ProfilingInfo* info = method->GetProfilingInfo(kRuntimePointerSize);
    if (info != nullptr) {
      RemoveElement(profiling_infos_, info);
    }
    method->SetProfilingInfo(nullptr);
  }

  bool in_cache = false;
  ScopedCodeCacheWrite ccw(private_region_);
  if (UNLIKELY(method->IsNative())) {
    auto it = jni_stubs_map_.find(JniStubKey(method));
    if (it != jni_stubs_map_.end() && it->second.RemoveMethod(method)) {
      in_cache = true;
      if (it->second.GetMethods().empty()) {
        if (release_memory) {
          FreeCodeAndData(it->second.GetCode());
        }
        jni_stubs_map_.erase(it);
      } else {
        it->first.UpdateShorty(it->second.GetMethods().front());
      }
    }
  } else {
    for (auto it = method_code_map_.begin(); it != method_code_map_.end();) {
      if (it->second == method) {
        in_cache = true;
        if (release_memory) {
          FreeCodeAndData(it->first);
        }
        VLOG(jit) << "JIT removed " << it->second->PrettyMethod() << ": " << it->first;
        it = method_code_map_.erase(it);
      } else {
        ++it;
      }
    }

    auto osr_it = osr_code_map_.find(method);
    if (osr_it != osr_code_map_.end()) {
      osr_code_map_.erase(osr_it);
    }
  }

  return in_cache;
}

// This notifies the code cache that the given method has been redefined and that it should remove
// any cached information it has on the method. All threads must be suspended before calling this
// method. The compiled code for the method (if there is any) must not be in any threads call stack.
void JitCodeCache::NotifyMethodRedefined(ArtMethod* method) {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  RemoveMethodLocked(method, /* release_memory= */ true);
}

// This invalidates old_method. Once this function returns one can no longer use old_method to
// execute code unless it is fixed up. This fixup will happen later in the process of installing a
// class redefinition.
// TODO We should add some info to ArtMethod to note that 'old_method' has been invalidated and
// shouldn't be used since it is no longer logically in the jit code cache.
// TODO We should add DCHECKS that validate that the JIT is paused when this method is entered.
void JitCodeCache::MoveObsoleteMethod(ArtMethod* old_method, ArtMethod* new_method) {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  if (old_method->IsNative()) {
    // Update methods in jni_stubs_map_.
    for (auto& entry : jni_stubs_map_) {
      JniStubData& data = entry.second;
      data.MoveObsoleteMethod(old_method, new_method);
    }
    return;
  }
  // Update ProfilingInfo to the new one and remove it from the old_method.
  if (old_method->GetProfilingInfo(kRuntimePointerSize) != nullptr) {
    DCHECK_EQ(old_method->GetProfilingInfo(kRuntimePointerSize)->GetMethod(), old_method);
    ProfilingInfo* info = old_method->GetProfilingInfo(kRuntimePointerSize);
    old_method->SetProfilingInfo(nullptr);
    // Since the JIT should be paused and all threads suspended by the time this is called these
    // checks should always pass.
    DCHECK(!info->IsInUseByCompiler());
    new_method->SetProfilingInfo(info);
    // Get rid of the old saved entrypoint if it is there.
    info->SetSavedEntryPoint(nullptr);
    info->method_ = new_method;
  }
  // Update method_code_map_ to point to the new method.
  for (auto& it : method_code_map_) {
    if (it.second == old_method) {
      it.second = new_method;
    }
  }
  // Update osr_code_map_ to point to the new method.
  auto code_map = osr_code_map_.find(old_method);
  if (code_map != osr_code_map_.end()) {
    osr_code_map_.Put(new_method, code_map->second);
    osr_code_map_.erase(old_method);
  }
}

void JitCodeCache::TransitionToDebuggable() {
  // Check that none of our methods have an entrypoint in the zygote exec
  // space (this should be taken care of by
  // ClassLinker::UpdateEntryPointsClassVisitor.
  {
    MutexLock mu(Thread::Current(), *Locks::jit_lock_);
    if (kIsDebugBuild) {
      for (const auto& it : method_code_map_) {
        ArtMethod* method = it.second;
        DCHECK(!method->IsPreCompiled());
        DCHECK(!IsInZygoteExecSpace(method->GetEntryPointFromQuickCompiledCode()));
      }
    }
    // Not strictly necessary, but this map is useless now.
    saved_compiled_methods_map_.clear();
  }
  if (kIsDebugBuild) {
    for (const auto& entry : zygote_map_) {
      ArtMethod* method = entry.method;
      if (method != nullptr) {
        DCHECK(!method->IsPreCompiled());
        DCHECK(!IsInZygoteExecSpace(method->GetEntryPointFromQuickCompiledCode()));
      }
    }
  }
}

size_t JitCodeCache::CodeCacheSizeLocked() {
  return GetCurrentRegion()->GetUsedMemoryForCode();
}

size_t JitCodeCache::DataCacheSize() {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  return DataCacheSizeLocked();
}

size_t JitCodeCache::DataCacheSizeLocked() {
  return GetCurrentRegion()->GetUsedMemoryForData();
}

bool JitCodeCache::Reserve(Thread* self,
                           JitMemoryRegion* region,
                           size_t code_size,
                           size_t stack_map_size,
                           size_t number_of_roots,
                           ArtMethod* method,
                           /*out*/ArrayRef<const uint8_t>* reserved_code,
                           /*out*/ArrayRef<const uint8_t>* reserved_data) {
  code_size = OatQuickMethodHeader::InstructionAlignedSize() + code_size;
  size_t data_size = RoundUp(ComputeRootTableSize(number_of_roots) + stack_map_size, sizeof(void*));

  const uint8_t* code;
  const uint8_t* data;
  while (true) {
    bool at_max_capacity = false;
    {
      ScopedThreadSuspension sts(self, kSuspended);
      MutexLock mu(self, *Locks::jit_lock_);
      WaitForPotentialCollectionToComplete(self);
      ScopedCodeCacheWrite ccw(*region);
      code = region->AllocateCode(code_size);
      data = region->AllocateData(data_size);
      at_max_capacity = IsAtMaxCapacity();
    }
    if (code != nullptr && data != nullptr) {
      break;
    }
    Free(self, region, code, data);
    if (at_max_capacity) {
      VLOG(jit) << "JIT failed to allocate code of size "
                << PrettySize(code_size)
                << ", and data of size "
                << PrettySize(data_size);
      return false;
    }
    // Run a code cache collection and try again.
    GarbageCollectCache(self);
  }

  *reserved_code = ArrayRef<const uint8_t>(code, code_size);
  *reserved_data = ArrayRef<const uint8_t>(data, data_size);

  MutexLock mu(self, *Locks::jit_lock_);
  histogram_code_memory_use_.AddValue(code_size);
  if (code_size > kCodeSizeLogThreshold) {
    LOG(INFO) << "JIT allocated "
              << PrettySize(code_size)
              << " for compiled code of "
              << ArtMethod::PrettyMethod(method);
  }
  histogram_stack_map_memory_use_.AddValue(data_size);
  if (data_size > kStackMapSizeLogThreshold) {
    LOG(INFO) << "JIT allocated "
              << PrettySize(data_size)
              << " for stack maps of "
              << ArtMethod::PrettyMethod(method);
  }
  return true;
}

void JitCodeCache::Free(Thread* self,
                        JitMemoryRegion* region,
                        const uint8_t* code,
                        const uint8_t* data) {
  MutexLock mu(self, *Locks::jit_lock_);
  ScopedCodeCacheWrite ccw(*region);
  FreeLocked(region, code, data);
}

void JitCodeCache::FreeLocked(JitMemoryRegion* region, const uint8_t* code, const uint8_t* data) {
  if (code != nullptr) {
    RemoveNativeDebugInfoForJit(reinterpret_cast<const void*>(FromAllocationToCode(code)));
    region->FreeCode(code);
  }
  if (data != nullptr) {
    region->FreeData(data);
  }
}

class MarkCodeClosure final : public Closure {
 public:
  MarkCodeClosure(JitCodeCache* code_cache, CodeCacheBitmap* bitmap, Barrier* barrier)
      : code_cache_(code_cache), bitmap_(bitmap), barrier_(barrier) {}

  void Run(Thread* thread) override REQUIRES_SHARED(Locks::mutator_lock_) {
    ScopedTrace trace(__PRETTY_FUNCTION__);
    DCHECK(thread == Thread::Current() || thread->IsSuspended());
    StackVisitor::WalkStack(
        [&](const art::StackVisitor* stack_visitor) {
          const OatQuickMethodHeader* method_header =
              stack_visitor->GetCurrentOatQuickMethodHeader();
          if (method_header == nullptr) {
            return true;
          }
          const void* code = method_header->GetCode();
          if (code_cache_->ContainsPc(code) && !code_cache_->IsInZygoteExecSpace(code)) {
            // Use the atomic set version, as multiple threads are executing this code.
            bitmap_->AtomicTestAndSet(FromCodeToAllocation(code));
          }
          return true;
        },
        thread,
        /* context= */ nullptr,
        art::StackVisitor::StackWalkKind::kSkipInlinedFrames);

    if (kIsDebugBuild) {
      // The stack walking code queries the side instrumentation stack if it
      // sees an instrumentation exit pc, so the JIT code of methods in that stack
      // must have been seen. We sanity check this below.
      for (const auto& it : *thread->GetInstrumentationStack()) {
        // The 'method_' in InstrumentationStackFrame is the one that has return_pc_ in
        // its stack frame, it is not the method owning return_pc_. We just pass null to
        // LookupMethodHeader: the method is only checked against in debug builds.
        OatQuickMethodHeader* method_header =
            code_cache_->LookupMethodHeader(it.second.return_pc_, /* method= */ nullptr);
        if (method_header != nullptr) {
          const void* code = method_header->GetCode();
          CHECK(bitmap_->Test(FromCodeToAllocation(code)));
        }
      }
    }
    barrier_->Pass(Thread::Current());
  }

 private:
  JitCodeCache* const code_cache_;
  CodeCacheBitmap* const bitmap_;
  Barrier* const barrier_;
};

void JitCodeCache::NotifyCollectionDone(Thread* self) {
  collection_in_progress_ = false;
  lock_cond_.Broadcast(self);
}

void JitCodeCache::MarkCompiledCodeOnThreadStacks(Thread* self) {
  Barrier barrier(0);
  size_t threads_running_checkpoint = 0;
  MarkCodeClosure closure(this, GetLiveBitmap(), &barrier);
  threads_running_checkpoint = Runtime::Current()->GetThreadList()->RunCheckpoint(&closure);
  // Now that we have run our checkpoint, move to a suspended state and wait
  // for other threads to run the checkpoint.
  ScopedThreadSuspension sts(self, kSuspended);
  if (threads_running_checkpoint != 0) {
    barrier.Increment(self, threads_running_checkpoint);
  }
}

bool JitCodeCache::IsAtMaxCapacity() const {
  return private_region_.GetCurrentCapacity() == private_region_.GetMaxCapacity();
}

bool JitCodeCache::ShouldDoFullCollection() {
  if (IsAtMaxCapacity()) {
    // Always do a full collection when the code cache is full.
    return true;
  } else if (private_region_.GetCurrentCapacity() < kReservedCapacity) {
    // Always do partial collection when the code cache size is below the reserved
    // capacity.
    return false;
  } else if (last_collection_increased_code_cache_) {
    // This time do a full collection.
    return true;
  } else {
    // This time do a partial collection.
    return false;
  }
}

void JitCodeCache::GarbageCollectCache(Thread* self) {
  ScopedTrace trace(__FUNCTION__);
  // Wait for an existing collection, or let everyone know we are starting one.
  {
    ScopedThreadSuspension sts(self, kSuspended);
    MutexLock mu(self, *Locks::jit_lock_);
    if (!garbage_collect_code_) {
      private_region_.IncreaseCodeCacheCapacity();
      return;
    } else if (WaitForPotentialCollectionToComplete(self)) {
      return;
    } else {
      number_of_collections_++;
      live_bitmap_.reset(CodeCacheBitmap::Create(
          "code-cache-bitmap",
          reinterpret_cast<uintptr_t>(private_region_.GetExecPages()->Begin()),
          reinterpret_cast<uintptr_t>(
              private_region_.GetExecPages()->Begin() + private_region_.GetCurrentCapacity() / 2)));
      collection_in_progress_ = true;
    }
  }

  TimingLogger logger("JIT code cache timing logger", true, VLOG_IS_ON(jit));
  {
    TimingLogger::ScopedTiming st("Code cache collection", &logger);

    bool do_full_collection = false;
    {
      MutexLock mu(self, *Locks::jit_lock_);
      do_full_collection = ShouldDoFullCollection();
    }

    VLOG(jit) << "Do "
              << (do_full_collection ? "full" : "partial")
              << " code cache collection, code="
              << PrettySize(CodeCacheSize())
              << ", data=" << PrettySize(DataCacheSize());

    DoCollection(self, /* collect_profiling_info= */ do_full_collection);

    VLOG(jit) << "After code cache collection, code="
              << PrettySize(CodeCacheSize())
              << ", data=" << PrettySize(DataCacheSize());

    {
      MutexLock mu(self, *Locks::jit_lock_);

      // Increase the code cache only when we do partial collections.
      // TODO: base this strategy on how full the code cache is?
      if (do_full_collection) {
        last_collection_increased_code_cache_ = false;
      } else {
        last_collection_increased_code_cache_ = true;
        private_region_.IncreaseCodeCacheCapacity();
      }

      bool next_collection_will_be_full = ShouldDoFullCollection();

      // Start polling the liveness of compiled code to prepare for the next full collection.
      if (next_collection_will_be_full) {
        if (Runtime::Current()->GetJITOptions()->CanCompileBaseline()) {
          for (ProfilingInfo* info : profiling_infos_) {
            info->SetBaselineHotnessCount(0);
          }
        } else {
          // Save the entry point of methods we have compiled, and update the entry
          // point of those methods to the interpreter. If the method is invoked, the
          // interpreter will update its entry point to the compiled code and call it.
          for (ProfilingInfo* info : profiling_infos_) {
            const void* entry_point = info->GetMethod()->GetEntryPointFromQuickCompiledCode();
            if (!IsInZygoteDataSpace(info) && ContainsPc(entry_point)) {
              info->SetSavedEntryPoint(entry_point);
              // Don't call Instrumentation::UpdateMethodsCode(), as it can check the declaring
              // class of the method. We may be concurrently running a GC which makes accessing
              // the class unsafe. We know it is OK to bypass the instrumentation as we've just
              // checked that the current entry point is JIT compiled code.
              info->GetMethod()->SetEntryPointFromQuickCompiledCode(GetQuickToInterpreterBridge());
            }
          }
        }

        // Change entry points of native methods back to the GenericJNI entrypoint.
        for (const auto& entry : jni_stubs_map_) {
          const JniStubData& data = entry.second;
          if (!data.IsCompiled() || IsInZygoteExecSpace(data.GetCode())) {
            continue;
          }
          // Make sure a single invocation of the GenericJNI trampoline tries to recompile.
          uint16_t new_counter = Runtime::Current()->GetJit()->HotMethodThreshold() - 1u;
          const OatQuickMethodHeader* method_header =
              OatQuickMethodHeader::FromCodePointer(data.GetCode());
          for (ArtMethod* method : data.GetMethods()) {
            if (method->GetEntryPointFromQuickCompiledCode() == method_header->GetEntryPoint()) {
              // Don't call Instrumentation::UpdateMethodsCode(), same as for normal methods above.
              method->SetCounter(new_counter);
              method->SetEntryPointFromQuickCompiledCode(GetQuickGenericJniStub());
            }
          }
        }
      }
      live_bitmap_.reset(nullptr);
      NotifyCollectionDone(self);
    }
  }
  Runtime::Current()->GetJit()->AddTimingLogger(logger);
}

void JitCodeCache::RemoveUnmarkedCode(Thread* self) {
  ScopedTrace trace(__FUNCTION__);
  std::unordered_set<OatQuickMethodHeader*> method_headers;
  {
    MutexLock mu(self, *Locks::jit_lock_);
    // Iterate over all compiled code and remove entries that are not marked.
    for (auto it = jni_stubs_map_.begin(); it != jni_stubs_map_.end();) {
      JniStubData* data = &it->second;
      if (IsInZygoteExecSpace(data->GetCode()) ||
          !data->IsCompiled() ||
          GetLiveBitmap()->Test(FromCodeToAllocation(data->GetCode()))) {
        ++it;
      } else {
        method_headers.insert(OatQuickMethodHeader::FromCodePointer(data->GetCode()));
        for (ArtMethod* method : data->GetMethods()) {
          VLOG(jit) << "JIT removed (JNI) " << method->PrettyMethod() << ": " << data->GetCode();
        }
        it = jni_stubs_map_.erase(it);
      }
    }
    for (auto it = method_code_map_.begin(); it != method_code_map_.end();) {
      const void* code_ptr = it->first;
      uintptr_t allocation = FromCodeToAllocation(code_ptr);
      if (IsInZygoteExecSpace(code_ptr) || GetLiveBitmap()->Test(allocation)) {
        ++it;
      } else {
        OatQuickMethodHeader* header = OatQuickMethodHeader::FromCodePointer(code_ptr);
        method_headers.insert(header);
        VLOG(jit) << "JIT removed " << it->second->PrettyMethod() << ": " << it->first;
        it = method_code_map_.erase(it);
      }
    }
    FreeAllMethodHeaders(method_headers);
  }
}

bool JitCodeCache::GetGarbageCollectCode() {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  return garbage_collect_code_;
}

void JitCodeCache::SetGarbageCollectCode(bool value) {
  Thread* self = Thread::Current();
  MutexLock mu(self, *Locks::jit_lock_);
  if (garbage_collect_code_ != value) {
    if (garbage_collect_code_) {
      // When dynamically disabling the garbage collection, we neee
      // to make sure that a potential current collection is finished, and also
      // clear the saved entry point in profiling infos to avoid dangling pointers.
      WaitForPotentialCollectionToComplete(self);
      for (ProfilingInfo* info : profiling_infos_) {
        info->SetSavedEntryPoint(nullptr);
      }
    }
    // Update the flag while holding the lock to ensure no thread will try to GC.
    garbage_collect_code_ = value;
  }
}

void JitCodeCache::DoCollection(Thread* self, bool collect_profiling_info) {
  ScopedTrace trace(__FUNCTION__);
  {
    MutexLock mu(self, *Locks::jit_lock_);

    if (Runtime::Current()->GetJITOptions()->CanCompileBaseline()) {
      // Update to interpreter the methods that have baseline entrypoints and whose baseline
      // hotness count is zero.
      // Note that these methods may be in thread stack or concurrently revived
      // between. That's OK, as the thread executing it will mark it.
      for (ProfilingInfo* info : profiling_infos_) {
        if (info->GetBaselineHotnessCount() == 0) {
          const void* entry_point = info->GetMethod()->GetEntryPointFromQuickCompiledCode();
          if (ContainsPc(entry_point)) {
            OatQuickMethodHeader* method_header =
                OatQuickMethodHeader::FromEntryPoint(entry_point);
            if (CodeInfo::IsBaseline(method_header->GetOptimizedCodeInfoPtr())) {
              info->GetMethod()->SetEntryPointFromQuickCompiledCode(GetQuickToInterpreterBridge());
            }
          }
        }
      }
      // TODO: collect profiling info
      // TODO: collect optimized code?
    } else {
      if (collect_profiling_info) {
        // Clear the profiling info of methods that do not have compiled code as entrypoint.
        // Also remove the saved entry point from the ProfilingInfo objects.
        for (ProfilingInfo* info : profiling_infos_) {
          const void* ptr = info->GetMethod()->GetEntryPointFromQuickCompiledCode();
          if (!ContainsPc(ptr) && !info->IsInUseByCompiler() && !IsInZygoteDataSpace(info)) {
            info->GetMethod()->SetProfilingInfo(nullptr);
          }

          if (info->GetSavedEntryPoint() != nullptr) {
            info->SetSavedEntryPoint(nullptr);
            // We are going to move this method back to interpreter. Clear the counter now to
            // give it a chance to be hot again.
            ClearMethodCounter(info->GetMethod(), /*was_warm=*/ true);
          }
        }
      } else if (kIsDebugBuild) {
        // Sanity check that the profiling infos do not have a dangling entry point.
        for (ProfilingInfo* info : profiling_infos_) {
          DCHECK(!Runtime::Current()->IsZygote());
          const void* entry_point = info->GetSavedEntryPoint();
          DCHECK(entry_point == nullptr || IsInZygoteExecSpace(entry_point));
        }
      }
    }

    // Mark compiled code that are entrypoints of ArtMethods. Compiled code that is not
    // an entry point is either:
    // - an osr compiled code, that will be removed if not in a thread call stack.
    // - discarded compiled code, that will be removed if not in a thread call stack.
    for (const auto& entry : jni_stubs_map_) {
      const JniStubData& data = entry.second;
      const void* code_ptr = data.GetCode();
      if (IsInZygoteExecSpace(code_ptr)) {
        continue;
      }
      const OatQuickMethodHeader* method_header = OatQuickMethodHeader::FromCodePointer(code_ptr);
      for (ArtMethod* method : data.GetMethods()) {
        if (method_header->GetEntryPoint() == method->GetEntryPointFromQuickCompiledCode()) {
          GetLiveBitmap()->AtomicTestAndSet(FromCodeToAllocation(code_ptr));
          break;
        }
      }
    }
    for (const auto& it : method_code_map_) {
      ArtMethod* method = it.second;
      const void* code_ptr = it.first;
      if (IsInZygoteExecSpace(code_ptr)) {
        continue;
      }
      const OatQuickMethodHeader* method_header = OatQuickMethodHeader::FromCodePointer(code_ptr);
      if (method_header->GetEntryPoint() == method->GetEntryPointFromQuickCompiledCode()) {
        GetLiveBitmap()->AtomicTestAndSet(FromCodeToAllocation(code_ptr));
      }
    }

    // Empty osr method map, as osr compiled code will be deleted (except the ones
    // on thread stacks).
    osr_code_map_.clear();
  }

  // Run a checkpoint on all threads to mark the JIT compiled code they are running.
  MarkCompiledCodeOnThreadStacks(self);

  // At this point, mutator threads are still running, and entrypoints of methods can
  // change. We do know they cannot change to a code cache entry that is not marked,
  // therefore we can safely remove those entries.
  RemoveUnmarkedCode(self);

  if (collect_profiling_info) {
    MutexLock mu(self, *Locks::jit_lock_);
    // Free all profiling infos of methods not compiled nor being compiled.
    auto profiling_kept_end = std::remove_if(profiling_infos_.begin(), profiling_infos_.end(),
      [this] (ProfilingInfo* info) NO_THREAD_SAFETY_ANALYSIS {
        const void* ptr = info->GetMethod()->GetEntryPointFromQuickCompiledCode();
        // We have previously cleared the ProfilingInfo pointer in the ArtMethod in the hope
        // that the compiled code would not get revived. As mutator threads run concurrently,
        // they may have revived the compiled code, and now we are in the situation where
        // a method has compiled code but no ProfilingInfo.
        // We make sure compiled methods have a ProfilingInfo object. It is needed for
        // code cache collection.
        if (ContainsPc(ptr) &&
            info->GetMethod()->GetProfilingInfo(kRuntimePointerSize) == nullptr) {
          info->GetMethod()->SetProfilingInfo(info);
        } else if (info->GetMethod()->GetProfilingInfo(kRuntimePointerSize) != info) {
          // No need for this ProfilingInfo object anymore.
          private_region_.FreeWritableData(reinterpret_cast<uint8_t*>(info));
          return true;
        }
        return false;
      });
    profiling_infos_.erase(profiling_kept_end, profiling_infos_.end());
  }
}

OatQuickMethodHeader* JitCodeCache::LookupMethodHeader(uintptr_t pc, ArtMethod* method) {
  static_assert(kRuntimeISA != InstructionSet::kThumb2, "kThumb2 cannot be a runtime ISA");
  if (kRuntimeISA == InstructionSet::kArm) {
    // On Thumb-2, the pc is offset by one.
    --pc;
  }
  if (!ContainsPc(reinterpret_cast<const void*>(pc))) {
    return nullptr;
  }

  if (!kIsDebugBuild) {
    // Called with null `method` only from MarkCodeClosure::Run() in debug build.
    CHECK(method != nullptr);
  }

  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  OatQuickMethodHeader* method_header = nullptr;
  ArtMethod* found_method = nullptr;  // Only for DCHECK(), not for JNI stubs.
  if (method != nullptr && UNLIKELY(method->IsNative())) {
    auto it = jni_stubs_map_.find(JniStubKey(method));
    if (it == jni_stubs_map_.end() || !ContainsElement(it->second.GetMethods(), method)) {
      return nullptr;
    }
    const void* code_ptr = it->second.GetCode();
    method_header = OatQuickMethodHeader::FromCodePointer(code_ptr);
    if (!method_header->Contains(pc)) {
      return nullptr;
    }
  } else {
    if (shared_region_.IsInExecSpace(reinterpret_cast<const void*>(pc))) {
      const void* code_ptr = zygote_map_.GetCodeFor(method, pc);
      if (code_ptr != nullptr) {
        return OatQuickMethodHeader::FromCodePointer(code_ptr);
      }
    }
    auto it = method_code_map_.lower_bound(reinterpret_cast<const void*>(pc));
    if (it != method_code_map_.begin()) {
      --it;
      const void* code_ptr = it->first;
      if (OatQuickMethodHeader::FromCodePointer(code_ptr)->Contains(pc)) {
        method_header = OatQuickMethodHeader::FromCodePointer(code_ptr);
        found_method = it->second;
      }
    }
    if (method_header == nullptr && method == nullptr) {
      // Scan all compiled JNI stubs as well. This slow search is used only
      // for checks in debug build, for release builds the `method` is not null.
      for (auto&& entry : jni_stubs_map_) {
        const JniStubData& data = entry.second;
        if (data.IsCompiled() &&
            OatQuickMethodHeader::FromCodePointer(data.GetCode())->Contains(pc)) {
          method_header = OatQuickMethodHeader::FromCodePointer(data.GetCode());
        }
      }
    }
    if (method_header == nullptr) {
      return nullptr;
    }
  }

  if (kIsDebugBuild && method != nullptr && !method->IsNative()) {
    DCHECK_EQ(found_method, method)
        << ArtMethod::PrettyMethod(method) << " "
        << ArtMethod::PrettyMethod(found_method) << " "
        << std::hex << pc;
  }
  return method_header;
}

OatQuickMethodHeader* JitCodeCache::LookupOsrMethodHeader(ArtMethod* method) {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  auto it = osr_code_map_.find(method);
  if (it == osr_code_map_.end()) {
    return nullptr;
  }
  return OatQuickMethodHeader::FromCodePointer(it->second);
}

ProfilingInfo* JitCodeCache::AddProfilingInfo(Thread* self,
                                              ArtMethod* method,
                                              const std::vector<uint32_t>& entries,
                                              bool retry_allocation)
    // No thread safety analysis as we are using TryLock/Unlock explicitly.
    NO_THREAD_SAFETY_ANALYSIS {
  DCHECK(CanAllocateProfilingInfo());
  ProfilingInfo* info = nullptr;
  if (!retry_allocation) {
    // If we are allocating for the interpreter, just try to lock, to avoid
    // lock contention with the JIT.
    if (Locks::jit_lock_->ExclusiveTryLock(self)) {
      info = AddProfilingInfoInternal(self, method, entries);
      Locks::jit_lock_->ExclusiveUnlock(self);
    }
  } else {
    {
      MutexLock mu(self, *Locks::jit_lock_);
      info = AddProfilingInfoInternal(self, method, entries);
    }

    if (info == nullptr) {
      GarbageCollectCache(self);
      MutexLock mu(self, *Locks::jit_lock_);
      info = AddProfilingInfoInternal(self, method, entries);
    }
  }
  return info;
}

ProfilingInfo* JitCodeCache::AddProfilingInfoInternal(Thread* self ATTRIBUTE_UNUSED,
                                                      ArtMethod* method,
                                                      const std::vector<uint32_t>& entries) {
  size_t profile_info_size = RoundUp(
      sizeof(ProfilingInfo) + sizeof(InlineCache) * entries.size(),
      sizeof(void*));

  // Check whether some other thread has concurrently created it.
  ProfilingInfo* info = method->GetProfilingInfo(kRuntimePointerSize);
  if (info != nullptr) {
    return info;
  }

  const uint8_t* data = private_region_.AllocateData(profile_info_size);
  if (data == nullptr) {
    return nullptr;
  }
  uint8_t* writable_data = private_region_.GetWritableDataAddress(data);
  info = new (writable_data) ProfilingInfo(method, entries);

  // Make sure other threads see the data in the profiling info object before the
  // store in the ArtMethod's ProfilingInfo pointer.
  std::atomic_thread_fence(std::memory_order_release);

  method->SetProfilingInfo(info);
  profiling_infos_.push_back(info);
  histogram_profiling_info_memory_use_.AddValue(profile_info_size);
  return info;
}

void* JitCodeCache::MoreCore(const void* mspace, intptr_t increment) {
  return shared_region_.OwnsSpace(mspace)
      ? shared_region_.MoreCore(mspace, increment)
      : private_region_.MoreCore(mspace, increment);
}

void JitCodeCache::GetProfiledMethods(const std::set<std::string>& dex_base_locations,
                                      std::vector<ProfileMethodInfo>& methods) {
  Thread* self = Thread::Current();
  WaitUntilInlineCacheAccessible(self);
  MutexLock mu(self, *Locks::jit_lock_);
  ScopedTrace trace(__FUNCTION__);
  uint16_t jit_compile_threshold = Runtime::Current()->GetJITOptions()->GetCompileThreshold();
  for (const ProfilingInfo* info : profiling_infos_) {
    ArtMethod* method = info->GetMethod();
    const DexFile* dex_file = method->GetDexFile();
    const std::string base_location = DexFileLoader::GetBaseLocation(dex_file->GetLocation());
    if (!ContainsElement(dex_base_locations, base_location)) {
      // Skip dex files which are not profiled.
      continue;
    }
    std::vector<ProfileMethodInfo::ProfileInlineCache> inline_caches;

    // If the method didn't reach the compilation threshold don't save the inline caches.
    // They might be incomplete and cause unnecessary deoptimizations.
    // If the inline cache is empty the compiler will generate a regular invoke virtual/interface.
    if (method->GetCounter() < jit_compile_threshold) {
      methods.emplace_back(/*ProfileMethodInfo*/
          MethodReference(dex_file, method->GetDexMethodIndex()), inline_caches);
      continue;
    }

    for (size_t i = 0; i < info->number_of_inline_caches_; ++i) {
      std::vector<TypeReference> profile_classes;
      const InlineCache& cache = info->cache_[i];
      ArtMethod* caller = info->GetMethod();
      bool is_missing_types = false;
      for (size_t k = 0; k < InlineCache::kIndividualCacheSize; k++) {
        mirror::Class* cls = cache.classes_[k].Read();
        if (cls == nullptr) {
          break;
        }

        // Check if the receiver is in the boot class path or if it's in the
        // same class loader as the caller. If not, skip it, as there is not
        // much we can do during AOT.
        if (!cls->IsBootStrapClassLoaded() &&
            caller->GetClassLoader() != cls->GetClassLoader()) {
          is_missing_types = true;
          continue;
        }

        const DexFile* class_dex_file = nullptr;
        dex::TypeIndex type_index;

        if (cls->GetDexCache() == nullptr) {
          DCHECK(cls->IsArrayClass()) << cls->PrettyClass();
          // Make a best effort to find the type index in the method's dex file.
          // We could search all open dex files but that might turn expensive
          // and probably not worth it.
          class_dex_file = dex_file;
          type_index = cls->FindTypeIndexInOtherDexFile(*dex_file);
        } else {
          class_dex_file = &(cls->GetDexFile());
          type_index = cls->GetDexTypeIndex();
        }
        if (!type_index.IsValid()) {
          // Could be a proxy class or an array for which we couldn't find the type index.
          is_missing_types = true;
          continue;
        }
        if (ContainsElement(dex_base_locations,
                            DexFileLoader::GetBaseLocation(class_dex_file->GetLocation()))) {
          // Only consider classes from the same apk (including multidex).
          profile_classes.emplace_back(/*ProfileMethodInfo::ProfileClassReference*/
              class_dex_file, type_index);
        } else {
          is_missing_types = true;
        }
      }
      if (!profile_classes.empty()) {
        inline_caches.emplace_back(/*ProfileMethodInfo::ProfileInlineCache*/
            cache.dex_pc_, is_missing_types, profile_classes);
      }
    }
    methods.emplace_back(/*ProfileMethodInfo*/
        MethodReference(dex_file, method->GetDexMethodIndex()), inline_caches);
  }
}

bool JitCodeCache::IsOsrCompiled(ArtMethod* method) {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  return osr_code_map_.find(method) != osr_code_map_.end();
}

bool JitCodeCache::NotifyCompilationOf(ArtMethod* method,
                                       Thread* self,
                                       bool osr,
                                       bool prejit,
                                       bool baseline,
                                       JitMemoryRegion* region) {
  const void* existing_entry_point = method->GetEntryPointFromQuickCompiledCode();
  if (!osr && ContainsPc(existing_entry_point)) {
    OatQuickMethodHeader* method_header =
        OatQuickMethodHeader::FromEntryPoint(existing_entry_point);
    if (CodeInfo::IsBaseline(method_header->GetOptimizedCodeInfoPtr()) == baseline) {
      VLOG(jit) << "Not compiling "
                << method->PrettyMethod()
                << " because it has already been compiled"
                << " baseline=" << std::boolalpha << baseline;
      return false;
    }
  }

  if (NeedsClinitCheckBeforeCall(method) && !prejit) {
    // We do not need a synchronization barrier for checking the visibly initialized status
    // or checking the initialized status just for requesting visible initialization.
    ClassStatus status = method->GetDeclaringClass()
        ->GetStatus<kDefaultVerifyFlags, /*kWithSynchronizationBarrier=*/ false>();
    if (status != ClassStatus::kVisiblyInitialized) {
      // Unless we're pre-jitting, we currently don't save the JIT compiled code if we cannot
      // update the entrypoint due to needing an initialization check.
      if (status == ClassStatus::kInitialized) {
        // Request visible initialization but do not block to allow compiling other methods.
        // Hopefully, this will complete by the time the method becomes hot again.
        Runtime::Current()->GetClassLinker()->MakeInitializedClassesVisiblyInitialized(
            self, /*wait=*/ false);
      }
      VLOG(jit) << "Not compiling "
                << method->PrettyMethod()
                << " because it has the resolution stub";
      // Give it a new chance to be hot.
      ClearMethodCounter(method, /*was_warm=*/ false);
      return false;
    }
  }

  if (osr) {
    MutexLock mu(self, *Locks::jit_lock_);
    if (osr_code_map_.find(method) != osr_code_map_.end()) {
      return false;
    }
  }

  if (UNLIKELY(method->IsNative())) {
    MutexLock mu(self, *Locks::jit_lock_);
    JniStubKey key(method);
    auto it = jni_stubs_map_.find(key);
    bool new_compilation = false;
    if (it == jni_stubs_map_.end()) {
      // Create a new entry to mark the stub as being compiled.
      it = jni_stubs_map_.Put(key, JniStubData{});
      new_compilation = true;
    }
    JniStubData* data = &it->second;
    data->AddMethod(method);
    if (data->IsCompiled()) {
      OatQuickMethodHeader* method_header = OatQuickMethodHeader::FromCodePointer(data->GetCode());
      const void* entrypoint = method_header->GetEntryPoint();
      // Update also entrypoints of other methods held by the JniStubData.
      // We could simply update the entrypoint of `method` but if the last JIT GC has
      // changed these entrypoints to GenericJNI in preparation for a full GC, we may
      // as well change them back as this stub shall not be collected anyway and this
      // can avoid a few expensive GenericJNI calls.
      data->UpdateEntryPoints(entrypoint);
      if (collection_in_progress_) {
        if (!IsInZygoteExecSpace(data->GetCode())) {
          GetLiveBitmap()->AtomicTestAndSet(FromCodeToAllocation(data->GetCode()));
        }
      }
    }
    return new_compilation;
  } else {
    ProfilingInfo* info = method->GetProfilingInfo(kRuntimePointerSize);
    if (CanAllocateProfilingInfo() && baseline && info == nullptr) {
      // We can retry allocation here as we're the JIT thread.
      if (ProfilingInfo::Create(self, method, /* retry_allocation= */ true)) {
        info = method->GetProfilingInfo(kRuntimePointerSize);
      }
    }
    if (info == nullptr) {
      // When prejitting, we don't allocate a profiling info.
      if (!prejit && !IsSharedRegion(*region)) {
        VLOG(jit) << method->PrettyMethod() << " needs a ProfilingInfo to be compiled";
        // Because the counter is not atomic, there are some rare cases where we may not hit the
        // threshold for creating the ProfilingInfo. Reset the counter now to "correct" this.
        ClearMethodCounter(method, /*was_warm=*/ false);
        return false;
      }
    } else {
      MutexLock mu(self, *Locks::jit_lock_);
      if (info->IsMethodBeingCompiled(osr)) {
        return false;
      }
      info->SetIsMethodBeingCompiled(true, osr);
    }
    return true;
  }
}

ProfilingInfo* JitCodeCache::NotifyCompilerUse(ArtMethod* method, Thread* self) {
  MutexLock mu(self, *Locks::jit_lock_);
  ProfilingInfo* info = method->GetProfilingInfo(kRuntimePointerSize);
  if (info != nullptr) {
    if (!info->IncrementInlineUse()) {
      // Overflow of inlining uses, just bail.
      return nullptr;
    }
  }
  return info;
}

void JitCodeCache::DoneCompilerUse(ArtMethod* method, Thread* self) {
  MutexLock mu(self, *Locks::jit_lock_);
  ProfilingInfo* info = method->GetProfilingInfo(kRuntimePointerSize);
  DCHECK(info != nullptr);
  info->DecrementInlineUse();
}

void JitCodeCache::DoneCompiling(ArtMethod* method, Thread* self, bool osr) {
  DCHECK_EQ(Thread::Current(), self);
  MutexLock mu(self, *Locks::jit_lock_);
  if (UNLIKELY(method->IsNative())) {
    auto it = jni_stubs_map_.find(JniStubKey(method));
    DCHECK(it != jni_stubs_map_.end());
    JniStubData* data = &it->second;
    DCHECK(ContainsElement(data->GetMethods(), method));
    if (UNLIKELY(!data->IsCompiled())) {
      // Failed to compile; the JNI compiler never fails, but the cache may be full.
      jni_stubs_map_.erase(it);  // Remove the entry added in NotifyCompilationOf().
    }  // else Commit() updated entrypoints of all methods in the JniStubData.
  } else {
    ProfilingInfo* info = method->GetProfilingInfo(kRuntimePointerSize);
    if (info != nullptr) {
      DCHECK(info->IsMethodBeingCompiled(osr));
      info->SetIsMethodBeingCompiled(false, osr);
    }
  }
}

void JitCodeCache::InvalidateAllCompiledCode() {
  art::MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  size_t cnt = profiling_infos_.size();
  size_t osr_size = osr_code_map_.size();
  for (ProfilingInfo* pi : profiling_infos_) {
    // NB Due to OSR we might run this on some methods multiple times but this should be fine.
    ArtMethod* meth = pi->GetMethod();
    pi->SetSavedEntryPoint(nullptr);
    // We had a ProfilingInfo so we must be warm.
    ClearMethodCounter(meth, /*was_warm=*/true);
    ClassLinker* linker = Runtime::Current()->GetClassLinker();
    if (meth->IsObsolete()) {
      linker->SetEntryPointsForObsoleteMethod(meth);
    } else {
      linker->SetEntryPointsToInterpreter(meth);
    }
  }
  osr_code_map_.clear();
  VLOG(jit) << "Invalidated the compiled code of " << (cnt - osr_size) << " methods and "
            << osr_size << " OSRs.";
}

void JitCodeCache::InvalidateCompiledCodeFor(ArtMethod* method,
                                             const OatQuickMethodHeader* header) {
  DCHECK(!method->IsNative());
  ProfilingInfo* profiling_info = method->GetProfilingInfo(kRuntimePointerSize);
  const void* method_entrypoint = method->GetEntryPointFromQuickCompiledCode();
  if ((profiling_info != nullptr) &&
      (profiling_info->GetSavedEntryPoint() == header->GetEntryPoint())) {
    // When instrumentation is set, the actual entrypoint is the one in the profiling info.
    method_entrypoint = profiling_info->GetSavedEntryPoint();
    // Prevent future uses of the compiled code.
    profiling_info->SetSavedEntryPoint(nullptr);
  }

  // Clear the method counter if we are running jitted code since we might want to jit this again in
  // the future.
  if (method_entrypoint == header->GetEntryPoint()) {
    // The entrypoint is the one to invalidate, so we just update it to the interpreter entry point
    // and clear the counter to get the method Jitted again.
    Runtime::Current()->GetInstrumentation()->UpdateMethodsCode(
        method, GetQuickToInterpreterBridge());
    ClearMethodCounter(method, /*was_warm=*/ profiling_info != nullptr);
  } else {
    MutexLock mu(Thread::Current(), *Locks::jit_lock_);
    auto it = osr_code_map_.find(method);
    if (it != osr_code_map_.end() && OatQuickMethodHeader::FromCodePointer(it->second) == header) {
      // Remove the OSR method, to avoid using it again.
      osr_code_map_.erase(it);
    }
  }

  // In case the method was pre-compiled, clear that information so we
  // can recompile it ourselves.
  if (method->IsPreCompiled()) {
    method->ClearPreCompiled();
  }
}

void JitCodeCache::Dump(std::ostream& os) {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  os << "Current JIT code cache size (used / resident): "
     << GetCurrentRegion()->GetUsedMemoryForCode() / KB << "KB / "
     << GetCurrentRegion()->GetResidentMemoryForCode() / KB << "KB\n"
     << "Current JIT data cache size (used / resident): "
     << GetCurrentRegion()->GetUsedMemoryForData() / KB << "KB / "
     << GetCurrentRegion()->GetResidentMemoryForData() / KB << "KB\n";
  if (!Runtime::Current()->IsZygote()) {
    os << "Zygote JIT code cache size (at point of fork): "
       << shared_region_.GetUsedMemoryForCode() / KB << "KB / "
       << shared_region_.GetResidentMemoryForCode() / KB << "KB\n"
       << "Zygote JIT data cache size (at point of fork): "
       << shared_region_.GetUsedMemoryForData() / KB << "KB / "
       << shared_region_.GetResidentMemoryForData() / KB << "KB\n";
  }
  os << "Current JIT mini-debug-info size: " << PrettySize(GetJitMiniDebugInfoMemUsage()) << "\n"
     << "Current JIT capacity: " << PrettySize(GetCurrentRegion()->GetCurrentCapacity()) << "\n"
     << "Current number of JIT JNI stub entries: " << jni_stubs_map_.size() << "\n"
     << "Current number of JIT code cache entries: " << method_code_map_.size() << "\n"
     << "Total number of JIT compilations: " << number_of_compilations_ << "\n"
     << "Total number of JIT compilations for on stack replacement: "
        << number_of_osr_compilations_ << "\n"
     << "Total number of JIT code cache collections: " << number_of_collections_ << std::endl;
  histogram_stack_map_memory_use_.PrintMemoryUse(os);
  histogram_code_memory_use_.PrintMemoryUse(os);
  histogram_profiling_info_memory_use_.PrintMemoryUse(os);
}

void JitCodeCache::PostForkChildAction(bool is_system_server, bool is_zygote) {
  Thread* self = Thread::Current();

  // Remove potential tasks that have been inherited from the zygote.
  // We do this now and not in Jit::PostForkChildAction, as system server calls
  // JitCodeCache::PostForkChildAction first, and then does some code loading
  // that may result in new JIT tasks that we want to keep.
  ThreadPool* pool = Runtime::Current()->GetJit()->GetThreadPool();
  if (pool != nullptr) {
    pool->RemoveAllTasks(self);
  }

  MutexLock mu(self, *Locks::jit_lock_);

  // Reset potential writable MemMaps inherited from the zygote. We never want
  // to write to them.
  shared_region_.ResetWritableMappings();

  if (is_zygote || Runtime::Current()->IsSafeMode()) {
    // Don't create a private region for a child zygote. Regions are usually map shared
    // (to satisfy dual-view), and we don't want children of a child zygote to inherit it.
    return;
  }

  // Reset all statistics to be specific to this process.
  number_of_compilations_ = 0;
  number_of_osr_compilations_ = 0;
  number_of_collections_ = 0;
  histogram_stack_map_memory_use_.Reset();
  histogram_code_memory_use_.Reset();
  histogram_profiling_info_memory_use_.Reset();

  size_t initial_capacity = Runtime::Current()->GetJITOptions()->GetCodeCacheInitialCapacity();
  size_t max_capacity = Runtime::Current()->GetJITOptions()->GetCodeCacheMaxCapacity();
  std::string error_msg;
  if (!private_region_.Initialize(initial_capacity,
                                  max_capacity,
                                  /* rwx_memory_allowed= */ !is_system_server,
                                  is_zygote,
                                  &error_msg)) {
    LOG(WARNING) << "Could not create private region after zygote fork: " << error_msg;
  }
}

JitMemoryRegion* JitCodeCache::GetCurrentRegion() {
  return Runtime::Current()->IsZygote() ? &shared_region_ : &private_region_;
}

void JitCodeCache::VisitAllMethods(const std::function<void(const void*, ArtMethod*)>& cb) {
  for (const auto& it : jni_stubs_map_) {
    const JniStubData& data = it.second;
    if (data.IsCompiled()) {
      for (ArtMethod* method : data.GetMethods()) {
        cb(data.GetCode(), method);
      }
    }
  }
  for (auto it : method_code_map_) {  // Includes OSR methods.
    cb(it.first, it.second);
  }
  for (auto it : saved_compiled_methods_map_) {
    cb(it.second, it.first);
  }
  for (auto it : zygote_map_) {
    if (it.code_ptr != nullptr && it.method != nullptr) {
      cb(it.code_ptr, it.method);
    }
  }
}

void ZygoteMap::Initialize(uint32_t number_of_methods) {
  MutexLock mu(Thread::Current(), *Locks::jit_lock_);
  // Allocate for 40-80% capacity. This will offer OK lookup times, and termination
  // cases.
  size_t capacity = RoundUpToPowerOfTwo(number_of_methods * 100 / 80);
  const uint8_t* memory = region_->AllocateData(
      capacity * sizeof(Entry) + sizeof(ZygoteCompilationState));
  if (memory == nullptr) {
    LOG(WARNING) << "Could not allocate data for the zygote map";
    return;
  }
  const Entry* data = reinterpret_cast<const Entry*>(memory);
  region_->FillData(data, capacity, Entry { nullptr, nullptr });
  map_ = ArrayRef(data, capacity);
  compilation_state_ = reinterpret_cast<const ZygoteCompilationState*>(
      memory + capacity * sizeof(Entry));
  region_->WriteData(compilation_state_, ZygoteCompilationState::kInProgress);
}

const void* ZygoteMap::GetCodeFor(ArtMethod* method, uintptr_t pc) const {
  if (map_.empty()) {
    return nullptr;
  }

  if (method == nullptr) {
    // Do a linear search. This should only be used in debug builds.
    CHECK(kIsDebugBuild);
    for (const Entry& entry : map_) {
      const void* code_ptr = entry.code_ptr;
      if (code_ptr != nullptr) {
        OatQuickMethodHeader* method_header = OatQuickMethodHeader::FromCodePointer(code_ptr);
        if (method_header->Contains(pc)) {
          return code_ptr;
        }
      }
    }
    return nullptr;
  }

  std::hash<ArtMethod*> hf;
  size_t index = hf(method) & (map_.size() - 1u);
  size_t original_index = index;
  // Loop over the array: we know this loop terminates as we will either
  // encounter the given method, or a null entry. Both terminate the loop.
  // Note that the zygote may concurrently write new entries to the map. That's OK as the
  // map is never resized.
  while (true) {
    const Entry& entry = map_[index];
    if (entry.method == nullptr) {
      // Not compiled yet.
      return nullptr;
    }
    if (entry.method == method) {
      if (entry.code_ptr == nullptr) {
        // This is a race with the zygote which wrote the method, but hasn't written the
        // code. Just bail and wait for the next time we need the method.
        return nullptr;
      }
      if (pc != 0 && !OatQuickMethodHeader::FromCodePointer(entry.code_ptr)->Contains(pc)) {
        return nullptr;
      }
      return entry.code_ptr;
    }
    index = (index + 1) & (map_.size() - 1);
    DCHECK_NE(original_index, index);
  }
}

void ZygoteMap::Put(const void* code, ArtMethod* method) {
  if (map_.empty()) {
    return;
  }
  CHECK(Runtime::Current()->IsZygote());
  std::hash<ArtMethod*> hf;
  size_t index = hf(method) & (map_.size() - 1);
  size_t original_index = index;
  // Because the size of the map is bigger than the number of methods that will
  // be added, we are guaranteed to find a free slot in the array, and
  // therefore for this loop to terminate.
  while (true) {
    const Entry* entry = &map_[index];
    if (entry->method == nullptr) {
      // Note that readers can read this memory concurrently, but that's OK as
      // we are writing pointers.
      region_->WriteData(entry, Entry { method, code });
      break;
    }
    index = (index + 1) & (map_.size() - 1);
    DCHECK_NE(original_index, index);
  }
  DCHECK_EQ(GetCodeFor(method), code);
}

}  // namespace jit
}  // namespace art
