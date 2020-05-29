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

#ifndef ART_RUNTIME_JIT_JIT_H_
#define ART_RUNTIME_JIT_JIT_H_

#include <android-base/unique_fd.h>

#include "base/histogram-inl.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "base/runtime_debug.h"
#include "base/timing_logger.h"
#include "handle.h"
#include "offsets.h"
#include "interpreter/mterp/mterp.h"
#include "jit/debugger_interface.h"
#include "jit/profile_saver_options.h"
#include "obj_ptr.h"
#include "thread_pool.h"

namespace art {

class ArtMethod;
class ClassLinker;
class DexFile;
class OatDexFile;
struct RuntimeArgumentMap;
union JValue;

namespace mirror {
class Object;
class Class;
class ClassLoader;
class DexCache;
class String;
}   // namespace mirror

namespace jit {

class JitCodeCache;
class JitMemoryRegion;
class JitOptions;

static constexpr int16_t kJitCheckForOSR = -1;
static constexpr int16_t kJitHotnessDisabled = -2;
// At what priority to schedule jit threads. 9 is the lowest foreground priority on device.
// See android/os/Process.java.
static constexpr int kJitPoolThreadPthreadDefaultPriority = 9;
// We check whether to jit-compile the method every Nth invoke.
// The tests often use threshold of 1000 (and thus 500 to start profiling).
static constexpr uint32_t kJitSamplesBatchSize = 512;  // Must be power of 2.

class JitOptions {
 public:
  static JitOptions* CreateFromRuntimeArguments(const RuntimeArgumentMap& options);

  uint16_t GetCompileThreshold() const {
    return compile_threshold_;
  }

  uint16_t GetWarmupThreshold() const {
    return warmup_threshold_;
  }

  uint16_t GetOsrThreshold() const {
    return osr_threshold_;
  }

  uint16_t GetPriorityThreadWeight() const {
    return priority_thread_weight_;
  }

  uint16_t GetInvokeTransitionWeight() const {
    return invoke_transition_weight_;
  }

  size_t GetCodeCacheInitialCapacity() const {
    return code_cache_initial_capacity_;
  }

  size_t GetCodeCacheMaxCapacity() const {
    return code_cache_max_capacity_;
  }

  bool DumpJitInfoOnShutdown() const {
    return dump_info_on_shutdown_;
  }

  const ProfileSaverOptions& GetProfileSaverOptions() const {
    return profile_saver_options_;
  }

  bool GetSaveProfilingInfo() const {
    return profile_saver_options_.IsEnabled();
  }

  int GetThreadPoolPthreadPriority() const {
    return thread_pool_pthread_priority_;
  }

  bool UseJitCompilation() const {
    return use_jit_compilation_;
  }

  bool UseTieredJitCompilation() const {
    return use_tiered_jit_compilation_;
  }

  bool CanCompileBaseline() const {
    return use_tiered_jit_compilation_ ||
           use_baseline_compiler_ ||
           interpreter::IsNterpSupported();
  }

  void SetUseJitCompilation(bool b) {
    use_jit_compilation_ = b;
  }

  void SetSaveProfilingInfo(bool save_profiling_info) {
    profile_saver_options_.SetEnabled(save_profiling_info);
  }

  void SetWaitForJitNotificationsToSaveProfile(bool value) {
    profile_saver_options_.SetWaitForJitNotificationsToSave(value);
  }

  void SetJitAtFirstUse() {
    use_jit_compilation_ = true;
    compile_threshold_ = 0;
  }

  void SetUseBaselineCompiler() {
    use_baseline_compiler_ = true;
  }

  bool UseBaselineCompiler() const {
    return use_baseline_compiler_;
  }

 private:
  // We add the sample in batches of size kJitSamplesBatchSize.
  // This method rounds the threshold so that it is multiple of the batch size.
  static uint32_t RoundUpThreshold(uint32_t threshold);

  bool use_jit_compilation_;
  bool use_tiered_jit_compilation_;
  bool use_baseline_compiler_;
  size_t code_cache_initial_capacity_;
  size_t code_cache_max_capacity_;
  uint32_t compile_threshold_;
  uint32_t warmup_threshold_;
  uint32_t osr_threshold_;
  uint16_t priority_thread_weight_;
  uint16_t invoke_transition_weight_;
  bool dump_info_on_shutdown_;
  int thread_pool_pthread_priority_;
  ProfileSaverOptions profile_saver_options_;

