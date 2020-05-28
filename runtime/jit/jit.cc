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

#include "jit.h"

#include <dlfcn.h>

#include "art_method-inl.h"
#include "base/enums.h"
#include "base/file_utils.h"
#include "base/logging.h"  // For VLOG.
#include "base/memfd.h"
#include "base/memory_tool.h"
#include "base/runtime_debug.h"
#include "base/scoped_flock.h"
#include "base/utils.h"
#include "class_root-inl.h"
#include "debugger.h"
#include "dex/type_lookup_table.h"
#include "gc/space/image_space.h"
#include "entrypoints/entrypoint_utils-inl.h"
#include "entrypoints/runtime_asm_entrypoints.h"
#include "image-inl.h"
#include "interpreter/interpreter.h"
#include "jit-inl.h"
#include "jit_code_cache.h"
#include "jni/java_vm_ext.h"
#include "mirror/method_handle_impl.h"
#include "mirror/var_handle.h"
#include "oat_file.h"
#include "oat_file_manager.h"
#include "oat_quick_method_header.h"
#include "profile/profile_boot_info.h"
#include "profile/profile_compilation_info.h"
#include "profile_saver.h"
#include "runtime.h"
#include "runtime_options.h"
#include "stack.h"
#include "stack_map.h"
#include "thread-inl.h"
#include "thread_list.h"

using android::base::unique_fd;

namespace art {
namespace jit {

static constexpr bool kEnableOnStackReplacement = true;

// Maximum permitted threshold value.
static constexpr uint32_t kJitMaxThreshold = std::numeric_limits<uint16_t>::max();

// Different compilation threshold constants. These can be overridden on the command line.

// Non-debug default
static constexpr uint32_t kJitDefaultCompileThreshold = 20 * kJitSamplesBatchSize;
// Fast-debug build.
static constexpr uint32_t kJitStressDefaultCompileThreshold = 2 * kJitSamplesBatchSize;
// Slow-debug build.
static constexpr uint32_t kJitSlowStressDefaultCompileThreshold = 2;

// Different warm-up threshold constants. These default to the equivalent compile thresholds divided
// by 2, but can be overridden at the command-line.
static constexpr uint32_t kJitDefaultWarmUpThreshold = kJitDefaultCompileThreshold / 2;
static constexpr uint32_t kJitStressDefaultWarmUpThreshold = kJitStressDefaultCompileThreshold / 2;
static constexpr uint32_t kJitSlowStressDefaultWarmUpThreshold =
    kJitSlowStressDefaultCompileThreshold / 2;

DEFINE_RUNTIME_DEBUG_FLAG(Jit, kSlowMode);

// JIT compiler
void* Jit::jit_library_handle_ = nullptr;
JitCompilerInterface* Jit::jit_compiler_ = nullptr;
JitCompilerInterface* (*Jit::jit_load_)(void) = nullptr;

JitOptions* JitOptions::CreateFromRuntimeArguments(const RuntimeArgumentMap& options) {
  auto* jit_options = new JitOptions;
  jit_options->use_jit_compilation_ = options.GetOrDefault(RuntimeArgumentMap::UseJitCompilation);
  jit_options->use_tiered_jit_compilation_ =
      options.GetOrDefault(RuntimeArgumentMap::UseTieredJitCompilation);

  jit_options->code_cache_initial_capacity_ =
      options.GetOrDefault(RuntimeArgumentMap::JITCodeCacheInitialCapacity);
  jit_options->code_cache_max_capacity_ =
      options.GetOrDefault(RuntimeArgumentMap::JITCodeCacheMaxCapacity);
  jit_options->dump_info_on_shutdown_ =
      options.Exists(RuntimeArgumentMap::DumpJITInfoOnShutdown);
  jit_options->profile_saver_options_ =
      options.GetOrDefault(RuntimeArgumentMap::ProfileSaverOpts);
  jit_options->thread_pool_pthread_priority_ =
      options.GetOrDefault(RuntimeArgumentMap::JITPoolThreadPthreadPriority);

  // Set default compile threshold to aide with sanity checking defaults.
  jit_options->compile_threshold_ =
      kIsDebugBuild
      ? (Jit::kSlowMode
         ? kJitSlowStressDefaultCompileThreshold
         : kJitStressDefaultCompileThreshold)
      : kJitDefaultCompileThreshold;

  // When not running in slow-mode, thresholds are quantized to kJitSamplesbatchsize.
  const uint32_t kJitThresholdStep = Jit::kSlowMode ? 1u : kJitSamplesBatchSize;

  // Set default warm-up threshold to aide with sanity checking defaults.
  jit_options->warmup_threshold_ =
      kIsDebugBuild ? (Jit::kSlowMode
                       ? kJitSlowStressDefaultWarmUpThreshold
                       : kJitStressDefaultWarmUpThreshold)
      : kJitDefaultWarmUpThreshold;

  // Warmup threshold should be less than compile threshold (so long as compile threshold is not
  // zero == JIT-on-first-use).
  DCHECK_LT(jit_options->warmup_threshold_, jit_options->compile_threshold_);
  DCHECK_EQ(RoundUp(jit_options->warmup_threshold_, kJitThresholdStep),
            jit_options->warmup_threshold_);

  if (options.Exists(RuntimeArgumentMap::JITCompileThreshold)) {
    jit_options->compile_threshold_ = *options.Get(RuntimeArgumentMap::JITCompileThreshold);
  }
  jit_options->compile_threshold_ = RoundUp(jit_options->compile_threshold_, kJitThresholdStep);

  if (options.Exists(RuntimeArgumentMap::JITWarmupThreshold)) {
    jit_options->warmup_threshold_ = *options.Get(RuntimeArgumentMap::JITWarmupThreshold);
  }
  jit_options->warmup_threshold_ = RoundUp(jit_options->warmup_threshold_, kJitThresholdStep);

  if (options.Exists(RuntimeArgumentMap::JITOsrThreshold)) {
    jit_options->osr_threshold_ = *options.Get(RuntimeArgumentMap::JITOsrThreshold);
  } else {
    jit_options->osr_threshold_ = jit_options->compile_threshold_ * 2;
    if (jit_options->osr_threshold_ > kJitMaxThreshold) {
      jit_options->osr_threshold_ =
          RoundDown(kJitMaxThreshold, kJitThresholdStep);
    }
  }
  jit_options->osr_threshold_ = RoundUp(jit_options->osr_threshold_, kJitThresholdStep);

  // Enforce ordering constraints between thresholds if not jit-on-first-use (when the compile
  // threshold is 0).
  if (jit_options->compile_threshold_ != 0) {
    // Clamp thresholds such that OSR > compile > warm-up (see Jit::MaybeCompileMethod).
    jit_options->osr_threshold_ = std::clamp(jit_options->osr_threshold_,
                                             2u * kJitThresholdStep,
                                             RoundDown(kJitMaxThreshold, kJitThresholdStep));
    jit_options->compile_threshold_ = std::clamp(jit_options->compile_threshold_,
                                                 kJitThresholdStep,
                                                 jit_options->osr_threshold_ - kJitThresholdStep);
    jit_options->warmup_threshold_ =
        std::clamp(jit_options->warmup_threshold_,
                   0u,
                   jit_options->compile_threshold_ - kJitThresholdStep);
  }

  if (options.Exists(RuntimeArgumentMap::JITPriorityThreadWeight)) {
    jit_options->priority_thread_weight_ =
        *options.Get(RuntimeArgumentMap::JITPriorityThreadWeight);
    if (jit_options->priority_thread_weight_ > jit_options->warmup_threshold_) {
      LOG(FATAL) << "Priority thread weight is above the warmup threshold.";
    } else if (jit_options->priority_thread_weight_ == 0) {
      LOG(FATAL) << "Priority thread weight cannot be 0.";
    }
  } else {
    jit_options->priority_thread_weight_ = std::max(
        jit_options->warmup_threshold_ / Jit::kDefaultPriorityThreadWeightRatio,
        static_cast<size_t>(1));
  }

  if (options.Exists(RuntimeArgumentMap::JITInvokeTransitionWeight)) {
    jit_options->invoke_transition_weight_ =
        *options.Get(RuntimeArgumentMap::JITInvokeTransitionWeight);
    if (jit_options->invoke_transition_weight_ > jit_options->warmup_threshold_) {
      LOG(FATAL) << "Invoke transition weight is above the warmup threshold.";
    } else if (jit_options->invoke_transition_weight_  == 0) {
      LOG(FATAL) << "Invoke transition weight cannot be 0.";
    }
  } else {
    jit_options->invoke_transition_weight_ = std::max(
        jit_options->warmup_threshold_ / Jit::kDefaultInvokeTransitionWeightRatio,
        static_cast<size_t>(1));
  }

  return jit_options;
}

void Jit::DumpInfo(std::ostream& os) {
  code_cache_->Dump(os);
  cumulative_timings_.Dump(os);
  MutexLock mu(Thread::Current(), lock_);
  memory_use_.PrintMemoryUse(os);
}

void Jit::DumpForSigQuit(std::ostream& os) {
  DumpInfo(os);
  ProfileSaver::DumpInstanceInfo(os);
}

void Jit::AddTimingLogger(const TimingLogger& logger) {
  cumulative_timings_.AddLogger(logger);
}

Jit::Jit(JitCodeCache* code_cache, JitOptions* options)
    : code_cache_(code_cache),
      options_(options),
      boot_completed_lock_("Jit::boot_completed_lock_"),
      cumulative_timings_("JIT timings"),
      memory_use_("Memory used for compilation", 16),
      lock_("JIT memory use lock"),
      zygote_mapping_methods_(),
      fd_methods_(-1),
      fd_methods_size_(0) {}

Jit* Jit::Create(JitCodeCache* code_cache, JitOptions* options) {
  if (jit_load_ == nullptr) {
    LOG(WARNING) << "Not creating JIT: library not loaded";
    return nullptr;
  }
  jit_compiler_ = (jit_load_)();
  if (jit_compiler_ == nullptr) {
    LOG(WARNING) << "Not creating JIT: failed to allocate a compiler";
    return nullptr;
  }
  std::unique_ptr<Jit> jit(new Jit(code_cache, options));

  // If the code collector is enabled, check if that still holds:
  // With 'perf', we want a 1-1 mapping between an address and a method.
  // We aren't able to keep method pointers live during the instrumentation method entry trampoline
  // so we will just disable jit-gc if we are doing that.
  if (code_cache->GetGarbageCollectCode()) {
    code_cache->SetGarbageCollectCode(!jit_compiler_->GenerateDebugInfo() &&
        !Runtime::Current()->GetInstrumentation()->AreExitStubsInstalled());
  }

  VLOG(jit) << "JIT created with initial_capacity="
      << PrettySize(options->GetCodeCacheInitialCapacity())
      << ", max_capacity=" << PrettySize(options->GetCodeCacheMaxCapacity())
      << ", compile_threshold=" << options->GetCompileThreshold()
      << ", profile_saver_options=" << options->GetProfileSaverOptions();

  // We want to know whether the compiler is compiling baseline, as this
  // affects how we GC ProfilingInfos.
  for (const std::string& option : Runtime::Current()->GetCompilerOptions()) {
    if (option == "--baseline") {
      options->SetUseBaselineCompiler();
      break;
    }
  }

  // Notify native debugger about the classes already loaded before the creation of the jit.
  jit->DumpTypeInfoForLoadedTypes(Runtime::Current()->GetClassLinker());
  return jit.release();
}

template <typename T>
bool Jit::LoadSymbol(T* address, const char* name, std::string* error_msg) {
  *address = reinterpret_cast<T>(dlsym(jit_library_handle_, name));
  if (*address == nullptr) {
    *error_msg = std::string("JIT couldn't find ") + name + std::string(" entry point");
    return false;
  }
  return true;
}

bool Jit::LoadCompilerLibrary(std::string* error_msg) {
  jit_library_handle_ = dlopen(
      kIsDebugBuild ? "libartd-compiler.so" : "libart-compiler.so", RTLD_NOW);
  if (jit_library_handle_ == nullptr) {
    std::ostringstream oss;
    oss << "JIT could not load libart-compiler.so: " << dlerror();
    *error_msg = oss.str();
    return false;
  }
  if (!LoadSymbol(&jit_load_, "jit_load", error_msg)) {
    dlclose(jit_library_handle_);
    return false;
  }
  return true;
}

bool Jit::CompileMethod(ArtMethod* method, Thread* self, bool baseline, bool osr, bool prejit) {
  DCHECK(Runtime::Current()->UseJitCompilation());
  DCHECK(!method->IsRuntimeMethod());

  RuntimeCallbacks* cb = Runtime::Current()->GetRuntimeCallbacks();
  // Don't compile the method if it has breakpoints.
  if (cb->IsMethodBeingInspected(method) && !cb->IsMethodSafeToJit(method)) {
    VLOG(jit) << "JIT not compiling " << method->PrettyMethod()
              << " due to not being safe to jit according to runtime-callbacks. For example, there"
              << " could be breakpoints in this method.";
    return false;
  }

  if (!method->IsCompilable()) {
    DCHECK(method->GetDeclaringClass()->IsObsoleteObject() ||
           method->IsProxyMethod()) << method->PrettyMethod();
    VLOG(jit) << "JIT not compiling " << method->PrettyMethod() << " due to method being made "
              << "obsolete while waiting for JIT task to run. This probably happened due to "
              << "concurrent structural class redefinition.";
    return false;
  }

  // Don't compile the method if we are supposed to be deoptimized.
  instrumentation::Instrumentation* instrumentation = Runtime::Current()->GetInstrumentation();
  if (instrumentation->AreAllMethodsDeoptimized() || instrumentation->IsDeoptimized(method)) {
    VLOG(jit) << "JIT not compiling " << method->PrettyMethod() << " due to deoptimization";
    return false;
  }

  JitMemoryRegion* region = GetCodeCache()->GetCurrentRegion();
  if (osr && GetCodeCache()->IsSharedRegion(*region)) {
    VLOG(jit) << "JIT not osr compiling "
              << method->PrettyMethod()
              << " due to using shared region";
    return false;
  }

  // If we get a request to compile a proxy method, we pass the actual Java method
  // of that proxy method, as the compiler does not expect a proxy method.
  ArtMethod* method_to_compile = method->GetInterfaceMethodIfProxy(kRuntimePointerSize);
  if (!code_cache_->NotifyCompilationOf(method_to_compile, self, osr, prejit, baseline, region)) {
    return false;
  }

  VLOG(jit) << "Compiling method "
            << ArtMethod::PrettyMethod(method_to_compile)
            << " osr=" << std::boolalpha << osr
            << " baseline=" << std::boolalpha << baseline;
  bool success = jit_compiler_->CompileMethod(self, region, method_to_compile, baseline, osr);
  code_cache_->DoneCompiling(method_to_compile, self, osr);
  if (!success) {
    VLOG(jit) << "Failed to compile method "
              << ArtMethod::PrettyMethod(method_to_compile)
              << " osr=" << std::boolalpha << osr;
  }
  if (kIsDebugBuild) {
    if (self->IsExceptionPending()) {
      mirror::Throwable* exception = self->GetException();
      LOG(FATAL) << "No pending exception expected after compiling "
                 << ArtMethod::PrettyMethod(method)
                 << ": "
                 << exception->Dump();
    }
  }
  return success;
}

void Jit::WaitForWorkersToBeCreated() {
  if (thread_pool_ != nullptr) {
    thread_pool_->WaitForWorkersToBeCreated();
  }
}

void Jit::DeleteThreadPool() {
  Thread* self = Thread::Current();
  if (thread_pool_ != nullptr) {
    std::unique_ptr<ThreadPool> pool;
    {
      ScopedSuspendAll ssa(__FUNCTION__);
      // Clear thread_pool_ field while the threads are suspended.
      // A mutator in the 'AddSamples' method will check against it.
      pool = std::move(thread_pool_);
    }

    // When running sanitized, let all tasks finish to not leak. Otherwise just clear the queue.
    if (!kRunningOnMemoryTool) {
      pool->StopWorkers(self);
      pool->RemoveAllTasks(self);
    }
    // We could just suspend all threads, but we know those threads
    // will finish in a short period, so it's not worth adding a suspend logic
    // here. Besides, this is only done for shutdown.
    pool->Wait(self, false, false);
  }
}

void Jit::StartProfileSaver(const std::string& filename,
                            const std::vector<std::string>& code_paths) {
  if (options_->GetSaveProfilingInfo()) {
    ProfileSaver::Start(options_->GetProfileSaverOptions(), filename, code_cache_, code_paths);
  }
}

void Jit::StopProfileSaver() {
  if (options_->GetSaveProfilingInfo() && ProfileSaver::IsStarted()) {
    ProfileSaver::Stop(options_->DumpJitInfoOnShutdown());
  }
}

bool Jit::JitAtFirstUse() {
  return HotMethodThreshold() == 0;
}

bool Jit::CanInvokeCompiledCode(ArtMethod* method) {
  return code_cache_->ContainsPc(method->GetEntryPointFromQuickCompiledCode());
}

Jit::~Jit() {
  DCHECK(!options_->GetSaveProfilingInfo() || !ProfileSaver::IsStarted());
  if (options_->DumpJitInfoOnShutdown()) {
    DumpInfo(LOG_STREAM(INFO));
    Runtime::Current()->DumpDeoptimizations(LOG_STREAM(INFO));
  }
  DeleteThreadPool();
  if (jit_compiler_ != nullptr) {
    delete jit_compiler_;
    jit_compiler_ = nullptr;
  }
  if (jit_library_handle_ != nullptr) {
    dlclose(jit_library_handle_);
    jit_library_handle_ = nullptr;
  }
}

void Jit::NewTypeLoadedIfUsingJit(mirror::Class* type) {
  if (!Runtime::Current()->UseJitCompilation()) {
    // No need to notify if we only use the JIT to save profiles.
    return;
  }
  jit::Jit* jit = Runtime::Current()->GetJit();
  if (jit->jit_compiler_->GenerateDebugInfo()) {
    jit_compiler_->TypesLoaded(&type, 1);
  }
}

void Jit::DumpTypeInfoForLoadedTypes(ClassLinker* linker) {
  struct CollectClasses : public ClassVisitor {
    bool operator()(ObjPtr<mirror::Class> klass) override REQUIRES_SHARED(Locks::mutator_lock_) {
      classes_.push_back(klass.Ptr());
      return true;
    }
    std::vector<mirror::Class*> classes_;
  };

  if (jit_compiler_->GenerateDebugInfo()) {
    ScopedObjectAccess so(Thread::Current());

    CollectClasses visitor;
    linker->VisitClasses(&visitor);
    jit_compiler_->TypesLoaded(visitor.classes_.data(), visitor.classes_.size());
  }
}

extern "C" void art_quick_osr_stub(void** stack,
                                   size_t stack_size_in_bytes,
                                   const uint8_t* native_pc,
                                   JValue* result,
                                   const char* shorty,
                                   Thread* self);

OsrData* Jit::PrepareForOsr(ArtMethod* method, uint32_t dex_pc, uint32_t* vregs) {
  if (!kEnableOnStackReplacement) {
    return nullptr;
  }

  // Cheap check if the method has been compiled already. That's an indicator that we should
  // osr into it.
  if (!GetCodeCache()->ContainsPc(method->GetEntryPointFromQuickCompiledCode())) {
    return nullptr;
  }

  // Fetch some data before looking up for an OSR method. We don't want thread
  // suspension once we hold an OSR method, as the JIT code cache could delete the OSR
  // method while we are being suspended.
  CodeItemDataAccessor accessor(method->DexInstructionData());
  const size_t number_of_vregs = accessor.RegistersSize();
  std::string method_name(VLOG_IS_ON(jit) ? method->PrettyMethod() : "");
  OsrData* osr_data = nullptr;

  {
    ScopedAssertNoThreadSuspension sts("Holding OSR method");
    const OatQuickMethodHeader* osr_method = GetCodeCache()->LookupOsrMethodHeader(method);
    if (osr_method == nullptr) {
      // No osr method yet, just return to the interpreter.
      return nullptr;
    }

    CodeInfo code_info(osr_method);

    // Find stack map starting at the target dex_pc.
    StackMap stack_map = code_info.GetOsrStackMapForDexPc(dex_pc);
    if (!stack_map.IsValid()) {
      // There is no OSR stack map for this dex pc offset. Just return to the interpreter in the
      // hope that the next branch has one.
      return nullptr;
    }

    // We found a stack map, now fill the frame with dex register values from the interpreter's
    // shadow frame.
    DexRegisterMap vreg_map = code_info.GetDexRegisterMapOf(stack_map);
    DCHECK_EQ(vreg_map.size(), number_of_vregs);

    size_t frame_size = osr_method->GetFrameSizeInBytes();

    // Allocate memory to put shadow frame values. The osr stub will copy that memory to
    // stack.
    // Note that we could pass the shadow frame to the stub, and let it copy the values there,
    // but that is engineering complexity not worth the effort for something like OSR.
    osr_data = reinterpret_cast<OsrData*>(malloc(sizeof(OsrData) + frame_size));
    if (osr_data == nullptr) {
      return nullptr;
    }
    memset(osr_data, 0, sizeof(OsrData) + frame_size);
    osr_data->frame_size = frame_size;

    // Art ABI: ArtMethod is at the bottom of the stack.
    osr_data->memory[0] = method;

    if (vreg_map.empty()) {
      // If we don't have a dex register map, then there are no live dex registers at
      // this dex pc.
    } else {
      for (uint16_t vreg = 0; vreg < number_of_vregs; ++vreg) {
        DexRegisterLocation::Kind location = vreg_map[vreg].GetKind();
        if (location == DexRegisterLocation::Kind::kNone) {
          // Dex register is dead or uninitialized.
          continue;
        }

        if (location == DexRegisterLocation::Kind::kConstant) {
          // We skip constants because the compiled code knows how to handle them.
          continue;
        }

        DCHECK_EQ(location, DexRegisterLocation::Kind::kInStack);

        int32_t vreg_value = vregs[vreg];
        int32_t slot_offset = vreg_map[vreg].GetStackOffsetInBytes();
        DCHECK_LT(slot_offset, static_cast<int32_t>(frame_size));
        DCHECK_GT(slot_offset, 0);
        (reinterpret_cast<int32_t*>(osr_data->memory))[slot_offset / sizeof(int32_t)] = vreg_value;
      }
    }

    osr_data->native_pc = stack_map.GetNativePcOffset(kRuntimeISA) +
        osr_method->GetEntryPoint();
    VLOG(jit) << "Jumping to "
              << method_name
              << "@"
              << std::hex << reinterpret_cast<uintptr_t>(osr_data->native_pc);
  }
  return osr_data;
}

bool Jit::MaybeDoOnStackReplacement(Thread* thread,
                                    ArtMethod* method,
                                    uint32_t dex_pc,
                                    int32_t dex_pc_offset,
                                    JValue* result) {
  Jit* jit = Runtime::Current()->GetJit();
  if (jit == nullptr) {
    return false;
  }

  if (UNLIKELY(__builtin_frame_address(0) < thread->GetStackEnd())) {
    // Don't attempt to do an OSR if we are close to the stack limit. Since
    // the interpreter frames are still on stack, OSR has the potential
    // to stack overflow even for a simple loop.
    // b/27094810.
    return false;
  }

  // Get the actual Java method if this method is from a proxy class. The compiler
  // and the JIT code cache do not expect methods from proxy classes.
  method = method->GetInterfaceMethodIfProxy(kRuntimePointerSize);

  // Before allowing the jump, make sure no code is actively inspecting the method to avoid
  // jumping from interpreter to OSR while e.g. single stepping. Note that we could selectively
  // disable OSR when single stepping, but that's currently hard to know at this point.
  if (Runtime::Current()->GetRuntimeCallbacks()->IsMethodBeingInspected(method)) {
    return false;
  }

  ShadowFrame* shadow_frame = thread->GetManagedStack()->GetTopShadowFrame();
  OsrData* osr_data = jit->PrepareForOsr(method,
                                         dex_pc + dex_pc_offset,
                                         shadow_frame->GetVRegArgs(0));

  if (osr_data == nullptr) {
    return false;
  }

  {
    thread->PopShadowFrame();
    ManagedStack fragment;
    thread->PushManagedStackFragment(&fragment);
    (*art_quick_osr_stub)(osr_data->memory,
                          osr_data->frame_size,
                          osr_data->native_pc,
                          result,
                          method->GetShorty(),
                          thread);

    if (UNLIKELY(thread->GetException() == Thread::GetDeoptimizationException())) {
      thread->DeoptimizeWithDeoptimizationException(result);
    }
    thread->PopManagedStackFragment(fragment);
  }
  free(osr_data);
  thread->PushShadowFrame(shadow_frame);
  VLOG(jit) << "Done running OSR code for " << method->PrettyMethod();
  return true;
}

void Jit::AddMemoryUsage(ArtMethod* method, size_t bytes) {
  if (bytes > 4 * MB) {
    LOG(INFO) << "Compiler allocated "
              << PrettySize(bytes)
              << " to compile "
              << ArtMethod::PrettyMethod(method);
  }
  MutexLock mu(Thread::Current(), lock_);
  memory_use_.AddValue(bytes);
}

void Jit::NotifyZygoteCompilationDone() {
  if (fd_methods_ == -1) {
    return;
  }

  size_t offset = 0;
  for (gc::space::ImageSpace* space : Runtime::Current()->GetHeap()->GetBootImageSpaces()) {
    const ImageHeader& header = space->GetImageHeader();
    const ImageSection& section = header.GetMethodsSection();
    // Because mremap works at page boundaries, we can only handle methods
    // within a page range. For methods that falls above or below the range,
    // the child processes will copy their contents to their private mapping
    // in `child_mapping_methods`. See `MapBootImageMethods`.
    uint8_t* page_start = AlignUp(header.GetImageBegin() + section.Offset(), kPageSize);
    uint8_t* page_end =
        AlignDown(header.GetImageBegin() + section.Offset() + section.Size(), kPageSize);
    if (page_end > page_start) {
      uint64_t capacity = page_end - page_start;
      memcpy(zygote_mapping_methods_.Begin() + offset, page_start, capacity);
      offset += capacity;
    }
  }

  // Do an msync to ensure we are not affected by writes still being in caches.
  if (msync(zygote_mapping_methods_.Begin(), fd_methods_size_, MS_SYNC) != 0) {
    PLOG(WARNING) << "Failed to sync boot image methods memory";
    code_cache_->GetZygoteMap()->SetCompilationState(ZygoteCompilationState::kNotifiedFailure);
    return;
  }

  // We don't need the shared mapping anymore, and we need to drop it in case
  // the file hasn't been sealed writable.
  zygote_mapping_methods_ = MemMap::Invalid();

  // Seal writes now. Zygote and children will map the memory private in order
  // to write to it.
  if (fcntl(fd_methods_, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_WRITE) == -1) {
    PLOG(WARNING) << "Failed to seal boot image methods file descriptor";
    code_cache_->GetZygoteMap()->SetCompilationState(ZygoteCompilationState::kNotifiedFailure);
    return;
  }

  std::string error_str;
  MemMap child_mapping_methods = MemMap::MapFile(
      fd_methods_size_,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE,
      fd_methods_,
      /* start= */ 0,
      /* low_4gb= */ false,
      "boot-image-methods",
      &error_str);

  if (!child_mapping_methods.IsValid()) {
    LOG(WARNING) << "Failed to create child mapping of boot image methods: " << error_str;
    code_cache_->GetZygoteMap()->SetCompilationState(ZygoteCompilationState::kNotifiedFailure);
    return;
  }

  // Ensure the contents are the same as before: there was a window between
  // the memcpy and the sealing where other processes could have changed the
  // contents.
  // Note this would not be needed if we could have used F_SEAL_FUTURE_WRITE,
  // see b/143833776.
  offset = 0;
  for (gc::space::ImageSpace* space : Runtime::Current()->GetHeap()->GetBootImageSpaces()) {
    const ImageHeader& header = space->GetImageHeader();
    const ImageSection& section = header.GetMethodsSection();
    // Because mremap works at page boundaries, we can only handle methods
    // within a page range. For methods that falls above or below the range,
    // the child processes will copy their contents to their private mapping
    // in `child_mapping_methods`. See `MapBootImageMethods`.
    uint8_t* page_start = AlignUp(header.GetImageBegin() + section.Offset(), kPageSize);
    uint8_t* page_end =
        AlignDown(header.GetImageBegin() + section.Offset() + section.Size(), kPageSize);
    if (page_end > page_start) {
      uint64_t capacity = page_end - page_start;
      if (memcmp(child_mapping_methods.Begin() + offset, page_start, capacity) != 0) {
        LOG(WARNING) << "Contents differ in boot image methods data";
        code_cache_->GetZygoteMap()->SetCompilationState(
            ZygoteCompilationState::kNotifiedFailure);
        return;
      }
      offset += capacity;
    }
  }

  // Future spawned processes don't need the fd anymore.
  fd_methods_.reset();

  // In order to have the zygote and children share the memory, we also remap
  // the memory into the zygote process.
  offset = 0;
  for (gc::space::ImageSpace* space : Runtime::Current()->GetHeap()->GetBootImageSpaces()) {
    const ImageHeader& header = space->GetImageHeader();
    const ImageSection& section = header.GetMethodsSection();
    // Because mremap works at page boundaries, we can only handle methods
    // within a page range. For methods that falls above or below the range,
    // the child processes will copy their contents to their private mapping
    // in `child_mapping_methods`. See `MapBootImageMethods`.
    uint8_t* page_start = AlignUp(header.GetImageBegin() + section.Offset(), kPageSize);
    uint8_t* page_end =
        AlignDown(header.GetImageBegin() + section.Offset() + section.Size(), kPageSize);
    if (page_end > page_start) {
      uint64_t capacity = page_end - page_start;
      if (mremap(child_mapping_methods.Begin() + offset,
                 capacity,
                 capacity,
                 MREMAP_FIXED | MREMAP_MAYMOVE,
                 page_start) == MAP_FAILED) {
        // Failing to remap is safe as the process will just use the old
        // contents.
        PLOG(WARNING) << "Failed mremap of boot image methods of " << space->GetImageFilename();
      }
      offset += capacity;
    }
  }

  LOG(INFO) << "Successfully notified child processes on sharing boot image methods";

  // Mark that compilation of boot classpath is done, and memory can now be
  // shared. Other processes will pick up this information.
  code_cache_->GetZygoteMap()->SetCompilationState(ZygoteCompilationState::kNotifiedOk);

  // The private mapping created for this process has been mremaped. We can
  // reset it.
  child_mapping_methods.Reset();
}

class JitCompileTask final : public Task {
 public:
  enum class TaskKind {
    kAllocateProfile,
    kCompile,
    kCompileBaseline,
    kCompileOsr,
    kPreCompile,
  };