  JitOptions()
      : use_jit_compilation_(false),
        use_tiered_jit_compilation_(false),
        use_baseline_compiler_(false),
        code_cache_initial_capacity_(0),
        code_cache_max_capacity_(0),
        compile_threshold_(0),
        warmup_threshold_(0),
        osr_threshold_(0),
        priority_thread_weight_(0),
        invoke_transition_weight_(0),
        dump_info_on_shutdown_(false),
        thread_pool_pthread_priority_(kJitPoolThreadPthreadDefaultPriority) {}

  DISALLOW_COPY_AND_ASSIGN(JitOptions);
};

// Implemented and provided by the compiler library.
class JitCompilerInterface {
 public:
  virtual ~JitCompilerInterface() {}
  virtual bool CompileMethod(
      Thread* self, JitMemoryRegion* region, ArtMethod* method, bool baseline, bool osr)
      REQUIRES_SHARED(Locks::mutator_lock_) = 0;
  virtual void TypesLoaded(mirror::Class**, size_t count)
      REQUIRES_SHARED(Locks::mutator_lock_) = 0;
  virtual bool GenerateDebugInfo() = 0;
  virtual void ParseCompilerOptions() = 0;

  virtual std::vector<uint8_t> PackElfFileForJIT(ArrayRef<const JITCodeEntry*> elf_files,
                                                 ArrayRef<const void*> removed_symbols,
                                                 bool compress,
                                                 /*out*/ size_t* num_symbols) = 0;
};

// Data structure holding information to perform an OSR.
struct OsrData {
  // The native PC to jump to.
  const uint8_t* native_pc;

  // The frame size of the compiled code to jump to.
  size_t frame_size;

  // The dynamically allocated memory of size `frame_size` to copy to stack.
  void* memory[0];

  static constexpr MemberOffset NativePcOffset() {
    return MemberOffset(OFFSETOF_MEMBER(OsrData, native_pc));
  }

  static constexpr MemberOffset FrameSizeOffset() {
    return MemberOffset(OFFSETOF_MEMBER(OsrData, frame_size));
  }

  static constexpr MemberOffset MemoryOffset() {
    return MemberOffset(OFFSETOF_MEMBER(OsrData, memory));
  }
};

class Jit {
 public:
  static constexpr size_t kDefaultPriorityThreadWeightRatio = 1000;
  static constexpr size_t kDefaultInvokeTransitionWeightRatio = 500;
  // How frequently should the interpreter check to see if OSR compilation is ready.
  static constexpr int16_t kJitRecheckOSRThreshold = 101;  // Prime number to avoid patterns.

  DECLARE_RUNTIME_DEBUG_FLAG(kSlowMode);

  virtual ~Jit();

  // Create JIT itself.
  static Jit* Create(JitCodeCache* code_cache, JitOptions* options);

  bool CompileMethod(ArtMethod* method, Thread* self, bool baseline, bool osr, bool prejit)
      REQUIRES_SHARED(Locks::mutator_lock_);

  const JitCodeCache* GetCodeCache() const {
    return code_cache_;
  }

  JitCodeCache* GetCodeCache() {
    return code_cache_;
  }

  JitCompilerInterface* GetJitCompiler() const {
    return jit_compiler_;
  }

  void CreateThreadPool();
  void DeleteThreadPool();
  void WaitForWorkersToBeCreated();

  // Dump interesting info: #methods compiled, code vs data size, compile / verify cumulative
  // loggers.
  void DumpInfo(std::ostream& os) REQUIRES(!lock_);
  // Add a timing logger to cumulative_timings_.
  void AddTimingLogger(const TimingLogger& logger);