  JitCompileTask(ArtMethod* method, TaskKind kind) : method_(method), kind_(kind), klass_(nullptr) {
    ScopedObjectAccess soa(Thread::Current());
    // For a non-bootclasspath class, add a global ref to the class to prevent class unloading
    // until compilation is done.
    // When we precompile, this is either with boot classpath methods, or main
    // class loader methods, so we don't need to keep a global reference.
    if (method->GetDeclaringClass()->GetClassLoader() != nullptr &&
        kind_ != TaskKind::kPreCompile) {
      klass_ = soa.Vm()->AddGlobalRef(soa.Self(), method_->GetDeclaringClass());
      CHECK(klass_ != nullptr);
    }
  }

  ~JitCompileTask() {
    if (klass_ != nullptr) {
      ScopedObjectAccess soa(Thread::Current());
      soa.Vm()->DeleteGlobalRef(soa.Self(), klass_);
    }
  }

  void Run(Thread* self) override {
    {
      ScopedObjectAccess soa(self);
      switch (kind_) {
        case TaskKind::kPreCompile:
        case TaskKind::kCompile:
        case TaskKind::kCompileBaseline:
        case TaskKind::kCompileOsr: {
          Runtime::Current()->GetJit()->CompileMethod(
              method_,
              self,
              /* baseline= */ (kind_ == TaskKind::kCompileBaseline),
              /* osr= */ (kind_ == TaskKind::kCompileOsr),
              /* prejit= */ (kind_ == TaskKind::kPreCompile));
          break;
        }
        case TaskKind::kAllocateProfile: {
          if (ProfilingInfo::Create(self, method_, /* retry_allocation= */ true)) {
            VLOG(jit) << "Start profiling " << ArtMethod::PrettyMethod(method_);
          }
          break;
        }
      }
    }
    ProfileSaver::NotifyJitActivity();
  }