  void AddMemoryUsage(ArtMethod* method, size_t bytes)
      REQUIRES(!lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  uint16_t OSRMethodThreshold() const {
    return options_->GetOsrThreshold();
  }

  uint16_t HotMethodThreshold() const {
    return options_->GetCompileThreshold();
  }

  uint16_t WarmMethodThreshold() const {
    return options_->GetWarmupThreshold();
  }

  uint16_t PriorityThreadWeight() const {
    return options_->GetPriorityThreadWeight();
  }

  // Return whether we should do JIT compilation. Note this will returns false
  // if we only need to save profile information and not compile methods.
  bool UseJitCompilation() const {
    return options_->UseJitCompilation();
  }

  bool GetSaveProfilingInfo() const {
    return options_->GetSaveProfilingInfo();
  }

  // Wait until there is no more pending compilation tasks.
  void WaitForCompilationToFinish(Thread* self);

  // Profiling methods.
  void MethodEntered(Thread* thread, ArtMethod* method)
      REQUIRES_SHARED(Locks::mutator_lock_);

  ALWAYS_INLINE void AddSamples(Thread* self,
                                ArtMethod* method,
                                uint16_t samples,
                                bool with_backedges)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void InvokeVirtualOrInterface(ObjPtr<mirror::Object> this_object,
                                ArtMethod* caller,
                                uint32_t dex_pc,
                                ArtMethod* callee)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void NotifyInterpreterToCompiledCodeTransition(Thread* self, ArtMethod* caller)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    AddSamples(self, caller, options_->GetInvokeTransitionWeight(), false);
  }

  void NotifyCompiledCodeToInterpreterTransition(Thread* self, ArtMethod* callee)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    AddSamples(self, callee, options_->GetInvokeTransitionWeight(), false);
  }

  // Starts the profile saver if the config options allow profile recording.
  // The profile will be stored in the specified `filename` and will contain
  // information collected from the given `code_paths` (a set of dex locations).
  void StartProfileSaver(const std::string& filename,
                         const std::vector<std::string>& code_paths);
  void StopProfileSaver();

  void DumpForSigQuit(std::ostream& os) REQUIRES(!lock_);

  static void NewTypeLoadedIfUsingJit(mirror::Class* type)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // If debug info generation is turned on then write the type information for types already loaded
  // into the specified class linker to the jit debug interface,
  void DumpTypeInfoForLoadedTypes(ClassLinker* linker);

  // Return whether we should try to JIT compiled code as soon as an ArtMethod is invoked.
  bool JitAtFirstUse();

  // Return whether we can invoke JIT code for `method`.
  bool CanInvokeCompiledCode(ArtMethod* method);

  // Return whether the runtime should use a priority thread weight when sampling.
  static bool ShouldUsePriorityThreadWeight(Thread* self);

  // Return the information required to do an OSR jump. Return null if the OSR
  // cannot be done.
  OsrData* PrepareForOsr(ArtMethod* method, uint32_t dex_pc, uint32_t* vregs)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // If an OSR compiled version is available for `method`,
  // and `dex_pc + dex_pc_offset` is an entry point of that compiled
  // version, this method will jump to the compiled code, let it run,
  // and return true afterwards. Return false otherwise.
  static bool MaybeDoOnStackReplacement(Thread* thread,
                                        ArtMethod* method,
                                        uint32_t dex_pc,
                                        int32_t dex_pc_offset,
                                        JValue* result)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Load the compiler library.
  static bool LoadCompilerLibrary(std::string* error_msg);

  ThreadPool* GetThreadPool() const {
    return thread_pool_.get();
  }

  // Stop the JIT by waiting for all current compilations and enqueued compilations to finish.
  void Stop();

  // Start JIT threads.
  void Start();

  // Transition to a child state.
  void PostForkChildAction(bool is_system_server, bool is_zygote);

  // Prepare for forking.
  void PreZygoteFork();

  // Adjust state after forking.
  void PostZygoteFork();

  // Called when system finishes booting.
  void BootCompleted();

  // Compile methods from the given profile (.prof extension). If `add_to_queue`
  // is true, methods in the profile are added to the JIT queue. Otherwise they are compiled
  // directly.
  // Return the number of methods added to the queue.
  uint32_t CompileMethodsFromProfile(Thread* self,
                                     const std::vector<const DexFile*>& dex_files,
                                     const std::string& profile_path,
                                     Handle<mirror::ClassLoader> class_loader,
                                     bool add_to_queue);

  // Compile methods from the given boot profile (.bprof extension). If `add_to_queue`
  // is true, methods in the profile are added to the JIT queue. Otherwise they are compiled
  // directly.
  // Return the number of methods added to the queue.
  uint32_t CompileMethodsFromBootProfile(Thread* self,
                                         const std::vector<const DexFile*>& dex_files,
                                         const std::string& profile_path,
                                         Handle<mirror::ClassLoader> class_loader,
                                         bool add_to_queue);