  void Finalize() override {
    delete this;
  }

 private:
  ArtMethod* const method_;
  const TaskKind kind_;
  jobject klass_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(JitCompileTask);
};

static std::string GetProfileFile(const std::string& dex_location) {
  // Hardcoded assumption where the profile file is.
  // TODO(ngeoffray): this is brittle and we would need to change change if we
  // wanted to do more eager JITting of methods in a profile. This is
  // currently only for system server.
  return dex_location + ".prof";
}

static std::string GetBootProfileFile(const std::string& profile) {
  // The boot profile can be found next to the compilation profile, with a
  // different extension.
  return ReplaceFileExtension(profile, "bprof");
}

/**
 * A JIT task to run after all profile compilation is done.
 */
class JitDoneCompilingProfileTask final : public SelfDeletingTask {
 public:
  explicit JitDoneCompilingProfileTask(const std::vector<const DexFile*>& dex_files)
      : dex_files_(dex_files) {}

  void Run(Thread* self ATTRIBUTE_UNUSED) override {
    // Madvise DONTNEED dex files now that we're done compiling methods.
    for (const DexFile* dex_file : dex_files_) {
      if (IsAddressKnownBackedByFileOrShared(dex_file->Begin())) {
        int result = madvise(const_cast<uint8_t*>(AlignDown(dex_file->Begin(), kPageSize)),
                             RoundUp(dex_file->Size(), kPageSize),
                             MADV_DONTNEED);
        if (result == -1) {
          PLOG(WARNING) << "Madvise failed";
        }
      }
    }

    if (Runtime::Current()->IsZygote()) {
      // Record that we are done compiling the profile.
      Runtime::Current()->GetJit()->GetCodeCache()->GetZygoteMap()->SetCompilationState(
          ZygoteCompilationState::kDone);
    }
  }

 private:
  std::vector<const DexFile*> dex_files_;

  DISALLOW_COPY_AND_ASSIGN(JitDoneCompilingProfileTask);
};

/**
 * A JIT task to run Java verification of boot classpath classes that were not
 * verified at compile-time.
 */
class ZygoteVerificationTask final : public Task {
 public:
  ZygoteVerificationTask() {}

  void Run(Thread* self) override {
    // We are going to load class and run verification, which may also need to load
    // classes. If the thread cannot load classes (typically when the runtime is
    // debuggable), then just return.
    if (!self->CanLoadClasses()) {
      return;
    }
    Runtime* runtime = Runtime::Current();
    ClassLinker* linker = runtime->GetClassLinker();
    const std::vector<const DexFile*>& boot_class_path =
        runtime->GetClassLinker()->GetBootClassPath();
    ScopedObjectAccess soa(self);
    StackHandleScope<1> hs(self);
    MutableHandle<mirror::Class> klass = hs.NewHandle<mirror::Class>(nullptr);
    uint64_t start_ns = ThreadCpuNanoTime();
    uint64_t number_of_classes = 0;
    for (const DexFile* dex_file : boot_class_path) {
      if (dex_file->GetOatDexFile() != nullptr &&
          dex_file->GetOatDexFile()->GetOatFile() != nullptr) {
        // If backed by an .oat file, we have already run verification at
        // compile-time. Note that some classes may still have failed
        // verification there if they reference updatable mainline module
        // classes.
        continue;
      }
      for (uint32_t i = 0; i < dex_file->NumClassDefs(); ++i) {
        const dex::ClassDef& class_def = dex_file->GetClassDef(i);
        const char* descriptor = dex_file->GetClassDescriptor(class_def);
        ScopedNullHandle<mirror::ClassLoader> null_loader;
        klass.Assign(linker->FindClass(self, descriptor, null_loader));
        if (klass == nullptr) {
          self->ClearException();
          LOG(WARNING) << "Could not find " << descriptor;
          continue;
        }
        ++number_of_classes;
        if (linker->VerifyClass(self, klass) == verifier::FailureKind::kHardFailure) {
          DCHECK(self->IsExceptionPending());
          LOG(FATAL) << "Methods in the boot classpath failed to verify: "
                     << self->GetException()->Dump();
        }
        CHECK(!self->IsExceptionPending());
      }
    }
    LOG(INFO) << "Verified "
              << number_of_classes
              << " classes from mainline modules in "
              << PrettyDuration(ThreadCpuNanoTime() - start_ns);
  }
};

class ZygoteTask final : public Task {
 public:
  ZygoteTask() {}

  void Run(Thread* self) override {
    Runtime* runtime = Runtime::Current();
    uint32_t added_to_queue = 0;
    for (gc::space::ImageSpace* space : Runtime::Current()->GetHeap()->GetBootImageSpaces()) {
      const std::string& profile_file = space->GetProfileFile();
      if (profile_file.empty()) {
        continue;
      }
      LOG(INFO) << "JIT Zygote looking at profile " << profile_file;

      const std::vector<const DexFile*>& boot_class_path =
          runtime->GetClassLinker()->GetBootClassPath();
      ScopedNullHandle<mirror::ClassLoader> null_handle;
      // We add to the queue for zygote so that we can fork processes in-between
      // compilations.
      if (Runtime::Current()->IsPrimaryZygote()) {
        std::string boot_profile = GetBootProfileFile(profile_file);
        // We avoid doing compilation at boot for the secondary zygote, as apps
        // forked from it are not critical for boot.
        added_to_queue += runtime->GetJit()->CompileMethodsFromBootProfile(
            self, boot_class_path, boot_profile, null_handle, /* add_to_queue= */ true);
      }
      added_to_queue += runtime->GetJit()->CompileMethodsFromProfile(
          self, boot_class_path, profile_file, null_handle, /* add_to_queue= */ true);
    }

    JitCodeCache* code_cache = runtime->GetJit()->GetCodeCache();
    code_cache->GetZygoteMap()->Initialize(added_to_queue);
  }

  void Finalize() override {
    delete this;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ZygoteTask);
};

class JitProfileTask final : public Task {
 public:
  JitProfileTask(const std::vector<std::unique_ptr<const DexFile>>& dex_files,
                 jobject class_loader) {
    ScopedObjectAccess soa(Thread::Current());
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::ClassLoader> h_loader(hs.NewHandle(
        soa.Decode<mirror::ClassLoader>(class_loader)));
    ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
    for (const auto& dex_file : dex_files) {
      dex_files_.push_back(dex_file.get());
      // Register the dex file so that we can guarantee it doesn't get deleted
      // while reading it during the task.
      class_linker->RegisterDexFile(*dex_file.get(), h_loader.Get());
    }
    // We also create our own global ref to use this class loader later.
    class_loader_ = soa.Vm()->AddGlobalRef(soa.Self(), h_loader.Get());
  }

  void Run(Thread* self) override {
    ScopedObjectAccess soa(self);
    StackHandleScope<1> hs(self);
    Handle<mirror::ClassLoader> loader = hs.NewHandle<mirror::ClassLoader>(
        soa.Decode<mirror::ClassLoader>(class_loader_));

    std::string profile = GetProfileFile(dex_files_[0]->GetLocation());
    std::string boot_profile = GetBootProfileFile(profile);

    Jit* jit = Runtime::Current()->GetJit();

    jit->CompileMethodsFromBootProfile(
        self,
        dex_files_,
        boot_profile,
        loader,
        /* add_to_queue= */ false);

    jit->CompileMethodsFromProfile(
        self,
        dex_files_,
        profile,
        loader,
        /* add_to_queue= */ true);
  }

  void Finalize() override {
    delete this;
  }

  ~JitProfileTask() {
    ScopedObjectAccess soa(Thread::Current());
    soa.Vm()->DeleteGlobalRef(soa.Self(), class_loader_);
  }

 private:
  std::vector<const DexFile*> dex_files_;
  jobject class_loader_;