  // Register the dex files to the JIT. This is to perform any compilation/optimization
  // at the point of loading the dex files.
  void RegisterDexFiles(const std::vector<std::unique_ptr<const DexFile>>& dex_files,
                        jobject class_loader);

  // Called by the compiler to know whether it can directly encode the
  // method/class/string.
  bool CanEncodeMethod(ArtMethod* method, bool is_for_shared_region) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool CanEncodeClass(ObjPtr<mirror::Class> cls, bool is_for_shared_region) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool CanEncodeString(ObjPtr<mirror::String> string, bool is_for_shared_region) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool CanAssumeInitialized(ObjPtr<mirror::Class> cls, bool is_for_shared_region) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Map boot image methods after all compilation in zygote has been done.
  void MapBootImageMethods() REQUIRES(Locks::mutator_lock_);

  // Notify to other processes that the zygote is done profile compiling boot
  // class path methods.
  void NotifyZygoteCompilationDone();

  void EnqueueOptimizedCompilation(ArtMethod* method, Thread* self);

  void EnqueueCompilationFromNterp(ArtMethod* method, Thread* self)
      REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  Jit(JitCodeCache* code_cache, JitOptions* options);

  // Compile an individual method listed in a profile. If `add_to_queue` is
  // true and the method was resolved, return true. Otherwise return false.
  bool CompileMethodFromProfile(Thread* self,
                                ClassLinker* linker,
                                uint32_t method_idx,
                                Handle<mirror::DexCache> dex_cache,
                                Handle<mirror::ClassLoader> class_loader,
                                bool add_to_queue,
                                bool compile_after_boot)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Compile the method if the number of samples passes a threshold.
  // Returns false if we can not compile now - don't increment the counter and retry later.
  bool MaybeCompileMethod(Thread* self,
                          ArtMethod* method,
                          uint32_t old_count,
                          uint32_t new_count,
                          bool with_backedges)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static bool BindCompilerMethods(std::string* error_msg);

  // JIT compiler
  static void* jit_library_handle_;
  static JitCompilerInterface* jit_compiler_;
  static JitCompilerInterface* (*jit_load_)(void);
  template <typename T> static bool LoadSymbol(T*, const char* symbol, std::string* error_msg);

  // JIT resources owned by runtime.
  jit::JitCodeCache* const code_cache_;
  const JitOptions* const options_;

  std::unique_ptr<ThreadPool> thread_pool_;
  std::vector<std::unique_ptr<OatDexFile>> type_lookup_tables_;

  Mutex boot_completed_lock_;
  bool boot_completed_ GUARDED_BY(boot_completed_lock_) = false;
  std::deque<Task*> tasks_after_boot_ GUARDED_BY(boot_completed_lock_);

  // Performance monitoring.
  CumulativeLogger cumulative_timings_;
  Histogram<uint64_t> memory_use_ GUARDED_BY(lock_);
  Mutex lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;

  // In the JIT zygote configuration, after all compilation is done, the zygote
  // will copy its contents of the boot image to the zygote_mapping_methods_,
  // which will be picked up by processes that will map the memory
  // in-place within the boot image mapping.
  //
  // zygote_mapping_methods_ is shared memory only usable by the zygote and not
  // inherited by child processes. We create it eagerly to ensure other
  // processes cannot seal writable the file.
  MemMap zygote_mapping_methods_;

  // The file descriptor created through memfd_create pointing to memory holding
  // boot image methods. Created by the zygote, and inherited by child
  // processes. The descriptor will be closed in each process (including the
  // zygote) once they don't need it.
  android::base::unique_fd fd_methods_;

  // The size of the memory pointed by `fd_methods_`. Cached here to avoid
  // recomputing it.
  size_t fd_methods_size_;

  DISALLOW_COPY_AND_ASSIGN(Jit);
};

// Helper class to stop the JIT for a given scope. This will wait for the JIT to quiesce.
class ScopedJitSuspend {
 public:
  ScopedJitSuspend();
  ~ScopedJitSuspend();

 private:
  bool was_on_;
};

}  // namespace jit
}  // namespace art

#endif  // ART_RUNTIME_JIT_JIT_H_