  DISALLOW_COPY_AND_ASSIGN(JitProfileTask);
};

static void CopyIfDifferent(void* s1, const void* s2, size_t n) {
  if (memcmp(s1, s2, n) != 0) {
    memcpy(s1, s2, n);
  }
}

void Jit::MapBootImageMethods() {
  if (Runtime::Current()->IsJavaDebuggable()) {
    LOG(INFO) << "Not mapping boot image methods due to process being debuggable";
    return;
  }
  CHECK_NE(fd_methods_.get(), -1);
  if (!code_cache_->GetZygoteMap()->CanMapBootImageMethods()) {
    LOG(WARNING) << "Not mapping boot image methods due to error from zygote";
    // We don't need the fd anymore.
    fd_methods_.reset();
    return;
  }

  std::string error_str;
  MemMap child_mapping_methods = MemMap::MapFile(
      fd_methods_size_,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE,
      fd_methods_,
      /* start= */ 0,
      /* low_4gb= */ false,
      "boot-image-methods",
      &error_str);

  // We don't need the fd anymore.
  fd_methods_.reset();

  if (!child_mapping_methods.IsValid()) {
    LOG(WARNING) << "Failed to create child mapping of boot image methods: " << error_str;
    return;
  }
  size_t offset = 0;
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  for (gc::space::ImageSpace* space : Runtime::Current()->GetHeap()->GetBootImageSpaces()) {
    const ImageHeader& header = space->GetImageHeader();
    const ImageSection& section = header.GetMethodsSection();
    uint8_t* page_start = AlignUp(header.GetImageBegin() + section.Offset(), kPageSize);
    uint8_t* page_end =
        AlignDown(header.GetImageBegin() + section.Offset() + section.Size(), kPageSize);
    if (page_end <= page_start) {
      // Section doesn't contain one aligned entire page.
      continue;
    }
    uint64_t capacity = page_end - page_start;
    // Walk over methods in the boot image, and check for ones whose class is
    // not initialized in the process, but are in the zygote process. For
    // such methods, we need their entrypoints to be stubs that do the
    // initialization check.
    header.VisitPackedArtMethods([&](ArtMethod& method) NO_THREAD_SAFETY_ANALYSIS {
      // Methods in the boot image should never have their single
      // implementation flag set (and therefore never have a `data_` pointing
      // to an ArtMethod for single implementation).
      CHECK(method.IsIntrinsic() || !method.HasSingleImplementationFlag());
      if (method.IsRuntimeMethod()) {
        return;
      }
      if (method.GetDeclaringClassUnchecked()->IsVisiblyInitialized() ||
          !method.IsStatic() ||
          method.IsConstructor()) {
        // Method does not need any stub.
        return;
      }

      //  We are going to mremap the child mapping into the image:
      //
      //                            ImageSection       ChildMappingMethods
      //
      //         section start -->  -----------
      //                            |         |
      //                            |         |
      //            page_start -->  |         |   <-----   -----------
      //                            |         |            |         |
      //                            |         |            |         |
      //                            |         |            |         |
      //                            |         |            |         |
      //                            |         |            |         |
      //                            |         |            |         |
      //                            |         |            |         |
      //             page_end  -->  |         |   <-----   -----------
      //                            |         |
      //         section end   -->  -----------


      uint8_t* pointer = reinterpret_cast<uint8_t*>(&method);
      // Note: We could refactor this to only check if the ArtMethod entrypoint is inside the
      // page region. This would remove the need for the edge case handling below.
      if (pointer >= page_start && pointer + sizeof(ArtMethod) < page_end) {
        // For all the methods in the mapping, put the entrypoint to the
        // resolution stub.
        ArtMethod* new_method = reinterpret_cast<ArtMethod*>(
            child_mapping_methods.Begin() + offset + (pointer - page_start));
        const void* code = new_method->GetEntryPointFromQuickCompiledCode();
        if (!class_linker->IsQuickGenericJniStub(code) &&
            !class_linker->IsQuickToInterpreterBridge(code) &&
            !class_linker->IsQuickResolutionStub(code)) {
          LOG(INFO) << "Putting back the resolution stub to an ArtMethod";
          new_method->SetEntryPointFromQuickCompiledCode(GetQuickResolutionStub());
        }
      } else if (pointer < page_start && (pointer + sizeof(ArtMethod)) > page_start) {
        LOG(INFO) << "Copying parts of the contents of an ArtMethod spanning page_start";
        // If the method spans `page_start`, copy the contents of the child
        // into the pages we are going to remap into the image.
        //
        //         section start -->  -----------
        //                            |         |
        //                            |         |
        //            page_start -->  |/////////|            -----------
        //                            |/////////| -> copy -> |/////////|
        //                            |         |            |         |
        //
        CopyIfDifferent(child_mapping_methods.Begin() + offset,
                        page_start,
                        pointer + sizeof(ArtMethod) - page_start);
      } else if (pointer < page_end && (pointer + sizeof(ArtMethod)) > page_end) {
        LOG(INFO) << "Copying parts of the contents of an ArtMethod spanning page_end";
        // If the method spans `page_end`, copy the contents of the child
        // into the pages we are going to remap into the image.
        //
        //                            |         |            |         |
        //                            |/////////| -> copy -> |/////////|
        //             page_end  -->  |/////////|            -----------
        //                            |         |
        //         section end   -->  -----------
        //
        size_t bytes_to_copy = (page_end - pointer);
        CopyIfDifferent(child_mapping_methods.Begin() + offset + capacity - bytes_to_copy,
                        page_end - bytes_to_copy,
                        bytes_to_copy);
      }
    }, space->Begin(), kRuntimePointerSize);

    // Map the memory in the boot image range.
    if (mremap(child_mapping_methods.Begin() + offset,
               capacity,
               capacity,
               MREMAP_FIXED | MREMAP_MAYMOVE,
               page_start) == MAP_FAILED) {
      PLOG(WARNING) << "Fail to mremap boot image methods for " << space->GetImageFilename();
    }
    offset += capacity;
  }

  // The private mapping created for this process has been mremaped. We can
  // reset it.
  child_mapping_methods.Reset();
  LOG(INFO) << "Successfully mapped boot image methods";
}

// Return whether a boot image has a profile. This means we'll need to pre-JIT
// methods in that profile for performance.
static bool HasImageWithProfile() {
  for (gc::space::ImageSpace* space : Runtime::Current()->GetHeap()->GetBootImageSpaces()) {
    if (!space->GetProfileFile().empty()) {
      return true;
    }
  }
  return false;
}

void Jit::CreateThreadPool() {
  // There is a DCHECK in the 'AddSamples' method to ensure the tread pool
  // is not null when we instrument.

  // We need peers as we may report the JIT thread, e.g., in the debugger.
  constexpr bool kJitPoolNeedsPeers = true;
  thread_pool_.reset(new ThreadPool("Jit thread pool", 1, kJitPoolNeedsPeers));

  thread_pool_->SetPthreadPriority(options_->GetThreadPoolPthreadPriority());
  Start();

  Runtime* runtime = Runtime::Current();
  if (runtime->IsZygote()) {
    // To speed up class lookups, generate a type lookup table for
    // dex files not backed by oat file.
    for (const DexFile* dex_file : runtime->GetClassLinker()->GetBootClassPath()) {
      if (dex_file->GetOatDexFile() == nullptr) {
        TypeLookupTable type_lookup_table = TypeLookupTable::Create(*dex_file);
        type_lookup_tables_.push_back(
            std::make_unique<art::OatDexFile>(std::move(type_lookup_table)));
        dex_file->SetOatDexFile(type_lookup_tables_.back().get());
      }
    }

    // Add a task that will verify boot classpath jars that were not
    // pre-compiled.
    thread_pool_->AddTask(Thread::Current(), new ZygoteVerificationTask());
  }

  if (runtime->IsZygote() && HasImageWithProfile() && UseJitCompilation()) {
    // If we have an image with a profile, request a JIT task to
    // compile all methods in that profile.
    thread_pool_->AddTask(Thread::Current(), new ZygoteTask());

    // And create mappings to share boot image methods memory from the zygote to
    // child processes.

    // Compute the total capacity required for the boot image methods.
    uint64_t total_capacity = 0;
    for (gc::space::ImageSpace* space : Runtime::Current()->GetHeap()->GetBootImageSpaces()) {
      const ImageHeader& header = space->GetImageHeader();
      const ImageSection& section = header.GetMethodsSection();
      // Mappings need to be at the page level.
      uint8_t* page_start = AlignUp(header.GetImageBegin() + section.Offset(), kPageSize);
      uint8_t* page_end =
          AlignDown(header.GetImageBegin() + section.Offset() + section.Size(), kPageSize);
      if (page_end > page_start) {
        total_capacity += (page_end - page_start);
      }
    }

    // Create the child and zygote mappings to the boot image methods.
    if (total_capacity > 0) {
      // Start with '/boot' and end with '.art' to match the pattern recognized
      // by android_os_Debug.cpp for boot images.
      const char* name = "/boot-image-methods.art";
      unique_fd mem_fd = unique_fd(art::memfd_create(name, /* flags= */ MFD_ALLOW_SEALING));
      if (mem_fd.get() == -1) {
        PLOG(WARNING) << "Could not create boot image methods file descriptor";
        return;
      }
      if (ftruncate(mem_fd.get(), total_capacity) != 0) {
        PLOG(WARNING) << "Failed to truncate boot image methods file to " << total_capacity;
        return;
      }
      std::string error_str;

      // Create the shared mapping eagerly, as this prevents other processes
      // from adding the writable seal.
      zygote_mapping_methods_ = MemMap::MapFile(
        total_capacity,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd,
        /* start= */ 0,
        /* low_4gb= */ false,
        "boot-image-methods",
        &error_str);

      if (!zygote_mapping_methods_.IsValid()) {
        LOG(WARNING) << "Failed to create zygote mapping of boot image methods:  " << error_str;
        return;
      }
      if (zygote_mapping_methods_.MadviseDontFork() != 0) {
        LOG(WARNING) << "Failed to madvise dont fork boot image methods";
        zygote_mapping_methods_ = MemMap();
        return;
      }

      // We should use the F_SEAL_FUTURE_WRITE flag, but this has unexpected
      // behavior on private mappings after fork (the mapping becomes shared between
      // parent and children), see b/143833776.
      // We will seal the write once we are done writing to the shared mapping.
      if (fcntl(mem_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == -1) {
        PLOG(WARNING) << "Failed to seal boot image methods file descriptor";
        zygote_mapping_methods_ = MemMap();
        return;
      }
      fd_methods_ = unique_fd(mem_fd.release());
      fd_methods_size_ = total_capacity;
    }
  }
}

void Jit::RegisterDexFiles(const std::vector<std::unique_ptr<const DexFile>>& dex_files,
                           jobject class_loader) {
  if (dex_files.empty()) {
    return;
  }
  Runtime* runtime = Runtime::Current();
  // If the runtime is debuggable, no need to precompile methods.
  if (runtime->IsSystemServer() &&
      UseJitCompilation() && HasImageWithProfile() &&
      !runtime->IsJavaDebuggable()) {
    thread_pool_->AddTask(Thread::Current(), new JitProfileTask(dex_files, class_loader));
  }
}

bool Jit::CompileMethodFromProfile(Thread* self,
                                   ClassLinker* class_linker,
                                   uint32_t method_idx,
                                   Handle<mirror::DexCache> dex_cache,
                                   Handle<mirror::ClassLoader> class_loader,
                                   bool add_to_queue,
                                   bool compile_after_boot) {
  ArtMethod* method = class_linker->ResolveMethodWithoutInvokeType(
      method_idx, dex_cache, class_loader);
  if (method == nullptr) {
    self->ClearException();
    return false;
  }
  if (!method->IsCompilable() || !method->IsInvokable()) {
    return false;
  }
  if (method->IsPreCompiled()) {
    // Already seen by another profile.
    return false;
  }
  const void* entry_point = method->GetEntryPointFromQuickCompiledCode();
  if (class_linker->IsQuickToInterpreterBridge(entry_point) ||
      class_linker->IsQuickGenericJniStub(entry_point) ||
      // We explicitly check for the stub. The trampoline is for methods backed by
      // a .oat file that has a compiled version of the method.
      (entry_point == GetQuickResolutionStub())) {
    method->SetPreCompiled();
    if (!add_to_queue) {
      CompileMethod(method, self, /* baseline= */ false, /* osr= */ false, /* prejit= */ true);
    } else {
      Task* task = new JitCompileTask(method, JitCompileTask::TaskKind::kPreCompile);
      if (compile_after_boot) {
        MutexLock mu(Thread::Current(), boot_completed_lock_);
        if (!boot_completed_) {
          tasks_after_boot_.push_back(task);
          return true;
        }
        DCHECK(tasks_after_boot_.empty());
      }
      thread_pool_->AddTask(self, task);
      return true;
    }
  }
  return false;
}

uint32_t Jit::CompileMethodsFromBootProfile(
    Thread* self,
    const std::vector<const DexFile*>& dex_files,
    const std::string& profile_file,
    Handle<mirror::ClassLoader> class_loader,
    bool add_to_queue) {
  unix_file::FdFile profile(profile_file.c_str(), O_RDONLY, true);

  if (profile.Fd() == -1) {
    PLOG(WARNING) << "No boot profile: " << profile_file;
    return 0u;
  }

  ProfileBootInfo profile_info;
  if (!profile_info.Load(profile.Fd(), dex_files)) {
    LOG(ERROR) << "Could not load profile file: " << profile_file;
    return 0u;
  }

  ScopedObjectAccess soa(self);
  VariableSizedHandleScope handles(self);
  std::vector<Handle<mirror::DexCache>> dex_caches;
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  for (const DexFile* dex_file : profile_info.GetDexFiles()) {
    dex_caches.push_back(handles.NewHandle(class_linker->FindDexCache(self, *dex_file)));
  }

  uint32_t added_to_queue = 0;
  for (const std::pair<uint32_t, uint32_t>& pair : profile_info.GetMethods()) {
    if (CompileMethodFromProfile(self,
                                 class_linker,
                                 pair.second,
                                 dex_caches[pair.first],
                                 class_loader,
                                 add_to_queue,
                                 /*compile_after_boot=*/false)) {
      ++added_to_queue;
    }
  }
  return added_to_queue;
}

uint32_t Jit::CompileMethodsFromProfile(
    Thread* self,
    const std::vector<const DexFile*>& dex_files,
    const std::string& profile_file,
    Handle<mirror::ClassLoader> class_loader,
    bool add_to_queue) {

  if (profile_file.empty()) {
    LOG(WARNING) << "Expected a profile file in JIT zygote mode";
    return 0u;
  }

  // We don't generate boot profiles on device, therefore we don't
  // need to lock the file.
  unix_file::FdFile profile(profile_file.c_str(), O_RDONLY, true);

  if (profile.Fd() == -1) {
    PLOG(WARNING) << "No profile: " << profile_file;
    return 0u;
  }

  ProfileCompilationInfo profile_info;
  if (!profile_info.Load(profile.Fd())) {
    LOG(ERROR) << "Could not load profile file";
    return 0u;
  }
  ScopedObjectAccess soa(self);
  StackHandleScope<1> hs(self);
  MutableHandle<mirror::DexCache> dex_cache = hs.NewHandle<mirror::DexCache>(nullptr);
  ClassLinker* class_linker = Runtime::Current()->GetClassLinker();
  uint32_t added_to_queue = 0u;
  for (const DexFile* dex_file : dex_files) {
    if (LocationIsOnArtModule(dex_file->GetLocation().c_str())) {
      // The ART module jars are already preopted.
      continue;
    }

    std::set<dex::TypeIndex> class_types;
    std::set<uint16_t> all_methods;
    if (!profile_info.GetClassesAndMethods(*dex_file,
                                           &class_types,
                                           &all_methods,
                                           &all_methods,
                                           &all_methods)) {
      // This means the profile file did not reference the dex file, which is the case
      // if there's no classes and methods of that dex file in the profile.
      continue;
    }
    dex_cache.Assign(class_linker->FindDexCache(self, *dex_file));
    CHECK(dex_cache != nullptr) << "Could not find dex cache for " << dex_file->GetLocation();

    for (uint16_t method_idx : all_methods) {
      if (CompileMethodFromProfile(self,
                                   class_linker,
                                   method_idx,
                                   dex_cache,
                                   class_loader,
                                   add_to_queue,
                                   /*compile_after_boot=*/true)) {
        ++added_to_queue;
      }
    }
  }

  // Add a task to run when all compilation is done.
  JitDoneCompilingProfileTask* task = new JitDoneCompilingProfileTask(dex_files);
  MutexLock mu(Thread::Current(), boot_completed_lock_);
  if (!boot_completed_) {
    tasks_after_boot_.push_back(task);
  } else {
    DCHECK(tasks_after_boot_.empty());
    thread_pool_->AddTask(self, task);
  }
  return added_to_queue;
}

bool Jit::IgnoreSamplesForMethod(ArtMethod* method) REQUIRES_SHARED(Locks::mutator_lock_) {
  if (method->IsClassInitializer() || !method->IsCompilable() || method->IsPreCompiled()) {
    // We do not want to compile such methods.
    return true;
  }
  if (method->IsNative()) {
    ObjPtr<mirror::Class> klass = method->GetDeclaringClass();
    if (klass == GetClassRoot<mirror::MethodHandle>() ||
        klass == GetClassRoot<mirror::VarHandle>()) {
      // MethodHandle and VarHandle invocation methods are required to throw an
      // UnsupportedOperationException if invoked reflectively. We achieve this by having native
      // implementations that raise the exception. We need to disable JIT compilation of these JNI
      // methods as it can lead to transitioning between JIT compiled JNI stubs and generic JNI
      // stubs. Since these stubs have different stack representations we can then crash in stack
      // walking (b/78151261).
      return true;
    }
  }
  return false;
}

bool Jit::MaybeCompileMethod(Thread* self,
                             ArtMethod* method,
                             uint32_t old_count,
                             uint32_t new_count,
                             bool with_backedges) {
  if (thread_pool_ == nullptr) {
    return false;
  }
  if (UNLIKELY(method->IsPreCompiled()) && !with_backedges /* don't check for OSR */) {
    if (!NeedsClinitCheckBeforeCall(method) ||
        method->GetDeclaringClass()->IsVisiblyInitialized()) {
      const void* entry_point = code_cache_->GetSavedEntryPointOfPreCompiledMethod(method);
      if (entry_point != nullptr) {
        Runtime::Current()->GetInstrumentation()->UpdateMethodsCode(method, entry_point);
        return true;
      }
    }
  }

  if (IgnoreSamplesForMethod(method)) {
    return false;
  }
  if (HotMethodThreshold() == 0) {
    // Tests might request JIT on first use (compiled synchronously in the interpreter).
    return false;
  }
  DCHECK_GT(WarmMethodThreshold(), 0);
  DCHECK_GT(HotMethodThreshold(), WarmMethodThreshold());
  DCHECK_GT(OSRMethodThreshold(), HotMethodThreshold());
  DCHECK_GE(PriorityThreadWeight(), 1);
  DCHECK_LE(PriorityThreadWeight(), HotMethodThreshold());

  if (old_count < WarmMethodThreshold() && new_count >= WarmMethodThreshold()) {
    // Note: Native method have no "warm" state or profiling info.
    if (!method->IsNative() &&
        (method->GetProfilingInfo(kRuntimePointerSize) == nullptr) &&
        code_cache_->CanAllocateProfilingInfo() &&
        !options_->UseTieredJitCompilation()) {
      bool success = ProfilingInfo::Create(self, method, /* retry_allocation= */ false);
      if (success) {
        VLOG(jit) << "Start profiling " << method->PrettyMethod();
      }

      if (thread_pool_ == nullptr) {
        // Calling ProfilingInfo::Create might put us in a suspended state, which could
        // lead to the thread pool being deleted when we are shutting down.
        return false;
      }

      if (!success) {
        // We failed allocating. Instead of doing the collection on the Java thread, we push
        // an allocation to a compiler thread, that will do the collection.
        thread_pool_->AddTask(
            self, new JitCompileTask(method, JitCompileTask::TaskKind::kAllocateProfile));
      }
    }
  }
  if (UseJitCompilation()) {
    if (old_count < HotMethodThreshold() && new_count >= HotMethodThreshold()) {
      if (!code_cache_->ContainsPc(method->GetEntryPointFromQuickCompiledCode())) {
        DCHECK(thread_pool_ != nullptr);
        JitCompileTask::TaskKind kind =
            (options_->UseTieredJitCompilation() || options_->UseBaselineCompiler())
                ? JitCompileTask::TaskKind::kCompileBaseline
                : JitCompileTask::TaskKind::kCompile;
        thread_pool_->AddTask(self, new JitCompileTask(method, kind));
      }
    }
    if (old_count < OSRMethodThreshold() && new_count >= OSRMethodThreshold()) {
      if (!with_backedges) {
        return false;
      }
      DCHECK(!method->IsNative());  // No back edges reported for native methods.
      if (!code_cache_->IsOsrCompiled(method)) {
        DCHECK(thread_pool_ != nullptr);
        thread_pool_->AddTask(
            self, new JitCompileTask(method, JitCompileTask::TaskKind::kCompileOsr));
      }
    }
  }
  return true;
}

void Jit::EnqueueOptimizedCompilation(ArtMethod* method, Thread* self) {
  if (thread_pool_ == nullptr) {
    return;
  }
  // We arrive here after a baseline compiled code has reached its baseline
  // hotness threshold. If tiered compilation is enabled, enqueue a compilation
  // task that will compile optimize the method.
  if (options_->UseTieredJitCompilation()) {
    thread_pool_->AddTask(
        self, new JitCompileTask(method, JitCompileTask::TaskKind::kCompile));
  }
}

class ScopedSetRuntimeThread {
 public:
  explicit ScopedSetRuntimeThread(Thread* self)
      : self_(self), was_runtime_thread_(self_->IsRuntimeThread()) {
    self_->SetIsRuntimeThread(true);
  }

  ~ScopedSetRuntimeThread() {
    self_->SetIsRuntimeThread(was_runtime_thread_);
  }

 private:
  Thread* self_;
  bool was_runtime_thread_;
};

void Jit::MethodEntered(Thread* thread, ArtMethod* method) {
  Runtime* runtime = Runtime::Current();
  if (UNLIKELY(runtime->UseJitCompilation() && JitAtFirstUse())) {
    ArtMethod* np_method = method->GetInterfaceMethodIfProxy(kRuntimePointerSize);
    if (np_method->IsCompilable()) {
      if (!np_method->IsNative() && GetCodeCache()->CanAllocateProfilingInfo()) {
        // The compiler requires a ProfilingInfo object for non-native methods.
        ProfilingInfo::Create(thread, np_method, /* retry_allocation= */ true);
      }
      // TODO(ngeoffray): For JIT at first use, use kPreCompile. Currently we don't due to
      // conflicts with jitzygote optimizations.
      JitCompileTask compile_task(method, JitCompileTask::TaskKind::kCompile);
      // Fake being in a runtime thread so that class-load behavior will be the same as normal jit.
      ScopedSetRuntimeThread ssrt(thread);
      compile_task.Run(thread);
    }
    return;
  }

  ProfilingInfo* profiling_info = method->GetProfilingInfo(kRuntimePointerSize);
  // Update the entrypoint if the ProfilingInfo has one. The interpreter will call it
  // instead of interpreting the method. We don't update it for instrumentation as the entrypoint
  // must remain the instrumentation entrypoint.
  if ((profiling_info != nullptr) &&
      (profiling_info->GetSavedEntryPoint() != nullptr) &&
      (method->GetEntryPointFromQuickCompiledCode() != GetQuickInstrumentationEntryPoint())) {
    Runtime::Current()->GetInstrumentation()->UpdateMethodsCode(
        method, profiling_info->GetSavedEntryPoint());
  } else {
    AddSamples(thread, method, 1, /* with_backedges= */false);
  }
}

void Jit::InvokeVirtualOrInterface(ObjPtr<mirror::Object> this_object,
                                   ArtMethod* caller,
                                   uint32_t dex_pc,
                                   ArtMethod* callee ATTRIBUTE_UNUSED) {
  ScopedAssertNoThreadSuspension ants(__FUNCTION__);
  DCHECK(this_object != nullptr);
  ProfilingInfo* info = caller->GetProfilingInfo(kRuntimePointerSize);
  if (info != nullptr) {
    info->AddInvokeInfo(dex_pc, this_object->GetClass());
  }
}

void Jit::WaitForCompilationToFinish(Thread* self) {
  if (thread_pool_ != nullptr) {
    thread_pool_->Wait(self, false, false);
  }
}

void Jit::Stop() {
  Thread* self = Thread::Current();
  // TODO(ngeoffray): change API to not require calling WaitForCompilationToFinish twice.
  WaitForCompilationToFinish(self);
  GetThreadPool()->StopWorkers(self);
  WaitForCompilationToFinish(self);
}

void Jit::Start() {
  GetThreadPool()->StartWorkers(Thread::Current());
}

ScopedJitSuspend::ScopedJitSuspend() {
  jit::Jit* jit = Runtime::Current()->GetJit();
  was_on_ = (jit != nullptr) && (jit->GetThreadPool() != nullptr);
  if (was_on_) {
    jit->Stop();
  }
}

ScopedJitSuspend::~ScopedJitSuspend() {
  if (was_on_) {
    DCHECK(Runtime::Current()->GetJit() != nullptr);
    DCHECK(Runtime::Current()->GetJit()->GetThreadPool() != nullptr);
    Runtime::Current()->GetJit()->Start();
  }
}

static void* RunPollingThread(void* arg) {
  Jit* jit = reinterpret_cast<Jit*>(arg);
  do {
    sleep(10);
  } while (!jit->GetCodeCache()->GetZygoteMap()->IsCompilationNotified());

  // We will suspend other threads: we can only do that if we're attached to the
  // runtime.
  Runtime* runtime = Runtime::Current();
  bool thread_attached = runtime->AttachCurrentThread(
      "BootImagePollingThread",
      /* as_daemon= */ true,
      /* thread_group= */ nullptr,
      /* create_peer= */ false);
  CHECK(thread_attached);

  {
    // Prevent other threads from running while we are remapping the boot image
    // ArtMethod's. Native threads might still be running, but they cannot
    // change the contents of ArtMethod's.
    ScopedSuspendAll ssa(__FUNCTION__);
    runtime->GetJit()->MapBootImageMethods();
  }

  Runtime::Current()->DetachCurrentThread();
  return nullptr;
}

void Jit::PostForkChildAction(bool is_system_server, bool is_zygote) {
  // Clear the potential boot tasks inherited from the zygote.
  {
    MutexLock mu(Thread::Current(), boot_completed_lock_);
    tasks_after_boot_.clear();
  }

  Runtime* const runtime = Runtime::Current();
  // Check if we'll need to remap the boot image methods.
  if (!is_zygote && fd_methods_ != -1) {
    // Create a thread that will poll the status of zygote compilation, and map
    // the private mapping of boot image methods.
    // For child zygote, we instead query IsCompilationNotified() post zygote fork.
    zygote_mapping_methods_.ResetInForkedProcess();
    pthread_t polling_thread;
    pthread_attr_t attr;
    CHECK_PTHREAD_CALL(pthread_attr_init, (&attr), "new thread");
    CHECK_PTHREAD_CALL(pthread_attr_setdetachstate, (&attr, PTHREAD_CREATE_DETACHED),
                       "PTHREAD_CREATE_DETACHED");
    CHECK_PTHREAD_CALL(
        pthread_create,
        (&polling_thread, &attr, RunPollingThread, reinterpret_cast<void*>(this)),
        "Methods maps thread");
  }

  if (is_zygote || runtime->IsSafeMode()) {
    // Delete the thread pool, we are not going to JIT.
    thread_pool_.reset(nullptr);
    return;
  }
  // At this point, the compiler options have been adjusted to the particular configuration
  // of the forked child. Parse them again.
  jit_compiler_->ParseCompilerOptions();

  // Adjust the status of code cache collection: the status from zygote was to not collect.
  code_cache_->SetGarbageCollectCode(!jit_compiler_->GenerateDebugInfo() &&
      !Runtime::Current()->GetInstrumentation()->AreExitStubsInstalled());

  if (is_system_server && HasImageWithProfile()) {
    // Disable garbage collection: we don't want it to delete methods we're compiling
    // through boot and system server profiles.
    // TODO(ngeoffray): Fix this so we still collect deoptimized and unused code.
    code_cache_->SetGarbageCollectCode(false);
  }

  // We do this here instead of PostZygoteFork, as NativeDebugInfoPostFork only
  // applies to a child.
  NativeDebugInfoPostFork();
}

void Jit::PreZygoteFork() {
  if (thread_pool_ == nullptr) {
    return;
  }
  thread_pool_->DeleteThreads();

  NativeDebugInfoPreFork();
}

void Jit::PostZygoteFork() {
  if (thread_pool_ == nullptr) {
    // If this is a child zygote, check if we need to remap the boot image
    // methods.
    if (Runtime::Current()->IsZygote() &&
        fd_methods_ != -1 &&
        code_cache_->GetZygoteMap()->IsCompilationNotified()) {
      ScopedSuspendAll ssa(__FUNCTION__);
      MapBootImageMethods();
    }
    return;
  }
  if (Runtime::Current()->IsZygote() &&
      code_cache_->GetZygoteMap()->IsCompilationDoneButNotNotified()) {
    // Copy the boot image methods data to the mappings we created to share
    // with the children. We do this here as we are the only thread running and
    // we don't risk other threads concurrently updating the ArtMethod's.
    CHECK_EQ(GetTaskCount(), 1);
    NotifyZygoteCompilationDone();
    CHECK(code_cache_->GetZygoteMap()->IsCompilationNotified());
  }
  thread_pool_->CreateThreads();
}

void Jit::BootCompleted() {
  Thread* self = Thread::Current();
  std::deque<Task*> tasks;
  {
    MutexLock mu(self, boot_completed_lock_);
    tasks = std::move(tasks_after_boot_);
    boot_completed_ = true;
  }
  for (Task* task : tasks) {
    thread_pool_->AddTask(self, task);
  }
}

bool Jit::CanEncodeMethod(ArtMethod* method, bool is_for_shared_region) const {
  return !is_for_shared_region ||
      Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(method->GetDeclaringClass());
}

bool Jit::CanEncodeClass(ObjPtr<mirror::Class> cls, bool is_for_shared_region) const {
  return !is_for_shared_region || Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(cls);
}

bool Jit::CanEncodeString(ObjPtr<mirror::String> string, bool is_for_shared_region) const {
  return !is_for_shared_region || Runtime::Current()->GetHeap()->ObjectIsInBootImageSpace(string);
}

bool Jit::CanAssumeInitialized(ObjPtr<mirror::Class> cls, bool is_for_shared_region) const {
  if (!is_for_shared_region) {
    return cls->IsInitialized();
  } else {
    // Look up the class status in the oat file.
    const DexFile& dex_file = *cls->GetDexCache()->GetDexFile();
    const OatDexFile* oat_dex_file = dex_file.GetOatDexFile();
    // In case we run without an image there won't be a backing oat file.
    if (oat_dex_file == nullptr || oat_dex_file->GetOatFile() == nullptr) {
      return false;
    }
    uint16_t class_def_index = cls->GetDexClassDefIndex();
    return oat_dex_file->GetOatClass(class_def_index).GetStatus() >= ClassStatus::kInitialized;
  }
}

void Jit::EnqueueCompilationFromNterp(ArtMethod* method, Thread* self) {
  if (thread_pool_ == nullptr) {
    return;
  }
  if (GetCodeCache()->ContainsPc(method->GetEntryPointFromQuickCompiledCode())) {
    // If we already have compiled code for it, nterp may be stuck in a loop.
    // Compile OSR.
    thread_pool_->AddTask(
        self, new JitCompileTask(method, JitCompileTask::TaskKind::kCompileOsr));
    return;
  }
  if (GetCodeCache()->CanAllocateProfilingInfo()) {
    ProfilingInfo::Create(self, method, /* retry_allocation= */ false);
    thread_pool_->AddTask(
        self, new JitCompileTask(method, JitCompileTask::TaskKind::kCompileBaseline));
  } else {
    thread_pool_->AddTask(
        self, new JitCompileTask(method, JitCompileTask::TaskKind::kCompile));
  }
}

}  // namespace jit
}  // namespace art
